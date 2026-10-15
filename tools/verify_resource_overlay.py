"""Compare prepared resource closures against the installed effective VFS bytes.

The external reader is supplied explicitly. It only parses indexes and decrypts
selected records; no hashes are computed, no game is started and no files in the
installation or the resource-tool repository are changed.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import re
import subprocess
import sys
import tempfile
import types
import zlib
from pathlib import Path


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def current_closure_gap(snapshot, manifest, select_bundles):
    if snapshot.get("missing"):
        return {"reason": "snapshot-records-missing-resources", "missing": snapshot["missing"]}
    paths = [asset["path"] for asset in snapshot.get("assets", [])]
    if not paths:
        return {"reason": "snapshot-has-no-asset-identities"}
    try:
        _, current = select_bundles(manifest, extra_assets=paths)
    except ValueError as error:
        return {"reason": "current-asset-closure-unavailable", "detail": str(error)}
    recorded = {entry["path"].casefold() for entry in snapshot["bundles"]}
    actual = {name.casefold() for name in current}
    if recorded != actual:
        return {"reason": "current-asset-closure-changed",
                "new_dependencies": sorted(actual - recorded),
                "obsolete_dependencies": sorted(recorded - actual)}
    return None


def verify(game, resource_tools, snapshots):
    """Return exact byte checks; same manifest GUID is never sufficient proof."""
    sys.path.insert(0, str(resource_tools / "scripts"))
    helper = load("mhe_resource_overlay", resource_tools / "scripts/RefreshEndfieldResourceInputs.py")
    stub = types.ModuleType("config")
    stub.get_game_dir = lambda: str(game)
    sys.modules["config"] = stub
    unpacker = load("mhe_vfs_reader", resource_tools / "toolchains/EndfieldUnpacker/decrypt_vfs.py")
    unpacker.crc32 = lambda data: (zlib.crc32(data) + 2**31) % 2**32 - 2**31
    roots = [(layer, game / "Endfield_Data" / layer / "VFS")
             for layer in ("StreamingAssets", "Persistent")]
    selector = load("mhe_current_closure_selector", resource_tools / "tools/CustomModel/extract_native_bundles.py")
    with tempfile.TemporaryDirectory(prefix="mhe-current-manifest-") as temporary:
        manifest_path = Path(temporary) / "Bundles/Windows/manifest.hgmmap"
        helper.extract_overlay(unpacker, roots, Path(temporary),
                               re.compile(r"^Bundles/Windows/manifest\.hgmmap$", re.I))
        subprocess.run([str(resource_tools / "toolchains/FkArkEnd/ResConv/bin/Release/net10.0/ResConv.exe"),
                        str(manifest_path)], check=True, stdout=subprocess.DEVNULL)
        manifest = read(manifest_path.with_suffix(".json"))
    requested = {}
    reports = []
    for path in snapshots:
        snapshot = read(path)
        report = {"snapshot": str(path), "character": snapshot.get("character"),
                  "files": len(snapshot["bundles"]), "matched": 0, "gaps": []}
        closure_gap = current_closure_gap(snapshot, manifest, selector.select_bundles)
        report["current_closure_verified"] = closure_gap is None
        if closure_gap:
            report["gaps"].append(closure_gap)
        reports.append(report)
        for entry in snapshot["bundles"]:
            relative = entry["path"]
            if ".." in Path(relative).parts or Path(relative).is_absolute() or ":" in relative:
                raise ValueError("Invalid snapshot resource path: " + relative)
            requested.setdefault(relative.casefold(), []).append((path.parent / relative, report))
    pattern = re.compile("^(?:" + "|".join(re.escape(name) for name in requested) + ")$", re.I)
    records = {record.relative_path.casefold(): record
               for record in helper.select_overlay(unpacker, roots, pattern)}
    chunks = helper.build_chunk_index(roots)
    total_bytes = 0
    for name, users in requested.items():
        record = records.get(name)
        payload = None
        reason = "missing-current-vfs-record"
        if record:
            chunk_path = chunks.get(record.chunk)
            reason = "missing-current-chunk"
            if chunk_path and record.offset >= 0 and record.size > 0:
                if record.offset + record.size > chunk_path.stat().st_size:
                    raise ValueError("VFS range exceeds chunk: " + name)
                with chunk_path.open("rb") as stream:
                    stream.seek(record.offset)
                    payload = stream.read(record.size)
                if len(payload) != record.size:
                    raise ValueError("Short VFS read: " + name)
                if record.encrypted:
                    payload = unpacker.per_file_decrypt(payload, record.iv_seed)
                total_bytes += len(payload)
        for cached, report in users:
            if payload is not None and cached.is_file() and cached.read_bytes() == payload:
                report["matched"] += 1
            else:
                report["gaps"].append({"path": name, "reason": reason if payload is None
                                       else "missing-cached-file" if not cached.is_file()
                                       else "different-current-bytes"})
    for report in reports:
        report["listed_bundle_bytes_match"] = report["files"] > 0 and report["matched"] == report["files"]
        report["current_vfs_verified"] = not report["gaps"] and report["files"] > 0
    return {"schema": 1, "game_path": str(game),
            "overlay_order": ["StreamingAssets", "Persistent"],
            "verification": "direct decrypted byte comparison, no artifact hashes",
            "unique_resources": len(requested), "compared_bytes": total_bytes,
            "snapshots": reports, "runtime_verified": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--resource-tools", required=True, type=Path)
    parser.add_argument("--snapshots", required=True, nargs="+", type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()
    result = verify(args.game.resolve(), args.resource_tools.resolve(), args.snapshots)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    failed = sum(not row["current_vfs_verified"] for row in result["snapshots"])
    print(f"snapshots={len(result['snapshots'])} failed={failed} resources={result['unique_resources']}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
