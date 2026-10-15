"""Audit both special native weapon graphs on every exported character.

Body/fingers/grounding use the same native oracle and projection as the
198-mode mesh scan. Weapon meshes retain exact indexed skin and own palettes;
the fire blade uses source attachment47 with the measured identity geometry
adapter, while the bow uses the decoded IK_Weapon_L_001 installation. This is
an offline numerical/contact review, not native shader or cloth certification.
"""
import argparse,time,re
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
from endfield_profiles import ROOT,read,write
from laevat_profile import Graph
from endfield_mesh_audit import setup,oracle,fk,probe_floor,skin,render,OUT,P0,AUDIT_REVISION,audit_inputs
from endfield_pose_projection import ground_pose,resources,transported_attachment
from endfield_special_previews import geometry
from resource_audit_contract import SPECIAL,special_input_contract,validate_special_current

def pose_parts(scene,meta,clip,frames,world,graph,parts,mode):
 resource='wpn_misc_0016' if mode=='longsword' else 'wpn_misc_0051'
 inv_root=np.linalg.inv(graph.world(graph.paths[resource]));paths=list(dict.fromkeys(p for part in parts for p in part['palette']))
 own=np.asarray([inv_root@graph.world(graph.paths[p]) for p in paths],np.float32)
 mounts=[]
 if mode=='longsword':
  data=resources(mode);main=next(p for p in data['props']['props'] if p['role']=='main');prop=read(data['root']/main['file'])
  for i,frame in enumerate(frames):
   mount=transported_attachment(meta,world[i:i+1],scene,clip['slot'],int(frame),prop)
   if mount is None:raise ValueError('Native fire blade lacks an authored source main attachment')
   mounts.append(mount)
 else:
  socket=scene['r']['sockets']['IK_Weapon_L_001'];local=np.eye(4,dtype=np.float32);local[:3,:3]=Rotation.from_quat(socket['rotation_xyzw']).as_matrix();local[:3,3]=socket['position']
  hand=scene['index'][meta['bone_paths'][8]];mounts=world[:,hand]@local
 mounts=np.asarray(mounts,np.float32);posed=mounts[:,None]@own[None]
 center=resource+'/Root/wpn_bow_01_jnt/slot_M_string_01_vfx'
 if center in paths:
  events=read(resources(mode)['root']/'projectile_events.json')['tracks'].get(str(clip['slot']),[])
  released=np.array([any(float(e['time'])<=float(f)/clip['fps'] for e in events) for f in frames])
  active=(clip['slot']!=resources(mode)['moves']['idle'])&~released
  finger=scene['index'][scene['r']['fingers']['Bip001_R_Finger22']]
  posed[active,paths.index(center),:3,3]=world[active,finger,:3,3]
 lookup={p:len(scene['paths'])+i for i,p in enumerate(paths)};clones=[]
 for part in parts:
  clone=dict(part);clone['bone_indices']=np.asarray([[lookup[part['palette'][b]] for b in row] for row in part['local_indices']],np.int32);clones.append(clone)
 return np.concatenate([world,posed],axis=1),clones,mounts

def audit(cid,mode,graph,parts,stride,gallery):
 scene=setup(cid,update_profile=False);meta,clips=oracle(cid,mode);meta['contact_model']='wpn_misc_0016' if mode=='longsword' else 'wpn_misc_0051';count=0;bad=[];bounds_min=np.full(3,np.inf);bounds_max=np.full(3,-np.inf);selected=None;maximum=-1.;mount_distance=0.;hand=scene['index'][meta['bone_paths'][12 if mode=='longsword' else 8]]
 for clip in clips:
  n=len(clip['rotations']);frames=np.arange(n) if cid in P0 else np.unique(np.r_[np.arange(0,n,stride),0,n-1]);count+=len(frames)
  for at in range(0,len(frames),12):
   sample=frames[at:at+12];world=fk(scene,meta,clip,sample);world=ground_pose(scene,meta,clip,sample,world,probe_floor);posed,clones,mounts=pose_parts(scene,meta,clip,sample,world,graph,parts,mode)
   mount_distance=max(mount_distance,float(np.linalg.norm(mounts[:,:3,3]-world[:,hand,:3,3],axis=1).max()))
   for part in clones:
    points=skin(part,posed)
    if not np.isfinite(points).all() or np.max(np.abs(points))>100:bad.append({'slot':clip['slot'],'frame':int(sample[0]),'mesh':part['name'],'reason':'nonfinite/unbounded indexed skin'})
    bounds_min=np.minimum(bounds_min,points.min((0,1)));bounds_max=np.maximum(bounds_max,points.max((0,1)))
   distance=np.linalg.norm(world[:,hand,:3,3]-world[:,scene['index'][meta['bone_paths'][2]],:3,3],axis=1);i=int(np.argmax(distance))
   if distance[i]>maximum:maximum=float(distance[i]);selected=(clip,int(sample[i]))
 evidence=[]
 if selected:
  clip,frame=selected;sample=np.array([frame]);world=fk(scene,meta,clip,sample);world=ground_pose(scene,meta,clip,sample,world,probe_floor);posed,clones,_=pose_parts(scene,meta,clip,sample,world,graph,parts,mode)
  for view in ['rear','side','front']:
   image=gallery/(cid+'-'+mode+'-'+view+'.png');render(scene['parts']+clones,posed,image,cid+'\n'+mode+f' {clip["slot"]}:{frame}\nActual native own weapon',view);evidence.append(str(image.relative_to(ROOT)).replace('\\','/'))
 return {'character_id':cid,'mode':mode,'weapon':'wpn_misc_0016' if mode=='longsword' else 'wpn_misc_0051','source_slots':[c['slot'] for c in clips],'source_frames':sum(len(c['rotations'])for c in clips),'actual_mesh_frames':count,'sample_policy':'every core frame' if cid in P0 else f'every {stride}th frame plus clip boundaries','own_skin_parts':len(parts),'own_skin_vertices':sum(len(p['vertices'])for p in parts),'own_palette_bones':len(set(p for part in parts for p in part['palette'])),'weapon_root_to_holding_hand_max_m':mount_distance,'posed_skin_bounds_min':bounds_min.tolist(),'posed_skin_bounds_max':bounds_max.tolist(),'errors':bad,'status':'offline_special_mesh_checked' if not bad else 'offline_special_mesh_failed','evidence':evidence,'geometry_scale_policy':'authored native weapon scale1; attachment translation uses measured character scale','mount_evidence':'actual MHW main attachment47 + native/source measured +Y/+X identity geometry adapter' if mode=='longsword' else 'decoded CharacterTemplateData index10 point84 IK_Weapon_L_001 + actual per-character hand-relative socket','body_fingers_secondary_roots':'exact retarget.h oracle and own actual target main/secondary palettes','string_contact_policy':'actual target R_Finger22 candidate until actual projectile release; native animation draw contact remains unknown' if mode=='bow' else None,'native_skill_mount_verified':mode=='bow','runtime_verified':False,'visual_quality_certified':False,'limitations':['native shader/cloth/VFX execution not simulated','native bow limb charge curves not invented','finite geometry and direct hand contact do not certify absence of all mesh intersections']}

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--characters',nargs='*');parser.add_argument('--modes',nargs='*',choices=['longsword','bow']);parser.add_argument('--stride',type=int,default=12);parser.add_argument('--resume-log',type=Path);parser.add_argument('--report',type=Path);parser.add_argument('--gallery',type=Path);a=parser.parse_args();start=time.time();base=ROOT/'research/expansion-20261013';sword=Graph(read(base/'laevat-weapon-raw.json'));bow=Graph(read(base/'special-weapon-model-raw.json'));graphs={'longsword':sword,'bow':bow};parts={'longsword':geometry(sword,ROOT/'research/weapon-coverage/laevat-geometry','wpn_misc_0016'),'bow':geometry(bow,ROOT/'research/typhoea/geometry','wpn_misc_0051')};index=read(ROOT/'profiles/index.json');characters=a.characters or [c['character_id']for c in index['characters']];gallery=a.gallery or OUT/'special-cross-gallery';gallery.mkdir(parents=True,exist_ok=True);path=a.report or OUT/'special-cross-coverage.json';rows=read(path)['combinations'] if path.exists() else []
 completed=set()
 if a.resume_log:
  for line in a.resume_log.read_text(encoding='utf-8-sig').splitlines():
   match=re.match(r'^(chr_\w+) (\w+) native mesh frames \d+ errors 0 elapsed ',line)
   if match:completed.add(match.groups())
 for cid in characters:
  for mode in a.modes or ['longsword','bow']:
   scene=setup(cid,update_profile=False);meta,clips=oracle(cid,mode);meta['contact_model']='wpn_misc_0016' if mode=='longsword' else 'wpn_misc_0051';previous=next((r for r in rows if (r['character_id'],r['mode'])==(cid,mode)),None)
   weapon_id=next(key for key,value in SPECIAL.items() if value[0]==mode);profile=read(ROOT/'profiles'/next(c['profile'] for c in index['characters'] if c['character_id']==cid));contract=special_input_contract(ROOT,cid,weapon_id,profile)
   if (cid,mode) in completed and previous:
    validate_special_current(ROOT,cid,weapon_id,profile,previous)
    assert previous['source_frames']==sum(len(c['rotations']) for c in clips)
    assert all((ROOT/p).stat().st_mtime>=(OUT/'oracle'/cid/(mode+'.json')).stat().st_mtime for p in previous['evidence'])
    row=previous;row['resume_evidence']=str(a.resume_log.resolve().relative_to(ROOT)).replace('\\','/')
   else:
    row=audit(cid,mode,graphs[mode],parts[mode],max(1,a.stride),gallery)
    if contract!=special_input_contract(ROOT,cid,weapon_id,read(ROOT/'profiles'/next(c['profile'] for c in index['characters'] if c['character_id']==cid))):raise ValueError('Special audit inputs changed during scan')
    row['input_contract']=contract
   row['audit_revision']=AUDIT_REVISION;row['audit_inputs']=audit_inputs(scene,meta)
   rows=[r for r in rows if (r['character_id'],r['mode'])!=(cid,mode)]+[row];write(path,{'schema':2,'audit_revision':AUDIT_REVISION,'source_manifest':index['source_manifest'],'combinations':rows,'summary':{'combinations':len(rows),'checked':sum(r['status']=='offline_special_mesh_checked'for r in rows),'current_revision':sum(r.get('audit_revision')==AUDIT_REVISION for r in rows),'runtime_verified':0,'visual_quality_certified':0},'operation':'actual indexed target/finger meshes with exact retarget oracle; actual special weapon mesh/bindpose/palette; no game/server process'});print(cid,mode,'native cached' if (cid,mode) in completed else 'native mesh frames',row['actual_mesh_frames'],'errors',len(row['errors']),'elapsed',round(time.time()-start),flush=True)
if __name__=='__main__':main()
