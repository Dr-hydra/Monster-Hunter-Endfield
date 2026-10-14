"""Add only actual footwear vertices missed by the extremal rest probes.

The target uses at most 32 native probes. This changes the measured contact
surface, never source animation, body rotations or the source flight curve.
"""
import argparse,time
import numpy as np
from endfield_profiles import ROOT,read,write
from endfield_mesh_audit import setup,oracle,fk,probe_floor,skin,OUT,P0
from endfield_pose_projection import ground_pose


def worst_contact(cid,modes,stride):
 scene=setup(cid,update_profile=False);worst=None
 for mode in modes:
  meta,clips=oracle(cid,mode);feet=[scene['index'][meta['bone_paths'][i]] for i in [15,16,19,20]]
  selected_parts=[]
  for part in scene['parts']:
   mask=(np.isin(part['bone_indices'],feet)*part['weights']).sum(1)>.85
   if not mask.any():continue
   ids=np.flatnonzero(mask);small={k:v for k,v in part.items() if k!='_skin_groups'}
   for key in ['vertices','bone_indices','weights','bind_vertices']:small[key]=part[key][ids]
   selected_parts.append((part,ids,small))
  for clip in clips:
   n=len(clip['rotations']);selected=np.arange(n) if cid in P0 else np.unique(np.r_[np.arange(0,n,stride),0,n-1])
   for start in range(0,len(selected),16):
    frames=selected[start:start+16];world=fk(scene,meta,clip,frames);world=ground_pose(scene,meta,clip,frames,world,probe_floor)
    for part,vertices,small in selected_parts:
     points=skin(small,world);at=np.unravel_index(points[:,:,1].argmin(),points[:,:,1].shape);height=float(points[at][1])
     if worst is not None and height>=worst['height_m']:continue
     vertex=int(vertices[at[1]]);left_weight=sum(float(w) for b,w in zip(part['bone_indices'][vertex],part['weights'][vertex]) if int(b) in feet[:2])
     worst={'mode':mode,'slot':clip['slot'],'frame':int(frames[at[0]]),'mesh_id':part['mesh_id'],'mesh_name':part['name'],'vertex':vertex,'side':'left' if left_weight>.5 else 'right','height_m':height,'point':points[at].tolist()}
 return scene,worst


def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--characters',nargs='+',required=True);parser.add_argument('--stride',type=int,default=12);parser.add_argument('--tolerance',type=float,default=.025);a=parser.parse_args();registry=read(ROOT/'profiles/index.json');profiles={c['character_id']:ROOT/'profiles'/c['profile'] for c in registry['characters']};records=[];start=time.time()
 for cid in a.characters:
  added=[]
  while True:
   scene,worst=worst_contact(cid,registry['modes'],max(1,a.stride));profile=read(profiles[cid])
   if worst is None or worst['height_m']>=-a.tolerance or len(scene['probes'])>=32:break
   extra={k:worst[k] for k in ['mesh_id','vertex','side']};extra['evidence']=worst
   old=profile.get('ground_extra_foot_probe_vertices',[])
   assert not any(x['mesh_id']==extra['mesh_id'] and x['vertex']==extra['vertex'] for x in old),'An already measured probe cannot penetrate its own floor'
   profile['ground_extra_foot_probe_vertices']=old+[extra];write(profiles[cid],profile);setup(cid,update_profile=True);added.append(extra)
   print(cid,'added actual shoe vertex',worst['vertex'],'previous min m',round(worst['height_m'],5),'elapsed',round(time.time()-start),flush=True)
  row={'character_id':cid,'added':added,'final_probe_count':len(scene['probes']),'worst_sample_after':worst,'within_tolerance':worst is None or worst['height_m']>=-a.tolerance,'tolerance_m':a.tolerance,'runtime_verified':False};records.append(row)
  print(cid,'final probes',len(scene['probes']),'min m',round(worst['height_m'],5) if worst else None,flush=True)
 write(OUT/'sole-refinement.json',{'schema':1,'characters':records,'policy':'Actual indexed weighted target footwear only; source clearance and rotations unchanged; max native probe count32'})


if __name__=='__main__':main()
