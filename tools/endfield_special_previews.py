"""Actual target skin + authored independent native weapon snapshots.

Installation candidates are labeled; these renderings qualify coordinates and
mesh contact, not Unity shader, native skill mounting, or runtime certification.
"""
from pathlib import Path
import numpy as np
from endfield_profiles import read,write,ROOT
from laevat_profile import Graph
from endfield_mesh_audit import setup,oracle,fk,probe_floor,render,OUT
from endfield_pose_projection import ground_pose,transported_attachment,resources
from scipy.spatial.transform import Rotation
def geometry(graph,directory,resource):
 records=read(directory/'inspection.json');geo={r['id']:read(directory/r['file']) for r in records};result=[]
 for renderer in graph.objects.values():
  if renderer['type']!='SkinnedMeshRenderer' or renderer['mesh']['id'] not in geo:continue
  obj=geo[renderer['mesh']['id']];go=renderer['game_object']['id'];paths=[p for p,i in graph.paths.items() if graph.transforms[i]['game_object']['id']==go and p.startswith(resource+'/')]
  if not paths:continue
  mesh=graph.objects[renderer['mesh']['id']];skin=obj['m_Skin'];vertices=np.asarray(obj['m_Vertices'],np.float32).reshape(-1,3);indices=np.asarray([s['boneIndex'] for s in skin],np.int32);weights=np.asarray([s['weight'] for s in skin],np.float32);bind=np.asarray(mesh['bindposes'],np.float32).reshape(-1,4,4);v=np.column_stack([vertices,np.ones(len(vertices),np.float32)]);bound=np.einsum('vkij,vj->vki',bind[indices],v)
  result.append({'name':mesh['name'],'vertices':vertices,'faces':np.asarray(obj['m_Indices'],np.int32).reshape(-1,3),'weights':weights,'local_indices':indices,'bind_vertices':bound,'palette':[graph.path(b['id']) for b in renderer['bones']]})
 return result
def pose_weapon(scene,world,source,parts,resource,side,meta,slot,frame,draw=False):
 if resource=='wpn_misc_0016':
  data=resources('longsword');row=next(p for p in data['props']['props'] if p['role']=='main');prop=read(data['root']/row['file']);mount=transported_attachment(meta,world,scene,slot,frame,prop)
  if mount is None:raise ValueError('Laevatain mode adapter lacks an authored main attachment')
 else:
  # Original CharacterData names IK_Weapon_L_001. It is not a hand child in
  # the serialized model; runtime installs its exact hand-relative frame.
  socket=scene['r']['sockets']['IK_Weapon_L_001'];hand=scene['index'][meta['bone_paths'][8]];local=np.eye(4,dtype=np.float32);local[:3,:3]=Rotation.from_quat(socket['rotation_xyzw']).as_matrix();local[:3,3]=socket['position'];mount=world[0,hand]@local
 own=[];lookup={};inv_root=np.linalg.inv(source.world(source.paths[resource]));middle=resource+'/Root/wpn_bow_01_jnt/slot_M_string_01_vfx'
 for part in parts:
  for path in part['palette']:
   if path in lookup:continue
   local=inv_root@source.world(source.paths[path]);matrix=mount@local
   if draw and path==middle:
    finger=scene['r']['fingers']['Bip001_R_Finger22'];matrix[:3,3]=world[0,scene['index'][finger],:3,3]
   lookup[path]=len(scene['paths'])+len(own);own.append(matrix)
 clone=[]
 for part in parts:
  out=dict(part);out['bone_indices']=np.asarray([[lookup[part['palette'][i]] for i in row] for row in part['local_indices']],np.int32);clone.append(out)
 return np.concatenate([world,np.asarray(own,np.float32)[None]],axis=1),clone
def main():
 base=ROOT/'research/expansion-20261013';sword=Graph(read(base/'laevat-weapon-raw.json'));bow=Graph(read(base/'special-weapon-model-raw.json'));weapon_parts={'longsword':geometry(sword,ROOT/'research/weapon-coverage/laevat-geometry','wpn_misc_0016'),'bow':geometry(bow,ROOT/'research/typhoea/geometry','wpn_misc_0051')};evidence=[]
 destination=OUT/'special-gallery';destination.mkdir(exist_ok=True)
 for cid,mode in [('chr_0016_laevat','longsword'),('chr_0034_typhoea','bow'),('chr_0003_endminf','longsword'),('chr_0018_dapan','longsword')]:
  scene=setup(cid,update_profile=False);meta,clips=oracle(cid,mode);meta['contact_model']='wpn_misc_0016' if mode=='longsword' else 'wpn_misc_0051';by_slot={c['slot']:c for c in clips};sequence=[]
  if mode=='longsword':
   for slot in [101,102,104,106,107,140,307,311,1000]:
    if slot in by_slot:
     count=len(by_slot[slot]['rotations']);frames=[max(0,count//3),min(count-1,count*2//3)] if slot!=1000 else [0,count//4,count//2,count*3//4,count-1];sequence.extend((slot,f) for f in frames)
  else:
   for slot in [50110,50125,50136,50160,50161,51000,51001]:
    if slot in by_slot:sequence.append((slot,len(by_slot[slot]['rotations'])//2))
  for slot,frame in sequence:
   world=fk(scene,meta,by_slot[slot],np.array([frame]));world=ground_pose(scene,meta,by_slot[slot],np.array([frame]),world,probe_floor)
   world,props=pose_weapon(scene,world,sword if mode=='longsword' else bow,weapon_parts[mode],'wpn_misc_0016' if mode=='longsword' else 'wpn_misc_0051','R' if mode=='longsword' else 'L',meta,slot,frame,draw=mode=='bow' and slot in [50110,51001])
   for view in ['rear','side','front']:
    name=f'{cid}-{mode}-{slot}-{frame}-{view}.png';render(scene['parts']+props,world,destination/name,cid+'\n'+mode+f' {slot}:{frame} {view}\nSource geometry adapter' if mode=='longsword' else cid+'\n'+mode+f' {slot}:{frame} {view}\nNative IK left mount',view)
    evidence.append({'character_id':cid,'mode':mode,'slot':slot,'frame':frame,'view':view,'file':str((destination/name).relative_to(ROOT)).replace('\\','/'),'weapon': 'wpn_misc_0016' if mode=='longsword' else 'wpn_misc_0051','part_count':len(props),'source_installed_frame':'Exact source MHW attachment + identity native blade geometry-frame adapter' if mode=='longsword' else 'Original decoded IK_Weapon_L_001 expressed in actual target left hand','mount_source_verified':True,'native_skill_mount_verified':False if mode=='longsword' else True,'runtime_verified':False,'renderer_shader':'offline polygon material colors; native VFX/shader not simulated'})
  print(cid,mode,'snapshots',len(sequence)*3,flush=True)
 write(destination/'index.json',{'schema':1,'snapshots':evidence,'runtime_verified':False})
if __name__=='__main__':main()
