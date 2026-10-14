"""Verify GS charge/hold slots with no original weapon-own trigger.

This records the module's explicit bind-pose fallback. Absence of a TIML key
does not prove that MHW clears retained own-animation state, so no synthetic
LSK track or claim of native carry-state playback is created.
"""
from pathlib import Path
import json
import sys
from mhw_prop_skin_tracks import deformation, identity, phases

ROOT = Path(__file__).resolve().parents[1]
SLOTS = (20144, 20145, 20245, 20152, 20153, 20154, 20155, 20156, 20157, 20146, 20189, 20190)


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def main():
    folder = ROOT / "assets/modes/greatsword"
    manifest_path = folder / "source_skin_tracks.json"
    manifest = read(manifest_path)
    catalog = {row["slot"]: row for row in read(folder / "catalog.json")}
    timeline_path = ROOT / "research/multiweapon/greatsword/timeline-decoded.json"
    timeline = read(timeline_path)["actions"]
    sys.path.insert(0, str(ROOT / "research/mhw-longsword/20261012/tooling/mhw-anim-tools"))
    from core.formats.lmt.reader import read_lmt_file
    from core.formats.timl.reader import read_timl_data_bytes
    from core.formats.timl.semantics import format_timeline_parameter_label, format_datatype_hash_label
    lmt_path = ROOT / "research/multiweapon/extracted/hm/wp/wp00/mot/w00_00/w00_00.lmt"
    raw = lmt_path.read_bytes()
    actions = {action.id: action for action in read_lmt_file(lmt_path).actions}
    prop_id = next(iter(manifest["props"]))
    prop_path = folder / manifest["props"][prop_id]["file"]
    prop = read(prop_path)
    if not (ROOT / prop["source"]).is_file():
        raise ValueError("Original MOD3 is unavailable")
    bind_error = max(abs(value-expected) for matrix in deformation(prop, {})
                     for value, expected in zip(matrix, identity()))
    if bind_error > 2e-4:
        raise ValueError("Normalized original MOD3 bind palette is not identity")
    records = {}
    for slot in SLOTS:
        row = catalog[slot]
        source = row["source_slot"]
        original = timeline[str(source)]
        action = actions[source]
        if action.header.frame_count != row["frames"] or not action.header.timl_offset:
            raise ValueError(f"{slot}: original body dimensions/TIML are missing")
        raw_timeline = read_timl_data_bytes(raw, data_offset=action.header.timl_offset)
        raw_properties = [format_datatype_hash_label(transform.datatype_hash)
                          for kind in raw_timeline.types
                          if format_timeline_parameter_label(kind.timeline_parameter_hash) == "PlMotionCommon"
                          for transform in kind.transforms]
        decoded_properties = [prop["name"] for kind in original["types"]
                              if kind["name"] == "PlMotionCommon" for prop in kind["transforms"]]
        if raw_properties != decoded_properties or any(name in raw_properties for name in
                                                       ("mWeaponMotionNo", "mWeaponMotionInterpolationTime")):
            raise ValueError(f"{slot}: original raw TIML differs from no-trigger evidence")
        triggers, interpolation = phases(original)
        if triggers or interpolation or str(slot) in manifest["tracks"]:
            raise ValueError(f"{slot}: expected explicitly absent original own trigger")
        common = [kind for kind in original["types"] if kind["name"] == "PlMotionCommon"]
        if not common:
            raise ValueError(f"{slot}: missing original PlMotionCommon evidence")
        records[str(slot)] = dict(
            slot=slot, source_slot=source, frames=row["frames"], prop_id=prop_id,
            source_trigger_keys=[], source_interpolation_keys=[],
            source_common_properties=[prop["name"] for kind in common for prop in kind["transforms"]],
            original_trigger_absent=True, generated_own_track=False,
            original_lmt_timl_verified=True,
            display_policy="Original normalized MOD3 bind vertices; native module restores them on motion switch",
            native_carry_state_verified=False,
            limitation="Original MHW retained own-animation/carry state is unknown; absence is not an authored static flag")
    manifest["bind_fallbacks"] = records
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    report = dict(schema_version=1, status="passed", mode="greatsword", slots=records,
                  source_timeline=str(timeline_path.relative_to(ROOT)),
                  original_body_lmt=str(lmt_path.relative_to(ROOT)), original_lmt_timl_verified=True,
                  source_mod3=prop["source"], source_prop=str(prop_path.relative_to(ROOT)),
                  source_prop_bones=len(prop["rig_bones"]), max_normalized_bind_identity_error=bind_error,
                  generated_lsk_tracks=0, game_launched=False, server_started=False, hashes_computed=False)
    output = ROOT / "research/multiweapon/greatsword/bind-fallback-validation.json"
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({"status": "passed", "slots": len(records), "generated_lsk_tracks": 0,
                      "max_normalized_bind_identity_error": bind_error}))


if __name__ == "__main__":
    main()
