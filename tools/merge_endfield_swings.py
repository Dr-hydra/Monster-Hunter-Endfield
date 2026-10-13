"""Merge the verified local Endfield swing variants into the shared PCM cues.

Run after endfield_extract_swings.py and mhw_bake_audio.py. Event/media
identity is verified by the extractor; these cue times are presentation
timings and still need game review. MHW cues and source audio are preserved.
"""
from pathlib import Path
import json
import shutil
import wave

ROOT = Path(__file__).resolve().parents[1]


def main():
    source = ROOT / "research/endfield-swings"
    output = ROOT / "assets/audio"
    target = output / "cues.json"
    manifest = json.loads(target.read_text(encoding="utf-8-sig"))
    media = [929949674, 198925091, 484159784]
    identities = {f"endfield_swing_{i}" for i in range(1, 4)}
    manifest["clips"] = [c for c in manifest["clips"] if c["id"] not in identities]
    for i, (slot, at, source_id) in enumerate(zip([101, 102, 103], [.23, .27, .31], media), 1):
        name = f"endfield_swing_{i}"
        file = source / f"{name}.wav"
        with wave.open(str(file), "rb") as pcm:
            if (pcm.getframerate(), pcm.getnchannels(), pcm.getsampwidth()) != (48000, 2, 2):
                raise ValueError(f"Expected verified stereo 48k PCM16: {file}")
        shutil.copyfile(file, output / file.name)
        manifest["clips"].append(dict(id=name, file=file.name, label=f"普通斩{i} · 终末地原声",
                                      source_event=f"au_chr_0003_endmin_attack{i}", source_media=source_id))
        manifest["cues"][str(slot)] = [dict(time=at, clip=name, gain=.8)]
    manifest["ordinary_swing_source"] = "Current Endfield VFS Audit namespace, non-localized SFX; verified attack1/2/3 variants"
    target.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Merged 3 verified Endfield PCM variants; {len(manifest['clips'])} clips in shared timeline")


if __name__ == "__main__":
    main()
