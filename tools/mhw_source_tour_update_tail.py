"""Update changed native source-tour samples and reuse every unchanged frame.

No game/server process is used. Old and new source controller timestamps are
compared before rendering. This also applies the exported native sheath weapon
visibility instead of leaving a drawn source prop on a completed sheath pose.
"""
from pathlib import Path
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MODES = ("longsword", "sword_shield", "greatsword", "lance", "light_bowgun", "bow")
BLENDER = ROOT / "local/tools/blender/blender-4.5.14-windows-x64/blender.exe"
EXPORTER = ROOT / "build/development/Release/source_demo_timeline.exe"


def read(path):
    return json.loads(path.read_text("utf-8"))


def same(old, new):
    for key in ("slot", "motion"):
        if old[key] != new[key]:
            return False
    if old.get("weapons_visible", True) != new.get("weapons_visible", True):
        return False
    if abs(old["seconds"]-new["seconds"]) > 1e-6:
        return False
    return all(abs(a-b) <= 1e-6 for a,b in zip(old["hip"],new["hip"]))


def run(command, log):
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run([str(value) for value in command], cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"Source tour step failed ({result.returncode}); see {log}")


def main():
    reports = []
    for mode in MODES:
        folder = ROOT / "research/multiweapon" / mode / "source-tour"
        old_path = folder / "timeline.pre-sheath-hold.json"
        if not old_path.exists():
            old_path.write_bytes((folder / "timeline.json").read_bytes())
        old = read(old_path)
        run([EXPORTER, ROOT, mode, folder/"timeline.json"], folder/"tail-export.log")
        new = read(folder/"timeline.json")
        if not all("weapons_visible" in row for row in new["samples"]):
            raise ValueError("Source exporter has not been rebuilt with actual sheath visibility")
        changed = [index for index,row in enumerate(new["samples"])
                   if index >= len(old["samples"]) or not same(old["samples"][index],row)]
        selection = folder/"tail-render-indices.json"
        selection.write_text(json.dumps(dict(indices=changed,old_frames=len(old["samples"]),new_frames=len(new["samples"])),indent=2),encoding="utf-8")
        frames = (folder/"frames").resolve()
        removed = 0
        for path in frames.glob("*.png"):
            if path.stem.isdigit() and int(path.stem) >= len(new["samples"]):
                if path.resolve().parent != frames:
                    raise ValueError("Generated frame cleanup escapes source frame directory")
                path.unlink();removed += 1
        if changed:
            run([BLENDER,"--background","--factory-startup","--threads","2","--python",ROOT/"tools/mhw_source_tour_render.py",
                 "--",mode,"0","--only-indices",selection],folder/"tail-render.log")
        if len(list(frames.glob("*.png"))) != len(new["samples"]):
            raise ValueError("Source tail frame sequence has gaps or surplus")
        row = dict(mode=mode,old_frames=len(old["samples"]),new_frames=len(new["samples"]),
                   changed_frames=len(changed),unchanged_frames=len(new["samples"])-len(changed),
                   first_changed_frame=min(changed) if changed else None,old_tail_frames_removed=removed,
                   actual_native_visibility_exported=True,game_launched=False,server_started=False,hashes_computed=False)
        reports.append(row)
        print(json.dumps(row),flush=True)
    run([sys.executable,ROOT/"tools/mhw_source_tour_package.py","--publish"],ROOT/"research/multiweapon/source-tour-tail-package.log")
    (ROOT/"research/multiweapon/source-tour-tail-update.json").write_text(json.dumps(dict(schema_version=1,modes=reports,
        changed_samples="Exact native slot/seconds/motion/hip/weapons_visible comparison",game_launched=False,
        server_started=False,hashes_computed=False),ensure_ascii=False,indent=2)+"\n",encoding="utf-8")


if __name__ == "__main__":
    main()
