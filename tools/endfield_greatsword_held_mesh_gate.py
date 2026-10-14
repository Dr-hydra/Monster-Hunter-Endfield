"""Incrementally audit GS held clips against actual exported target/prop skins.

Keep previous core scans and their sample policies. New held clips use first,
middle and last frames for every character, with every frame for P0 and the
measured smallest/largest native-oracle scales. No game process is started.
"""
import argparse,copy,json,time
from pathlib import Path
import numpy as np
from endfield_profiles import ROOT,read,write
from endfield_mesh_audit import OUT,P0,oracle,setup,audit,audit_inputs,AUDIT_REVISION
from endfield_pose_projection import resources

SLOTS=[20145,20245]
EXPECTED_FRAMES={20145:53,20245:114}
OPTIONAL_EMPTY={'contact_model':None,'contact_schema':None,'contact_weights':{}}

def normalized(inputs,slots=None):
 result=copy.deepcopy(inputs)
 for key,value in OPTIONAL_EMPTY.items():result.setdefault(key,value)
 if slots is not None:
  result['clips']=[row for row in result['clips'] if row['slot'] in slots]
  result['source_own_skin']={key:value for key,value in result['source_own_skin'].items() if int(key) in slots}
 return result

def held_skin_evidence():
 data=resources('greatsword');skins=read(data['root']/'source_skin_tracks.json')
 validation_path=ROOT/'research/multiweapon/greatsword/bind-fallback-validation.json'
 validation=read(validation_path)
 assert validation['status']=='passed' and validation['generated_lsk_tracks']==0
 assert validation['source_prop_bones']==7 and validation['max_normalized_bind_identity_error']<1e-6
 result={}
 for slot in SLOTS:
  key=str(slot);track=skins['tracks'].get(key)
  if track:result[key]={'own_track':track};continue
  fallback=skins.get('bind_fallbacks',{}).get(key)
  assert fallback==validation['slots'].get(key),'Held original bind fallback evidence drift'
  assert fallback['slot']==slot and fallback['frames']==EXPECTED_FRAMES[slot]
  assert fallback['original_trigger_absent'] and not fallback['generated_own_track']
  assert fallback['original_lmt_timl_verified'],'Verify fallback against the original embedded LMT TIML'
  assert not fallback['source_trigger_keys'] and not fallback['source_interpolation_keys']
  assert 'mWeaponMotionNo' not in fallback['source_common_properties']
  assert not fallback['native_carry_state_verified'],'Do not certify unknown original carry state'
  assert fallback['prop_id'] in skins['props']
  result[key]={'original_bind_fallback':fallback,'validation':'research/multiweapon/greatsword/bind-fallback-validation.json','max_normalized_bind_identity_error':validation['max_normalized_bind_identity_error']}
 return result

def skin_geometry_evidence(value):
 result=copy.deepcopy(value)
 for row in result.values():
  # This positive verification field strengthens provenance without changing
  # any posed geometry. Earlier numeric scans can retain their exact input
  # floats after the original binary recheck has independently passed.
  row.get('original_bind_fallback',{}).pop('original_lmt_timl_verified',None)
 return result

def merge_row(base,extension,held_meta):
 old=base.get('incremental_held_audit')
 baseline=old['base_metrics'] if old else {key:copy.deepcopy(base[key]) for key in ['source_slots','frames','mesh_frames','finger_mesh_frames','source_weapon_mesh_frames','source_weapon_mesh_parts','skin_min_y','skin_max_y','minimum_mesh_frame','minimum_foot_mesh_y','foot_penetration_samples','evidence','errors','status','mesh_sample_policy']}
 assert not(set(SLOTS)&set(baseline['source_slots'])),'Baseline must keep original core only'
 result=copy.deepcopy(base)
 for key in ['frames','mesh_frames','finger_mesh_frames','source_weapon_mesh_frames']:result[key]=baseline[key]+extension[key]
 result['source_slots']=sorted(baseline['source_slots']+SLOTS)
 result['source_weapon_mesh_parts']=sorted(set(baseline['source_weapon_mesh_parts']+extension['source_weapon_mesh_parts']))
 result['skin_min_y']=min(baseline['skin_min_y'],extension['skin_min_y'])
 result['skin_max_y']=max(baseline['skin_max_y'],extension['skin_max_y'])
 if extension['skin_min_y']<baseline['skin_min_y']:result['minimum_mesh_frame']=extension['minimum_mesh_frame']
 result['minimum_foot_mesh_y']=min(baseline['minimum_foot_mesh_y'],extension['minimum_foot_mesh_y'])
 result['foot_penetration_samples']=baseline['foot_penetration_samples']+extension['foot_penetration_samples']
 result['errors']=baseline['errors']+extension['errors']
 result['status']='offline_mesh_checked' if not result['errors'] else 'offline_mesh_failed'
 result['evidence']=list(dict.fromkeys(baseline['evidence']+extension['evidence']))
 result['mesh_sample_policy']=baseline['mesh_sample_policy']+'; held 20145/20245: '+extension['mesh_sample_policy']
 inputs=normalized(result['audit_inputs'],baseline['source_slots'])
 inputs['clips']=sorted(inputs['clips']+held_meta['clips'],key=lambda row:row['slot'])
 inputs['source_own_skin'].update(extension['audit_inputs']['source_own_skin'])
 result['audit_inputs']=inputs
 result['incremental_held_audit']={
  'slots':SLOTS,'frames':sum(EXPECTED_FRAMES.values()),
  'base_metrics':baseline,'mesh_sample_policy':extension['mesh_sample_policy'],
  'selected_frames':extension['selected_frames'],'evidence':extension['evidence'],
  'source_skin_evidence':extension['source_skin_evidence'],
  'report':'research/character-coverage/greatsword-held-mesh-gate.json',
  'previous_core_replayed':False,'runtime_verified':False}
 return result

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--oracle-root',type=Path,default=OUT/'held-oracle')
 parser.add_argument('--characters',nargs='*')
 parser.add_argument('--no-merge',action='store_true')
 args=parser.parse_args()
 index=read(ROOT/'profiles/index.json');characters=[c['character_id'] for c in index['characters']]
 scales=[(oracle(cid,'greatsword')[0]['scale'],cid) for cid in characters]
 minimum=min(scales);maximum=max(scales)
 full=P0|{minimum[1],maximum[1]}
 selected=args.characters or characters
 report_path=OUT/'greatsword-held-mesh-gate.json'
 previous=read(report_path) if report_path.exists() else {}
 rows={row['character_id']:row for row in previous.get('characters',[])}
 coverage=read(OUT/'coverage.json');started=time.time()
 skin_evidence=held_skin_evidence()
 for cid in selected:
  meta,clips=oracle(cid,'greatsword',args.oracle_root)
  assert [clip['slot'] for clip in clips]==SLOTS
  assert {clip['slot']:len(clip['rotations']) for clip in clips}==EXPECTED_FRAMES
  assert meta['finger_paths'],'Actual hand/finger oracle is required'
  for clip in clips:assert len(clip['rotations'][0])==len(meta['bone_paths'])+len(meta['finger_paths'])
  scene=setup(cid,update_profile=False);inputs=audit_inputs(scene,meta)
  assert all(inputs['source_own_skin'][str(slot)] or str(slot) in skin_evidence for slot in SLOTS),'Held own-skin or original no-trigger bind evidence is required'
  base=next(row for row in coverage['combinations'] if row['character_id']==cid and row['mode']=='greatsword')
  base_meta,base_clips=oracle(cid,'greatsword')
  baseline_slots={clip['slot'] for clip in base_clips}
  assert normalized(base['audit_inputs'],baseline_slots)==normalized(audit_inputs(scene,base_meta),baseline_slots),cid+' existing core provenance changed; rerun affected core before merging'
  selections={clip['slot']:np.arange(len(clip['rotations'])) if cid in full else np.unique([0,len(clip['rotations'])//2,len(clip['rotations'])-1]) for clip in clips}
  explicit={str(slot):frames.tolist() for slot,frames in selections.items()}
  old=rows.get(cid)
  if old and old.get('audit_inputs')==inputs and old.get('selected_frames')==explicit and skin_geometry_evidence(old.get('source_skin_evidence',{}))==skin_geometry_evidence(skin_evidence) and old['status']=='offline_mesh_checked':
   item=old;print(cid,'cached',item['mesh_frames'],flush=True)
   item['source_skin_evidence']=skin_evidence
  else:
   item=audit(cid,'greatsword',scene,meta,clips,12,selections=selections,gallery_directory=OUT/'held-gallery',oracle_directory=args.oracle_root)
   item['selected_frames']=explicit
   item['mesh_sample_policy']='all 167 held frames' if cid in full else 'held first/middle/last: six actual mesh frames'
   item['audit_revision']=AUDIT_REVISION;item['audit_inputs']=inputs
   item['source_skin_evidence']=skin_evidence
   rows[cid]=item
   print(cid,'held mesh',item['mesh_frames'],'parts',item['mesh_parts'],'errors',len(item['errors']),'foot issues',len(item['foot_penetration_samples']),'elapsed',round(time.time()-started),flush=True)
  write(report_path,{'schema':1,'slots':SLOTS,'frames_per_character':167,'sample_policy':'Every character first/middle/last of each held clip; all frames P0 and oracle scale extrema','full_frame_characters':sorted(full),'scale_extrema':{'minimum':{'character_id':minimum[1],'scale':minimum[0]},'maximum':{'character_id':maximum[1],'scale':maximum[0]}},'characters':[rows[x] for x in characters if x in rows],'summary':{'characters':len(rows),'offline_mesh_checked':sum(x['status']=='offline_mesh_checked' for x in rows.values()),'mesh_frames':sum(x['mesh_frames'] for x in rows.values())},'operation':'Incremental native retarget oracle plus exact indexed target/source prop skin; previous core scan retained with exact semantic provenance; no game process','runtime_verified':False,'visual_quality_certified':False})
 if args.no_merge:return
 assert set(rows)==set(characters),'Complete all characters before updating coverage'
 assert all(row['status']=='offline_mesh_checked' and not row['foot_penetration_samples'] for row in rows.values()),'Held mesh gate failed'
 updated=[]
 for base in coverage['combinations']:
  if base['mode']=='greatsword':
   meta,_=oracle(base['character_id'],'greatsword',args.oracle_root)
   updated.append(merge_row(base,rows[base['character_id']],meta))
  else:updated.append(base)
 coverage['combinations']=updated
 coverage['summary']['greatsword_held_incremental_characters']=len(rows)
 coverage['summary']['greatsword_held_incremental_mesh_frames']=sum(x['mesh_frames'] for x in rows.values())
 write(OUT/'coverage.json',coverage)
 print('Merged33 current GS core rows; held source metadata and incremental sample provenance retained',flush=True)

if __name__=='__main__':main()
