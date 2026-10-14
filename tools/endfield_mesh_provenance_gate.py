"""Check saved mesh evidence against current semantic inputs, without hashes."""
import copy,time
from endfield_profiles import ROOT,read,write
from endfield_mesh_audit import OUT,setup,oracle,audit_inputs,AUDIT_REVISION
from endfield_pose_projection import resources
from endfield_greatsword_held_mesh_gate import held_skin_evidence

def normalized(value):
 result=copy.deepcopy(value)
 result.setdefault('contact_model',None)
 result.setdefault('contact_schema',None)
 result.setdefault('contact_weights',{})
 if result['contact_schema'] is None and not result['contact_weights']:result['contact_model']=None
 return result

def differences(saved,current):
 return [key for key in sorted(set(saved)|set(current)) if saved.get(key)!=current.get(key)]

def main():
 index=read(ROOT/'profiles/index.json');base=read(OUT/'coverage.json');special=read(OUT/'special-cross-coverage.json');rows=[];errors=[];start=time.time()
 exact_core=read(OUT/'final-core-oracle-exact-gate.json')
 assert exact_core['summary']['rows']==198 and exact_core['summary']['exact_array_equal']==198
 assert not exact_core['changed_rows'],'Every previously audited native core float must still match'
 assert len(base['combinations'])==198 and len(special['combinations'])==66
 for char in index['characters']:
  cid=char['character_id'];scene=setup(cid,update_profile=False)
  for mode in index['modes']:
   row=next(x for x in base['combinations'] if (x['character_id'],x['mode'])==(cid,mode))
   meta,clips=oracle(cid,mode)
   if row.get('incremental_held_audit'):
    held_meta,held=oracle(cid,mode,OUT/'held-oracle')
    assert held_meta['bone_paths']==meta['bone_paths'] and held_meta['finger_paths']==meta['finger_paths']
    meta['clips']=sorted(meta['clips']+held_meta['clips'],key=lambda x:x['slot'])
    clips=sorted(clips+held,key=lambda x:x['slot'])
    assert row['incremental_held_audit']['source_skin_evidence']==held_skin_evidence(),cid+' held original own-skin/bind fallback provenance changed'
   current=normalized(audit_inputs(scene,meta));saved=normalized(row['audit_inputs']);diff=differences(saved,current)
   if diff:errors.append({'character_id':cid,'mode':mode,'matrix':'base','changed_inputs':diff})
   moves=resources(mode)['moves'];required={int(moves['idle'])}
   required.update(int(slot) for sequence in moves.get('commands',moves.get('moves',{})).values() for slot in sequence)
   if required!=set(row['source_slots']):errors.append({'character_id':cid,'mode':mode,'matrix':'base','missing_required_slots':sorted(required-set(row['source_slots'])),'extra_slots':sorted(set(row['source_slots'])-required)})
   assert row['source_slots']==[c['slot'] for c in clips] and row['frames']==sum(len(c['rotations']) for c in clips),(cid,mode,'slot/frame coverage drift')
   assert row['audit_revision']==AUDIT_REVISION and row['status']=='offline_mesh_checked' and not row['errors']
   assert not row['foot_penetration_samples'],(cid,mode,'actual shoe penetration exceeds recorded 30 mm threshold')
   assert all((ROOT/p).exists() for p in row['evidence']),(cid,mode,'missing evidence')
   rows.append({'character_id':cid,'mode':mode,'matrix':'base','semantic_inputs_current':not diff,'required_core_covered':required==set(row['source_slots']),'source_slots':row['source_slots'],'source_frames':row['frames'],'actual_mesh_frames':row['mesh_frames'],'held_incremental':bool(row.get('incremental_held_audit')),'runtime_verified':False})
   if mode in ['longsword','bow']:
    s=next(x for x in special['combinations'] if (x['character_id'],x['mode'])==(cid,mode))
    meta['contact_model']=s['weapon'];diff=differences(normalized(s['audit_inputs']),normalized(audit_inputs(scene,meta)))
    if diff:errors.append({'character_id':cid,'mode':mode,'matrix':'special','changed_inputs':diff})
    assert s['source_slots']==[c['slot'] for c in clips] and s['source_frames']==sum(len(c['rotations']) for c in clips)
    assert s['audit_revision']==AUDIT_REVISION and s['status']=='offline_special_mesh_checked' and not s['errors']
    assert all((ROOT/p).exists() for p in s['evidence'])
    rows.append({'character_id':cid,'mode':mode,'matrix':'special','semantic_inputs_current':not diff,'source_slots':s['source_slots'],'source_frames':s['source_frames'],'actual_mesh_frames':s['actual_mesh_frames'],'runtime_verified':False})
  print(cid,'semantic inputs checked elapsed',round(time.time()-start),flush=True)
 write(OUT/'final-mesh-provenance.json',{'schema':1,'audit_revision':AUDIT_REVISION,'rows':rows,'errors':errors,'summary':{'base_combinations':198,'special_combinations':66,'current_input_rows':sum(x['semantic_inputs_current'] for x in rows),'required_core_rows':sum(x.get('required_core_covered',True) for x in rows),'changed_rows':len(errors),'native_core_exact_array_rows':198,'runtime_verified':0},'native_core_evidence':'research/character-coverage/final-core-oracle-exact-gate.json','operation':'Exact current required core plus rig/basis/shoe/finger/ownskin/contact/support-ground semantic comparison; held original no-trigger bind evidence checked; all previous native main/finger/hip arrays independently replayed; no artifact hashes; no source poses resampled','runtime_verified':False})
 assert not errors,errors[:8]
 print('All264 mesh evidence rows match current semantic inputs',flush=True)

if __name__=='__main__':main()
