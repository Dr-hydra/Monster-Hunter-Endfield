"""Scratch-only target gate for geometry-reconstructed source hilt contact.

No published weights are changed. Candidate intervals keep the original source
wrist offset and rotations. This tests fixed-length native IK, actual target
knuckles and actual indexed native fireblade hilt, not an authored IK flag.
"""
import argparse,time
import numpy as np
from endfield_profiles import ROOT,read,write
from endfield_mesh_audit import setup,oracle,fk,skin,render,OUT,probe_floor
from endfield_pose_projection import resources,ground_pose
from endfield_special_mesh_audit import pose_parts
from endfield_special_previews import geometry
from laevat_profile import Graph
from mhw_source_contact_audit import point_to_triangles,intervals


def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--characters',nargs='+');parser.add_argument('--resume',action='store_true');a=parser.parse_args()
 characters=a.characters or [c['character_id'] for c in read(ROOT/'profiles/index.json')['characters']]
 candidates=read(ROOT/'research/multiweapon/longsword-contact-candidates.json');data=resources('longsword');tracks=data['weapon']['tracks'];original={slot:[float(f['support_weight']) for f in row['frames']] for slot,row in tracks.items()}
 graph=Graph(read(ROOT/'research/expansion-20261013/laevat-weapon-raw.json'));parts=geometry(graph,ROOT/'research/weapon-coverage/laevat-geometry','wpn_misc_0016');blade=next(p for p in parts if p['name']=='S_wpn_misc_00016_02_lod0')
 root=np.linalg.inv(graph.world(graph.paths['wpn_misc_0016']));local_rest=np.asarray([root@graph.world(graph.paths[p]) for p in blade['palette']],np.float32)
 local=dict(blade);local['bone_indices']=local['local_indices'];local_vertices=skin(local,local_rest[None])[0];hilt_faces=blade['faces'][np.max(local_vertices[blade['faces'],1],axis=1)<0]
 assert len(hilt_faces)>0;destination=OUT/'longsword-contact-candidate-gate.json';rows=read(destination)['characters'] if a.resume and destination.exists() else [];start=time.time();gallery=OUT/'contact-candidate-gallery';gallery.mkdir(exist_ok=True)
 def save():
  write(destination,{'schema':2,'model_id':'wpn_misc_0016','characters':rows,'native_hilt_faces':len(hilt_faces),'candidate_source':'research/multiweapon/longsword-contact-candidates.json','authored_contact_flag':False,'assets_mutated':False,'published':False,'runtime_verified':False,'policy':'Exact native fixed-length solver; original wrist offset/rotation; both actual target proximal knuckles within measured target half palm width of actual negative-Y indexed fireblade hilt; settled blend only'})
 try:
  for slot,candidate in candidates['tracks'].items():
   for f,mask in zip(tracks[slot]['frames'],candidate['candidate_mask']):f['support_weight']=float(mask)
  for cid in characters:
   if any(r['character_id']==cid for r in rows):
    print(cid,'reused unchanged initial P0 candidate gate',flush=True);continue
   scene=setup(cid,update_profile=False);meta,clips=oracle(cid,'longsword');meta['contact_candidate_scratch']=True;ids=[scene['index'][scene['r']['fingers']['Bip001_L_Finger'+str(n)]] for n in [1,2]]
   index=scene['index'][scene['r']['fingers']['Bip001_L_Finger1']];pinky=scene['index'][scene['r']['fingers']['Bip001_L_Finger4']];tolerance=float(np.linalg.norm(scene['rest'][index,:3,3]-scene['rest'][pinky,:3,3])/2)
   right=scene['index'][meta['bone_paths'][12]];left=scene['index'][meta['bone_paths'][8]]
   char_rows=[]
   for clip in clips:
    candidate=candidates['tracks'].get(str(clip['slot']))
    if not candidate:continue
    source_mask=np.asarray(candidate['candidate_mask'],bool);selected=np.flatnonzero(source_mask);passing=np.zeros(len(source_mask),np.int8);samples=[];worst=0.;unreachable=0;maximum_residual=0.
    for at in range(0,len(selected),12):
     frames=selected[at:at+12];world=fk(scene,meta,clip,frames);world=ground_pose(scene,meta,clip,frames,world,probe_floor);posed,clones,_=pose_parts(scene,meta,clip,frames,world,graph,parts,'longsword');actual_blade=next(p for p in clones if p['name']==blade['name']);vertices=skin(actual_blade,posed)
     for k,frame in enumerate(frames):
      triangles=vertices[k,hilt_faces];knuckles=world[k,ids,:3,3];distance=float(max(point_to_triangles(knuckles,triangles)));worst=max(worst,distance)
      # Exact wrist-offset residual is measured with the same transported
      # source frame as the native helper, independently of hilt proximity.
      from scipy.spatial.transform import Rotation
      source=tracks[str(clip['slot'])]['frames'][int(frame)];basis=Rotation.from_quat(meta['source_basis_xyzw']).as_matrix();offset=basis@Rotation.from_quat(source['rotation_xyzw']).as_matrix()@np.asarray(source['support_offset'])*meta['scale']
      residual=float(np.linalg.norm(world[k,left,:3,3]-world[k,right,:3,3]-offset));maximum_residual=max(maximum_residual,residual)
      failure=('longsword',clip['slot'],int(frame)) in scene.get('_unreachable_contact',set());unreachable+=int(failure)
      # Blend-in is presentation transition, so only settled contact is
      # eligible for promotion. Rejected frames keep original source pose.
      settled=float(frame)/clip['fps']>=.18
      passing[frame]=int(settled and not failure and residual<.005 and distance<=tolerance)
      if (distance>tolerance or residual>.005) and len(samples)<8:samples.append({'frame':int(frame),'knuckle_hilt_max_m':distance,'wrist_source_offset_residual_m':residual,'settled':settled,'unreachable':failure})
    row={'slot':clip['slot'],'frames':len(source_mask),'candidate_frames':int(source_mask.sum()),'passing_frames':int(passing.sum()),'pass_mask':passing.tolist(),'pass_intervals':intervals(passing),'max_knuckle_hilt_m':worst,'maximum_wrist_source_offset_residual_m':maximum_residual,'unreachable_frames':unreachable,'rejected_samples':samples};char_rows.append(row)
    if cid in ['chr_0003_endminf','chr_0016_laevat','chr_0034_typhoea'] and clip['slot'] in [102,307,311,1000] and passing.any():
     frame=int(np.flatnonzero(passing)[len(np.flatnonzero(passing))//2]);sample=np.array([frame]);world=fk(scene,meta,clip,sample);world=ground_pose(scene,meta,clip,sample,world,probe_floor);posed,clones,_=pose_parts(scene,meta,clip,sample,world,graph,parts,'longsword')
     for view in ['rear','side','front']:render(scene['parts']+clones,posed,gallery/f'{cid}-{clip["slot"]}-{frame}-{view}.png',cid+f'\nScratch contact candidate {clip["slot"]}:{frame}\nActual native hilt',view)
   rows.append({'character_id':cid,'target_palm_half_width_m':tolerance,'tracks':char_rows,'candidate_frames':sum(r['candidate_frames'] for r in char_rows),'passing_frames':sum(r['passing_frames'] for r in char_rows),'runtime_verified':False,'published':False})
   save()
   print(cid,'candidate frames',rows[-1]['candidate_frames'],'passing',rows[-1]['passing_frames'],'elapsed',round(time.time()-start),flush=True)
 finally:
  for slot,weights in original.items():
   for frame,weight in zip(tracks[slot]['frames'],weights):frame['support_weight']=weight
 save()


if __name__=='__main__':main()
