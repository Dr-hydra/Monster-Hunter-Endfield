"""Build character and equipment profiles from local, reference-preserving graphs.

All generated geometry stays in research/. No game process or resource hashes.
The native runtime uses its actual renderer bindposes; serialized rest matrices
below are offline evidence, not a hard-coded replacement for the live skeleton.
"""
import argparse
import json
import subprocess
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
from laevat_profile import Graph, PRIMARY, SEMANTICS, matrix

ROOT=Path(__file__).resolve().parents[1]
BE=Path(r'G:\Better Endfield')
CACHE=BE/'research/custom_model/1.5.3/shared/postmodel-native-parser-history'
TABLES=BE/'inputs/1.5.3/Windows/tables/Table'
MODES=['longsword','sword_shield','greatsword','lance','light_bowgun','bow']
DEFAULT={1:'longsword',2:'bow',3:'greatsword',5:'lance',6:'light_bowgun'}

def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def write(p,j):
 p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(j,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def pose(t):return {'position':[t['position'][x] for x in 'XYZ'],'rotation_xyzw':[t['rotation'][x] for x in 'XYZW'],'scale':[t['scale'][x] for x in 'XYZ']}
def analyze(cid,data):
 g=Graph(data);root=f'{cid}_postmodel';skel=root+'/Root/Bip001';byname={}
 for name in PRIMARY:
  ids=[i for p,i in g.paths.items() if p.startswith(skel+'/') and p.endswith('/'+name)]
  assert len(ids)==1,(cid,name,ids);byname[name]=ids[0]
 primary=set(byname.values());renderers=[];palette=set()
 for o in g.objects.values():
  if o['type']!='SkinnedMeshRenderer':continue
  ids={r['id'] for r in o['bones']};mesh=g.objects.get(o.get('mesh',{}).get('id'),{})
  if not ids.intersection(primary):continue
  go=o['game_object']['id'];path=next((p for p,i in g.paths.items() if g.transforms[i]['game_object']['id']==go),None)
  if not path or not path.startswith(root+'/'):continue
  renderers.append({'path':path,'mesh_name':mesh.get('name'),'mesh_id':mesh.get('id'),'vertex_count':mesh.get('vertex_count',0),'palette_paths':[g.path(x['id']) for x in o['bones']],'bindpose_count':len(mesh.get('bindposes',[])),'bindposes_source_layout':'native backend M00,M10,M20,M30 columns map to exact Unity eRC rows','bindposes_row_major':mesh.get('bindposes',[])})
  palette.update(ids)
 # Some central parent bones carry no vertices but are actual palette ancestors.
 covered=set(palette)
 for i in tuple(palette):
  while i in g.transforms:
   covered.add(i);i=g.transforms[i]['parent']['id']
 assert primary<=covered,(cid,'primary outside skin ancestry',primary-covered)
 bones={s:g.path(byname[n]) for s,n in zip(SEMANTICS,PRIMARY)}
 root_id=g.paths[root];to_root=np.linalg.inv(g.world(root_id));rest={s:(to_root@g.world(byname[n])).tolist() for s,n in zip(SEMANTICS,PRIMARY)}
 sockets={}
 for side in ['R','L','M']:
  matches=[(p,i) for p,i in g.paths.items() if p.startswith(skel+'/') and p.endswith('/wep_'+side)]
  assert len(matches)==1,(cid,'socket',side);p,i=matches[0];sockets['wep_'+side]={'name':'wep_'+side,'path':p,**pose(g.transforms[i])}
 for side,label in [('R','right'),('L','left')]:
  name='IK_Weapon_'+side+'_001';matches=[(p,i) for p,i in g.paths.items() if p.startswith(root+'/Root/IK_Root/') and p.endswith('/'+name)]
  if len(matches)!=1:raise ValueError((cid,'native IK socket identity',name))
  path,tid=matches[0];hand=byname['Bip001_'+side+'_Hand'];local=np.linalg.inv(g.world(hand))@g.world(tid)
  sockets[name]={'name':name,'path':path,'position':local[:3,3].tolist(),'rotation_xyzw':Rotation.from_matrix(local[:3,:3]).as_quat().tolist(),'scale':[1,1,1],'hand':label,'source_transform_is_hand_child':False,'evidence':'exact authored IK world frame expressed in actual same-character hand frame'}
 finger_bones={}
 for side in ['L','R']:
  for n in ['Finger0','Finger01','Finger02','Finger1','Finger11','Finger12','Finger2','Finger21','Finger22','Finger3','Finger31','Finger32','Finger4','Finger41','Finger42']:
   name=f'Bip001_{side}_{n}';matches=[(p,i) for p,i in g.paths.items() if p.startswith(skel+'/') and p.endswith('/'+name)]
   if len(matches)==1 and matches[0][1] in palette:finger_bones[name]=matches[0][0]
 lengths={}
 for side in ['left','right']:
  for label,a,b in [('upper_arm','upper_arm','lower_arm'),('forearm','lower_arm','hand'),('thigh','upper_leg','lower_leg'),('calf','lower_leg','foot')]:
   pa=np.asarray(rest[side+'_'+a])[:3,3];pb=np.asarray(rest[side+'_'+b])[:3,3];lengths[side+'_'+label]=float(np.linalg.norm(pb-pa))
 skin_rest={};rest_evidence={};registrations={};raw_skin_rest={};anchor_order=PRIMARY[:5]+PRIMARY[5:]
 for renderer in sorted(renderers,key=lambda r:('lod0' not in r['mesh_name'],'body' not in r['mesh_name'])):
  renderer_in_model=to_root@g.world(g.paths[renderer['path']])
  raw_matrices={path:renderer_in_model@np.linalg.inv(np.asarray(bp,dtype=float).reshape(4,4)) for path,bp in zip(renderer['palette_paths'],renderer['bindposes_row_major'])}
  available=[name for name in anchor_order if g.path(byname[name]) in raw_matrices]
  anchor=available[0];anchor_path=g.path(byname[anchor]);anchor_serial=to_root@g.world(byname[anchor]);registration=anchor_serial@np.linalg.inv(raw_matrices[anchor_path])
  registrations[renderer['path']]={'matrix':registration.T.reshape(-1).tolist(),'operation':'registeredModelRest = F_model * (rendererInModel * inverse(rawBindpose))','anchor_bone':anchor,'mesh_id':renderer['mesh_id'],'renderer_path':renderer['path'],'max_main_position_error_m':max(float(np.linalg.norm((registration@m)[:3,3]-(to_root@g.world(g.paths[path]))[:3,3])) for path,m in raw_matrices.items() if path in bones.values())}
  for path,bp in zip(renderer['palette_paths'],renderer['bindposes_row_major']):
   if path not in skin_rest:
    raw_skin_rest[path]=raw_matrices[path];skin_rest[path]=registration@raw_matrices[path];rest_evidence[path]={'kind':'native-palette-bindpose','renderer_path':renderer['path'],'mesh_id':renderer['mesh_id'],'frame_registration_required':True}
 # Skinless ancestors retain the authored relative transform. Verify this
 # against directly skinned parents in validation; no current animation read.
 for path in sorted(set(bones.values()),key=lambda p:-p.count('/')):
  if path in skin_rest:continue
  children=[p for p in skin_rest if g.transforms[g.paths[p]]['parent']['id']==g.paths[path]]
  assert children,(cid,'skinless parent has no palette child',path)
  child=min(children,key=lambda p:('Bip001_Spine' not in p,p))
  skin_rest[path]=skin_rest[child]@np.linalg.inv(matrix(g.transforms[g.paths[child]]));rest_evidence[path]={'kind':'palette-child-derived-parent','child_path':child,'serialized_child_local':pose(g.transforms[g.paths[child]])}
 registration=np.asarray(next(iter(registrations.values()))['matrix']).reshape(4,4).T
 registration_error={s:float(np.linalg.norm(skin_rest[p][:3,3]-np.asarray(rest[s])[:3,3])) for s,p in bones.items()}
 needed_paths=set(bones.values())|set(finger_bones.values())|{s['path'] for s in sockets.values()}
 for p in tuple(needed_paths):
  while p.startswith(skel+'/'):
   needed_paths.add(p);p=p.rsplit('/',1)[0]
 locals_by_path={p:pose(g.transforms[g.paths[p]]) for p in sorted(needed_paths)}
 return dict(graph=g,skeleton=skel,bones=bones,rest=rest,skin_rest=skin_rest,raw_skin_rest=raw_skin_rest,registration=registration,registrations=registrations,registration_error=registration_error,rest_evidence=rest_evidence,locals=locals_by_path,sockets=sockets,fingers=finger_bones,lengths=lengths,renderers=renderers,palette=palette)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--export-geometry',action='store_true');a=p.parse_args()
 chars=read(TABLES/'CharacterTable.json');texts=read(TABLES/'I18nTextTable_CN.json');equipment=read(TABLES/'WeaponBasicTable.json');catalog=read(BE/'inputs/1.5.3/Windows/catalog/characters.json');cc={x['characterId']:x for x in catalog['characters']}
 out=ROOT/'research/character-coverage';entries=[];analysis=[]
 for cid,row in chars.items():
  if cid=='chr_9000_endmin':continue
  src=out/(cid+'.json') if cid=='chr_0038_purrche' else CACHE/'sweep-identities'/(cid+'.json')
  if not src.exists():raise FileNotFoundError(src)
  data=read(src);r=analyze(cid,data);name=texts.get(str(row.get('name',{}).get('id')),cid)
  label=('女' if cid.endswith('endminf') else '男')+'管理员' if 'endmin' in cid else name
  basename={'chr_0003_endminf':'female_admin','chr_0016_laevat':'laevatain','chr_0034_typhoea':'typhoea'}.get(cid,cid)
  old=read(ROOT/'profiles'/(basename+'.json')) if (ROOT/'profiles'/(basename+'.json')).exists() else {}
  s=r['sockets']['wep_R'];q=s['rotation_xyzw'];pos=s['position']
  profile={'schema':2,'schema_version':2,'character_id':cid,'display_name':label,'label':label,'mode':DEFAULT[row['weaponType']],'supported_modes':MODES,'status':'offline-profile-qualified','experimental':False,'skeleton_root_path':r['skeleton'],'bones':dict(zip(SEMANTICS,PRIMARY)),'bone_paths':r['bones'],'finger_bones':r['fingers'],'practice_blade_axis':old.get('practice_blade_axis',[0,1,0]),'blade_direction':old.get('blade_direction',[0,1,0]),'weapon':{'length_m':old.get('weapon',{}).get('length_m',1.25),'grip_position':pos,'grip_rotation_xyzw':q,'sockets':[{'name':name,'path':item['path'],'hand':'left' if name in ['wep_L','IK_Weapon_L_001'] else 'right','hand_position':item['position'],'hand_rotation_xyzw':item['rotation_xyzw']} for name,item in r['sockets'].items() if name!='wep_M']},'mount_sockets':r['sockets'],'body_proportions':r['lengths'],'default_weapon_id':row.get('defaultWeaponId'),'weapon_type':row['weaponType'],'rig_family':cc.get(cid,{}).get('rigFamily'),'visual_weapons':{},'evidence':{'source':'current local tables + reference-preserving native postmodel graph','source_graph':str(src),'manifest':data['snapshot']['manifest_version'],'main_bones':21,'main_palette_bone_count':len(r['palette']),'main_renderer_count':len(r['renderers']),'runtime_verified':False,'primary_skin_palette_verified':True,'secondary_rig_excluded':True,'runtime_rest_policy':'derive from actual renderer bindposes, never apply cached serialized matrices as live rest'}}
  if 'visual_weapon' in old:profile['visual_weapon']=old['visual_weapon'];profile['visual_weapons']['longsword']=old['visual_weapon']
  for key in ['ground_foot_probes','ground_foot_mesh_rest_min_y','secondary_skin_roots']:
   if key in old:profile[key]=old[key]
  profile['rest_local_transforms']={path.rsplit('/',1)[-1]:value for path,value in r['locals'].items()}
  profile['rest_local_paths']=r['locals']
  profile['ancestor_palette_main_bones']=[name for name,semantic in zip(PRIMARY,SEMANTICS) if r['graph'].paths[r['bones'][semantic]] not in r['palette']]
  profile['rest_model_matrices']={name:r['skin_rest'][r['bones'][semantic]].T.reshape(-1).tolist() for name,semantic in zip(PRIMARY,SEMANTICS)}
  profile['rest_model_evidence']={name:r['rest_evidence'][r['bones'][semantic]] for name,semantic in zip(PRIMARY,SEMANTICS)}
  profile['mesh_to_model_bind_registration']=r['registration'].T.reshape(-1).tolist()
  profile['bind_registration_by_renderer']=r['registrations']
  profile['bind_registration_max_position_error_m']=max(r['registration_error'].values())
  profile['bind_registration_policy']='offline raw native bindpose frame -> authored model frame; runtime apply only if actual bindposes still use that raw frame, never twice'
  profile['rest_matrix_policy']='prefer live actual renderer bindposes; missing parent from palette child plus authored static local chain; offline matrices are client-version-qualified fallback'
  for mode,res in [('longsword','laevatain_full'),('bow','typhoea_bow')]:
   path=ROOT/'profiles/weapons'/(res+'.json')
   if path.exists():profile['visual_weapons'][mode]=read(path)
  if 'visual_weapon' in profile and 'longsword' in profile['visual_weapons']:
   profile['visual_weapon']=profile['visual_weapons']['longsword']
  write(ROOT/'profiles'/(basename+'.json'),profile)
  write(out/(cid+'-rig.json'),{'schema':1,'character_id':cid,'manifest':data['snapshot']['manifest_version'],'serialized_model_rest':r['rest'],'registered_model_rest':{path:m.tolist() for path,m in r['skin_rest'].items()},'bind_registration_by_renderer':r['registrations'],'renderers':r['renderers'],'body_proportions':r['lengths'],'runtime_verified':False,'main_skeleton_root':r['skeleton']})
  if a.export_geometry:
   inputs=out/'purrche-inputs' if cid=='chr_0038_purrche' else CACHE/'sweep-offline'/cid/'inputs'
   dest=out/'geometry'/cid
   if not (dest/'inspection.json').exists():
    subprocess.run(['dotnet',str(ROOT/'tools/endfield_weapon_inspector/bin/Release/net8.0-windows/EndfieldWeaponInspector.dll'),str(inputs),str(dest),'S_actor_'+cid.split('_',2)[2]+'.*lod0$'],check=True,stdout=subprocess.DEVNULL)
  item={'character_id':cid,'label':label,'profile':basename+'.json','default_mode':profile['mode'],'supported_modes':MODES,'status':'offline-profile-qualified','runtime_verified':False,'source_mesh_available':True,'finger_bones':len(r['fingers'])};entries.append(item)
  analysis.append({'character_id':cid,'main_bones':21,'main_palette_bones':len(r['palette']),'finger_bones':len(r['fingers']),'renderers':len(r['renderers']),'arm_lengths':r['lengths'],'full_path_isolation':True})
  print(f'{cid}: 21 primary, {len(r["fingers"])} finger bones, {len(r["renderers"])} body renderers',flush=True)
 assert len(entries)==33,len(entries)
 write(ROOT/'profiles/index.json',{'schema':2,'source_manifest':entries and read(out/'chr_0038_purrche.json')['snapshot']['manifest_version'],'characters':entries,'modes':MODES,'runtime_verified':False})
 write(out/'profile-validation.json',{'schema':1,'characters':analysis,'character_count':len(entries),'combinations':len(entries)*len(MODES),'scope':'exact named primary bones in actual skin palettes, actual full paths, native sockets and rest topology; all runtime display still untested'})
 weapon_rows=[]
 for wid,row in equipment.items():
  model=row.get('modelPath','');path='assets/beyond/dynamicassets/'+model.lower()
  weapon_rows.append({'weapon_id':wid,'weapon_type':row['weaponType'],'default_mode':DEFAULT.get(row['weaponType']),'applicable_modes':['longsword','sword_shield'] if row['weaponType']==1 else [DEFAULT[row['weaponType']]],'model_path':path,'source_model_path':model,'geometry_policy':'native independent prefab with own skin/bindpose; incompatible class uses mode source model','runtime_verified':False,'original_restore_required':True})
 assert len(weapon_rows)==80,len(weapon_rows)
 write(ROOT/'profiles/weapons/equipment-index.json',{'schema':1,'equipment':weapon_rows,'equipment_count':len(weapon_rows),'shared_model_count':len({x['model_path'] for x in weapon_rows}),'runtime_verified':False})

if __name__=='__main__':main()
