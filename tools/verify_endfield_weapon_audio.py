"""Read-only proof, PCM and lifecycle validation for special weapon themes."""
from pathlib import Path
import json
import math
from publish_endfield_weapon_audio import CORE_EVENTS, DefaultRoute, RESEARCH, ROOT, pcm_audit, read


def main():
    manifest = read(ROOT / "assets/audio/theme_cues.json")
    source = {event["key"]: event for event in read(RESEARCH / "mapping.json")["events"]}
    if manifest["manifest_identity"] != read(RESEARCH / "mapping.json")["manifest_identity"]:
        raise ValueError("Published theme uses different effective VFS")
    clips = {clip["id"]: clip for clip in manifest["clips"]}
    if len(clips) != len(manifest["clips"]) or not manifest.get("uniform_pcm") or manifest.get("native_game_audio_called"):
        raise ValueError("Invalid theme PCM ownership")
    counts = dict(events=0, pcm=0, actions=0)
    for key in CORE_EVENTS:
        event, proof = source[key], manifest["event_sources"][key]
        resolver = DefaultRoute(RESEARCH / "banks" / f'{event["event_id"]}.bnk.xml')
        actual = resolver.route(event["event_id"])
        if proof["routing"] != actual or proof["decisions"] != resolver.decisions or proof["source_config"] != event["source_config"]:
            raise ValueError("Published theme route differs from actual source default routing")
        counts["events"] += 1
        for voice in actual:
            for ident in voice["media_ids"]:
                clip = clips[f"endfield_theme_{ident}"]
                if Path(clip["file"]).name != clip["file"] or not clip["file"].endswith(".wav"):
                    raise ValueError("Theme PCM path escapes audio root")
                published, original = ROOT / "assets/audio" / clip["file"], RESEARCH / "wav" / f"{ident}.wav"
                if published.read_bytes() != original.read_bytes():
                    raise ValueError("Published theme PCM samples differ from decoded source")
                actual_pcm = pcm_audit(published)
                for field, value in actual_pcm.items():
                    if not math.isclose(value, clip[field], rel_tol=1e-8, abs_tol=1e-9):
                        raise ValueError("Published PCM metadata differs for " + field)
                if clip["source_event"] != event["event"] or clip["event_id"] != event["event_id"] or clip["media_id"] != ident:
                    raise ValueError("Published clip Event/Media evidence differs")
                counts["pcm"] += 1
    for theme, stages in manifest["themes"].items():
        for stage, cues in stages.items():
            for cue in cues:
                if cue["clip"] not in clips or any(variant not in clips for variant in cue.get("variants", [])):
                    raise ValueError("Theme lifecycle references absent PCM")
                if cue["time"] != 0 or not math.isfinite(cue["gain"]) or not 0 <= cue["gain"] <= 2:
                    raise ValueError("Invalid theme lifecycle trigger/gain")
                if cue["kind"] not in ("oneshot", "loop", "stop") or not cue["group"].startswith("theme."):
                    raise ValueError("Theme can collide with action audio groups")
                counts["actions"] += 1
    charge = manifest["themes"]["typhoea_bow"]["charge"][0]
    if "endfield_theme_390433704" in charge["variants"] or len(charge["variants"]) != 3:
        raise ValueError("Non-default source State sound leaked into random alternatives")
    pad = manifest["themes"]["laevatain_blade"]["start"][0]
    if not pad["loop"] or pad["source_loop"] or not pad["lifecycle_policy"]:
        raise ValueError("Fire ambience loop provenance is ambiguous")
    for key in ("typhoea_bow", "laevatain_blade"):
        start = "charge" if key == "typhoea_bow" else "start"
        stop = "cancel" if key == "typhoea_bow" else "stop"
        if manifest["themes"][key][start][0]["group"] != manifest["themes"][key][stop][0]["group"]:
            raise ValueError("Theme exit does not stop the owned audio group")
    print(json.dumps(dict(status="passed", **counts, raw_game_audio_called=False, hashes_computed=False)))


if __name__ == "__main__":
    main()
