"""Refresh source references from the final actual native controller timeline.

Compare the currently rendered timeline before exporting; preserve unchanged
frames and render all changed source poses/visibility. No game/server is used.
"""
from pathlib import Path
import argparse
import json
import subprocess
import sys
from mhw_source_tour_update_tail import same

ROOT = Path(__file__).resolve().parents[1]
BLENDER = ROOT / "local/tools/blender/blender-4.5.14-windows-x64/blender.exe"
EXPORTER = ROOT / "build/development/Release/source_demo_timeline.exe"
MODES = ("longsword", "sword_shield", "greatsword", "lance", "light_bowgun", "bow")


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def run(args, log):
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run([str(arg) for arg in args], cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"Source reference refresh failed; see {log}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", action="append", choices=MODES)
    args = parser.parse_args()
    records = []
    for mode in args.mode or MODES:
        folder = ROOT / "research/multiweapon" / mode / "source-tour"
        timeline = folder / "timeline.json"
        old = read(timeline)
        (folder / "timeline.pre-final-refresh.json").write_bytes(timeline.read_bytes())
        run([EXPORTER, ROOT, mode, timeline], folder / "final-native-export.log")
        new = read(timeline)
        frames = (folder / "frames").resolve()
        changed = [index for index, row in enumerate(new["samples"])
                   if index >= len(old["samples"]) or not same(old["samples"][index], row)
                   or not (frames / f"{index:05d}.png").is_file()]
        removed = 0
        for path in frames.glob("*.png"):
            if path.stem.isdigit() and int(path.stem) >= len(new["samples"]):
                if path.resolve().parent != frames:
                    raise ValueError("Generated-frame cleanup escapes this source directory")
                path.unlink()
                removed += 1
        selection = folder / "final-render-indices.json"
        selection.write_text(json.dumps(dict(indices=changed), indent=2), encoding="utf-8")
        if changed:
            run([BLENDER, "--background", "--factory-startup", "--threads", "2", "--python",
                 ROOT / "tools/mhw_source_tour_render.py", "--", mode, "0", "--only-indices", selection],
                folder / "final-source-render.log")
        if len(list(frames.glob("*.png"))) != len(new["samples"]):
            raise ValueError("Source frame sequence has gaps or surplus")
        row = dict(mode=mode, old_frames=len(old["samples"]), new_frames=len(new["samples"]),
                   rendered_frames=len(changed), reused_frames=len(new["samples"])-len(changed),
                   surplus_frames_removed=removed, game_launched=False, server_started=False, hashes_computed=False)
        records.append(row)
        (folder / "final-refresh-evidence.json").write_text(json.dumps(row, indent=2)+"\n", encoding="utf-8")
        print(json.dumps(row), flush=True)
    run([sys.executable, ROOT / "tools/mhw_source_tour_package.py", "--publish", *sum((["--mode", mode] for mode in args.mode or MODES), [])],
        ROOT / "research/multiweapon/final-source-tour-package.log")


if __name__ == "__main__":
    main()
