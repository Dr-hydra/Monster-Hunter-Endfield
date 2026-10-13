"""Read cached Endfield table and native object graphs; no game or runtime changes."""
import json
from pathlib import Path
from collections import Counter
import numpy as np

BASE = Path(r'G:\Better Endfield')
OUT = Path(__file__).parent
TABLES = BASE / 'inputs/1.5.3/Windows/tables/Table'
GRAPHS = BASE / 'research/custom_model/1.5.3/shared/postmodel-native-parser-history/sweep-identities'
PRIMARY = ['Bip001_Pelvis','Bip001_Spine','Bip001_Spine1','Bip001_Neck','Bip001_Head',
 'Bip001_L_Clavicle','Bip001_L_UpperArm','Bip001_L_Forearm','Bip001_L_Hand',
 'Bip001_R_Clavicle','Bip001_R_UpperArm','Bip001_R_Forearm','Bip001_R_Hand',
 'Bip001_L_Thigh','Bip001_L_Calf','Bip001_L_Foot','Bip001_L_Toe0',
 'Bip001_R_Thigh','Bip001_R_Calf','Bip001_R_Foot','Bip001_R_Toe0']

def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

cn = read(TABLES/'I18nTextTable_CN.json')
text = read(TABLES/'TextTable.json')
chars = read(TABLES/'CharacterTable.json')
weapons = read(TABLES/'WeaponBasicTable.json')
catalog = {c['characterId']:c for c in read(BASE/'inputs/1.5.3/Windows/catalog/characters.json')['characters']}
def label(row):
    return cn.get(str(row.get('name',{}).get('id')),row.get('engName',''))
type_names = {int(k.removeprefix('LUA_WEAPON_TYPE_')):cn.get(str(v['id'])) for k,v in text.items() if k.startswith('LUA_WEAPON_TYPE_')}
character_summary = [{'id':k,'name':label(v),'weaponType':v.get('weaponType'),
 'weaponCategory':type_names.get(v.get('weaponType')),'defaultWeaponId':v.get('defaultWeaponId'),
 'rigFamily':catalog.get(k,{}).get('rigFamily'),'sizeType':catalog.get(k,{}).get('sizeType')}
 for k,v in chars.items()]

def mat(t):
    p = [t['position'][k] for k in 'XYZ']
    x,y,z,w = [t['rotation'][k] for k in 'XYZW']
    q = np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
                  [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
                  [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
    m = np.eye(4); m[:3,:3] = q @ np.diag([t['scale'][k] for k in 'XYZ']); m[:3,3] = p
    return m

results=[]
for path in sorted(GRAPHS.glob('chr_*.json')):
    graph=read(path); cid=graph['snapshot']['character']; objects={o['id']:o for o in graph['objects']}
    gos={o['id']:o for o in objects.values() if o['type']=='GameObject'}
    transforms={o['id']:o for o in objects.values() if o['type']=='Transform'}
    def name(tid):
        return gos.get(transforms.get(tid,{}).get('game_object',{}).get('id'),{}).get('name','')
    roots=[o for o in gos.values() if o.get('name')==cid+'_postmodel']
    if not roots:
        results.append({'id':cid,'error':'postmodel GameObject missing'});continue
    root=roots[0]
    root_tid=next((c['id'] for c in root['components'] if c['id'] in transforms),None)
    visited=set()
    def descend(tid):
        if tid not in transforms or tid in visited:return
        visited.add(tid)
        for ref in transforms[tid]['children']:descend(ref['id'])
    descend(root_tid)
    def depth(tid):
        count=0
        while tid in transforms and count<128:
            count+=1;tid=transforms[tid].get('parent',{}).get('id')
        return count
    byname={}
    for tid in sorted(visited,key=lambda t:(depth(t),t)):
        byname.setdefault(name(tid),tid)
    duplicate_primary={n:[t for t in sorted(visited) if name(t)==n] for n in PRIMARY}
    duplicate_primary={n:v for n,v in duplicate_primary.items() if len(v)>1}
    duplicate_paths={}
    for n,tids in duplicate_primary.items():
        duplicate_paths[n]=[]
        for tid in tids:
            chain=[];current=tid
            for _ in range(20):
                if current not in transforms:break
                chain.append(name(current));current=transforms[current].get('parent',{}).get('id')
            duplicate_paths[n].append('/'.join(reversed(chain)))
    cache={}
    def world(tid):
        if tid in cache:return cache[tid]
        if tid not in transforms:return np.eye(4)
        t=transforms[tid];m=mat(t);par=t.get('parent',{}).get('id')
        if par in transforms:m=world(par)@m
        cache[tid]=m;return m
    to_model=np.linalg.inv(world(root_tid))
    rows={}
    for n in PRIMARY:
        tid=byname.get(n)
        if not tid:continue
        t=transforms[tid];parent=t.get('parent',{}).get('id');ancestor=parent
        while ancestor in transforms and name(ancestor) not in PRIMARY:
            ancestor=transforms[ancestor].get('parent',{}).get('id')
        model=to_model@world(tid)
        rows[n]={'parent':name(parent),'primary_parent':name(ancestor),
         'position':t['position'],'rotation':t['rotation'],'scale':t['scale'],
         'model_position':model[:3,3].round(7).tolist()}
    body_bind=[]
    bind_rest={}
    bind_sources={}
    for o in objects.values():
        if o['type']!='SkinnedMeshRenderer':continue
        mesh=objects.get(o.get('mesh',{}).get('id'),{})
        go=gos.get(o.get('game_object',{}).get('id'),{})
        rt=next((c['id'] for c in go.get('components',[]) if c['id'] in visited),None)
        if rt and '_lod0' in mesh.get('name','') and len(o.get('bones',[]))==len(mesh.get('bindposes',[])):
            renderer=to_model@world(rt)
            for bone,bind in zip(o['bones'],mesh['bindposes']):
                n=name(bone['id'])
                if n not in PRIMARY or bone['id']!=byname.get(n):continue
                pose=renderer@np.linalg.inv(np.array(bind).reshape(4,4))
                bind_rest.setdefault(n,pose.round(7).tolist())
                bind_sources.setdefault(n,[]).append(mesh['name'])
        if '_body_' not in mesh.get('name','') or '_lod0' not in mesh.get('name',''):continue
        names=[name(b['id']) for b in o.get('bones',[])]
        body_bind.append({'mesh':mesh['name'],'bone_count':len(names),
                         'primary_missing':[n for n in PRIMARY if n not in names],
                         'bindpose_count':len(mesh.get('bindposes',[]))})
    palm=['Bip001_'+side+'_Finger'+str(i) for side in ['L','R'] for i in [1,2,4]]
    socket=[n for n in byname if any(s in n.lower() for s in ['wep_', 'weapon','holster','sheath'])]
    results.append({'id':cid,'manifestVersion':graph['snapshot'].get('manifest_version'),
       'runtimeVerified':graph.get('runtime_verified'),'resolved_transform_count':len(visited),
       'missing_primary':[n for n in PRIMARY if n not in rows],
       'missing_palm':[n for n in palm if n not in byname],
       'socket_names':sorted(socket),'duplicate_primary_paths':duplicate_paths,
       'socket_poses':{n:{'parent':name(transforms[byname[n]].get('parent',{}).get('id')),
                         'position':transforms[byname[n]]['position'],'rotation':transforms[byname[n]]['rotation']}
                        for n in ['wep_R','wep_L','wep_M','IK_Weapon_R_001','IK_Weapon_L_001'] if n in byname},
       'primary':rows,'body_meshes':body_bind})
    results[-1].update({'bind_rest':bind_rest,'bind_sources':bind_sources,'missing_bind_primary':[n for n in PRIMARY if n not in bind_rest]})

reference=next(r for r in results if r['id']=='chr_0003_endminf')
for r in results:
    if 'primary' not in r:continue
    r['topology_matches_female_admin']=not r['missing_primary'] and all(r['primary'][n]['primary_parent']==reference['primary'][n]['primary_parent'] for n in PRIMARY)
    diffs=[]
    for n,b in r['primary'].items():
        ref=reference['primary'][n]
        p=np.array(b['model_position']);rp=np.array(ref['model_position'])
        diffs.append(float(np.linalg.norm(p-rp)))
    r['max_primary_position_difference_m']=round(max(diffs,default=0),5)
    if not r['missing_primary']:
        pos=lambda n:np.array(r['primary'][n]['model_position'])
        r['serialized_height_head_to_toe_m']=round(float(pos('Bip001_Head')[1]-min(pos('Bip001_L_Toe0')[1],pos('Bip001_R_Toe0')[1])),4)
        r['arm_upper_lower_m']=[round(float(np.linalg.norm(pos(a)-pos(b))),4) for a,b in [('Bip001_R_UpperArm','Bip001_R_Forearm'),('Bip001_R_Forearm','Bip001_R_Hand')]]
        r['geometry_status']='unqualified_cached_pose' if max(r['arm_upper_lower_m'])>1.0 else 'serialized_prefab_transform_measurements_not_runtime_bindpose_validation'

groups=[]
for r in results:
    signature=tuple(round(float(r['primary'][n][field][axis]),5)
                    for n in PRIMARY for field,axes in [('position','XYZ'),('rotation','XYZW'),('scale','XYZ')]
                    for axis in axes)
    matched=next((g for g in groups if g['signature']==signature),None)
    if matched:matched['members'].append(r['id'])
    else:groups.append({'signature':signature,'members':[r['id']]})
pose_groups=[{'id':'P'+str(i+1),'members':g['members']} for i,g in enumerate(groups)]

result={'scope':'cached tables and offline native object graphs, no current VFS re-extraction or runtime validation',
 'table_source':read(BASE/'inputs/1.5.3/Windows/tables/source-metadata.json'),
 'type_names':type_names,'weapon_counts':dict(Counter(v['weaponType'] for v in weapons.values())),
 'characters':character_summary,'skeletons':results,'serialized_primary_pose_groups_rounded_1e5':pose_groups,
 'measurement_caution':'Shortest hierarchy path selects the character skeleton when duplicate bone names exist (Pograni includes four Soldier_Root/Rush child rigs). Prefab Transform values are cached and may include authoring/pose artifacts. bind_rest matrices were collected experimentally; renderer coordinate alignment and cross-mesh consistency are not qualified. Do not use these matrices as a release retarget rig.'}
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'endfield-adaptation-evidence.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
for t,count in sorted(result['weapon_counts'].items()):
    print(f'{t}: {type_names[t]} weapons={count}: '+', '.join(c['name']+' ('+c['id']+')' for c in character_summary if c['weaponType']==t))
print('Skeletons:',len(results))
print('Topology same:',sum(r.get('topology_matches_female_admin',False) for r in results))
print('Primary pose groups:',len(pose_groups))
for g in pose_groups:print(g['id'],','.join(g['members']))
print('Missing palm:',[(r['id'],r['missing_palm']) for r in results if r['missing_palm']])
print('Geometry unqualified:',[(r['id'],r['arm_upper_lower_m']) for r in results if r['geometry_status']=='unqualified_cached_pose'])
print('Duplicate primary names:',[(r['id'],len(r['duplicate_primary_paths'])) for r in results if r['duplicate_primary_paths']])
for n in ['wep_R','wep_L','wep_M','IK_Weapon_R_001','IK_Weapon_L_001']:
    print(n,sum(n in r['socket_names'] for r in results),'/ 32')
