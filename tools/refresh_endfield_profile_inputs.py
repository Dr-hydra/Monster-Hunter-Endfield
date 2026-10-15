"""Extract current character/equipment closures once, then read graphs serially.

Only selected prefabs and their dependencies are read. External resource tools
are explicit local dependencies. Outputs must be new and outside the game.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import types
import zlib

from verify_resource_overlay import load, read


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def verify_tables(helper, unpacker, roots, tools, input_root, output):
    names = ("CharacterTable", "WeaponBasicTable", "I18nTextTable_CN")
    records = helper.extract_overlay(unpacker, roots, output,
        re.compile(r"^TableCfg/(?:" + "|".join(names) + r")\.bytes$", re.I))
    decoder = load("mhe_roster_decoder", tools / "toolchains/EndfieldUnpacker/decode_sparkbuffer.py")
    report = []
    for name in names:
        source = output / "TableCfg" / (name + ".bytes")
        if not source.is_file():
            raise ValueError("Current VFS table missing: " + name)
        decoded_name, current = decoder.parse_sparkbuffer(source.read_bytes())
        if decoded_name != name or current != read(input_root / "tables/Table" / (name + ".json")):
            raise ValueError("Prepared table differs from current effective VFS: " + name)
        report.append({"table": name, "rows": len(current), "current_vfs_equal": True,
                       "layer": next(r.layer for r in records if Path(r.relative_path).stem == name)})
    write(output / "table-verification.json", {"schema": 1,
        "input_root": str(input_root.resolve()),
        "game_version": read(input_root / "tables/source-metadata.json")["gameVersion"],
        "overlay_order": ["StreamingAssets", "Persistent"], "tables": report,
        "verification": "fresh SparkBuffer decode equals complete prepared table values; no hashes"})
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("game", "resource-tools", "input-root", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--game-version", required=True)
    parser.add_argument("--characters", nargs="+")
    parser.add_argument("--no-characters", action="store_true")
    parser.add_argument("--equipment", nargs="+", default=[])
    parser.add_argument("--all-equipment", action="store_true", help="Read the full current equipment closure as one graph")
    parser.add_argument("--allow-backend-warnings", action="store_true",
                        help="Keep decoder gaps explicit for separately reviewed target-reference qualification")
    args = parser.parse_args()
    game, tools, out = args.game.resolve(), args.resource_tools.resolve(), args.output.resolve()
    if out == game or out.is_relative_to(game) or (out.exists() and any(out.iterdir())):
        raise ValueError("Output must be new and outside the game directory")
    metadata = read(args.input_root / "tables/source-metadata.json")
    if metadata.get("gameVersion") != args.game_version or metadata.get("kind") != "persistent+base":
        raise ValueError("Current effective table snapshot required")
    chars = read(args.input_root / "tables/Table/CharacterTable.json")
    weapons = read(args.input_root / "tables/Table/WeaponBasicTable.json")
    selected = [] if args.no_characters else args.characters or sorted(set(chars) - {"chr_9000_endmin"})
    if not set(selected) <= set(chars) or not set(args.equipment) <= set(weapons):
        raise ValueError("Selection absent from current source tables")
    out.mkdir(parents=True)
    sys.path.insert(0, str(tools / "scripts"))
    helper = load("mhe_refresh_vfs", tools / "scripts/RefreshEndfieldResourceInputs.py")
    extractor = load("mhe_bundle_selector", tools / "tools/CustomModel/extract_native_bundles.py")
    stub = types.ModuleType("config")
    stub.get_game_dir = lambda: str(game)
    sys.modules["config"] = stub
    unpacker = load("mhe_refresh_reader", tools / "toolchains/EndfieldUnpacker/decrypt_vfs.py")
    unpacker.crc32 = lambda data: (zlib.crc32(data) + 2**31) % 2**32 - 2**31
    roots = [(layer, game / "Endfield_Data" / layer / "VFS")
             for layer in ("StreamingAssets", "Persistent")]
    verify_tables(helper, unpacker, roots, tools, args.input_root, out / "roster")
    common = out / "shared"
    helper.extract_overlay(unpacker, roots, common, re.compile(r"^Bundles/Windows/manifest\.hgmmap$", re.I))
    manifest_path = common / "Bundles/Windows/manifest.hgmmap"
    subprocess.run([str(tools / "toolchains/FkArkEnd/ResConv/bin/Release/net10.0/ResConv.exe"),
                    str(manifest_path)], check=True, stdout=subprocess.DEVNULL)
    manifest = read(manifest_path.with_suffix(".json"))
    plans = {cid: extractor.select_bundles(manifest, cid) for cid in selected}
    if args.all_equipment:
        plans["equipment"] = extractor.select_bundles(manifest, extra_assets=[
            "assets/beyond/dynamicassets/" + row["modelPath"].lower() for row in weapons.values()])
    for wid in args.equipment:
        plans[wid] = extractor.select_bundles(manifest, extra_assets=[
            "assets/beyond/dynamicassets/" + weapons[wid]["modelPath"].lower()])
    names = sorted({name for _, bundles in plans.values() for name in bundles})
    records = helper.extract_overlay(unpacker, roots, common,
                                    re.compile("^(?:" + "|".join(map(re.escape, names)) + ")$", re.I))
    by_name = {r.relative_path.casefold(): r for r in records}
    if {name.casefold() for name in names} - set(by_name):
        raise ValueError("Current VFS dependency closure incomplete")
    print(f"Extracted {len(records)} unique bundles for {len(plans)} prefabs", flush=True)
    reports = []
    for identifier, (assets, bundles) in plans.items():
        folder = out / identifier
        inputs = folder / "inputs"
        snapshot = {"schema": 1, "character": identifier if identifier.startswith("chr_") else None,
                    "game": str(game), "game_version": args.game_version,
                    "manifest_version": manifest["Version"], "assets": assets,
                    "bundles": [{"path": name, "layer": by_name[name.casefold()].layer,
                                 "size": by_name[name.casefold()].size} for name in bundles], "missing": []}
        write(inputs / "extraction.json", snapshot)
        for name in bundles:
            source = common / by_name[name.casefold()].relative_path
            destination = inputs / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            os.link(source, destination)
        with (folder / "reader.log").open("w", encoding="utf-8") as log:
            subprocess.run(["dotnet", str(tools / "toolchains/native-asset-reader/identities/NativeAssetReader.dll"),
                            str(inputs), str(folder / "raw.json")], check=True, stdout=log, stderr=subprocess.STDOUT)
        data = read(folder / "raw.json")
        report = {"id": identifier, "snapshot": str(inputs / "extraction.json"),
                  "files": len(bundles), "matched": len(bundles), "gaps": [],
                  "current_vfs_verified": True, "current_closure_verified": True,
                  "backend_errors": data.get("backend_errors", []),
                  "graph_decode_complete": not data.get("backend_errors"),
                  "objects": len(data["objects"])}
        reports.append(report)
        write(out / "summary.json", {"schema": 1, "game_version": args.game_version,
              "overlay_order": ["StreamingAssets", "Persistent"], "snapshots": reports,
              "table_verification_report": str(out / "roster/table-verification.json"),
              "verification": "fresh effective VFS extraction; serialized graph decoder completeness recorded per item",
              "runtime_verified": False})
        print(f"{identifier}: objects={report['objects']} backend_errors={len(report['backend_errors'])}", flush=True)
    if any(not row["graph_decode_complete"] for row in reports) and not args.allow_backend_warnings:
        raise ValueError("Some dependency graphs have decoder gaps; inspect summary.json and reader logs before qualifying target references")


if __name__ == "__main__":
    main()
