"""Quantify transported source wrist contact after the actual native IK.

This is a contact diagnostic, not a closed-mesh collision certification. Arm
lengths remain fixed and a target outside anatomical reach is a warning rather
than a reason to invent a wrist/weapon rotation. No game process is required.
"""
import argparse,time
import numpy as np
from scipy.spatial.transform import Rotation
from endfield_profiles import ROOT,read,write
from endfield_mesh_audit import setup,oracle,fk,OUT,P0,AUDIT_REVISION
from endfield_pose_projection import resources,source_frame,contact_weight


def audit(cid,mode,stride,scene=None):
 scene=scene or setup(cid,update_profile=False);meta,clips=oracle(cid,mode)
 basis=Rotation.from_quat(meta['source_basis_xyzw']).as_matrix()
 supporting=[10,11,12] if mode=='bow' else [6,7,8]
 dominant=8 if mode=='bow' else 12
 ids=[scene['index'][meta['bone_paths'][i]] for i in supporting+[dominant]]
 lengths=[np.linalg.norm(scene['rest'][ids[i+1],:3,3]-scene['rest'][ids[i],:3,3]) for i in range(2)]
 residual=[];unreachable=[];worst=None;count=0
 for clip in clips:
  n=len(clip['rotations']);selection=np.arange(n) if cid in P0 else np.unique(np.r_[np.arange(0,n,stride),0,n-1])
  # Source-side contact is deliberately absent on one-handed moves. Ignore
  # blend-in frames until the runtime contact weight reaches full strength.
  active=[int(f) for f in selection if (s:=source_frame(meta,clip['slot'],int(f))) and contact_weight(meta,clip['slot'],int(f),s)>.999 and float(f)/clip['fps']>=.18]
  if not active:continue
  for start in range(0,len(active),16):
   frames=np.asarray(active[start:start+16]);world=fk(scene,meta,clip,frames)
   for i,frame in enumerate(frames):
    source=source_frame(meta,clip['slot'],int(frame))
    offset=basis@Rotation.from_quat(source['rotation_xyzw']).as_matrix()@np.asarray(source['support_offset'])*meta['scale']
    desired=world[i,ids[3],:3,3]+offset
    error=float(np.linalg.norm(world[i,ids[2],:3,3]-desired))
    reach=float(np.linalg.norm(desired-world[i,ids[0],:3,3]))
    excess=float(max(0.,reach-sum(lengths)));residual.append(error);unreachable.append(excess);count+=1
    if worst is None or error>worst['residual_m']:worst={'slot':clip['slot'],'frame':int(frame),'residual_m':error,'reach_excess_m':excess,'actual_wrist':world[i,ids[2],:3,3].tolist(),'source_contact_target':desired.tolist()}
 warnings=[]
 if residual and max(residual)>.05:warnings.append('Full-weight supporting-wrist source contact exceeds 5 cm in sampled frames; actual target anatomy and source contact reach retained for review')
 return {'character_id':cid,'mode':mode,'status':'offline_contact_measured','audit_revision':AUDIT_REVISION,'full_weight_frames':count,'max_residual_m':max(residual,default=0.),'p95_residual_m':float(np.percentile(residual,95)) if residual else 0.,'max_anatomical_reach_excess_m':max(unreachable,default=0.),'worst_frame':worst,'coupled_solve_frames':sum(e[0]==mode for e in scene.get('_coupled_frames',set())),'unreachable_contact_frames':sum(e[0]==mode for e in scene.get('_unreachable_contact',set())),'warnings':warnings,'runtime_verified':False,'visual_quality_certified':False,'metric':'Actual transported wrist-to-source contact after native fixed-length IK; not finger-to-grip mesh distance or body intersection certification'}


def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--characters',nargs='*');parser.add_argument('--stride',type=int,default=12);a=parser.parse_args();index=read(ROOT/'profiles/index.json');rows=[];start=time.time()
 for cid in a.characters or [c['character_id'] for c in index['characters']]:
  scene=setup(cid,update_profile=False)
  for mode in index['modes']:
   row=audit(cid,mode,max(1,a.stride),scene);rows.append(row)
   print(cid,mode,'contact frames',row['full_weight_frames'],'max m',round(row['max_residual_m'],4),'elapsed',round(time.time()-start),flush=True)
   write(OUT/'contact-coverage.json',{'schema':1,'audit_revision':AUDIT_REVISION,'combinations':rows,'summary':{'combinations':len(rows),'warnings':sum(bool(r['warnings']) for r in rows),'runtime_verified':0,'visual_quality_certified':0},'operation':'Exact retarget.h wrist contact measurement, actual target rest lengths, resource-authored support offsets; no game/server process'})


if __name__=='__main__':main()
