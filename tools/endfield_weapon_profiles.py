"""Offline special-weapon and all-equipment resource validation/visual profiles."""
import argparse,json,os,subprocess,sys
from pathlib import Path
import numpy as np
from laevat_profile import Graph,matrix

ROOT=Path(__file__).resolve().parents[1];BE=Path(r'G:\Better Endfield');OUT=ROOT/'research/weapon-coverage'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def write(p,j):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(j,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def relative(g,p):return p.split('/',1)[1] if '/' in p else ''
def vec(t):return [t[x] for x in 'XYZ']
def special():
 source=ROOT/'research/expansion-20261013';lae=Graph(read(source/'laevat-weapon-raw.json'));bow=Graph(read(source/'special-weapon-model-raw.json'))
 base=read(ROOT/'profiles/weapons/laevatain_longsword.json');full=dict(base);full.update({'schema':2,'id':'laevatain_blade','display_name':'莱万汀火焰长刀','mount_side':'right','renderer_paths':['Mesh_Sword/lod0/S_wpn_misc_00016_02_lod0','Mesh_Ring/lod0/S_wpn_misc_00016_01_lod0','Mesh_VFX/lod0/S_wpn_misc_00016_03_lod0'],'clone_policy':'pure-gameobjects-transforms-multiple-owned-skinned-renderers','vfx_status':'own 38-bone VFX/ring/native materials; independent material timeline; original skill installation not yet decoded','controls':{'type':'flame_blade','start_duration':.6,'material_effects':[{'renderer_path':'Mesh_Sword/lod0/S_wpn_misc_00016_02_lod0','property':'_VFXColorIntensity','rest_value':80,'start_duration':.6},{'renderer_path':'Mesh_Sword/lod0/S_wpn_misc_00016_02_lod0','property':'_VFXColorAlpha','rest_value':6,'start_duration':.6}],'source_start_asset':'p_laevat_skill_ult_wep_01_sword_start_asset','source_envelope_evidence':'native .asset duration=0.6; blade native material VFXColorIntensity80/VFXColorAlpha6; material property block on clone only'},'parts':[{'renderer':p,'bones':n} for p,n in zip(['blade','ring','chain_vfx'],[1,1,38])]})
 full['mount_mode']='resource-derived-MHW-mode-blade-adapter'
 full['mount_evidence']='Native blade skin from exact bindpose has root blade +Y / transverse +X / face normal +Z. Original normalized MHW bs_swo001 has the same geometry frame and grip origin. In MHW modes drive the clone root with transported actual source main attachment; preserve wrists and own blade/ring/VFX palettes. Original skill StaticWeaponData10 default GEN_wep_M_1_final is rest data, not a proven dynamic held mount.'
 full['mount_driver']='mhw_source_attachment'
 full['source_model_adapter']={'mode':'longsword','source_prop':'assets/modes/longsword/props/bs_swo001.json','source_attachment_function':47,'source_hand_function':12,'geometry_rotation_xyzw':[0,0,0,1],'geometry_position':[0,0,0],'native_blade_axis':[0,1,0],'source_blade_axis':[0,1,0],'native_transverse_axis':[1,0,0],'source_transverse_axis':[1,0,0],'geometry_frame_verified':True,'native_skill_held_mount_verified':False,'evidence':'Actual indexed source/native skinned meshes; imported MHW root bind removed exactly once. Shared +Y blade/+X width/+Z face normal, grip origin0. MHW attachment is transported through aligned source hand rest, not copied target wep_R rest.'}
 full['native_skill_mount']={'weapon_index':10,'default_character_point':152,'default_transform':'GEN_wep_M_1_final','held_mount_verified':False,'source_config':'research/weapon-coverage/config-trees/asset-data_chr_0016_laevat.decoded.json','original_animation':'A_actor_laevat_battle_attack_ult_01','animation_tracks':312,'animation_frames':311,'original_animation_acl_verified':True}
 write(ROOT/'profiles/weapons/laevatain_full.json',full)
 root='wpn_misc_0051';asset=next(a for a in bow.data['snapshot']['assets'] if a['path'].endswith('/wpn_misc_0051.prefab'))
 center=root+'/Root/wpn_bow_01_jnt/slot_M_string_01_vfx';up=root+'/Root/wpn_bow_01_jnt/slot_up_string_01_vfx';down=root+'/Root/wpn_bow_01_jnt/slot_dn_string_01_vfx'
 upper=bow.world(bow.paths[up])[:3,3];lower=bow.world(bow.paths[down])[:3,3];length=float(np.linalg.norm(upper-lower))
 data={'schema':2,'id':'typhoea_bow','display_name':'提夫罗斯原生弓','resource_id':root,'asset_path':asset['path'],'asset_path_hash':str(asset['pathHashHead']),'renderer_path':'Mesh_all/lod0/S_wpn_misc_0051_01_lod0','renderer_paths':['Mesh_all/lod0/S_wpn_misc_0051_01_lod0','Mesh_all/lod0/S_wpn_misc_0051_02_lod0'],'mount_bone_path':'Root/wpn_bow_01_jnt','mount_reference_path':'Root/wpn_bow_01_jnt','mount_position':[0,0,0],'mount_rotation_xyzw':[0,0,0,1],'mount_side':'left','mount_socket_name':'IK_Weapon_L_001','mount_source_verified':True,'original_mount_character_point':84,'original_mount_weapon_index':10,'mount_mode':'authored-native-IK-left-weapon-frame','mount_runtime_verified':False,'mount_evidence':'Exact decoded CharacterTemplateData StaticWeaponData index10 wpn_misc_0051 fightMountTarget.characterMountPoint84 -> native IK_Weapon_L_001. Prefab root authored identity; source IK frame expressed in the same-character hand and migrated with its actual proportions. Runtime and native bow animation curves remain separate validation.','blade_root_axis':[0,0,1],'edge_root_axis':[1,0,0],'blade_length_m':length,'material_policy':'readonly-native-shared-materials-plus-owned-property-block','clone_policy':'pure-owned-gameobjects-transforms-two-skinned-renderers-full-own-bone-palettes','controls':{'type':'bow','string_center_path':relative(bow,center),'string_upper_path':relative(bow,up),'string_lower_path':relative(bow,down),'string_rest_center_root':bow.world(bow.paths[center])[:3,3].tolist(),'string_rest_upper_root':upper.tolist(),'string_rest_lower_root':lower.tolist(),'draw_finger_bone':'Bip001_R_Finger22','draw_point_policy':'target right middle-finger distal phalanx from actual skin palette; derived anatomical string-contact candidate, source native draw contact unknown','draw_point_verified':False,'release_policy':'restore authored own string center immediately; local arrow projectile separately','launch_point_path':'Root/FunnelPoint/LaunchPoint_1','bow_arm_policy':'preserve native own nine-bone bind hierarchy; do not invent drawn angular curves'},'vfx_status':'native string/flow material retained; index draw point is anatomical candidate; native limb charge curves unresolved','source_manifest':bow.data['snapshot']['manifest_version'],'runtime_verified':False}
 write(ROOT/'profiles/weapons/typhoea_bow.json',data)
 parts=[]
 for graph,who in [(lae,'wpn_misc_0016'),(bow,'wpn_misc_0051')]:
  for o in graph.objects.values():
   if o['type']!='SkinnedMeshRenderer':continue
   go=o['game_object']['id'];matches=[p for p,i in graph.paths.items() if graph.transforms[i]['game_object']['id']==go and p.startswith(who+'/')]
   if not matches:continue
   mesh=graph.objects[o['mesh']['id']];parts.append({'resource_id':who,'path':relative(graph,matches[0]),'mesh':mesh['name'],'vertices':mesh['vertex_count'],'bones':[relative(graph,graph.path(x['id'])) for x in o['bones']],'bindposes':len(mesh['bindposes']),'native_materials':[graph.objects[x['id']]['name'] for x in o['materials']]})
 write(OUT/'special-parts-validation.json',{'schema':1,'parts':parts,'count':len(parts),'runtime_verified':False})
 write(ROOT/'profiles/weapons/index.json',{'schema':1,'weapons':[{'id':'laevatain_blade','profile':'laevatain_full.json','modes':['longsword'],'mount_side':'right','runtime_verified':False},{'id':'typhoea_bow','profile':'typhoea_bow.json','modes':['bow'],'mount_side':'left','runtime_verified':False}], 'default_by_mode':{'longsword':'laevatain_blade','bow':'typhoea_bow'},'source_props':'assets/modes/<mode>/props/index.json','runtime_verified':False})

def equipment(extract):
 index=read(ROOT/'profiles/weapons/equipment-index.json');manifest=read(ROOT/'research/character-coverage/purrche-inputs/Bundles/Windows/manifest.json');assets={a['path'].lower():a for a in manifest['Assets']};required=[]
 for row in index['equipment']:
  asset=assets.get(row['model_path']);row['asset_exists']=bool(asset)
  if not asset:raise ValueError('weapon model missing '+row['model_path'])
  row['asset_path_hash']=str(asset['pathHashHead']);required.append(row['model_path'])
 write(ROOT/'profiles/weapons/equipment-index.json',index)
 if extract or (OUT/'equipment-raw.json').exists():
  env=dict(os.environ);env['PYTHONPATH']=str(BE/'scripts');inputs=OUT/'equipment-inputs';inputs.mkdir(parents=True,exist_ok=True)
  if not (inputs/'extraction.json').exists():
   command=['python',str(BE/'tools/CustomModel/extract_native_bundles.py'),'--game',r'G:\Arknights Endfield','--asset-only','--unpacker',str(BE/'toolchains/EndfieldUnpacker'),'--resconv',str(BE/'toolchains/FkArkEnd/ResConv/bin/Release/net10.0/ResConv.exe'),'--output',str(inputs)]
   for path in required:command+=['--extra-asset',path]
   subprocess.run(command,check=True,env=env)
  raw=OUT/'equipment-raw.json'
  if not raw.exists():subprocess.run(['dotnet',str(BE/'toolchains/native-asset-reader/identities/NativeAssetReader.dll'),str(inputs),str(raw)],check=True)
  data=read(raw)
  supplement=OUT/'equipment-geometry/static-renderers.json'
  if supplement.exists():
   known={x['id'] for x in data['objects']};data['objects'] += [x for x in read(supplement) if x['id'] not in known]
  g=Graph(data);rows=[];registry=read(ROOT/'profiles/weapons/index.json')
  geometric={r['id']:read(OUT/'equipment-geometry'/r['file']) for r in read(OUT/'equipment-geometry/inspection.json')} if (OUT/'equipment-geometry/inspection.json').exists() else {}
  for row in index['equipment']:
   root=row['weapon_id'];renderers=[]
   for o in g.objects.values():
    if o['type'] not in ['SkinnedMeshRenderer','MeshRenderer']:continue
    go=o['game_object']['id'];paths=[p for p,i in g.paths.items() if g.transforms[i]['game_object']['id']==go and p.startswith(root+'/')]
    if not paths:continue
    if o['type']=='SkinnedMeshRenderer':mesh=g.objects[o['mesh']['id']]
    else:
     filter=next(x for x in g.objects.values() if x['type']=='MeshFilter' and x['game_object']['id']==go);mesh=g.objects[filter['mesh']['id']]
    renderers.append({'path':relative(g,paths[0]),'mesh':mesh['name'],'mesh_id':mesh['id'],'renderer_kind':'skinned' if o['type']=='SkinnedMeshRenderer' else 'static','vertex_count':mesh['vertex_count'],'palette_paths':[relative(g,g.path(x['id'])) for x in o.get('bones',[])],'bindpose_count':len(mesh['bindposes']),'materials':[g.objects[x['id']]['name'] for x in o['materials']]})
   qualified=bool(renderers) and all(r['renderer_kind']=='static' or len(r['palette_paths'])==r['bindpose_count'] for r in renderers)
   entries=[x for x in renderers if '/lod0/' in '/'+x['path']]
   geometry_ok=all(r['mesh_id'] in geometric for r in entries)
   rows.append({'weapon_id':root,'native_renderers':renderers,'resource_root_exists':root in g.paths,'own_bone_palettes_complete':qualified,'geometry_exported':geometry_ok,'runtime_verified':False})
   if qualified and entries:
    main=entries[0];axis=np.array([0,1,0]);length=1.25
    if main['mesh_id'] in geometric:
     geometry=geometric[main['mesh_id']];vertices=np.asarray(geometry['m_Vertices']).reshape(-1,3)
     if main['renderer_kind']=='static':
      transform=np.linalg.inv(g.world(g.paths[root]))@g.world(g.paths[root+'/'+main['path']]);vertices=(transform@np.column_stack([vertices,np.ones(len(vertices))]).T).T[:,:3]
     values,axes=np.linalg.eigh(np.cov(vertices.T));axis=axes[:,values.argmax()];projection=vertices@axis
     if abs(projection.min())>abs(projection.max()):axis=-axis;projection=-projection
     length=float(np.ptp(projection));
    visual={'schema':2,'id':root,'display_name':root,'resource_id':root,'asset_path':row['model_path'],'asset_path_hash':row['asset_path_hash'],'renderer_path':main['path'],'renderer_paths':[r['path'] for r in entries],'renderer_kinds':{r['path']:r['renderer_kind'] for r in entries},'mount_bone_path':main['palette_paths'][0] if main['palette_paths'] else 'Mesh_all','mount_reference_path':'','mount_position':[0,0,0],'mount_rotation_xyzw':[0,0,0,1],'mount_side':'right','mount_mode':'preserve-native-prefab-root-at-authored-character-socket','mount_runtime_verified':False,'mount_evidence':'Native prefab mesh and original local transforms retained. Equipment source socket is read per character; shape compatibility and semantic grip not yet manually qualified.','blade_root_axis':axis.tolist(),'edge_root_axis':[1,0,0],'blade_length_m':length,'axis_status':'principal geometry axis measured; endpoint semantic assignment not certified','material_policy':'readonly-native-shared-materials','controls':{},'source_manifest':g.data['snapshot']['manifest_version'],'runtime_verified':False}
    write(ROOT/'profiles/weapons/equipment'/(root+'.json'),visual);registry['weapons'].append({'id':root,'profile':'equipment/'+root+'.json','modes':row['applicable_modes'],'mount_side':'right','runtime_verified':False})
  write(OUT/'equipment-validation.json',{'schema':1,'equipment_count':len(rows),'equipment':rows,'source_manifest':g.data['snapshot']['manifest_version'],'runtime_verified':False})
  write(ROOT/'profiles/weapons/index.json',registry)
  bad=[r for r in rows if not r['resource_root_exists'] or not r['own_bone_palettes_complete']]
  print('Equipment',len(rows),'qualified',len(rows)-len(bad),'exceptions',len(bad))
  if bad:write(OUT/'equipment-exceptions.json',bad)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--extract-equipment',action='store_true');a=p.parse_args();special();equipment(a.extract_equipment)
if __name__=='__main__':main()
