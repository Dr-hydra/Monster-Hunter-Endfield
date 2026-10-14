"""Publish correlated local combat rules and untouched authored collision lanes.

Requires already extracted source facts/COL/TIML. No game/service is started,
source curves are unchanged, and no content hash is calculated. Teaching
parameters and unknown native defensive timings remain separately labelled.
"""
from pathlib import Path
import json
import math
import sys
from mhw_decode_combat_params import read as read_parameter_file

ROOT = Path(__file__).resolve().parents[1]
OFFSETS = {'longsword': 0, 'sword_shield': 10000, 'greatsword': 20000,
           'lance': 30000, 'light_bowgun': 40000, 'bow': 50000}


def source_timeline(mode):
    sys.path.insert(0,str(ROOT/'research/mhw-longsword/20261012/tooling/mhw-anim-tools'))
    from core.formats.lmt.reader import read_lmt_file
    from core.formats.timl.reader import read_timl_data_bytes
    from core.formats.timl.semantics import format_timeline_parameter_label,format_datatype_hash_label
    number={'longsword':'03','sword_shield':'01','greatsword':'00','lance':'06','light_bowgun':'13','bow':'11'}[mode]
    path=ROOT/f'research/multiweapon/extracted/hm/wp/wp{number}/mot/w{number}_00/w{number}_00.lmt'
    if mode=='longsword':path=ROOT/'research/mhw-original/extracted/hm/wp/wp03/mot/w03_00/w03_00.lmt'
    raw=path.read_bytes();actions={}
    for action in read_lmt_file(path).actions:
        if not action.header.timl_offset:actions[str(action.id)]=None;continue
        timeline=read_timl_data_bytes(raw,data_offset=action.header.timl_offset)
        actions[str(action.id)]=dict(animation_length=timeline.animation_length,loop_start=timeline.loop_start_point,loop_control=timeline.loop_control,
            types=[dict(name=format_timeline_parameter_label(t.timeline_parameter_hash),hash=t.timeline_parameter_hash,
                transforms=[dict(name=format_datatype_hash_label(f.datatype_hash),hash=f.datatype_hash,type=f.data_type,
                    keys=[dict(frame=k.frame_timing,value=k.value,interpolation=k.interpolation,easing=k.easing) for k in f.keyframes]) for f in t.transforms]) for t in timeline.types])
    return dict(actions=actions)


def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def role(mode, row):
    name = row.get('author_name') or ''
    if '仲間' in name:
        return 'ally_only'
    if 'ディレイ' in name or 'delay' in name.lower():
        return 'delayed_effect'
    if mode=='sword_shield' and row['bank']==3:
        return 'constraint_attack'
    if mode == 'greatsword' and row['bank']:
        return 'body'
    if mode in ('sword_shield', 'lance') and row['bank']:
        return 'shield'
    return 'weapon'


def collision_row(mode, row, source, begin, end):
    return dict(begin=begin, end=end, lane=row['lane'], bank=row['bank'],
                node_id=row['node_id'], flag=row['flag'], source_slot=source,
                source_start_frame=row['start_frame'], source_end_frame=row['end_frame'],
                geometry_id=(row.get('collision_record') or {}).get('geometry_id'),
                move_id=(row.get('collision_record') or {}).get('move_id'),
                contact_role=role(mode, row), source_name=row.get('author_name'),
                timing_scope='source_col_timl')


def authored(mode, catalog, source):
    result = {}
    for item in catalog:
        if 'source_slot' not in item:
            continue
        slot, original, duration = item['slot'], item['source_slot'], item['duration']
        rows = []
        for row in source.get(str(original), []):
            begin, end = row['start_frame']/60, min(duration, row['end_frame']/60)
            if not (math.isfinite(begin) and math.isfinite(end) and 0 <= begin < end):
                continue
            rows.append(collision_row(mode, row, original, begin, end))
        result[str(slot)] = rows
    if mode == 'longsword':
        stage = load(ROOT/'research/mhw-original/helmbreaker-composition.json')['stage_map']
        rows = []
        for original in (170, 171, 172, 173):
            for row in source.get(str(original), []):
                active = [i for i, frame in enumerate(stage) if frame['slot'] == original and
                          row['start_frame'] <= frame['frame'] < row['end_frame']]
                if not active:
                    continue
                runs = []; first = last = active[0]
                for frame in active[1:]:
                    if frame > last + 1:
                        runs.append((first, last+1)); first = frame
                    last = frame
                runs.append((first, last+1))
                for start, end in runs:
                    copied = collision_row(mode, row, original, start/60, min(end, len(stage)-1)/60)
                    copied['composed_output_frames'] = [start, end]
                    if copied['end'] > copied['begin']:
                        rows.append(copied)
        result['1000'] = rows
    for rows in result.values():
        rows.sort(key=lambda x: (x['begin'], x['end'], x['lane'], x['bank'], x['node_id']))
        for index, row in enumerate(rows):
            row['window_id'] = index
    return result


def named_attack_fields(path):
    decoded=read_parameter_file(path,'col')
    fields=decoded['fields'];result={}
    for name,record in fields.items():
        if name.startswith('Moves_.') and name.endswith('.Index'):
            prefix=name.removesuffix('Index')
            result[record['value']]={key:fields[prefix+key]['value'] for key in
                ('Attack_Type','Motion_Value','Part_Modifier','Elemental_Modifier','Status_Modifier','Stun','Exhaust','Energy_Gen')
                if prefix+key in fields}
            if prefix+'Damage_Motion_Value' in fields:
                result[record['value']]['Motion_Value']=fields[prefix+'Damage_Motion_Value']['value']
                result[record['value']]['motion_value_source_field']='Damage_Motion_Value'
    return result


def flag_intervals(document,slot,bit,duration):
    fields=[f for t in document['actions'][str(slot)]['types'] if t['name']=='PlMotionCommon'
            for f in t['transforms'] if f['name']=='mFlag2']
    assert len(fields)==1
    keys=fields[0]['keys'];result=[]
    for i,key in enumerate(keys):
        end=keys[i+1]['frame']/60 if i+1<len(keys) else duration
        if key['value']&(1<<bit) and key['frame']/60<end:
            if result and abs(result[-1][1]-key['frame']/60)<1e-6:result[-1][1]=end
            else:result.append([key['frame']/60,end])
    return result


def source_bow_tiers():
    params=read_parameter_file(ROOT/'research/multiweapon/extracted/hm/wp/wp11/wp11_param.w11p','w11p')['fields']
    path=ROOT/'research/multiweapon/extracted/hm/wp/wp11/shell/collision/object.col'
    col=read_parameter_file(path,'col')['fields'];attacks=named_attack_fields(path);result={}
    for row,kind,base in ((0,'normal',0),(1,'gosya',10)):
        values=[]
        for level in range(1,5):
            name=f'bow_{"01" if row==0 else "02"}_{kind}_{level}.shlp'
            fields=read_parameter_file(ROOT/'research/multiweapon/extracted/hm/wp/wp11/shell/data'/name,'shlp')['fields']
            modifiers={}
            for key,field in fields.items():
                if key.startswith('Modifiers_') and key.endswith('.Name'):
                    prefix=key.removesuffix('Name');value=[v['value'] for k,v in fields.items() if k.startswith(prefix+'Value')]
                    assert value;modifiers[field['value']]=value[0] if len(value)==1 else value
            index=fields['Obj_Collision_Index']['value'];move=col[f'Names_[{index}].Move_Id']['value'];geometry=col[f'Names_[{index}].Collider_Geomtry_Param_Id']['value']
            radius=col[f'Collider_Node_Param_.Collider_Geomtry_Params_[{geometry}].Collider_Geomtry_Param_Inner_[0].Attack_Range']['value']
            values.append(dict(arrows=params[f'Arrows_Shot_[{row}].Level_{level}']['value'],shell_id=base+level-1,
                motion_value=attacks[move]['Motion_Value'],speed=fields['Projectile_Velocity']['value']*.01,
                range=modifiers['MaxRange']*.01,radius=radius*fields['Projectile_Hitbox_Range']['value']*.01,
                source_shell=name,source_collision_index=index,source_move=move,source_geometry=geometry,
                source_unit_scope='source_model_centimeters_to_published_meters',source_modifiers=modifiers))
        result[125 if row==0 else 136]=values
    return result,[params[f'Bow_Charge_[0].Level_{level}']['value'] for level in range(1,5)]


def native_longsword():
    path=ROOT/'research/mhw-original/combat-weapon-params.json'
    if not path.exists():
        value=read_parameter_file(ROOT/'research/mhw-original/extracted/hm/wp/wp03/wp03_param.w03p','w03p')
        path.write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8')
    fields=load(path)['fields'];value=lambda key:fields[key]['value']
    parameters=dict(foresight_minimum_gauge=100*value('Gauge_Required_to_Active_Foresight_That_Gives_45_I_Frames'),
        special_sheathe_seconds=value('Iai_Sheathed_Time'),special_sheathe_ready_seconds=73/60,
        gauge_level_cost=value('Gauge_Consumption_for_Helm_Plus_Iai_Spirit_Slash'),
        level_durations=[0]+[value('Gauge_Duration_'+level) for level in ('White','Yellow','Red')],
        helm_gauge_seconds=value('Spirit_Regeneration_Duration_After_Helm_Breaker_In_Seconds_'),
        red_helm_gauge_seconds=value('Spirit_Regeneration_After_Red_Gauge_Spirit_Helm_Breaker_Duration_in_Seconds_'),
        iai_gauge_seconds=value('Spirit_Regeneration_After_Iai_Slash_Duration_in_Seconds_'),
        iai_timed_gauge_seconds=value('Spirit_Regeneration_After_Timed_Iai_Slash_Duration_in_Seconds_'),
        gauge_upgrade_no_decay_seconds=value('Gauge_No_Decay_Time_after_Spirit_Gauge_Upgrade'),
        level_damage_multipliers=[1]+[value('Gauge_Damage_Buff_'+level) for level in ('White','Yellow','Red')])
    costs={104:100*value('Spirit_Blade_1_Cost'),106:100*value('Spirit_Blade_2_Cost'),
           108:100*value('Spirit_Blade_3_Cost'),109:100*value('Spirit_Round_Slash_Cost')}
    delay=dict(source_scope='source_w03p_shlp_col',
        first_delay=value('Helmbreaker_Hit_Delay_of_First_Hit'),
        between_delay=value('Helmbreaker_Hit_Delay_of_Hits_Between_First_Last'),
        last_delay=value('Helmbreaker_Hit_Delay_of_Last_Hit'),hit_limit=value('Helmbreaker_Hit_Limit'),
        triggers={},anchor_scope='local_target_contact_anchor',native_anchor_controller_verified=False,
        shell_geometry=dict(source='hm/wp/wp03/shell/collision/object.col',geometry_id=1,
            native_attack_range=100,range_scale=0.01,source_unit_scope='source_model_centimeters_to_published_meters',
            shell_hitbox_multiplier=1,shell_velocity=0,shell_lifespan=.5,
            modifiers=dict(CapsuleLength=0,HitStickTime=.5,HitStickEnableCollision=1,HitStickOffset=0)))
    shell_col=ROOT/'research/mhw-original/extracted/hm/wp/wp03/shell/collision/object.col'
    shell_fields=read_parameter_file(shell_col,'col')['fields'];shell_attacks=named_attack_fields(shell_col)
    shell_values=[0]
    for level in ('white','yellow','red'):
        shell=read_parameter_file(ROOT/f'research/mhw-original/extracted/hm/wp/wp03/shell/data/finish_delay_hit_{level}.shlp','shlp')['fields']
        node=shell['Obj_Collision_Index']['value'];move=shell_fields[f'Names_[{node}].Move_Id']['value']
        shell_values.append(shell_attacks[move]['Motion_Value'])
    delay['pulse_motion_values_by_level']=shell_values
    return parameters,costs,delay


def append_delayed_windows(delay,rows,motions,catalog):
    durations={str(row['slot']):row['duration'] for row in catalog}
    count=delay['hit_limit']
    if not isinstance(count,int) or not 1<=count<=32:raise ValueError('Unsafe source hit limit')
    offsets=[delay['first_delay']+delay['between_delay']*i for i in range(count)]
    if count>1:offsets[-1]=offsets[-2]+delay['last_delay']
    for slot in ('1000','173'):
        for row in rows.get(slot,[]):
            if row['source_slot']!=173 or row['node_id']!=82 or row['contact_role']!='delayed_effect':continue
            rule=motions[slot];identifiers=[]
            for offset in offsets:
                begin,end=row['begin']+offset,min(durations[slot],row['end']+offset)
                if not begin<end:raise ValueError('Delayed source pulse cannot fit published clip')
                identifiers.append(len(rule['outgoing']));rule['outgoing'].append([begin,end]);rule['gauge_gain_by_window'].append(0)
                rule['motion_value_by_window'].append(0)
                rule['motion_value_scope_by_window'].append('source_shlp_col_by_gauge_level')
                rule.setdefault('motion_value_by_level_by_window',[[mv]*4 for mv in rule['motion_value_by_window'][:-1]])
                rule['motion_value_by_level_by_window'].append(delay['pulse_motion_values_by_level'])
            delay['triggers'].setdefault(slot,[]).append(dict(window_id=row['window_id'],begin=row['begin'],end=row['end'],
                node_id=row['node_id'],geometry_id=row['geometry_id'],pulse_window_ids=identifiers,pulse_offsets=offsets,
                source_start_frame=row['source_start_frame'],source_end_frame=row['source_end_frame'],
                anchor_scope=delay['anchor_scope'],trigger_contact_geometry_known=False))


def main():
    basis = load(ROOT/'tools/data/mhw_combat_rule_sources.json')
    notes = load(ROOT/basis['notes_source'])['modes']
    native_parameters,spirit_costs,native_delay=native_longsword()
    summary = []
    for mode, offset in OFFSETS.items():
        folder = ROOT/f'assets/modes/{mode}'
        catalog, moves = load(folder/'catalog.json'), load(folder/'moves.json')
        evidence=load(ROOT/f'research/multiweapon/{mode}/collision-evidence.json')
        source=evidence['actions']
        rows = authored(mode, catalog, source)
        attacks={int(bank):named_attack_fields(ROOT/record['source']) for bank,record in evidence['banks'].items()}
        for collisions in rows.values():
            for row in collisions:
                if row['move_id'] is not None:
                    row['source_move']=attacks.get(row['bank'],{}).get(row['move_id'],{})
                if row['contact_role']=='constraint_attack':
                    geometry_fields=read_parameter_file(ROOT/evidence['banks'][str(row['bank'])]['source'],'col')['fields']
                    prefix=f"Collider_Node_Param_.Collider_Geomtry_Params_[{row['geometry_id']}].Collider_Geomtry_Param_Inner_[0]."
                    name_index=next(item['index'] for item in evidence['banks'][str(row['bank'])]['names'] if item['node_id']==row['node_id'])
                    row['source_bank_scope']='native_uWp01_wp01_02_registered_bank_3'
                    row['native_bone_binding_known']=False
                    row['geometry_scope']='local_weapon_sweep_approximation_of_source_constraint_attack'
                    row['source_geometry']=dict(type=geometry_fields['Collider_Node_Param_.Collider_Geomtry_Param_Type']['value'],
                        native_attack_range=geometry_fields[prefix+'Attack_Range']['value'],
                        unnamed_inner_fields={key:geometry_fields[prefix+key]['value'] for key in ('Unk_9','Unk_10','Unk_11','Unk_12')},
                        names_attachment_override=geometry_fields[f'Names_[{name_index}].Unk_1']['value'],exact_named_bone_unverified=True)
        labels = {item['label'] for item in notes[mode]['labels']}
        if not all(item in labels for item in basis['logical_rules'][mode]):
            raise ValueError('Missing original Hunter Notes basis for '+mode)
        motions = {}
        timeline=source_timeline(mode)
        bow_tiers,bow_charge=source_bow_tiers() if mode=='bow' else ({},[])
        for item in catalog:
            slot, original = item['slot'], item.get('source_slot', item['slot']-offset)
            collisions = rows.get(str(slot), [])
            rule = dict(outgoing=[[row['begin'], row['end']] for row in collisions], defensive=[],
                        gauge_gain=0, gauge_cost=0, reload_at=-1,
                        gauge_gain_by_window=[100*row.get('source_move',{}).get('Energy_Gen',0)
                            if mode=='longsword' and row['contact_role']=='weapon' else 0 for row in collisions],
                        hit_timing_scope='source_col_timl', defensive_timing_scope='unknown')
            rule['motion_value_by_window']=[row.get('source_move',{}).get('Motion_Value',100) for row in collisions]
            rule['motion_value_scope_by_window']=['source_col' if 'Motion_Value' in row.get('source_move',{}) else 'local_teaching_fallback' for row in collisions]
            rule['damage_scope']='local_base_damage_times_source_motion_value_divided_by_100_not_complete_mhw_damage'
            if mode == 'longsword':
                facts = {'spirit_blade': original in (104,106,108,109),
                         'roundslash': original == 109, 'foresight': original == 140,
                         'iai': original == 307, 'iai_spirit': original in (308,309,310,311),
                         'helm_thrust': original == 170, 'helm_finish': slot == 1000 or original == 173}
                rule.update(facts)
                if original in spirit_costs:rule['gauge_cost']=spirit_costs[original]
                rule['gauge_generation_scope']='source_col_base_without_hitzone_or_sharpness_multipliers'
                defensive = 'foresight' if facts['foresight'] else 'iai_spirit' if facts['iai_spirit'] else 'iai' if facts['iai'] else None
                if defensive:
                    rule['defensive'] = [basis['teaching_windows'][defensive]]
                    rule['defensive_timing_scope'] = basis['defensive_timing_scope']
            if slot in moves['commands'].get('guard', []):
                rule['guard'] = True
            if mode == 'greatsword' and original == 147:
                rule['tackle'] = True
            if mode == 'greatsword' and original in (146,189,190):
                enhanced_slot={146:275,189:276,190:277}[original]
                first=[r for r in collisions if r['source_start_frame']==37 and r['source_end_frame']==43 and r['contact_role']=='weapon']
                final=[r for r in collisions if r['source_start_frame']==101 and r['source_end_frame']==113 and r['contact_role']=='weapon']
                enhanced=[r for r in source[str(enhanced_slot)] if r['start_frame']==46 and r['end_frame']==58]
                assert len(first)==len(final)==len(enhanced)==1
                enhanced_mv=attacks[enhanced[0]['bank']][enhanced[0]['collision_record']['move_id']]['Motion_Value']
                rule.update(true_charge=True,true_charge_first_window=first[0]['window_id'],true_charge_final_window=final[0]['window_id'],
                    weak_finish_multiplier=enhanced_mv/final[0]['source_move']['Motion_Value'],weak_finish_scope='motion_has_weakpoint_first_hit',
                    weak_finish_evidence=dict(source_finish_slot=enhanced_slot,first_frames=[37,43],final_frames=[101,113],enhanced_finish_frames=[46,58],
                        normal_motion_value=final[0]['source_move']['Motion_Value'],enhanced_motion_value=enhanced_mv,pose_branch_replayed=False))
            if mode == 'lance' and original == 120:
                rule['lance_counter'] = True
                rule['counter_short_slot']=30122
                rule['counter_full_slot']=30121
                rule['counter_full_seconds']=115/60
                rule['defensive'] = [[0,item['duration']]]
                rule['defensive_timing_scope'] = 'local_hold_guard_controller_source_stance_pose'
                rule['counter_full_timing_scope']='source_action_phase_3_with_local_release_controller'
                rule['role_scope'] = 'source_native_charge_start_120_release_122_then_106_and_full_121'
            if mode=='lance' and original==122:
                rule['completion_followup_slot']=30106
                rule['completion_followup_scope']='source_native_cChargeStartStab_animation_complete'
            if mode=='sword_shield' and original in (344,345,346,347,348):
                normal,timed={344:(345,347),345:(346,348),347:(346,348),346:(319,341),348:(319,341)}[original]
                just=flag_intervals(timeline,original,27,item['duration']);gate=flag_intervals(timeline,original,28,item['duration'])
                assert len(just)==len(gate)==1 and gate[0][0]<=just[0][0]<just[0][1]<=gate[0][1]
                rule['timed_inputs']=[dict(command='special',begin=just[0][0],end=just[0][1],timed_slot=10000+timed,normal_slot=10000+normal,
                    input_gate_begin=gate[0][0],input_gate_end=gate[0][1],source_flag_bit=27,source_gate_bit=28,
                    source_scope='source_timl_flag2_native_cWp01RushBase_capture')]
            if mode=='sword_shield' and original in (319,341):
                rule['hit_followup_slot']=10320 if original==319 else 10342
                rule['hit_followup_scope']='local_target_contact_source_native_rush_const_stab_emstick'
            if mode=='sword_shield' and original in (320,342):
                rule['completion_followup_slot']=10321 if original==320 else 10343
                rule['completion_followup_scope']='source_native_rush_const_stab_emstick_to_land'
            if mode == 'light_bowgun' and original in (102,107):
                rule['reload'] = original == 102
                rule['continuous_reload'] = original == 107
                rule['reload_at'] = item['duration']
                rule['reload_timing_scope'] = 'local_completion_of_source_clip'
            if (mode == 'light_bowgun' and original in (82,85,88)) or (mode == 'bow' and (original in (125,136) or slot == 51000)):
                rule['projectile'] = True
            if mode=='bow' and slot==51001:rule['projectile']=False
            if mode=='bow' and original in bow_tiers and slot not in (51000,51001):
                markers=[key['frame']/60 for typ in timeline['actions'][str(original)]['types'] if typ['name']=='PlMotionCommon'
                         for field in typ['transforms'] if field['name']=='mTrigger' for key in field['keys'] if key['value']&1]
                assert markers==([0] if original==125 else [19/60])
                rule.update(projectile_tiers=bow_tiers[original],projectile_markers=markers,
                    projectile_scope='source_w11p_shll_shlp_col_native_normal_and_gosya',
                    projectile_marker_scope='source_timl_mTrigger_bit0',
                    outgoing=[[marker,item['duration']] for marker in markers],
                    gauge_gain_by_window=[0]*len(markers),motion_value_by_window=[100]*len(markers),
                    motion_value_scope_by_window=['projectile_tier_mv_applied_once_on_launch']*len(markers))
            motions[str(slot)] = rule
        parameters = {k:v for k,v in basis['parameters'].items() if k not in ('normal_gauge_gain','spirit_blade_cost')}
        data = dict(schema_version=1, mode=mode, numeric_scope=basis['numeric_scope'],
                    sources=[dict(kind='hunter_notes', path=basis['notes_source'], labels=basis['logical_rules'][mode]),
                             dict(kind='collision',path=f'research/multiweapon/{mode}/collision-evidence.json'),
                             dict(kind='timeline',path=f'research/multiweapon/{mode}/timeline-decoded.json')],
                    parameters=parameters, motions=motions, authored_windows=rows,
                    helm_followup=dict(slot=1000,start_seconds=1.5),
                    limits=basis['limitations'], native_damage_replayed=False)
        if mode=='longsword':
            data['numeric_scope']='mixed_source_base_and_local_teaching'
            data['source_parameters']=native_parameters
            data['source_parameters_evidence']='research/mhw-original/combat-weapon-params.json'
            data['delayed_hit']=json.loads(json.dumps(native_delay))
            append_delayed_windows(data['delayed_hit'],rows,motions,catalog)
        if mode=='bow':data['bow_charge_seconds']=bow_charge
        (folder/'combat_rules.json').write_text(json.dumps(data,ensure_ascii=False,separators=(',',':')),encoding='utf-8')
        if mode=='lance':
            moves['commands']['counter']=[30120]
            moves['hold_commands']['counter']=dict(frame=-1,loop=True,loop_frame=150,releases=[dict(seconds=0,slot=30122),dict(seconds=115/60,slot=30121)])
            moves['labels']['30120']='反击持盾准备';moves['labels']['30122']='取消突刺过渡'
        if mode=='sword_shield':
            moves['commands']['special']=[10344,10345,10346,10319,10320]
            for slot in (10344,10345,10346,10347,10348):moves.setdefault('hardware_routes',{}).setdefault(str(slot),{})['1']='special'
        if mode in ('lance','sword_shield'):(folder/'moves.json').write_text(json.dumps(moves,ensure_ascii=False,separators=(',',':')),encoding='utf-8')
        summary.append(dict(mode=mode,motions=len(motions),authored_windows=sum(len(v) for v in rows.values()),
                            excluded_ally_lanes=sum(x['contact_role']=='ally_only' for r in rows.values() for x in r),
                            delayed_effect_lanes=sum(x['contact_role']=='delayed_effect' for r in rows.values() for x in r)))
    print(json.dumps(summary,ensure_ascii=True))


if __name__ == '__main__':
    main()
