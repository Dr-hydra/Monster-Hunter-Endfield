"""Fully decode all source-reference movies and check native sheath tails."""
from pathlib import Path
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "local/python-packages"))
import imageio_ffmpeg


def read(path):
    return json.loads(path.read_text("utf-8"))


def main():
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    index = read(ROOT / "assets/previews/source-tours/index.json")
    if len(index["modes"]) != 6:
        raise ValueError("Expected six original source tour movies")
    rows = []
    for mode in index["modes"]:
        movie = ROOT / "assets" / mode["file"]
        result = subprocess.run([ffmpeg,"-v","error","-i",str(movie),"-map","0:v:0","-map","0:a:0","-f","null","-"],
                                capture_output=True,text=True)
        if result.returncode or result.stderr:
            raise ValueError(f'{mode["mode"]}: video/audio full decode failed: {result.stderr[-1200:]}')
        folder = ROOT / "research/multiweapon" / mode["mode"] / "source-tour"
        timeline = read(folder / "timeline.json")
        moves = read(ROOT / timeline["data_root"] / "moves.json")
        tail = timeline["samples"][-1]
        expected = moves["commands"]["normal_sheathe"][0]
        if tail["slot"] != expected or tail["weapons_visible"]:
            raise ValueError("Source tail does not retain completed hidden standard sheath")
        if mode["source_mesh_frames"] != len(timeline["samples"]) or mode["video_bytes"] != movie.stat().st_size:
            raise ValueError("Source tour index does not match final exported frames/movie")
        rows.append(dict(mode=mode["mode"],video_bytes=movie.stat().st_size,frames=len(timeline["samples"]),
                         seconds=mode["seconds"],final_slot=tail["slot"],final_weapons_visible=tail["weapons_visible"],
                         video_audio_full_decode=True))
    report = dict(schema_version=1,status="passed",modes=rows,game_launched=False,server_started=False,hashes_computed=False)
    (ROOT / "research/multiweapon/source-tour-tail-verify.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(dict(status="passed",videos=len(rows),bytes=sum(row["video_bytes"] for row in rows),
                         all_final_source_sheath_hidden=True,all_video_audio_full_decode=True)))


if __name__ == "__main__":
    main()
