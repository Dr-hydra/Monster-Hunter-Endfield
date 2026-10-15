"""Evaluate actual exported skin meshes with the exact native retarget oracle.

Audits all character/mode pairs. P0 models use every core frame; remaining
models use all FK frames and regular/key mesh frames. Clothes retain authored
relative bones (offline rendering does not simulate game cloth/shader/VFX).
"""
import argparse,json,struct,time,sys,collections,re
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
from PIL import Image,ImageDraw
from endfield_profiles import read,write,analyze,CACHE,ROOT
from endfield_pose_projection import mode_root,solve_grip,ground_pose,resources,posed_source_parts

OUT=ROOT/'research/character-coverage';P0={'chr_0016_laevat','chr_0034_typhoea','chr_0018_dapan'}
AUDIT_REVISION='registered-sole-upright-secondary-source-own-skin-20261014'

def audit_inputs(scene,meta):
 # Semantic provenance, deliberately without artifact hashes. Source aliases
 # and JSON reformatting do not invalidate numerically identical poses.
 data=resources(meta['mode']);rig=data['rig']
 masks=data['contact_masks'];contact_model=meta.get('contact_model')
 if masks and contact_model is None:contact_model='source:'+next(p['id'] for p in data['props']['props'] if p['role']=='main')
 contact=masks['models'].get(contact_model,{}).get('characters',{}).get(meta['character_id'],{}) if masks else None
 result={'revision':AUDIT_REVISION,'scale':meta['scale'],
         'body_basis':meta['source_basis_xyzw'],
         'stage_basis':meta['source_stage_basis_xyzw'],
         'source_sole_height':rig.get('sole_geometry',{}).get('hip_height',1.0568615),
         'clips':meta['clips'],'shoe_probes':scene['probes'],
         'secondary_roots':scene['secondary_roots'],
         'finger_paths':meta['finger_paths'],
         'contact_model':contact_model,'contact_schema':masks['schema_version'] if masks else None,
         'contact_weights':{slot:row['weights'] for slot,row in contact.get('tracks',{}).items()} if contact else {},
         'source_own_skin':{str(c['slot']):read(data['root']/'source_skin_tracks.json')['tracks'].get(str(c['slot'])) for c in meta['clips']},
         'attachment_policy':'source authored hand-child; bind removed once'}
 if scene.get('hand_probes') and data['support_ground']:
  result['ground_hand_probes']=scene['hand_probes']
  result['source_hand_support']={str(c['slot']):data['support_ground']['tracks'][str(c['slot'])] for c in meta['clips'] if str(c['slot']) in data['support_ground']['tracks']}
 return result
def oracle(cid,mode,directory=None):
 directory=directory or OUT/'oracle'
 meta=read(directory/cid/(mode+'.json'));data=(directory/cid/(mode+'.ltp')).read_bytes();magic,n,bones,version=struct.unpack_from('<4sIII',data);assert magic==b'LTP1';at=16;clips=[]
 for _ in range(n):
  slot,count,fps=struct.unpack_from('<III',data,at);at+=12;size=count*(bones*4+3)*4;values=np.frombuffer(data,dtype='<f4',count=count*(bones*4+3),offset=at).reshape(count,bones*4+3).copy();at+=size
  clips.append({'slot':slot,'fps':fps,'rotations':values[:,:bones*4].reshape(count,bones,4),'hip':values[:,bones*4:]})
 assert at==len(data);return meta,clips
def setup(cid,update_profile=True):
 names={'chr_0003_endminf':'female_admin','chr_0016_laevat':'laevatain','chr_0034_typhoea':'typhoea'};profile=ROOT/'profiles'/(names.get(cid,cid)+'.json');data=read(profile)
 recorded=data.get('evidence',{}).get('source_graph')
 source=Path(recorded) if recorded else OUT/(cid+'.json')
 if recorded and not source.is_absolute():source=ROOT/source
 if not source.exists():
  if data.get('evidence',{}).get('current_vfs_verified'):raise FileNotFoundError('Current qualified graph is unavailable: '+str(source))
  source=CACHE/'sweep-identities'/(cid+'.json')
 r=analyze(cid,read(source));g=r['graph'];model=cid+'_postmodel';to_model=np.linalg.inv(g.world(g.paths[model]));paths=sorted([p for p in g.paths if p==model or p.startswith(model+'/')],key=lambda p:(p.count('/'),p));index={p:i for i,p in enumerate(paths)};rest=[];parents=[];locals=[]
 for p in paths:
  m=r['skin_rest'].get(p,to_model@g.world(g.paths[p]));rest.append(m);parent=g.transforms[g.paths[p]]['parent']['id'];pp=g.path(parent) if parent in g.transforms else None;parents.append(index.get(pp,-1));locals.append(np.linalg.inv(rest[index[pp]])@m if pp in index else m)
 geometry_root=OUT/'geometry'/cid;geometries={x['id']:read(geometry_root/x['file']) for x in read(geometry_root/'inspection.json')};parts=[]
 for renderer in r['renderers']:
  if '/lod0/' not in renderer['path'] or renderer['mesh_id'] not in geometries:continue
  mesh=geometries[renderer['mesh_id']];vertices=np.asarray(mesh['m_Vertices'],dtype=np.float32).reshape(-1,3);indices=np.asarray(mesh['m_Indices'],dtype=np.int32).reshape(-1,3);skin_info=mesh.get('m_Skin')
  if not skin_info or len(skin_info)!=len(vertices):continue
  bone_indices=np.asarray([s['boneIndex'] for s in skin_info],dtype=np.int32);weights=np.asarray([s['weight'] for s in skin_info],dtype=np.float32);palette=renderer['palette_paths'];bind=np.asarray(renderer['bindposes_row_major'],dtype=np.float32).reshape(-1,4,4)
  assert bone_indices.min()>=0 and bone_indices.max()<len(palette);assert np.max(np.abs(weights.sum(1)-1))<.003
  source=np.column_stack([vertices,np.ones(len(vertices),dtype=np.float32)]);bind_vertices=np.einsum('vkij,vj->vki',bind[bone_indices],source,optimize=True)
  mesh_id=renderer['mesh_id'];parts.append({'name':renderer['mesh_name'],'vertices':vertices,'faces':indices,'bone_indices':np.asarray([[index[palette[i]] for i in row] for row in bone_indices],dtype=np.int32),'weights':weights,'bind_vertices':bind_vertices,'mesh_id':mesh_id})
 names={'chr_0003_endminf':'female_admin','chr_0016_laevat':'laevatain','chr_0034_typhoea':'typhoea'};profile=ROOT/'profiles'/(names.get(cid,cid)+'.json');data=read(profile)
 world=np.asarray(rest,dtype=np.float32);probes=[]
 for side in ['left','right']:
  foot_ids=[index[r['bones'][side+'_foot']],index[r['bones'][side+'_toes']]];candidates=[]
  for part in parts:
   mask=(np.isin(part['bone_indices'],foot_ids)*part['weights']).sum(1)>.85
   points=skin(part,world[None])[0]
   ids=np.flatnonzero(mask)
   if len(ids):
    for v in ids:candidates.append((float(points[v,1]),part,int(v),points[v]))
  candidates.sort(key=lambda t:(t[0],t[3][0],t[3][2]))
  if candidates:
   # Extremal sole vertices give mesh-backed contact evidence, retaining all
   # actual bone weights rather than pretending the ankle is the sole.
   # Include extremal weighted shoe vertices, not only the flat rest sole:
   # a pitched toe or heel otherwise becomes an unmeasured ground contact.
   directions=list(np.eye(3))+list(-np.eye(3))+[np.array([x,y,z]) for x in [-1,1] for y in [-1,1] for z in [-1,1]]
   selected=[min(candidates,key=lambda c:float(c[3]@direction)) for direction in directions]
   seen=set()
   for _,part,v,point in selected:
    key=(part['mesh_id'],v)
    if key in seen:continue
    seen.add(key);probes.append({'side':side,'mesh_id':part['mesh_id'],'mesh_name':part['name'],'vertex':v,'rest_model_point':point.tolist(),'influences':[{'bone_path':paths[int(b)],'bone_name':paths[int(b)].rsplit('/',1)[-1],'weight':float(w),'local_point':p[:3].tolist()} for b,w,p in zip(part['bone_indices'][v],part['weights'][v],part['bind_vertices'][v]) if w>1e-5]})
  assert candidates,(cid,side,'no actual sole vertices')
 for extra in data.get('ground_extra_foot_probe_vertices',[]):
  part=next((p for p in parts if p['mesh_id']==extra['mesh_id']),None)
  if part is None:raise ValueError('Recorded additional shoe probe mesh is absent')
  v=int(extra['vertex']);assert 0<=v<len(part['vertices'])
  if any(p['mesh_id']==part['mesh_id'] and p['vertex']==v for p in probes):continue
  point=skin(part,world[None])[0,v]
  probes.append({'side':extra['side'],'mesh_id':part['mesh_id'],'mesh_name':part['name'],'vertex':v,'rest_model_point':point.tolist(),'influences':[{'bone_path':paths[int(b)],'bone_name':paths[int(b)].rsplit('/',1)[-1],'weight':float(w),'local_point':p[:3].tolist()} for b,w,p in zip(part['bone_indices'][v],part['weights'][v],part['bind_vertices'][v]) if w>1e-5],'evidence':'Actual footwear vertex from recorded core-frame clearance regression'})
 assert len(probes)<=32,'Native probe bound exceeded'
 secondary={};body_prefix=r['skeleton']+'/'
 for part in parts:
  bone_paths=np.asarray([[paths[int(b)] for b in row] for row in part['bone_indices']]);body=np.asarray([[p==r['skeleton'] or p.startswith(body_prefix) for p in row] for row in bone_paths]);weights=part['weights']
  external_roots={p.split('/',3)[0]+'/'+p.split('/',3)[1]+'/'+p.split('/',3)[2] for p in bone_paths[~body] if p.startswith(model+'/Root/')}
  for root_path in external_roots:
   ext=np.asarray([[p==root_path or p.startswith(root_path+'/') for p in row] for row in bone_paths]);ext_weight=(weights*ext).sum(1);body_weight=(weights*body).sum(1);cross=np.flatnonzero((ext_weight>.001)&(body_weight>.001))
   if not len(cross):continue
   entry=secondary.setdefault(root_path,{'scores':collections.Counter(),'vertices':0,'meshes':set()});entry['vertices']+=len(cross);entry['meshes'].add(part['name'])
   for vertex in cross:
    for bone,weight,is_body in zip(bone_paths[vertex],weights[vertex],body[vertex]):
     if is_body and weight>1e-5:entry['scores'][bone]+=float(weight*ext_weight[vertex])
 secondary_roots=[]
 for root_path,item in secondary.items():
  relevant=[p.split('/') for p,score in item['scores'].items() if score>.001];common=[]
  for column in zip(*relevant):
   if len(set(column))!=1:break
   common.append(column[0])
  driver='/'.join(common)
  if driver not in index or driver==r['skeleton']:continue
  secondary_roots.append({'root_path':root_path,'root_name':root_path.rsplit('/',1)[-1],'driver_path':driver,'driver_name':driver.rsplit('/',1)[-1],'anchor_path':driver,'anchor_name':driver.rsplit('/',1)[-1],'anchor_relative_matrix':(np.linalg.inv(world[index[driver]])@world[index[root_path]]).T.reshape(-1).tolist(),'root_rest_model_matrix':world[index[root_path]].T.reshape(-1).tolist(),'driver_rest_model_matrix':world[index[driver]].T.reshape(-1).tolist(),'shared_skin_vertices':item['vertices'],'mesh_names':sorted(item['meshes']),'paired_body_influences':[{'path':p,'weight':score}for p,score in item['scores'].most_common(8)],'evidence':'Actual exported indexed skin has vertices mixing independent root and main-skeleton influences; common body ancestor supplies a rigid presentation transport. Original spring/cloth driver is not emulated.','policy':'driverPose * inverse(driverBindRest) * externalRootBindRest; keep authored child local TRS','native_cloth_physics_verified':False})
 hand_probes=data.get('ground_hand_probes',[])
 if isinstance(hand_probes,dict):hand_probes=[p for side in ['left','right'] for p in hand_probes.get(side,[])]
 scene={'paths':paths,'index':index,'rest':world,'locals':np.asarray(locals,dtype=np.float32),'parents':parents,'parts':parts,'r':r,'probes':probes,'hand_probes':hand_probes,'secondary_roots':secondary_roots}
 if update_profile:
  data['ground_foot_probes']=probes;data['ground_foot_mesh_rest_min_y']=min(p['rest_model_point'][1] for p in probes);data['secondary_skin_roots']=secondary_roots;write(profile,data)
 return scene
def probe_floor(scene,world):
 values=[]
 for probe in scene['probes']:
  value=np.zeros((len(world),3),dtype=np.float32)
  for influence in probe['influences']:
   m=world[:,scene['index'][influence['bone_path']],:3,:];p=np.r_[influence['local_point'],1.];value+=np.einsum('fij,j->fi',m,p).astype(np.float32)*influence['weight']
  values.append(value[:,1])
 return np.asarray(values).min(0)
def fk(scene,meta,clip,selection):
 rotations=clip['rotations'][selection];frames=len(selection);world=np.zeros((frames,len(scene['paths']),4,4),dtype=np.float32);paths=meta['bone_paths']+meta['finger_paths'];overrides={scene['index'][p]:i for i,p in enumerate(paths)};desired=Rotation.from_quat(rotations.reshape(-1,4)).as_matrix().reshape(frames,rotations.shape[1],3,3).astype(np.float32);hips=scene['index'][meta['bone_paths'][0]]
 for i,parent in enumerate(scene['parents']):
  world[:,i]=world[:,parent]@scene['locals'][i] if parent>=0 else scene['locals'][i]
  if i in overrides:world[:,i,:3,:3]=desired[:,overrides[i]]
  if i==hips:world[:,i,:3,3]+=clip['hip'][selection]
 for entry in scene['secondary_roots']:
  root=scene['index'][entry['root_path']];driver=scene['index'][entry['driver_path']];change=world[:,driver]@np.linalg.inv(scene['rest'][driver]);branch=[i for i,p in enumerate(scene['paths']) if p==entry['root_path'] or p.startswith(entry['root_path']+'/')]
  world[:,branch]=change[:,None]@world[:,branch]
 return solve_grip(scene,meta,clip,selection,world)
def skin(part,world):
 # Group the actual nonzero influences by palette bone instead of expanding
 # every frame × vertex × four zero-filled skin matrices. This is the same
 # linear blend operation and keeps every original weight/local coordinate.
 if '_skin_groups' not in part:
  vertices=np.repeat(np.arange(len(part['vertices'])),4);bones=part['bone_indices'].reshape(-1);weights=part['weights'].reshape(-1);points=part['bind_vertices'].reshape(-1,4)*weights[:,None];valid=weights>0
  vertices=vertices[valid];bones=bones[valid];points=points[valid];part['_skin_groups']=[(int(b),vertices[bones==b],points[bones==b]) for b in np.unique(bones)]
 result=np.zeros((len(world),len(part['vertices']),3),dtype=np.float32)
 for bone,indices,points in part['_skin_groups']:
  result[:,indices]+=np.einsum('fij,vj->fvi',world[:,bone,:3,:],points,optimize=False)
 return result
def render(parts,world,filename,label,view='rear'):
 vertices=[];faces=[];colors=[];base=0
 for part in parts:
  points=skin(part,world)[0];triangle=part['faces'];vertices.append(points);faces.append(triangle+base);base+=len(points)
  name=part['name'];color=np.array([.60,.67,.74]) if 'cloth' in name else np.array([.86,.75,.66]) if any(s in name for s in ['body','face']) else np.array([.32,.34,.40]);colors.extend([color]*len(triangle))
 points=np.concatenate(vertices);tri=np.concatenate(faces);colors=np.asarray(colors);angle={'rear':0,'front':np.pi,'side':np.pi/2}.get(view,.6);rot=np.array([[np.cos(angle),0,np.sin(angle)],[0,1,0],[-np.sin(angle),0,np.cos(angle)]],dtype=float);p=points@rot.T
 # The actual indexed triangles are rasterized by depth (no stick skeleton).
 lo=p.min(0);hi=p.max(0);scale=min(310/max(hi[0]-lo[0],.1),390/max(hi[1]-lo[1],.1));xy=np.column_stack([(p[:,0]-(lo[0]+hi[0])/2)*scale+180,430-(p[:,1]-lo[1])*scale]);depth=p[tri,2].mean(1);normal=np.cross(p[tri[:,1]]-p[tri[:,0]],p[tri[:,2]]-p[tri[:,0]]);norm=np.linalg.norm(normal,axis=1);normal/=np.maximum(norm[:,None],1e-8);light=np.clip(.48+.52*np.abs(normal@np.array([.2,.65,-.65])),.2,1);color=(colors*light[:,None]*255).astype(np.uint8)
 image=Image.new('RGB',(360,480),(21,26,31));draw=ImageDraw.Draw(image);draw.line([(15,433),(345,433)],fill=(82,88,94),width=1)
 for i in np.argsort(-depth):
  if norm[i]<1e-10:continue
  draw.polygon([tuple(v) for v in xy[tri[i]]],fill=tuple(color[i]))
 draw.text((10,8),label,fill=(231,237,240));draw.text((10,462),'Actual mesh / offline skin / runtime pending',fill=(161,177,190));image.save(filename)
def audit(cid,mode,scene,meta,clips,stride,*,selections=None,gallery_directory=None,oracle_directory=None):
 slot_names=read(mode_root(mode)/'moves.json');warnings=[];bad=[];finite=True;mesh_frames=0;fk_frames=0;minimum=1e6;maximum=-1e6;minimum_evidence=None;minimum_foot=1e6;foot_penetration=[];selected_snapshot=None;highest_movement=-1;weapon_frames=0;weapon_parts=set();finger_frames=0
 dominant=(12 if mode!='bow' else 8);body_indices=[scene['index'][p] for p in meta['bone_paths']];rest_feet=np.min(scene['rest'][[body_indices[i] for i in [15,16,19,20]],1,3]);foot_parts=[part for part in scene['parts'] if 'body' in part['name']];batch=12
 for clip in clips:
  count=len(clip['rotations']);fk_frames+=count
  if not(np.isfinite(clip['rotations']).all() and np.isfinite(clip['hip']).all()):finite=False;bad.append({'slot':clip['slot'],'reason':'nonfinite source oracle'});continue
  selection=(np.asarray(selections[clip['slot']],dtype=np.int32) if selections is not None else np.arange(count) if cid in P0 else np.unique(np.r_[np.arange(0,count,stride),0,count-1]));assert len(selection) and selection.min()>=0 and selection.max()<count;mesh_frames+=len(selection)
  if meta['finger_paths']:finger_frames+=len(selection)
  for start in range(0,len(selection),batch):
   frames=selection[start:start+batch];world=fk(scene,meta,clip,frames);positions=world[:,:,:3,3][:,body_indices];world=ground_pose(scene,meta,clip,frames,world,probe_floor)
   for part in scene['parts']:
    points=skin(part,world)
    if not np.isfinite(points).all():finite=False;bad.append({'slot':clip['slot'],'frame':int(frames[0]),'mesh':part['name'],'reason':'nonfinite skin'});continue
    observed=float(points[:,:,1].min())
    if observed<minimum:
     at=np.unravel_index(points[:,:,1].argmin(),points[:,:,1].shape);minimum_evidence={'slot':clip['slot'],'frame':int(frames[at[0]]),'mesh':part['name'],'vertex':int(at[1]),'point':points[at].tolist()};minimum=observed
    maximum=max(maximum,float(points[:,:,1].max()))
    actual_feet=[body_indices[i] for i in [15,16,19,20]];mask=(np.isin(part['bone_indices'],actual_feet)*part['weights']).sum(1)>.85
    if mask.any():
     low=points[:,mask,1].min(1);minimum_foot=min(minimum_foot,float(low.min()))
     for frame,value in zip(frames,low):
      if value<-.03 and len(foot_penetration)<16:foot_penetration.append({'slot':clip['slot'],'frame':int(frame),'mesh':part['name'],'minimum_foot_y':float(value)})
    if np.max(np.abs(points))>100:bad.append({'slot':clip['slot'],'frame':int(frames[0]),'mesh':part['name'],'reason':'unbounded mesh transform'})
   prop_world,props=posed_source_parts(scene,meta,clip,frames,world)
   for prop in props:
    points=skin(prop,prop_world);weapon_parts.add(prop['name']);weapon_frames+=len(frames)
    if not np.isfinite(points).all() or np.max(np.abs(points))>100:bad.append({'slot':clip['slot'],'frame':int(frames[0]),'mesh':prop['name'],'reason':'nonfinite/unbounded transported source weapon mesh'})
   movement=np.linalg.norm(positions[:,dominant]-positions[:,2],axis=1);at=int(np.argmax(movement))
   if movement[at]>highest_movement:highest_movement=float(movement[at]);selected_snapshot=(clip,int(frames[at]))
 if scene['r']['registration_error'] and max(scene['r']['registration_error'].values())>.002:warnings.append('source serialized local/rest position variance exceeds 2 mm; per-renderer correction metadata retained')
 warnings+=['native shader/cloth/VFX not simulated','source authored attachments verified; native C++ selection not replayed','contact/terrain scene behavior requires downstream runtime checks']
 if mode=='longsword':warnings.append('external sheath hidden: exact native stowed attachment unresolved; no guessed hip or left hand mount')
 gallery=gallery_directory or OUT/'gallery';gallery.mkdir(parents=True,exist_ok=True);image_path=gallery/(cid+'-'+mode+'.png')
 if selected_snapshot:
  clip,frame=selected_snapshot;world=fk(scene,meta,clip,np.array([frame]));world=ground_pose(scene,meta,clip,np.array([frame]),world,probe_floor);world,props=posed_source_parts(scene,meta,clip,np.array([frame]),world);render(scene['parts']+props,world,image_path,cid+'\n'+mode+' slot '+str(clip['slot'])+' frame '+str(frame))
  if cid in P0 and mode in ['longsword','bow']:
   for view in ['front','side']:render(scene['parts']+props,world,gallery/(cid+'-'+mode+'-'+view+'.png'),cid+'\n'+mode+' '+view,view)
 oracle_path=(oracle_directory or OUT/'oracle')/cid/(mode+'.json')
 return {'character_id':cid,'mode':mode,'status':'offline_mesh_checked' if finite and not bad else 'offline_mesh_failed','evidence':[str(image_path.relative_to(ROOT)).replace('\\','/'),str(oracle_path.relative_to(ROOT)).replace('\\','/')],'source_slots':[c['slot'] for c in clips],'frames':fk_frames,'mesh_frames':mesh_frames,'mesh_sample_policy':('explicit per-clip frame selection' if selections is not None else 'all core frames' if cid in P0 else f'every {stride}th frame plus clip boundaries'),'mesh_parts':len(scene['parts']),'mesh_vertices':sum(len(p['vertices']) for p in scene['parts']),'finger_mesh_frames':finger_frames,'source_weapon_mesh_frames':weapon_frames,'source_weapon_mesh_parts':sorted(weapon_parts),'supporting_hand_solver':'exact native retarget.h DLL; bow supports right hand, other modes left','skin_min_y':minimum,'skin_max_y':maximum,'minimum_mesh_frame':minimum_evidence,'minimum_foot_mesh_y':minimum_foot,'foot_penetration_samples':foot_penetration,'errors':bad,'warnings':warnings,'runtime_verified':False,'visual_quality_certified':False,'ground_policy':'actual target weighted shoe vertices matched to source actual sole clearance; upright root basis; no real scenario simulation'}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--characters',nargs='*');p.add_argument('--modes',nargs='*');p.add_argument('--stride',type=int,default=12);p.add_argument('--sole-only',action='store_true');p.add_argument('--resume-log',type=Path);a=p.parse_args();index=read(ROOT/'profiles/index.json');combinations=[];start=time.time()
 existing=OUT/'coverage.json'
 if existing.exists():combinations=read(existing).get('combinations',[])
 completed=set()
 if a.resume_log:
  for line in a.resume_log.read_text(encoding='utf-8-sig').splitlines():
   match=re.match(r'^(chr_\w+) (\w+) mesh \d+ parts \d+ errors 0 elapsed ',line)
   if match:completed.add(match.groups())
 selected=a.characters or [c['character_id'] for c in index['characters']]
 for cid in selected:
  scene=setup(cid)
  if a.sole_only:
   print(cid,'actual weighted footwear probes',len(scene['probes']),flush=True);continue
  for mode in a.modes or index['modes']:
   meta,clips=oracle(cid,mode);inputs=audit_inputs(scene,meta)
   previous=next((c for c in combinations if (c['character_id'],c['mode'])==(cid,mode)),None)
   if (cid,mode) in completed and previous:
    assert previous['source_slots']==[c['slot'] for c in clips]
    assert previous['frames']==sum(len(c['rotations']) for c in clips)
    assert 'upright root basis' in previous['ground_policy']
    image=ROOT/previous['evidence'][0]
    assert image.stat().st_mtime>=(OUT/'oracle'/cid/(mode+'.json')).stat().st_mtime
    item=previous;item['resume_evidence']=str(a.resume_log.resolve().relative_to(ROOT)).replace('\\','/')
   else:item=audit(cid,mode,scene,meta,clips,max(1,a.stride))
   item['audit_revision']=AUDIT_REVISION;item['audit_inputs']=inputs
   combinations=[c for c in combinations if (c['character_id'],c['mode'])!=(cid,mode)]+[item];write(existing,{'schema':2,'audit_revision':AUDIT_REVISION,'combinations':combinations,'summary':{'combinations':len(combinations),'offline_mesh_checked':sum(c['status']=='offline_mesh_checked' for c in combinations),'current_revision':sum(c.get('audit_revision')==AUDIT_REVISION for c in combinations),'runtime_verified':0,'visual_quality_certified':0},'source_manifest':index['source_manifest'],'operation':'exact native retarget.h oracle + registered actual target skin/bindpose/indices; actual indexed mesh snapshot; no game process','runtime_verified':False});print(cid,mode,'cached' if (cid,mode) in completed else 'mesh',item['mesh_frames'],'parts',item['mesh_parts'],'errors',len(item['errors']),'elapsed',round(time.time()-start),flush=True)
if __name__=='__main__':main()
