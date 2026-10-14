"""Verify actual mesh frames affected by the final coupled-arm reach predicate.

The existing GS scan need not be rerendered in full: find coupled-solver frames
at its recorded mesh sample policy, then skin those frames and their neighbors
with the exact final native poles/reach epsilon. This is an incremental check.
"""
import copy,time
import numpy as np
from endfield_profiles import ROOT,read,write
from endfield_mesh_audit import OUT,P0,setup,oracle,fk,audit,audit_inputs

CHARACTERS=['chr_0016_laevat','chr_0018_dapan','chr_0034_typhoea','chr_0038_purrche']

def main():
 rows=[];started=time.time()
 for cid in CHARACTERS:
  scene=setup(cid,update_profile=False);meta,clips=oracle(cid,'greatsword');selection={};coupled=[]
  for clip in clips:
   n=len(clip['rotations']);samples=np.arange(n) if cid in P0 else np.unique(np.r_[np.arange(0,n,12),0,n-1])
   for at in range(0,len(samples),32):fk(scene,meta,clip,samples[at:at+32])
   activated=sorted(frame for mode,slot,frame in scene.get('_coupled_frames',set()) if mode=='greatsword' and slot==clip['slot'])
   if activated:
    coupled.extend({'slot':clip['slot'],'frame':f} for f in activated)
    selection[clip['slot']]=np.unique([max(0,min(n-1,f+delta)) for f in activated for delta in [-1,0,1]])
  assert coupled,cid+' coupled regression lost expected reach-clamped samples'
  selected=[clip for clip in clips if clip['slot'] in selection];filtered=copy.deepcopy(meta);filtered['clips']=[c for c in meta['clips'] if c['slot'] in selection]
  item=audit(cid,'greatsword',scene,filtered,selected,12,selections=selection,gallery_directory=OUT/'final-coupled-regression-gallery')
  item['coupled_frames']=coupled;item['selected_frames']={str(slot):frames.tolist() for slot,frames in selection.items()}
  item['audit_inputs']=audit_inputs(scene,filtered)
  item['mesh_sample_policy']='Every coupled frame encountered under existing GS scan policy, with immediate previous/next frames'
  rows.append(item)
  print(cid,'final coupled samples',len(coupled),'mesh',item['mesh_frames'],'errors',len(item['errors']),'foot issues',len(item['foot_penetration_samples']),'elapsed',round(time.time()-started),flush=True)
  write(OUT/'final-coupled-contact-mesh-regression.json',{'schema':1,'characters':rows,'summary':{'characters':len(rows),'mesh_frames':sum(x['mesh_frames'] for x in rows),'checked':sum(x['status']=='offline_mesh_checked' for x in rows)},'solver_policy':'Exact retarget.h DLL; native SolveArm.clamped adaptive epsilon/minimum and maximum reach; native right/left poles (1,0,-1)/(-1,0,-1); fixed lengths and wrist orientations','previous_core_rerendered':False,'runtime_verified':False,'visual_quality_certified':False})
 assert all(x['status']=='offline_mesh_checked' and not x['foot_penetration_samples'] for x in rows)
 print('Final coupled-arm mesh regression passed',flush=True)

if __name__=='__main__':main()
