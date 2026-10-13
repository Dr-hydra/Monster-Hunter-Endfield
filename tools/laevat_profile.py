"""Derive Laevatain profiles from local native references and exact mesh fields.

No game edits, full export, resource hashes, or generated asset distribution.
Run the small tools/laevat_inspector first; emitted resource evidence stays local.
"""
from pathlib import Path
import json
import re
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
RESEARCH=ROOT/'research/laevatain'
PRIMARY=['Bip001_Pelvis','Bip001_Spine','Bip001_Spine1','Bip001_Neck','Bip001_Head',
 'Bip001_L_Clavicle','Bip001_L_UpperArm','Bip001_L_Forearm','Bip001_L_Hand',
 'Bip001_R_Clavicle','Bip001_R_UpperArm','Bip001_R_Forearm','Bip001_R_Hand',
 'Bip001_L_Thigh','Bip001_L_Calf','Bip001_L_Foot','Bip001_L_Toe0',
 'Bip001_R_Thigh','Bip001_R_Calf','Bip001_R_Foot','Bip001_R_Toe0']
SEMANTICS=['hips','spine','chest','neck','head','left_shoulder','left_upper_arm','left_lower_arm','left_hand',
 'right_shoulder','right_upper_arm','right_lower_arm','right_hand','left_upper_leg','left_lower_leg',
 'left_foot','left_toes','right_upper_leg','right_lower_leg','right_foot','right_toes']

def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def write(path,data):
 path.parent.mkdir(parents=True,exist_ok=True)
 path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def matrix(t):
 x,y,z,w=[t['rotation'][c] for c in 'XYZW'];q=np.array([x,y,z,w],dtype=float);q/=np.linalg.norm(q);x,y,z,w=q
 m=np.eye(4);m[:3,:3]=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
  [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])@np.diag([t['scale'][c] for c in 'XYZ'])
 m[:3,3]=[t['position'][c] for c in 'XYZ'];return m
class Graph:
 def __init__(self,data):
  self.data=data;self.objects={o['id']:o for o in data['objects']};self.transforms={i:o for i,o in self.objects.items() if o['type']=='Transform'}
  self.paths={self.path(i):i for i in self.transforms}
 def name(self,i):return self.objects[self.transforms[i]['game_object']['id']]['name']
 def path(self,i):
  result=[];seen=set()
  while i in self.transforms:
   if i in seen:raise ValueError('transform cycle')
   seen.add(i);result.append(self.name(i));i=self.transforms[i]['parent']['id']
  return '/'.join(result[::-1])
 def world(self,i):
  t=self.transforms[i];parent=t['parent']['id'];return (self.world(parent) if parent in self.transforms else np.eye(4))@matrix(t)

def main():
 inventory=ROOT/'research/expansion-20261013'
 character=Graph(read(inventory/'special-weapons-raw.json'));weapon=Graph(read(inventory/'laevat-weapon-raw.json'))
 model='chr_0016_laevat_postmodel';skeleton=model+'/Root/Bip001'
 main_bones={}
 for name in PRIMARY:
  candidates=[i for path,i in character.paths.items() if path.startswith(skeleton+'/') and path.endswith('/'+name)]
  assert len(candidates)==1,(name,len(candidates));main_bones[name]=candidates[0]
 palettes=[o for o in character.objects.values() if o['type']=='SkinnedMeshRenderer' and o.get('game_object',{}).get('id') in character.objects]
 palette_ids=set()
 for renderer in palettes:
  if any(b.get('id')==main_bones['Bip001_Pelvis'] for b in renderer['bones']):palette_ids.update(b['id'] for b in renderer['bones'])
 assert set(main_bones.values())<=palette_ids,'primary bones not in main SMR palettes'
 hand=main_bones['Bip001_R_Hand'];socket_path=character.path(hand)+'/wep_R';socket_id=character.paths[socket_path]
 socket=character.transforms[socket_id];assert socket['parent']['id']==hand
 hp=[socket['position'][c] for c in 'XYZ'];hq=[socket['rotation'][c] for c in 'XYZW'];hq=(np.array(hq)/np.linalg.norm(hq)).tolist()
 mesh=next(o for o in weapon.objects.values() if o['type']=='Mesh' and o['name']=='S_wpn_misc_00016_02_lod0')
 renderer=next(o for o in weapon.objects.values() if o['type']=='SkinnedMeshRenderer' and o['mesh']['id']==mesh['id'])
 assert len(renderer['bones'])==1 and renderer['bones'][0]['id']==renderer['root_bone']['id']
 tree=(RESEARCH/'native-inspection/blade-mesh-tree.txt').read_text('utf-8')
 # The reader graph's flattened matrices use the backend's own field layout.
 # Exact Unity eRC fields avoid the historical row/column-major ambiguity.
 section=tree.split('vector m_BindPose',1)[1].split('vector m_BoneNameHashes',1)[0]
 elements={ (int(r),int(c)):float(v) for r,c,v in re.findall(r'float e(\d)(\d) = ([^\s]+)',section) }
 assert len(elements)==16
 bind=np.array([[elements[r,c] for c in range(4)] for r in range(4)])
 vertices=np.asarray(read(RESEARCH/'native-inspection/blade-geometry.json')['m_Vertices']).reshape(-1,3)
 assert len(vertices)==3070 and np.isfinite(vertices).all()
 bone=renderer['bones'][0]['id'];weapon_root=weapon.paths['wpn_misc_0016']
 bone_in_root=np.linalg.inv(weapon.world(weapon_root))@weapon.world(bone)
 skinned=(bone_in_root@bind@np.column_stack([vertices,np.ones(len(vertices))]).T).T[:,:3]
 direction=(bone_in_root@bind@np.array([0,0,1,0]))[:3];direction/=np.linalg.norm(direction)
 edge=(bone_in_root@bind@np.array([1,0,0,0]))[:3];edge/=np.linalg.norm(edge)
 assert np.linalg.norm(direction-[0,1,0])<1e-5
 asset=next(a for a in weapon.data['snapshot']['assets'] if a['path'].endswith('/wpn_misc_0016.prefab'))
 visual={
  'schema':1,'resource_id':'wpn_misc_0016','asset_path':asset['path'],'asset_path_hash':str(asset['pathHashHead']),
  'renderer_path':'Mesh_Sword/lod0/S_wpn_misc_00016_02_lod0','mount_bone_path':'Root/wep_quan_jnt/wep_jian_jnt',
  'mount_reference_path':'','mount_position':[0,0,0],'mount_rotation_xyzw':[0,0,0,1],
  'mount_mode':'experimental-authored-prefab-root-at-wep_R','mount_runtime_verified':False,
  'mount_evidence':'Preserve the authored prefab root TRS at wep_R; exact native skill attachment was not observed. WeaponMountPoint 100 is not assumed to be a hand attachment.',
  'blade_root_axis':[round(float(x),7) for x in direction],'edge_root_axis':[round(float(x),7) for x in edge],
  'blade_length_m':round(float((skinned@direction).max()),6),'material_policy':'readonly-native-shared-materials',
  'clone_policy':'pure-gameobjects-transforms-and-one-skinned-renderer','vfx_status':'native sword material only; complete flame VFX pending',
  'runtime_verified':False,'source_manifest':weapon.data['snapshot']['manifest_version']}
 hand_axis=(matrix(socket)[:3,:3]@direction);hand_axis/=np.linalg.norm(hand_axis)
 profile={'schema':1,'character_id':'chr_0016_laevat','display_name':'莱万汀','mode':'longsword','status':'experimental',
  'skeleton_root_path':skeleton,'bones':dict(zip(SEMANTICS,PRIMARY)),
  'practice_blade_axis':[round(float(x),7) for x in hand_axis],'blade_direction':visual['blade_root_axis'],
  'weapon':{'length_m':visual['blade_length_m'],'sockets':[{'name':'wep_R','hand_position':hp,'hand_rotation_xyzw':hq}],
   'grip_position':hp,'grip_rotation_xyzw':hq},'visual_weapon':visual,
  'evidence':{'source':'current VFS read-only extracted character/weapon graphs','manifest':weapon.data['snapshot']['manifest_version'],
   'main_bones':21,'socket_full_path':socket_path,'runtime_verified':False,'secondary_rig_excluded':True}}
 write(ROOT/'profiles/laevatain.json',profile);write(ROOT/'profiles/weapons/laevatain_longsword.json',visual)
 evidence={'vertex_count':len(vertices),'index_count':sum(s['index_count'] for s in mesh['submeshes']),
  'mesh_bounds_min':vertices.min(0).tolist(),'mesh_bounds_max':vertices.max(0).tolist(),
  'skinned_bounds_root_min':skinned.min(0).tolist(),'skinned_bounds_root_max':skinned.max(0).tolist(),
  'raw_bindpose_row_major_from_exact_typetree':bind.tolist(),'own_bone_in_prefab':bone_in_root.tolist(),
  'mesh_axis':'+Z','bone_axis':'+Y','prefab_root_axis':'+Y','renderer_local_position_ignored_for_skin_axis':{'X':2.717,'Y':-.132,'Z':0},
  'socket_local_position':hp,'socket_local_rotation_xyzw':hq,
  'primary_bones_in_main_palette':len(main_bones),'mountpoint_100_role':'Not proven a grip; must not drive a corrective rotation',
  'runtime_verified':False}
 write(RESEARCH/'profile-evidence.json',evidence)
 print(f'Laevatain: {len(main_bones)} main palette bones, single-bone native blade; root axis +Y, length {visual["blade_length_m"]} m; runtime not tested')

if __name__=='__main__':main()
