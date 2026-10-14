"""Publish only exact per-character/per-model target-gated hilt intervals.

Original pose, source wrist offset, source rotations and authored flags are
never changed. Presentation weights fade inside verified intervals only.
"""
import math
from endfield_profiles import ROOT,read,write


def feather(mask,fps,seconds=.1):
 values=[0.]*len(mask);first=0
 while first<len(mask):
  if not mask[first]:first+=1;continue
  last=first
  while last+1<len(mask) and mask[last+1]:last+=1
  for f in range(first,last+1):
   x=max(0.,min(1.,(f-first)/(fps*seconds),(last-f)/(fps*seconds)))
   values[f]=x*x*(3-2*x)
  first=last+1
 return values


def main():
 candidates=read(ROOT/'research/multiweapon/longsword-contact-candidates.json');characters={r['character_id'] for r in read(ROOT/'profiles/index.json')['characters']};models={};total=0
 reports=[('wpn_misc_0016','research/character-coverage/longsword-contact-candidate-gate.json'),
          ('source:longsword_bs_swo001','research/character-coverage/longsword-source-contact-candidate-gate.json')]
 for model,path in reports:
  gate=read(ROOT/path);assert {r['character_id'] for r in gate['characters']}==characters
  target={}
  for row in gate['characters']:
   tracks={}
   for record in row['tracks']:
    slot=str(record['slot']);source=candidates['tracks'][slot];mask=record['pass_mask'];fps=int(source['fps'])
    assert len(mask)==record['frames']==len(source['candidate_mask'])
    assert all(v in [0,1] for v in mask)
    assert all(not v or bool(s) for v,s in zip(mask,source['candidate_mask']))
    weights=feather(mask,fps);assert all(math.isfinite(w) and 0<=w<=1 and (not w or m) for w,m in zip(weights,mask))
    tracks[slot]={'fps':fps,'frames':len(mask),'pass_mask':mask,'intervals':record['pass_intervals'],'weights':weights}
    total+=sum(w>0 for w in weights)
   assert set(tracks)==set(candidates['tracks'])
   target[row['character_id']]={'target_palm_half_width_m':row['target_palm_half_width_m'],'tracks':tracks}
  models[model]={'model_id':model,'characters':target,'evidence':path,'source_hilt_faces':gate.get('source_hilt_faces',gate.get('native_hilt_faces'))}
 output={'schema_version':2,'mode':'longsword','provenance':'reconstructed_from_actual_source_hilt_and_exact_target_geometry',
         'authored_contact_flag':False,'source_offsets_modified':False,'source_rotations_modified':False,
         'fade_seconds':.1,'fade_policy':'smoothstep_inside_exact_verified_interval_only',
         'source_candidate_evidence':'research/multiweapon/longsword-contact-candidates.json','models':models,
         'summary':{'characters':len(characters),'model_character_pairs':len(models)*len(characters),'positive_weight_frames':total},
         'runtime_verified':False,'visual_quality_certified':False}
 write(ROOT/'assets/modes/longsword/contact_masks.json',output)
 print('Published independent geometry-backed masks:',len(models),'models x',len(characters),'characters;',total,'positive weighted frames')


if __name__=='__main__':main()
