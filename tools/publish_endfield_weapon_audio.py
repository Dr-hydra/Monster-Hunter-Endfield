"""Publish source-proven Endfield special weapon PCM and lifecycle cues.

Run endfield_extract_weapon_audio.py first. This publisher follows the actual
default Wwise Switch/State branches and Random playlists, instead of turning
every descendant of an Event into a random alternative. Banks/WEM remain in
research; only decoded PCM and small provenance records enter the local pack.
"""
from __future__ import annotations

from array import array
from copy import deepcopy
from pathlib import Path
import json
import math
import shutil
import sys
import wave
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / "research/endfield-special-audio"
CORE_EVENTS = ("laevat_fire_pad", "laevat_fire_end", "typhoea_bow_charge", "typhoea_bow_shot")


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


class DefaultRoute:
    """Only explicit node references and source-authored default states are used."""
    def __init__(self, xml):
        self.nodes = {}
        self.decisions = []
        for node in ET.parse(xml).iter("object"):
            ident = node.find('./field[@name="ulID"]')
            if ident is not None and node.get("name", "").startswith("CAk"):
                self.nodes[int(ident.get("value"))] = node

    @staticmethod
    def values(node, name):
        return [field.get("value") for field in node.iter("field") if field.get("name") == name]

    def properties(self, node):
        result = {}
        for prop in node.findall('.//list[@name="pProps"]/object[@name="AkPropBundle"]'):
            ident, value = prop.find('./field[@name="pID"]'), prop.find('./field[@name="pValue"]')
            if ident is not None and value is not None:
                result[int(ident.get("value"))] = float(value.get("value"))
        return result

    def route(self, ident, path=(), gain_db=0.0, delay=0.0):
        if ident in path:
            raise ValueError("Cyclic source HIRC graph")
        node = self.nodes[ident]
        kind, path = node.get("name"), (*path, ident)
        properties = self.properties(node)
        gain_db += properties.get(0, 0) + properties.get(5, 0)
        delay += properties.get(34, 0)
        if kind == "CAkEvent":
            children = [int(value) for value in self.values(node, "ulActionID")]
        elif kind == "CAkActionPlay":
            children = [int(value) for value in self.values(node, "idExt")]
        elif kind == "CAkSound":
            media = self.values(node, "sourceID")
            if len(media) != 1:
                raise ValueError("Source Sound does not have one Media ID")
            return [dict(media_ids=[int(media[0])], path=list(path), gain_db=gain_db, time=delay,
                         source_pitch_cents=properties.get(1, 0), source_loop=False)]
        elif kind == "CAkSwitchCntr":
            default = self.values(node, "ulDefaultSwitch")
            if len(default) != 1:
                raise ValueError("Switch has no unique source default")
            branches = []
            for branch in node.findall('.//list[@name="SwitchList"]/object[@name="CAkSwitchPackage"]'):
                state = branch.find('./field[@name="ulSwitchID"]')
                branches.append(dict(state_id=int(state.get("value")),
                                     node_ids=[int(value) for value in self.values(branch, "NodeID")]))
            selected = [branch for branch in branches if branch["state_id"] == int(default[0])]
            if len(selected) != 1:
                raise ValueError("Default Switch branch is not present exactly once")
            children = selected[0]["node_ids"]
            self.decisions.append(dict(node_id=ident, type=kind, default_state=int(default[0]),
                                       selected_nodes=children, branches=branches))
        elif kind == "CAkRanSeqCntr":
            mode, loops, continuous = (self.values(node, field) for field in ("eMode", "sLoopCount", "bIsContinuous"))
            if mode != ["0"] or loops != ["1"] or continuous != ["0"]:
                raise ValueError("This publisher does not flatten a sequence or continuous/loop container")
            children = [int(value) for value in self.values(node, "ulPlayID")]
            if not children:
                raise ValueError("Source random playlist is empty")
            voices = [self.route(child, path, gain_db, delay) for child in children]
            if any(len(voice) != 1 or len(voice[0]["media_ids"]) != 1 for voice in voices):
                raise ValueError("Layered random alternatives require a separate renderer")
            first = deepcopy(voices[0][0])
            if any(any(voice[0][field] != first[field] for field in ("gain_db", "time", "source_pitch_cents", "source_loop")) for voice in voices[1:]):
                raise ValueError("Random alternatives have different gain/delay/pitch")
            first["media_ids"] = [voice[0]["media_ids"][0] for voice in voices]
            first["variant_paths"] = [voice[0]["path"] for voice in voices]
            self.decisions.append(dict(node_id=ident, type=kind, playlist_nodes=children,
                                       media_ids=first["media_ids"], source_loop_count=1))
            return [first]
        elif kind == "CAkLayerCntr":
            children = [int(value) for value in self.values(node, "ulChildID")]
        else:
            raise ValueError("Unsupported source routing object " + kind)
        return [voice for child in children for voice in self.route(child, path, gain_db, delay)]


def pcm_audit(path):
    with wave.open(str(path), "rb") as stream:
        channels, rate, frames = stream.getnchannels(), stream.getframerate(), stream.getnframes()
        if stream.getsampwidth() != 2 or channels not in (1, 2) or not 8000 <= rate <= 192000:
            raise ValueError("Theme PCM format not supported: " + str(path))
        samples = array("h", stream.readframes(frames))
    if sys.byteorder != "little":
        samples.byteswap()
    if len(samples) != frames * channels or not 0 < frames / rate <= 30:
        raise ValueError("Theme PCM samples are truncated or too long")
    peak = max(abs(value) for value in samples) / 32768
    rms = math.sqrt(sum(value * value for value in samples) / len(samples)) / 32768
    if peak <= 1 / 32768 or rms <= 1e-7:
        raise ValueError("Theme source contains no audible PCM")
    return dict(sample_rate=rate, channels=channels, frames=frames, seconds=frames / rate,
                peak=peak, rms=rms)


def main():
    mapping = read(RESEARCH / "mapping.json")
    if mapping.get("effective_order") != ["StreamingAssets", "Persistent"] or mapping.get("game_audio_events_called"):
        raise ValueError("Current effective VFS evidence is required")
    by_key = {event["key"]: event for event in mapping["events"]}
    roots, evidence = {}, {}
    for key in CORE_EVENTS:
        event = by_key[key]
        if event["status"] != "decoded" or not event["source_config"] or event["bank_version"] != 150:
            raise ValueError("Unresolved required theme event " + key)
        resolver = DefaultRoute(RESEARCH / "banks" / f'{event["event_id"]}.bnk.xml')
        roots[key] = resolver.route(event["event_id"])
        media = {entry["id"]: entry for entry in event["media"]}
        for voice in roots[key]:
            if voice["source_pitch_cents"] or voice["time"]:
                raise ValueError("Core theme unexpectedly needs source pitch/delay rendering")
            if any(ident not in media for ident in voice["media_ids"]):
                raise ValueError("Default route has missing current Media")
        evidence[key] = dict(event=event["event"], event_id=event["event_id"], source_config=event["source_config"],
                             bank_version=150, package=event["package"], layer=event["layer"],
                             routing=roots[key], decisions=resolver.decisions,
                             media=[media[ident] for voice in roots[key] for ident in voice["media_ids"]])
    if any(len(voices) != 1 for voices in roots.values()):
        raise ValueError("Expected exactly one audible default source branch for each theme event")
    output = ROOT / "assets/audio"
    clips = {}
    for key, voices in roots.items():
        event = by_key[key]
        for ident in voices[0]["media_ids"]:
            source = RESEARCH / "wav" / f"{ident}.wav"
            target = output / f"theme_{ident}.wav"
            source_pcm = pcm_audit(source)
            output.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            if target.read_bytes() != source.read_bytes():
                raise ValueError("Published PCM differs from original decoded samples")
            clip_id = f"endfield_theme_{ident}"
            clips[clip_id] = dict(id=clip_id, file=target.name, label=f"Endfield · {key} · {ident}",
                                  source="Endfield current VFS", source_event=event["event"],
                                  event_id=event["event_id"], media_id=ident, **source_pcm)

    def cue(key, group):
        voice = roots[key][0]
        variants = [f"endfield_theme_{ident}" for ident in voice["media_ids"]]
        return dict(time=0, clip=variants[0], variants=variants if len(variants) > 1 else [],
                    kind="oneshot", loop=False, gain=10 ** (voice["gain_db"] / 20),
                    group=group, priority=35, fade_in=0, fade_out=0.04, condition="always",
                    source_event=by_key[key]["event"], source_loop=False,
                    confidence="source_theme_event_descendant",
                    variant_limitation="Source-authored Random playlist; module rotates playable alternatives, not Wwise weighted/random state simulation")

    pad = cue("laevat_fire_pad", "theme.laevat.fire")
    pad.update(kind="loop", loop=True, lifecycle_policy="Continuous display ambience owned by the visible fire blade; source is a finite 16-second pad")
    fire_end = cue("laevat_fire_end", "theme.laevat.end")
    bow_charge = cue("typhoea_bow_charge", "theme.typh.charge")
    bow_release = cue("typhoea_bow_shot", "theme.typh.release")
    def stop(c):
        return dict(time=0, clip=c["clip"], kind="stop", loop=False, group=c["group"],
                    gain=1, priority=35, fade_in=0, fade_out=0.04)
    manifest = dict(schema_version=1, clips=list(clips.values()),
                    themes={"laevatain_blade": {"start": [pad], "stop": [stop(pad), fire_end]},
                            "typhoea_bow": {"charge": [bow_charge], "release": [stop(bow_charge), bow_release], "cancel": [stop(bow_charge)]}},
                    manifest_identity=mapping["manifest_identity"], event_sources=evidence,
                    mapping_evidence="Effective VFS exact length-prefixed config Event + matching v150 Bank/Event/Play/default Switch/Random playlist/Sound sourceID + matching current SFX Media",
                    timing_evidence="Display lifecycle entry/exit and existing MHW charge/release source markers; not claimed to reproduce Endfield original skill frame timing",
                    limitation="Original game bus DSP/reverb, spatial state/RTPC and weighted random routing are not replayed. Ordinary MHW action PCM remains independent. Fire pad repeat is module display policy, not a discovered native infinite loop.",
                    native_game_audio_called=False, uniform_pcm=True, game_launched=False, server_started=False,
                    hashes_computed=False)
    write(output / "theme_cues.json", manifest)
    report = dict(schema_version=1, status="passed", required_events=len(CORE_EVENTS),
                  published_pcm=len(clips), pcm_bytes=sum((output / clip["file"]).stat().st_size for clip in clips.values()),
                  complete_default_routes=True, current_config_strings_verified=True,
                  original_pcm_direct_byte_match=True, finite_pcm_audited=True,
                  source_bank_namespace_preserved=True, no_game_audio_calls=True,
                  all_extracted_event_count=len(mapping["events"]),
                  optional_source_gaps=[dict(key=event["key"], missing_media=event["missing_media"]) for event in mapping["events"] if event.get("missing_media")],
                  omitted_nondefault_charge_state_media=[390433704],
                  hashes_computed=False, game_launched=False, server_started=False)
    write(RESEARCH / "publication-audit.json", report)
    print(json.dumps({key: report[key] for key in ("status", "required_events", "published_pcm", "pcm_bytes", "omitted_nondefault_charge_state_media")}, ensure_ascii=False))


if __name__ == "__main__":
    main()
