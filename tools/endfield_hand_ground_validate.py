"""Record full actual hand/shoe skin clearance for the two corrected clips."""
import numpy as np
from endfield_profiles import ROOT,read,write
from endfield_mesh_audit import setup,oracle,fk,skin,probe_floor
from endfield_pose_projection import ground_pose


def main():
 cid='chr_0018_dapan';scene=setup(cid,update_profile=False);meta,clips=oracle(cid,'sword_shield');selected={10011,10112};hands={side:{scene['index'][p] for n,p in scene['r']['fingers'].items() if n.startswith('Bip001_'+('L' if side=='left' else 'R')+'_Finger')}|{scene['index'][scene['r']['bones'][side+'_hand']]} for side in ['left','right']};feet={scene['index'][meta['bone_paths'][i]] for i in [15,16,19,20]};parts=[]
 for part in scene['parts']:
  subset={side:np.flatnonzero((np.isin(part['bone_indices'],list(ids))*part['weights']).sum(1)>.85) for side,ids in {**hands,'feet':feet}.items()}
  if any(len(x) for x in subset.values()):parts.append((part,subset))
 records=[]
 for clip in clips:
  if clip['slot'] not in selected:continue
  rows=[]
  for at in range(0,len(clip['rotations']),12):
   frames=np.arange(at,min(at+12,len(clip['rotations'])));world=fk(scene,meta,clip,frames);world=ground_pose(scene,meta,clip,frames,world,probe_floor);minima={side:np.full(len(frames),np.inf) for side in ['left','right','feet']}
   for part,subset in parts:
    values=skin(part,world)
    for side,vertices in subset.items():
     if len(vertices):minima[side]=np.minimum(minima[side],values[:,vertices,1].min(1))
   for i,frame in enumerate(frames):rows.append({'frame':int(frame),'left_min_y':float(minima['left'][i]),'right_min_y':float(minima['right'][i]),'foot_min_y':float(minima['feet'][i])})
  minimum={side:min(r[side+'_min_y'] for r in rows) for side in ['left','right','foot']};passed=all(v>=-.03 for v in minimum.values());records.append({'slot':clip['slot'],'frames':len(rows),'minimum':minimum,'passed':passed,'actual_skin_clearance':rows})
  print(cid,clip['slot'],len(rows),'actual indexed frames',minimum,'passed',passed,flush=True)
 result={'schema':1,'character_id':cid,'source_mode':'sword_shield','clips':records,'tolerance_m':.03,'runtime_verified':False,'visual_quality_certified':False,'source_evidence':['research/multiweapon/source-sns11-hand-ground.json','research/multiweapon/source-sns112-hand-ground.json'],'hand_probes':{s:len(v) for s,v in read(ROOT/'profiles/chr_0018_dapan.json')['ground_hand_probes'].items()},'policy':'Exact final source supportkind1/root,kind2 independent arm,kind3 actual target floor guard; fixed bone lengths and original wrist rotation; full actual weighted hands/feet mesh checked at every frame'}
 write(ROOT/'research/character-coverage/final-hand-body-ground.json',result)
 if not all(r['passed'] for r in records):raise RuntimeError('An actual hand/foot body vertex still penetrates the offline ground plane beyond tolerance')


if __name__=='__main__':main()
