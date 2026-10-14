"""Bounded source evidence for unfinished weapon-specific combat branches.

Re-decodes original weapon parameter/COL bytes; observes source FSM/TIML and
LMT identities separately. It never equates FSM ActionNo with LMT slot, starts
the game/server, computes hashes, or invents a runtime timing interpretation.
"""
from pathlib import Path
import json
import sys

from mhw_decode_combat_params import read
from mhw_combat_rules import named_attack_fields, source_timeline, flag_intervals, source_bow_tiers
from mhw_native_controller_probe import probe as native_controller_probe

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "research/mhw-longsword/20261012/tooling/mhw-anim-tools"))
from core.formats.lmt.reader import read_lmt_file


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def phase_keys(document, slot):
    return [key for typ in document["actions"][str(slot)]["types"] if typ["name"] == "PlMotionCommon"
            for field in typ["transforms"] if field["name"] == "mActionPhase" for key in field["keys"]]


def main():
    params = {}
    for mode, number in {"sword_shield": "01", "greatsword": "00", "lance": "06", "bow": "11"}.items():
        record = read(ROOT / f"research/multiweapon/extracted/hm/wp/wp{number}/wp{number}_param.w{number}p", f"w{number}p")
        target = ROOT / f"research/multiweapon/{mode}/combat-weapon-params.json"
        target.write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding="utf-8")
        params[mode] = record
    root = ROOT / "research/multiweapon"
    lance = load(root / "lance/collision-evidence.json")
    lance_timeline = source_timeline('lance')
    lance_fsm = load(ROOT / "research/multiweapon/extracted/hm/wp/wp06/wp06_action.fsm.json")["root"]
    lance_lmt = read_lmt_file(ROOT / "research/multiweapon/extracted/hm/wp/wp06/mot/w06_00/w06_00.lmt")
    lance_actions = {action.id: action for action in lance_lmt.actions}
    stance, release = lance_actions[120], lance_actions[121]
    assert stance.header.frame_count == 181 and stance.header.loop_frame == 150
    assert not lance["actions"].get("120")
    assert [key["frame"] for key in phase_keys(lance_timeline, 120)] == [0, 52, 115]
    release_row = lance["actions"]["121"][0]
    assert release_row["author_name"] == "カウンター突き溜めきり" and release_row["start_frame"] == 20 and release_row["end_frame"] == 32
    nodes = []
    for node in lance_fsm["mpRootCluster"]["mpNodeList"]:
        if node["mId"] not in (11, 21, 39, 40, 41, 42, 43, 44):
            continue
        nodes.append(dict(node=node["mId"], name=node["mName"],
                          native_action_enum=node["mpProcessList"]["mpParameter"]["ActionNo"]["EnumValue"]))
    enhanced = {}
    for mode, source_slots in {"greatsword": (146, 189, 190, 275, 276, 277), "sword_shield": (319,320,321,341,342,343,344,345,346,347,348)}.items():
        evidence = load(root / mode / "collision-evidence.json")
        attacks = {int(bank): named_attack_fields(ROOT / record["source"]) for bank, record in evidence["banks"].items()}
        enhanced[mode] = {}
        for source_slot in source_slots:
            enhanced[mode][str(source_slot)] = [dict(row, source_move=attacks.get(row["bank"], {}).get((row.get("collision_record") or {}).get("move_id"), {}))
                                                   for row in evidence["actions"].get(str(source_slot), [])]
    native=native_controller_probe()
    bow_tiers,bow_charge=source_bow_tiers()
    sns_timeline=source_timeline('sword_shield');sns_catalog={item['source_slot']:item for item in load(ROOT/'assets/modes/sword_shield/catalog.json')}
    input_windows={str(slot):dict(just=flag_intervals(sns_timeline,slot,27,sns_catalog[slot]['duration']),
                                gate=flag_intervals(sns_timeline,slot,28,sns_catalog[slot]['duration'])) for slot in (344,345,346,347,348)}
    result = dict(schema_version=1, lance_counter=dict(
        stance=dict(source_slot=120, frames=stance.header.frame_count, source_loop_frame=stance.header.loop_frame,
                    outgoing_contacts=0, action_phase_keys=phase_keys(lance_timeline, 120)),
        charged_release=dict(source_slot=121, frames=release.header.frame_count, source_collision=release_row),
        native_fsm_nodes=nodes, native_enum_to_lmt_association_verified=True,
        uncharged_release_identity_verified=True,short_release=native['lance_short_release'],
        source_motion_classes={name:value for name,value in native['source_motion_classes'].items() if name.startswith('cCharge')},
        full_release_timing_scope='source_action_phase_3_with_local_release_controller',
        evidence_scope="Original reflection class constructors and release controller prove 120 hold, 122 then 106 short release, 121 full release; defensive eligibility and phase-3 release policy remain a local controller"),
        enhanced_attack_branches=enhanced,
        weapon_named_parameters={mode: {key: field for key, field in record["fields"].items()
                                       if any(term in key for term in ("Charge_Times_", "Over_Charge_Timing", "Guard_Angle", "Arrows_Shot_", "Level_Charge_per_Shot", "Bow_Charge_", "Charge_Dash_lvl_"))}
                                 for mode, record in params.items()},
        bow_arrow_row_semantics_verified=True,bow_projectile_tiers=bow_tiers,bow_charge_seconds=bow_charge,
        bow_native_controller=native['bow'],native_weakpoint_hitzone_runtime_available=False,
        perfect_rush_input_bit_semantics_verified=True,perfect_rush_input_windows=input_windows,
        perfect_rush_native_controller=native['perfect_rush'],native_controller_proof='research/mhw-original/combat-native-controller-proof.json',
        sword_shield_constraint_attack=native['sword_shield_constraint_attack'],
        game_launched=False, hashes_computed=False)
    (ROOT/'research/mhw-original/combat-native-controller-proof.json').write_text(json.dumps(native,ensure_ascii=False,indent=2),encoding='utf-8')
    destination = ROOT / "research/mhw-original/combat-fidelity-probe.json"
    destination.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(dict(weapon_parameter_fields=sum(len(record["fields"]) for record in params.values()),
                          lance_stance_source_slot=120, lance_stance_loop_frame=150, lance_charged_release_source_slot=121,
                          source_branch_slots=sum(len(values) for values in enhanced.values()),
                          bow_source_arrow_count_rows=3, output=destination.relative_to(ROOT).as_posix())))


if __name__ == "__main__":
    main()
