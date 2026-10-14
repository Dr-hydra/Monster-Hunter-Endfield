"""Measure source roll hand support from actual weighted MOD3 glove vertices.

This investigation does not rewrite clips or grounding. Run with portable
Blender in background; output includes full-skin hand minima and bind probes.
"""
from pathlib import Path
import importlib.util,json,sys
import numpy as np
import bpy
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('multi',ROOT/'tools/mhw_multi_bake.py');M=importlib.util.module_from_spec(spec);spec.loader.exec_module(M)
spec=importlib.util.spec_from_file_location('source',ROOT/'tools/mhw_source_model.py');S=importlib.util.module_from_spec(spec);spec.loader.exec_module(S)


def main():
 requested=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [];source_slot=int(requested[0]) if requested else 11
 bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
 base=ROOT/'research/mhw-original/extracted/pl/m_equip/pl043_0000';rig,_,_=S.import_model(base/'body/mod/m_body043_0000.mod3','Original hand support')
 _,meshes,_=S.import_model(base/'arm/mod/m_arm043_0000.mod3','Original glove skin',rig)
 helper=M.BASE.ensure_mhw_root_motion_bone(rig);mapping={int(b.name.split('_')[-1]):b.name for b in rig.data.bones if b.name.startswith('MhBone_')};inverse={v:k for k,v in mapping.items()}
 for p in rig.pose.bones:p.rotation_mode='QUATERNION'
 fingers=json.loads((ROOT/'assets/modes/sword_shield/finger_tracks.json').read_text(encoding='utf-8'))['bones'];hand_functions={side:{8 if side=='left' else 12}|{b['function'] for b in fingers if b['name'].startswith(side+'_')} for side in ['left','right']}
 rows={side:[] for side in hand_functions}
 for mesh in meshes:
  groups={g.index:g.name for g in mesh.vertex_groups}
  for vertex in mesh.data.vertices:
   weights=[(groups[g.group],float(g.weight)) for g in vertex.groups if groups.get(g.group) in inverse and g.weight>1e-7]
   p=mesh.matrix_world@vertex.co
   for side,functions in hand_functions.items():
    if sum(w for b,w in weights if inverse[b] in functions)<.85:continue
    total=sum(w for b,w in weights);rows[side].append({'mesh':mesh.name,'vertex':vertex.index,'rest_position':list(M.CONVERT@p),'influences':[{'function':inverse[b],'bone':b,'weight':w/total,'local_point':list(rig.data.bones[b].matrix_local.inverted()@p)} for b,w in weights]})
 assert all(rows.values());required={helper.name}
 for side,points in rows.items():
  for point in points:
   for inf in point['influences']:
    bone=rig.data.bones[inf['bone']]
    while bone:required.add(bone.name);bone=bone.parent
 probes=[]
 directions=list(np.eye(3))+list(-np.eye(3))+[np.array([x,y,z]) for x in [-1,1] for y in [-1,1] for z in [-1,1]]
 for side,points in rows.items():
  seen=set()
  for direction in directions:
   point=min(points,key=lambda p:float(np.dot(p['rest_position'],direction)));key=(point['mesh'],point['vertex'])
   if key in seen:continue
   seen.add(key);probes.append(dict(point,side=side))
 bones=sorted(required);bone_index={b:i for i,b in enumerate(bones)};prepared={}
 for side,points in rows.items():
  indices=[];weights=[];locals=[]
  for point in points:
   inf=point['influences'];indices.append([bone_index[i['bone']] for i in inf]+[0]*(4-len(inf)));weights.append([i['weight'] for i in inf]+[0]*(4-len(inf)));locals.append([i['local_point']+[1] for i in inf]+[[0,0,0,1]]*(4-len(inf)))
  prepared[side]=(np.asarray(indices,np.int32),np.asarray(weights,np.float32),np.asarray(locals,np.float32))
 lmt=M.BASE.read_lmt_file(ROOT/'research/multiweapon/extracted/hm/wp/wp01/mot/w01_00/w01_00.lmt');idle=M.BASE.compile_pose_evaluation(next(a for a in lmt.actions if a.id==1),mapping,version=lmt.header.version).sample(0)
 action=next(a for a in lmt.actions if a.id==source_slot);ev=M.BASE.compile_pose_evaluation(action,mapping,version=lmt.header.version)
 ground=json.loads((ROOT/'assets/modes/sword_shield/ground_tracks.json').read_text(encoding='utf-8'));neutral=ground['neutral_source_sole_floor'];sole=ground['tracks'][str(10000+source_slot)]['clearance'];results={side:[] for side in rows};raw={side:[] for side in rows};extremal={side:[] for side in rows}
 convert=np.asarray(M.CONVERT,np.float32)
 for frame in range(action.header.frame_count):
  for p in rig.pose.bones:p.location=(0,0,0);p.rotation_quaternion=(1,0,0,0);p.scale=(1,1,1)
  for (name,usage),value in ev.sample(frame,base=idle).items():
   name=name or helper.name;sem=M.BASE.get_usage_semantics(usage);value=M.BASE.adapt_track_frames_for_target_space(rig,M.BASE.TrackSpaceTarget('bone',name),sem,[(float(frame),value)])[0][1];setattr(rig.pose.bones[name],sem.blender_path_hint,value)
  bpy.context.view_layer.update();matrices=np.asarray([rig.pose.bones[b].matrix for b in bones],np.float32)
  for side,(indices,weights,locals) in prepared.items():
   points=np.einsum('vwij,vwj->vwi',matrices[indices],locals);points=np.einsum('vwi,vw->vi',points[:,:,:3],weights)@convert.T;index=int(points[:,1].argmin());minimum=float(points[index,1]);raw[side].append(minimum);results[side].append(max(0.,minimum-neutral));extremal[side].append({'mesh':rows[side][index]['mesh'],'vertex':rows[side][index]['vertex']})
 track={'fps':60,'frames':action.header.frame_count,'shoe_clearance':sole}
 for side in rows:
  index=next(b for b in fingers if b['name']==side+'_index0')['rest_position'];pinky=next(b for b in fingers if b['name']==side+'_pinky0')['rest_position'];radius=float(np.linalg.norm(np.asarray(index)-pinky)/2)
  mask=[int(h+radius<s) for h,s in zip(results[side],sole)];track[side]={'raw_skin_min_y':raw[side],'clearance':results[side],'hand_lower_than_shoes_mask':mask,'palm_half_width_m':radius,'actual_vertices':extremal[side]}
 report={'schema':1,'mode':'sword_shield','source_slot':source_slot,'published':False,'assets_mutated':False,'source_hand_vertices':{side:len(v) for side,v in rows.items()},'source_probes':probes,'neutral_source_floor':neutral,'track':track,'source_evidence':f'Actual original arm043 MOD3 skin weights/bindposes + original w01 action{source_slot} evaluated on full hunter hierarchy with original idle context','policy':'Source hand lower than source shoe sole by measured half-palm width identifies hand-support phases; no authored ground-contact flag claimed'}
 path=ROOT/f'research/multiweapon/source-sns{source_slot}-hand-ground.json';M.dump(path,report)
 print('Actual source hand skin vertices',report['source_hand_vertices'],'probe count',len(probes),flush=True)
 for frame in [0,8,12,16,24,36,48,59,72]:
  if frame>=track['frames']:continue
  print('frame',frame,'sole',round(sole[frame],5),'left hand',round(results['left'][frame],5),'right hand',round(results['right'][frame],5),'raw',round(raw['left'][frame],5),'left support',track['left']['hand_lower_than_shoes_mask'][frame],flush=True)


if __name__=='__main__':main()
