"""Resource-backed offline projection of the native presentation transforms.

World hand/finger rotations come from the native retarget oracle. Supporting
hand solving calls native/retarget.h through a tiny offline DLL. Mesh sole
clearance comes from actual source shoe vertices; source props use exact
authored hand-child attachments and already-normalized geometry.
"""
import ctypes
import struct
from functools import lru_cache
import numpy as np
from scipy.spatial.transform import Rotation
from endfield_profiles import ROOT,read

@lru_cache(maxsize=6)
def mode_root(mode):
 registry=read(ROOT/'assets/modes/index.json')
 return ROOT/next(row['data_root'] for row in registry['modes'] if row['id']==mode)

@lru_cache(maxsize=6)
def resources(mode):
 root=mode_root(mode)
 return {'root':root,'ground':read(root/'ground_tracks.json'),'weapon':read(root/'source_weapon_tracks.json'),'rig':read(root/'source_rig.json'),'moves':read(root/'moves.json'),'props':read(root/'props/index.json'),'contact_masks':read(root/'contact_masks.json') if (root/'contact_masks.json').exists() else None,'support_ground':read(root/'support_ground_tracks.json') if (root/'support_ground_tracks.json').exists() else None}

@lru_cache(maxsize=1)
def solver_library():
 path=ROOT/'build/offline-retarget-dual/Release/endfield_pose_solver.dll'
 if not path.is_file():path=ROOT/'build/offline-retarget/Release/endfield_pose_solver.dll'
 return ctypes.CDLL(str(path))

@lru_cache(maxsize=1)
def solver():
 library=solver_library()
 function=library.MheSolveSupportingHand
 function.argtypes=[ctypes.POINTER(ctypes.c_float)]*3+[ctypes.c_float]+[ctypes.POINTER(ctypes.c_float)]*2
 function.restype=ctypes.c_int
 return function

@lru_cache(maxsize=1)
def coupled_solver():
 function=solver_library().MheSolveTwoHandGrip
 function.argtypes=[ctypes.POINTER(ctypes.c_float)]*3+[ctypes.c_float]+[ctypes.POINTER(ctypes.c_float)]*2
 function.restype=ctypes.c_int
 return function

def pointer(array):return array.ctypes.data_as(ctypes.POINTER(ctypes.c_float))

def source_frame(meta,slot,frame):
 item=resources(meta['mode'])['weapon']['tracks'].get(str(slot))
 return item['frames'][min(frame,len(item['frames'])-1)] if item else None

def contact_weight(meta,slot,frame,source):
 if meta.get('contact_candidate_scratch'):return float(source['support_weight'])
 data=resources(meta['mode']);masks=data['contact_masks']
 if masks:
  model=meta.get('contact_model')
  if model is None:model='source:'+next(p['id'] for p in data['props']['props'] if p['role']=='main')
  row=masks['models'].get(model,{}).get('characters',{}).get(meta['character_id'],{}).get('tracks',{}).get(str(slot))
  if row:return float(row['weights'][min(frame,len(row['weights'])-1)])
 return float(source['support_weight'])

def solve_grip(scene,meta,clip,selection,world,strength=1.,blend_seconds=.18):
 data=resources(meta['mode']);basis=Rotation.from_quat(meta['source_basis_xyzw']).as_matrix()
 supporting=[10,11,12] if meta['mode']=='bow' else [6,7,8]
 dominant=8 if meta['mode']=='bow' else 12
 ids=[scene['index'][meta['bone_paths'][b]] for b in supporting+[dominant]]
 branch=[]
 for bone in ids[:3]:
  path=scene['paths'][bone]
  branch.append([i for i,p in enumerate(scene['paths']) if p==path or p.startswith(path+'/')])
 for f,frame in enumerate(selection):
  source=source_frame(meta,clip['slot'],int(frame))
  if not source:continue
  weight=contact_weight(meta,clip['slot'],int(frame),source)*strength*min(1.,float(frame)/clip['fps']/max(.01,blend_seconds))
  if weight<.001:continue
  offset=(basis@Rotation.from_quat(source['rotation_xyzw']).as_matrix()@np.asarray(source['support_offset'])*meta['scale']).astype(np.float32)
  points=world[f,ids,:3,3].reshape(-1).astype(np.float32)
  rotations=Rotation.from_matrix(world[f,ids[:3],:3,:3]).as_quat().reshape(-1).astype(np.float32)
  pole=np.array([-1,0,-1] if dominant==12 else [1,0,-1],dtype=np.float32)
  # Only move the dominant hand if the authored supporting contact exceeds
  # this target arm's measured reach. Existing reachable poses are unchanged.
  current=points.reshape(4,3);goal=current[2]*(1-weight)+(current[3]+offset)*weight
  upper_length=float(np.linalg.norm(current[1]-current[0]));lower_length=float(np.linalg.norm(current[2]-current[1]));reach=upper_length+lower_length
  epsilon=min(1e-4,min(upper_length,lower_length)*.001);requested=float(np.linalg.norm(goal-current[0]))
  use_coupled=abs(requested-float(np.clip(requested,abs(upper_length-lower_length)+epsilon,reach-epsilon)))>1e-6
  if use_coupled:
   dominant_chain=[10,11,12] if dominant==12 else [6,7,8]
   dual_ids=[scene['index'][meta['bone_paths'][b]] for b in dominant_chain+supporting]
   dual_points=world[f,dual_ids,:3,3].reshape(-1).astype(np.float32)
   dual_rotations=Rotation.from_matrix(world[f,dual_ids,:3,:3]).as_quat().reshape(-1).astype(np.float32)
   dual_poles=np.array([1,0,-1,-1,0,-1],dtype=np.float32);dual_output=np.empty(24,dtype=np.float32)
   if coupled_solver()(pointer(dual_points),pointer(dual_rotations),pointer(offset),weight,pointer(dual_poles),pointer(dual_output)):
    desired=Rotation.from_quat(dual_output.reshape(6,4)).as_matrix()
    for k,bone in enumerate(dual_ids):
     old=world[f,bone].copy();new=old.copy();new[:3,:3]=desired[k]
     path=scene['paths'][bone];indices=[i for i,p in enumerate(scene['paths']) if p==path or p.startswith(path+'/')]
     world[f,indices]=(new@np.linalg.inv(old))@world[f,indices]
    scene.setdefault('_coupled_frames',set()).add((meta['mode'],clip['slot'],int(frame)))
    continue
   scene.setdefault('_unreachable_contact',set()).add((meta['mode'],clip['slot'],int(frame)))
  output=np.empty(12,dtype=np.float32)
  if not solver()(pointer(points),pointer(rotations),pointer(offset),weight,pointer(pole),pointer(output)):
   raise ValueError('exact native supporting-hand solver rejected finite source frame')
  desired=Rotation.from_quat(output.reshape(3,4)).as_matrix()
  for k,bone in enumerate(ids[:3]):
   old=world[f,bone].copy();new=old.copy();new[:3,:3]=desired[k]
   change=new@np.linalg.inv(old)
   world[f,branch[k]]=change@world[f,branch[k]]
 return world

def ground_pose(scene,meta,clip,selection,world,probe_floor):
 source=resources(meta['mode'])['ground']['tracks'].get(str(clip['slot']))
 clearances=np.asarray(source['clearance'],np.float32)[selection]*meta['scale'] if source else np.zeros(len(selection),np.float32)
 target_floor=probe_floor(scene,world)
 correction=clearances-target_floor
 support=resources(meta['mode'])['support_ground']
 track=support['tracks'].get(str(clip['slot'])) if support else None
 def hand_floor(side):
  values=[]
  for probe in scene.get('hand_probes',[]):
   if probe['side']!=side:continue
   point=np.zeros((len(world),3),np.float32)
   for influence in probe['influences']:
    m=world[:,scene['index'][influence['bone_path']],:3,:];p=np.r_[influence['local_point'],1.];point+=np.einsum('fij,j->fi',m,p).astype(np.float32)*influence['weight']
   values.append(point[:,1])
  return np.asarray(values).min(0) if values else None
 if track:
  for side in ['left','right']:
   entry=track[side];kind=np.asarray(entry.get('kind',entry['active']),np.int8)[selection];active=np.asarray(entry['active'],bool)[selection]&(kind==1);values=hand_floor(side)
   if values is not None:
    desired=np.asarray(entry['clearance'],np.float32)[selection]*meta['scale']-values
    correction=np.where(active,np.maximum(correction,desired),correction)
  for side in ['left','right']:
   entry=track[side];kind=np.asarray(entry.get('kind',entry['active']),np.int8)[selection];active=(np.asarray(entry['active'],bool)[selection]&(kind==2))|(kind==3);values=hand_floor(side)
   if values is None:continue
   desired_clearance=np.where(kind==3,0,np.asarray(entry['clearance'],np.float32)[selection]*meta['scale'])
   desired=desired_clearance-(values+correction)
   arm=[6,7,8] if side=='left' else [10,11,12];ids=[scene['index'][meta['bone_paths'][i]] for i in arm];pole=np.array([-1,0,-1] if side=='left' else [1,0,-1],np.float32)
   for f in np.flatnonzero(active&(desired>1e-5)):
    points=world[f,ids,:3,3];goal=points[2].copy();goal[1]+=desired[f];inputs=np.concatenate([points,goal[None]]).reshape(-1).astype(np.float32)
    rotations=Rotation.from_matrix(world[f,ids,:3,:3]).as_quat().reshape(-1).astype(np.float32);offset=np.zeros(3,np.float32);output=np.empty(12,np.float32)
    if not solver()(pointer(inputs),pointer(rotations),pointer(offset),1.,pointer(pole),pointer(output)):raise ValueError('Hand ground arm solver rejected actual skin goal')
    solved=Rotation.from_quat(output.reshape(3,4)).as_matrix()
    for k,bone in enumerate(ids):
     old=world[f,bone].copy();new=old.copy();new[:3,:3]=solved[k];path=scene['paths'][bone];branch=[i for i,p in enumerate(scene['paths']) if p==path or p.startswith(path+'/')]
     world[f,branch]=(new@np.linalg.inv(old))@world[f,branch]
    scene.setdefault('_hand_ground_ik_frames',set()).add((meta['mode'],clip['slot'],int(selection[f]),side))
 world[:,:,:3,3]+=np.column_stack([np.zeros(len(selection)),correction,np.zeros(len(selection))])[:,None,:]
 return world

def transported_attachment(meta,world,scene,slot,frame,prop):
 source=source_frame(meta,slot,frame)
 if not source or 'attachments' not in source:return None
 function=prop['source_attachment_function'];hand_id=8 if prop['source_hand_function']==8 else 12
 side='left' if hand_id==8 else 'right';hand=source[side];attachment=source['attachments'].get(str(function))
 if not attachment:return None
 hand_world=world[0,scene['index'][meta['bone_paths'][hand_id]]]
 actual_source_rotation=Rotation.from_quat(hand['rotation_xyzw']).as_matrix()
 source_rest=Rotation.from_quat(meta['source_rest_xyzw'][hand_id]).as_matrix()
 aligned=Rotation.from_quat(meta['target_aligned_rest_xyzw'][hand_id]).as_matrix()
 basis=Rotation.from_quat(meta['source_basis_xyzw']).as_matrix()
 transport=hand_world[:3,:3]@aligned.T@basis@source_rest
 result=np.eye(4,dtype=np.float32)
 result[:3,:3]=transport@actual_source_rotation.T@Rotation.from_quat(attachment['rotation_xyzw']).as_matrix()
 result[:3,3]=hand_world[:3,3]+transport@actual_source_rotation.T@(np.asarray(attachment['position'])-hand['position'])*meta['scale']
 return result

@lru_cache(maxsize=6)
def prop_resources(mode):
 data=resources(mode);root=data['root'];manifest=read(root/'source_skin_tracks.json')
 props=[]
 for record in data['props']['props']:
  prop=read(root/record['file'])
  if 'source_attachment_function' not in prop:continue
  vertices=np.asarray(prop['vertices'],np.float32);homogeneous=np.column_stack([vertices,np.ones(len(vertices),np.float32)])
  props.append((record,prop,homogeneous,np.asarray(prop['skin_bone_indices'],np.int32),np.asarray(prop['skin_weights'],np.float32)))
 return manifest,props

@lru_cache(maxsize=64)
def skin_track(mode,slot):
 manifest,_=prop_resources(mode);record=manifest['tracks'].get(str(slot))
 if not record:return None
 path=resources(mode)['root']/record['file'];head=path.read_bytes()[:16];magic,frames,bones,fps=struct.unpack('<4sIII',head)
 assert magic==b'LSK1' and frames==record['frames'] and bones==record['bone_count']
 data=np.memmap(path,dtype='<f4',mode='r',offset=16,shape=(frames,bones,4,4)).transpose(0,1,3,2)
 return record['prop_id'],data

def posed_source_parts(scene,meta,clip,selection,world):
 manifest,props=prop_resources(meta['mode']);parts=[];own=[];count=world.shape[1]
 track=skin_track(meta['mode'],clip['slot'])
 for record,prop,homogeneous,indices,weights in props:
  palettes=[];bone_count=len(prop['rig_bones'])
  for f,frame in enumerate(selection):
   mount=transported_attachment(meta,world[f:f+1],scene,clip['slot'],int(frame),prop)
   if mount is None:break
   local=track[1][min(int(frame),len(track[1])-1)] if track and track[0]==record['id'] else np.repeat(np.eye(4,dtype=np.float32)[None],bone_count,axis=0)
   palettes.append(mount@local)
  if len(palettes)!=len(selection):continue
  own.append(np.asarray(palettes,np.float32))
  parts.append({'name':record['id'],'faces':np.asarray(prop['indices'],np.int32).reshape(-1,3),'vertices':np.asarray(prop['vertices'],np.float32),'bone_indices':indices+count,'weights':weights,'bind_vertices':np.repeat(homogeneous[:,None,:],4,axis=1),'role':record['role'],'mount_evidence':'actual authored source hand-child attachment through measured palm rest; root bind removed once; own LSK palette applied','source_mesh':record['file']})
  count+=bone_count
 return (np.concatenate([world]+own,axis=1) if own else world),parts
