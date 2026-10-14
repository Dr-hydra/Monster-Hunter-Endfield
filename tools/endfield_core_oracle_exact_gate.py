"""Replay native source poses and compare every saved core float without hashes."""
import argparse,subprocess,shutil,time
import numpy as np
from endfield_profiles import ROOT,read,write
from endfield_mesh_audit import OUT,oracle


def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--generate',action='store_true')
 parser.add_argument('--refresh-changed-oracles',action='store_true',help='Caller must rerun the reported affected actual mesh rows afterward')
 args=parser.parse_args();index=read(ROOT/'profiles/index.json');fresh=OUT/'final-current-core-oracle';started=time.time()
 if args.generate:
  exe=ROOT/'build/offline-retarget-dual/Release/endfield_retarget_oracle.exe'
  with (OUT/'final-current-core-oracle-generate.log').open('w',encoding='utf-8') as log:
   for mode in index['modes']:
    meta,_=oracle(index['characters'][0]['character_id'],mode)
    slots=','.join(str(c['slot']) for c in meta['clips'])
    subprocess.run([str(exe),str(ROOT),str(fresh),mode,slots],stdout=log,stderr=subprocess.STDOUT,check=True)
 rows=[];errors=[]
 for char in index['characters']:
  cid=char['character_id']
  for mode in index['modes']:
   old_meta,old_clips=oracle(cid,mode);new_meta,new_clips=oracle(cid,mode,fresh)
   assert [c['slot'] for c in old_clips]==[c['slot'] for c in new_clips]
   changed_keys=[key for key in set(old_meta)|set(new_meta) if old_meta.get(key)!=new_meta.get(key)]
   failures=[]
   for old,new in zip(old_clips,new_clips):
    assert old['fps']==new['fps']
    for name in ['rotations','hip']:
     if not np.array_equal(old[name],new[name]):
      failures.append({'slot':old['slot'],'array':name,'old_shape':list(old[name].shape),'current_shape':list(new[name].shape),'maximum_absolute_difference':float(np.max(np.abs(old[name]-new[name]))) if old[name].shape==new[name].shape else None})
   ok=not changed_keys and not failures
   row={'character_id':cid,'mode':mode,'source_slots':[c['slot'] for c in old_clips],'source_frames':sum(len(c['rotations'])for c in old_clips),'changed_metadata':changed_keys,'changed_arrays':failures,'exact_array_equal':ok,'runtime_verified':False}
   rows.append(row)
   if not ok:
    errors.append(row)
    if args.refresh_changed_oracles:
     backup=OUT/'pre-final-core-oracle'/cid;backup.mkdir(parents=True,exist_ok=True)
     for suffix in ['.json','.ltp']:
      source=OUT/'oracle'/cid/(mode+suffix)
      shutil.copy2(source,backup/source.name);shutil.copy2(fresh/cid/source.name,source)
   print(cid,mode,'fresh native exact',ok,flush=True)
 result={'schema':1,'rows':rows,'changed_rows':errors,'summary':{'rows':len(rows),'source_frames':sum(r['source_frames']for r in rows),'exact_array_equal':sum(r['exact_array_equal']for r in rows),'refreshed_rows':len(errors)if args.refresh_changed_oracles else 0},'operation':'Every main/finger quaternion and hip float from a newly compiled native retarget oracle compared exactly plus every metadata field; no artifact hashes; refreshed rows require an actual mesh re-scan','runtime_verified':False,'elapsed_seconds':time.time()-started}
 report='core-oracle-refresh.json' if args.refresh_changed_oracles else 'final-core-oracle-exact-gate.json'
 write(OUT/report,result)
 if not args.refresh_changed_oracles:assert not errors,'Core floats drifted: regenerate affected mesh evidence'


if __name__=='__main__':main()
