"""Read-only checks against original decrypted MHW parameter bytes.

Reports contain counts and assertions only, no encryption keys or hashes.
Run from any directory; all artifacts remain in the G: workspace.
"""
from pathlib import Path
import json
import math
import struct

from mhw_decode_combat_params import read
from mhw_combat_rules import OFFSETS, authored, named_attack_fields, source_timeline, flag_intervals, source_bow_tiers
from mhw_multi_collision import fields as collision_fields, at as collision_value_at, read_col
from mhw_native_controller_probe import probe as native_controller_probe

ROOT = Path(__file__).resolve().parents[1]


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def close(actual, expected, label):
    if not math.isclose(actual, expected, rel_tol=1e-6, abs_tol=1e-6):
        raise AssertionError(f"{label}: {actual} != {expected}")


def main():
    recorded = load(ROOT / "research/mhw-original/combat-weapon-params.json")
    original = read(ROOT / recorded["source"], "w03p")
    assert original["fields"] == recorded["fields"], "w03p extraction differs from original bytes"
    assert original["decoded_bytes"] == 1572 and original["zero_padding_bytes"] == 12
    value = lambda key: original["fields"][key]["value"]
    rules = load(ROOT / "assets/modes/longsword/combat_rules.json")
    params = rules["source_parameters"]
    mapping = {
        "foresight_minimum_gauge": ("Gauge_Required_to_Active_Foresight_That_Gives_45_I_Frames", 100),
        "special_sheathe_seconds": ("Iai_Sheathed_Time", 1),
        "gauge_level_cost": ("Gauge_Consumption_for_Helm_Plus_Iai_Spirit_Slash", 1),
        "helm_gauge_seconds": ("Spirit_Regeneration_Duration_After_Helm_Breaker_In_Seconds_", 1),
        "red_helm_gauge_seconds": ("Spirit_Regeneration_After_Red_Gauge_Spirit_Helm_Breaker_Duration_in_Seconds_", 1),
        "iai_gauge_seconds": ("Spirit_Regeneration_After_Iai_Slash_Duration_in_Seconds_", 1),
        "iai_timed_gauge_seconds": ("Spirit_Regeneration_After_Timed_Iai_Slash_Duration_in_Seconds_", 1),
        "gauge_upgrade_no_decay_seconds": ("Gauge_No_Decay_Time_after_Spirit_Gauge_Upgrade", 1),
    }
    for key, (source, scale) in mapping.items():
        close(params[key], value(source) * scale, key)
    assert params["level_durations"][0] == 0
    for index, level in enumerate(("White", "Yellow", "Red"), start=1):
        close(params["level_durations"][index], value("Gauge_Duration_" + level), level)
        close(params["level_damage_multipliers"][index], value("Gauge_Damage_Buff_" + level), level + " damage multiplier")
    close(params["level_damage_multipliers"][0], 1, "uncoloured damage multiplier")
    for slot, source in {104: "Spirit_Blade_1_Cost", 106: "Spirit_Blade_2_Cost", 108: "Spirit_Blade_3_Cost", 109: "Spirit_Round_Slash_Cost"}.items():
        close(rules["motions"][str(slot)]["gauge_cost"], value(source) * 100, str(slot))
    for slot, motion in rules["motions"].items():
        gains = motion["gauge_gain_by_window"]
        assert len(gains) == len(motion["outgoing"])
        for row in rules["authored_windows"].get(slot, []):
            expected = row.get("source_move", {}).get("Energy_Gen", 0) * 100 if row["contact_role"] == "weapon" else 0
            close(gains[row["window_id"]], expected, f"{slot} gain {row['window_id']}")
    delay = rules["delayed_hit"]
    for key, source in {"first_delay": "Helmbreaker_Hit_Delay_of_First_Hit", "between_delay": "Helmbreaker_Hit_Delay_of_Hits_Between_First_Last", "last_delay": "Helmbreaker_Hit_Delay_of_Last_Hit", "hit_limit": "Helmbreaker_Hit_Limit"}.items():
        close(delay[key], value(source), key)
    assert delay["hit_limit"] == 7 and delay["native_anchor_controller_verified"] is False
    pulse_count = 0
    for slot, triggers in delay["triggers"].items():
        for trigger in triggers:
            windows, offsets = trigger["pulse_window_ids"], trigger["pulse_offsets"]
            assert len(windows) == len(offsets) == 7 and len(set(windows)) == 7
            close(offsets[0], delay["first_delay"], "first pulse")
            for index in range(1, 7):
                close(offsets[index] - offsets[index - 1], delay["last_delay"] if index == 6 else delay["between_delay"], "pulse interval")
            for window, offset in zip(windows, offsets):
                close(rules["motions"][slot]["outgoing"][window][0], trigger["begin"] + offset, "delayed source window")
                assert rules["motions"][slot]["gauge_gain_by_window"][window] == 0
            pulse_count += len(windows)
    # Re-decode every shell, rather than trusting a previously emitted report.
    shells = load(ROOT / "research/mhw-original/combat-delayed-shells.json")
    geometry = delay["shell_geometry"]
    modifiers_checked = sound_slots_checked = 0
    for name, shell in shells.items():
        decoded = read(ROOT / shell["source"], "shlp")
        assert decoded["fields"] == shell["fields"], name
        fields = decoded["fields"]
        # The original delayed shells contain no Wwise container or playable
        # sound request. Seven damage pulses do not prove seven separate PCM
        # events; the source animation's existing TIML audio still plays.
        assert fields["Wwise_Container_Header"]["value"] == 0, name + " gained a sound container"
        for key, field in fields.items():
            if key.startswith("Sound_"):
                assert field["value"] == -1, name + ": unexpected delayed sound request " + key
                sound_slots_checked += 1
        checked_names = set()
        for output, source in {"shell_lifespan": "Projectile_Model_Lifespan", "shell_velocity": "Projectile_Velocity", "shell_hitbox_multiplier": "Projectile_Hitbox_Range"}.items():
            close(geometry[output], fields[source]["value"], name + " " + output)
        for key, field in fields.items():
            if key.endswith(".Name") and field["value"] in geometry["modifiers"]:
                prefix = key.rsplit(".", 1)[0] + "."
                values = [v["value"] for k, v in fields.items() if k.startswith(prefix + "Value_if_")]
                assert len(values) == 1
                close(geometry["modifiers"][field["value"]], values[0], name + " " + field["value"])
                checked_names.add(field["value"])
                modifiers_checked += 1
        assert checked_names == set(geometry["modifiers"]), name + " missing source modifier"
    collider_record = load(ROOT / "research/mhw-original/combat-shell-colliders.json")
    collider = read(ROOT / collider_record["source"], "col")
    assert collider["fields"] == collider_record["fields"]
    geometry_id = geometry["geometry_id"]
    range_field = f"Collider_Node_Param_.Collider_Geomtry_Params_[{geometry_id}].Collider_Geomtry_Param_Inner_[0].Attack_Range"
    close(geometry["native_attack_range"], collider["fields"][range_field]["value"], "shell COL attack range")
    # Independently decode each real COL bank and reconstruct the published
    # clip/stage timing. These are source facts, not a test of the simulation's
    # defensive windows or a claim to replay MHW's damage formula.
    collision_banks = collision_rows = source_move_rows = 0
    motion_values_checked=timelines_checked=0
    for mode in OFFSETS:
        folder = ROOT / "assets/modes" / mode
        document = load(folder / "combat_rules.json")
        catalog = load(folder / "catalog.json")
        evidence = load(ROOT / f"research/multiweapon/{mode}/collision-evidence.json")
        raw_timeline=source_timeline(mode)
        raw_banks={int(bank):read_col(ROOT/record['source']) for bank,record in evidence['banks'].items()}
        raw_actions={}
        for slot,action in raw_timeline['actions'].items():
            if not action:continue
            hits=[]
            for timeline in action['types']:
                if timeline['name']!='CollisionTimelineObject':continue
                nodes,selectors,flags=collision_fields(timeline)
                for i,key in enumerate(flags):
                    begin=key['frame'];end=flags[i+1]['frame'] if i+1<len(flags) else action['animation_length'];mask=int(key['value'])
                    if end<=begin:continue
                    for lane,keys in nodes.items():
                        if not mask&(1<<lane):continue
                        bank=collision_value_at(selectors.get(lane,[]),begin);node=collision_value_at(keys,begin)
                        name=next((r for r in raw_banks.get(bank,{}).get('names',[]) if r['node_id']==node),None)
                        hits.append(dict(start_frame=begin,end_frame=end,lane=lane,bank=bank,node_id=node,flag=mask,author_name=name['name'] if name else None,collision_record=name))
            if hits:raw_actions[slot]=hits
        assert raw_actions==evidence['actions'],mode+': original LMT/TIML and COL windows differ from recorded source'
        timelines_checked+=1
        reconstructed = authored(mode, catalog, evidence["actions"])
        attacks = {}
        for bank, record in evidence["banks"].items():
            attacks[int(bank)] = named_attack_fields(ROOT / record["source"])
            collision_banks += 1
        for slot, expected_rows in reconstructed.items():
            actual_rows = document["authored_windows"][slot]
            assert len(actual_rows) == len(expected_rows), f"{mode}/{slot}: authored row count differs"
            for actual, expected in zip(actual_rows, expected_rows):
                for key, value_expected in expected.items():
                    assert actual[key] == value_expected, f"{mode}/{slot}: source {key} differs"
                if expected["move_id"] is not None:
                    move = attacks.get(expected["bank"], {}).get(expected["move_id"], {})
                    assert actual["source_move"] == move, f"{mode}/{slot}: source COL attack values differ"
                    source_move_rows += 1
                expected_mv=actual.get('source_move',{}).get('Motion_Value',100)
                close(document['motions'][slot]['motion_value_by_window'][actual['window_id']],expected_mv,f'{mode}/{slot} window MV')
                motion_values_checked+=1
                collision_rows += 1
        assert set(document["authored_windows"]) == set(reconstructed), mode + ": collision slot coverage differs"
    weapon_parameters_checked = 0
    for mode, extension in {"sword_shield": "w01p", "greatsword": "w00p", "lance": "w06p", "bow": "w11p"}.items():
        record = load(ROOT / f"research/multiweapon/{mode}/combat-weapon-params.json")
        decoded = read(ROOT / record["source"], extension)
        assert decoded["fields"] == record["fields"], mode + ": original weapon parameters differ"
        assert decoded["decoded_bytes"] == record["decoded_bytes"] and 0 <= decoded["zero_padding_bytes"] < 16
        weapon_parameters_checked += len(decoded["fields"])
    native=native_controller_probe()
    assert native['sword_shield_constraint_attack']['bank']==3
    sns_constraint=load(ROOT/'assets/modes/sword_shield/combat_rules.json')
    for slot,node,mv in ((10319,46,45),(10341,54,65)):
        contact=sns_constraint['authored_windows'][str(slot)][0]
        assert contact['bank']==3 and contact['node_id']==node and contact['contact_role']=='constraint_attack'
        assert contact['native_bone_binding_known'] is False and contact['source_move']['Attack_Type']==1
        close(contact['source_move']['Motion_Value'],mv,'source SnS Rush3 constraint MV')
        close(sns_constraint['motions'][str(slot)]['motion_value_by_window'][0],mv,'published SnS Rush3 constraint MV')
        assert contact['source_geometry']==dict(type=1,native_attack_range=100,unnamed_inner_fields={'Unk_9':0,'Unk_10':-40,'Unk_11':60,'Unk_12':0},names_attachment_override=-1,exact_named_bone_unverified=True)
    lance=load(ROOT/'assets/modes/lance/combat_rules.json');moves=load(ROOT/'assets/modes/lance/moves.json')
    assert moves['commands']['counter']==[30120] and moves['hold_commands']['counter']['loop_frame']==150
    assert lance['motions']['30120']['counter_short_slot']==30122 and lance['motions']['30120']['counter_full_slot']==30121
    assert lance['motions']['30122']['completion_followup_slot']==30106
    close(lance['motions']['30120']['counter_full_seconds'],115/60,'source phase-3 local release policy')
    tcs=load(ROOT/'assets/modes/greatsword/combat_rules.json');enhanced={146:(275,120,144),189:(276,175,210),190:(277,211,264)}
    for slot,(finish,normal,weak) in enhanced.items():
        rule=tcs['motions'][str(20000+slot)];assert rule['true_charge'] and rule['weak_finish_scope']=='motion_has_weakpoint_first_hit'
        close(rule['weak_finish_multiplier'],weak/normal,'source true charge weak finish')
        assert rule['outgoing'][rule['true_charge_first_window']]==[37/60,43/60]
        assert rule['outgoing'][rule['true_charge_final_window']]==[101/60,113/60]
        proof=rule['weak_finish_evidence'];assert proof['source_finish_slot']==finish and proof['enhanced_finish_frames']==[46,58]
        assert proof['normal_motion_value']==normal and proof['enhanced_motion_value']==weak and proof['pose_branch_replayed'] is False
    sns=load(ROOT/'assets/modes/sword_shield/combat_rules.json');timeline=source_timeline('sword_shield')
    catalog={row['source_slot']:row for row in load(ROOT/'assets/modes/sword_shield/catalog.json')}
    for slot,(normal,just) in {344:(345,347),345:(346,348),347:(346,348),346:(319,341),348:(319,341)}.items():
        rule=sns['motions'][str(10000+slot)];branch=rule['timed_inputs'][0];duration=catalog[slot]['duration']
        assert [branch['begin'],branch['end']]==flag_intervals(timeline,slot,27,duration)[0]
        assert [branch['input_gate_begin'],branch['input_gate_end']]==flag_intervals(timeline,slot,28,duration)[0]
        assert branch['timed_slot']==10000+just and branch['normal_slot']==10000+normal
    tiers,charge=source_bow_tiers();bow=load(ROOT/'assets/modes/bow/combat_rules.json');bow_timeline=source_timeline('bow')
    assert bow['bow_charge_seconds']==charge
    shell_list=(ROOT/'research/multiweapon/extracted/hm/wp/wp11/shell/bow.shll').read_bytes();cursor=12;paths=[]
    for _ in range(struct.unpack_from('<III',shell_list)[2]):
        tag=struct.unpack_from('<I',shell_list,cursor)[0];cursor+=4
        if not tag:paths.append('');continue
        end=shell_list.index(0,cursor);paths.append(shell_list[cursor:end].decode('ascii').replace('\\','/'));cursor=end+1
    assert cursor==len(shell_list),'Original bow SHLL decode incomplete'
    for slot,values in tiers.items():
        rule=bow['motions'][str(50000+slot)];assert rule['projectile_tiers']==values
        for tier in values:assert paths[tier['shell_id']]+'.shlp'=='hm/wp/wp11/shell/data/'+tier['source_shell']
        markers=[key['frame']/60 for typ in bow_timeline['actions'][str(slot)]['types'] if typ['name']=='PlMotionCommon'
                 for field in typ['transforms'] if field['name']=='mTrigger' for key in field['keys'] if key['value']&1]
        assert rule['projectile_markers']==markers and len(rule['outgoing'])==len(markers)
    for slot,triggers in rules['delayed_hit']['triggers'].items():
        for trigger in triggers:
            for window in trigger['pulse_window_ids']:
                assert rules['motions'][slot]['motion_value_by_level_by_window'][window]==[0,10,15,25]
    print(json.dumps({"w03p_fields": len(original["fields"]), "source_parameter_checks": len(mapping) + 3 + 4 + 4,
                      "delayed_pulse_windows": pulse_count, "original_shells_redecoded": len(shells),
                      "shell_modifiers_verified": modifiers_checked, "shell_col_range_verified": True,
                      "empty_delayed_shell_sound_slots_verified": sound_slots_checked,
                      "original_collision_banks_redecoded": collision_banks, "authored_collision_rows_verified": collision_rows,
                      "original_collision_move_rows_verified": source_move_rows, "additional_weapon_parameter_fields_verified": weapon_parameters_checked,
                      "original_lmt_timelines_redecoded":timelines_checked,"motion_values_by_window_verified":motion_values_checked,
                      "native_reflection_classes_verified":len(native['classes']),"weapon_specific_control_sources_verified":True,
                      "sword_shield_constraint_bank_and_motion_values_verified":True,
                      "passed": True}))


if __name__ == "__main__":
    main()
