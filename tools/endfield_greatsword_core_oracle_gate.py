"""Compare previously audited GS core arrays with a fresh native oracle.

New held clips remain separate. No tolerance or hash hides a changed source
rotation, finger rotation, root position, native basis or rest alignment.
"""
import argparse
import numpy as np
from endfield_profiles import ROOT,read,write
from endfield_mesh_audit import OUT,oracle


def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--current-root',default=OUT/'final-gs-core-oracle',type=type(OUT))
 args=parser.parse_args();rows=[];errors=[]
 for char in read(ROOT/'profiles/index.json')['characters']:
  cid=char['character_id'];old_meta,old_clips=oracle(cid,'greatsword');new_meta,new_clips=oracle(cid,'greatsword',args.current_root)
  old_by={c['slot']:c for c in old_clips};new_by={c['slot']:c for c in new_clips}
  removed=set(old_by)-set(new_by);added=set(new_by)-set(old_by)
  changed_keys=[key for key in set(old_meta)|set(new_meta) if key not in {'clips','frames'} and old_meta.get(key)!=new_meta.get(key)]
  failures=[];frames=0
  for slot,old in old_by.items():
   if slot not in new_by:continue
   new=new_by[slot];frames+=len(old['rotations'])
   for name in ['rotations','hip']:
    if not np.array_equal(old[name],new[name]):
     failures.append({'slot':slot,'array':name,'old_shape':list(old[name].shape),'current_shape':list(new[name].shape),'maximum_absolute_difference':float(np.max(np.abs(old[name]-new[name]))) if old[name].shape==new[name].shape else None})
   if old['fps']!=new['fps']:failures.append({'slot':slot,'fps_changed':True})
  old_items={c['slot']:c for c in old_meta['clips']};new_items={c['slot']:c for c in new_meta['clips']}
  changed_clip_meta=[slot for slot in old_items if old_items[slot]!=new_items.get(slot)]
  held_meta,held_clips=oracle(cid,'greatsword',OUT/'held-oracle')
  held_equal=all(held_meta.get(key)==new_meta.get(key)for key in set(held_meta)|set(new_meta)if key not in {'clips','frames'})
  held_equal=held_equal and {c['slot']for c in held_clips}=={20145,20245}
  held_equal=held_equal and all(c['fps']==new_by[c['slot']]['fps'] and np.array_equal(c['rotations'],new_by[c['slot']]['rotations']) and np.array_equal(c['hip'],new_by[c['slot']]['hip']) and next(m for m in held_meta['clips']if m['slot']==c['slot'])==new_items[c['slot']]for c in held_clips)
  ok=not removed and added=={20145,20245} and not failures and not changed_keys and not changed_clip_meta and held_equal
  row={'character_id':cid,'previous_core_slots':sorted(old_by),'previous_core_frames':frames,'new_held_slots':sorted(added),'held_frames':sum(len(c['rotations'])for c in held_clips),'held_exact_array_equal':held_equal,'changed_basis_or_rest_keys':changed_keys,'changed_clip_metadata':changed_clip_meta,'changed_arrays':failures,'removed_slots':sorted(removed),'exact_array_equal':ok,'runtime_verified':False}
  rows.append(row)
  if not ok:errors.append(row)
  print(cid,frames,'previous core frames exact',ok,flush=True)
 result={'schema':1,'rows':rows,'errors':errors,'summary':{'characters':len(rows),'previous_core_frames':sum(x['previous_core_frames'] for x in rows),'held_frames':sum(x['held_frames']for x in rows),'held_exact_array_equal':sum(x['held_exact_array_equal']for x in rows),'exact_array_equal':sum(x['exact_array_equal'] for x in rows)},'operation':'Fresh native retarget.h build; exact NumPy array equality of every previous main/finger quaternion and hip float plus exact semantic metadata; no artifact hashes; new 20145/20245 held clips independently compared between full and incremental native outputs','runtime_verified':False}
 write(OUT/'greatsword-core-oracle-exact-gate.json',result)
 assert not errors,'Previous GS core changed; rerun affected mesh scans before merging'


if __name__=='__main__':main()
