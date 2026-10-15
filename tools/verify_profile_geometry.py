"""Re-export current target meshes and compare exact prepared geometry payloads.

This gate retains prior pose audit evidence only when the actual mesh, weights,
indices and bindposes are identical. A matching resource name is insufficient.
"""
import argparse
import json
from pathlib import Path
import subprocess


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--current-inputs", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    inspector = root / "tools/endfield_weapon_inspector/bin/Release/net8.0-windows/EndfieldWeaponInspector.dll"
    reports = []
    for entry in read(root / "profiles/index.json")["characters"]:
        cid = entry["character_id"]
        current = args.current_inputs / cid / "geometry"
        previous = root / "research/character-coverage/geometry" / cid
        with (args.current_inputs / cid / "geometry.log").open("w", encoding="utf-8") as log:
            subprocess.run(["dotnet", str(inspector), str(args.current_inputs / cid / "inputs"),
                            str(current), "S_actor_" + cid.split("_", 2)[2] + ".*lod0$"],
                           check=True, stdout=log, stderr=subprocess.STDOUT)
        new = {r["id"]: r for r in read(current / "inspection.json") if r["kind"] == "Mesh"}
        old = ({r["id"]: r for r in read(previous / "inspection.json") if r["kind"] == "Mesh"}
               if (previous / "inspection.json").is_file() else {})
        changed = sorted(set(new) ^ set(old))
        for identity in new.keys() & old.keys():
            current_file = current / new[identity]["file"]
            previous_file = previous / old[identity]["file"]
            if current_file.read_bytes() != previous_file.read_bytes():
                if read(current_file) != read(previous_file):
                    changed.append(identity)
        reports.append({"character_id": cid, "current_meshes": len(new), "prior_meshes": len(old),
                        "new_target": not old,
                        "changed_meshes": changed, "exact_geometry_equal": not changed and bool(new)})
        print(f"{cid}: meshes={len(new)} changed={len(changed)}", flush=True)
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps({"schema": 1,
            "verification": "fresh current VFS geometry, direct full JSON payload comparison without hashes",
            "characters": reports, "runtime_verified": False}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return 1 if any(not row["exact_geometry_equal"] for row in reports) else 0


if __name__ == "__main__":
    raise SystemExit(main())
