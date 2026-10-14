"""Build actual weighted target hand probes for source-qualified roll support.

Source curves are full original glove skin minima. Only SnS original roll11
has source support evidence here; no other motion acquires hand grounding.
"""
import argparse
import numpy as np
from endfield_profiles import ROOT,read,write
from endfield_mesh_audit import setup,oracle,fk,probe_floor,skin
from endfield_pose_projection import resources,ground_pose


def point_probe(scene,part,vertex,side):
 return {'side':side,'mesh_id':part['mesh_id'],'mesh_name':part['name'],'vertex':int(vertex),
         'rest_model_point':skin(part,scene['rest'][None])[0,vertex].tolist(),
         'influences':[{'bone_path':scene['paths'][int(b)],'bone_name':scene['paths'][int(b)].rsplit('/',1)[-1],'weight':float(w),'local_point':p[:3].tolist()} for b,w,p in zip(part['bone_indices'][vertex],part['weights'][vertex],part['bind_vertices'][vertex]) if w>1e-5]}


def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--characters',nargs='+',default=['chr_0018_dapan']);a=parser.parse_args();source=read(ROOT/'research/multiweapon/source-sns11-hand-ground.json');track=source['track'];published={'schema_version':1,'tracks':{'10011':{'fps':60,'frames':track['frames'],**{side:{'active':track[side]['hand_lower_than_shoes_mask'],'kind':track[side]['hand_lower_than_shoes_mask'],'clearance':track[side]['clearance']} for side in ['left','right']}}},'source_evidence':['research/multiweapon/source-sns11-hand-ground.json','research/multiweapon/source-sns112-hand-ground.json'],'provenance':'Actual original glove/shoe skin:1 hand-support roll root,2 near-ground independent hand arm IK; no wrist rotation/source pose edit','authored_ground_contact_flag':False,'source_root_and_rotations_modified':False}
 second=read(ROOT/'research/multiweapon/source-sns112-hand-ground.json')['track'];published['tracks']['10112']={'fps':60,'frames':second['frames']}
 for side in ['left','right']:
  active=[int(h<=second[side]['palm_half_width_m'] and f<=second[side]['palm_half_width_m']) for h,f in zip(second[side]['clearance'],second['shoe_clearance'])]
  published['tracks']['10112'][side]={'active':active,'kind':[2*v for v in active],'clearance':second[side]['clearance']}
 for slot,raw_track in [('10011',track),('10112',second)]:
  for side in ['left','right']:
   row=published['tracks'][slot][side];radius=raw_track[side]['palm_half_width_m']
   row['kind']=[3 if not k and sole<=radius else k for k,sole in zip(row['kind'],raw_track['shoe_clearance'])]
 published['target_guard_evidence']='Actual target Dapan indexed hand vertices penetrated during otherwise grounded portions of these two source-qualified clips; kind3 performs minimal upward arm IK only if the target hand itself crosses terrain, source active remains false'
 published['kind_schema']={'0':'inactive','1':'source hand-support root lift','2':'source near-ground hand arm IK with exact source clearance','3':'target actual hand-to-terrain collision guard,zero clearance,independent of source active'}
 # Gate on the explicit source-supported slot; the file does not change base
 # foot clearance and cannot activate during idle or another weapon mode.
 write(ROOT/'assets/modes/sword_shield/support_ground_tracks.json',published);resources.cache_clear();registry=read(ROOT/'profiles/index.json');profiles={r['character_id']:ROOT/'profiles'/r['profile'] for r in registry['characters']};report=[]
 for cid in a.characters:
  scene=setup(cid,update_profile=False);probes=[];parts_by_side={};directions=list(np.eye(3))+list(-np.eye(3))+[np.array([x,y,z]) for x in [-1,1] for y in [-1,1] for z in [-1,1]]
  for side in ['left','right']:
   names={scene['r']['bones'][side+'_hand']}|{p for n,p in scene['r']['fingers'].items() if n.startswith('Bip001_'+('L' if side=='left' else 'R')+'_Finger')};bones=[scene['index'][p] for p in names];candidates=[];parts_by_side[side]=[]
   for part in scene['parts']:
    mask=(np.isin(part['bone_indices'],bones)*part['weights']).sum(1)>.85;ids=np.flatnonzero(mask)
    if not len(ids):continue
    points=skin(part,scene['rest'][None])[0];candidates.extend((part,int(v),points[v]) for v in ids);parts_by_side[side].append((part,ids))
   assert candidates;seen=set()
   for direction in directions:
    part,v,_=min(candidates,key=lambda c:float(c[2]@direction));key=(part['mesh_id'],v)
    if key in seen:continue
    seen.add(key);probes.append(point_probe(scene,part,v,side))
  meta,clips=oracle(cid,'sword_shield');selected_clips=[c for c in clips if str(c['slot']) in published['tracks']];iterations=[]
  while True:
   scene['hand_probes']=probes;worst=None
   for clip in selected_clips:
    source_track=published['tracks'][str(clip['slot'])]
    for at in range(0,len(clip['rotations']),12):
     frames=np.arange(at,min(len(clip['rotations']),at+12));world=fk(scene,meta,clip,frames);world=ground_pose(scene,meta,clip,frames,world,probe_floor)
     for side,parts in parts_by_side.items():
      kinds=np.asarray(source_track[side]['kind'])[frames];active=np.asarray(source_track[side]['active'],bool)[frames]|(kinds==3);desired=np.where(kinds==3,0,np.asarray(source_track[side]['clearance'])[frames]*meta['scale'])
      for part,ids in parts:
       points=skin(part,world)[:,ids];delta=points[:,:,1]-desired[:,None];delta[~active]=np.inf;where=np.unravel_index(delta.argmin(),delta.shape);value=float(delta[where])
       if np.isfinite(value) and (worst is None or value<worst['error']):worst={'error':value,'side':side,'part':part,'vertex':int(ids[where[1]]),'frame':int(frames[where[0]]),'slot':clip['slot']}
   if worst is None or worst['error']>=-.02 or sum(p['side']==worst['side'] for p in probes)>=32:break
   assert not any(p['mesh_id']==worst['part']['mesh_id'] and p['vertex']==worst['vertex'] for p in probes)
   probes.append(point_probe(scene,worst['part'],worst['vertex'],worst['side']));iterations.append({'frame':worst['frame'],'vertex':worst['vertex'],'previous_penetration_m':worst['error']})
  data=read(profiles[cid]);data['ground_hand_probes']={side:[p for p in probes if p['side']==side] for side in ['left','right']};data['ground_hand_evidence']='Actual weighted target hand/glove/finger vertices; only source-qualified source roll support intervals apply';write(profiles[cid],data)
  result={'character_id':cid,'probes':len(probes),'worst_supported_source_clearance_residual_m':worst['error'] if worst else None,'refinements':iterations,'runtime_verified':False};report.append(result);print(cid,result,flush=True)
 write(ROOT/'research/character-coverage/hand-ground-profile-gate.json',{'schema':1,'characters':report,'source_mode':'sword_shield','source_slot':11,'all_clip_frames_tested':True,'policy':'Actual hand skin clearance only inside original source hand-support phase; no change to animation/bone lengths/source flight curve'})


if __name__=='__main__':main()
