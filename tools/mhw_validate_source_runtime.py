"""Validate final source runtime closure and deterministic demo data coverage.

This is source-data verification, not in-game validation or a native host test.
It checks exact binary dimensions, interval/cue ordering and source provenance.
"""
from pathlib import Path
import array,json,math,struct,wave
ROOT=Path(__file__).resolve().parents[1]
MODES=['longsword','sword_shield','greatsword','lance','light_bowgun','bow']
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def fail(why):raise ValueError(why)
def binary(path,magic,frames,bones,fps,stride):
 raw=path.read_bytes()
 if len(raw)<16 or struct.unpack_from('<4sIII',raw)!=(magic,frames,bones,fps):fail('Bad header '+str(path))
 if len(raw)!=16+frames*stride*4:fail('Bad dimensions '+str(path))
 values=array.array('f');values.frombytes(raw[16:])
 if any(not math.isfinite(v) for v in values):fail('Nonfinite '+str(path))
 return values
def quaternions(values,stride,bones,path):
 for offset in range(0,len(values),stride):
  for b in range(bones):
   norm=sum(v*v for v in values[offset+b*4:offset+b*4+4])
   if abs(norm-1)>.02:fail('Nonunit original quaternion '+str(path))
def main():
 results=[];index=load(ROOT/'assets/modes/index.json');registered={m['id']:m for m in index['modes']}
 if set(registered)!=set(MODES):fail('Six modes not registered')
 for mode in MODES:
  folder=ROOT/registered[mode]['data_root'];catalog=load(folder/'catalog.json');by={str(x['slot']):x for x in catalog}
  moves=load(folder/'moves.json');ground=load(folder/'ground_tracks.json')['tracks'];fingers=load(folder/'finger_tracks.json');weapons=load(folder/'source_weapon_tracks.json')['tracks'];hits=load(folder/'hit_windows.json')['windows'];audio=load(folder/'audio/cues.json');props=load(folder/'props/index.json')['props'];skin=load(folder/'source_skin_tracks.json')
  core={str(s) for command in moves['commands'].values() for s in command}
  if not core.issubset(by):fail(mode+' missing core clip')
  maximum=0
  for slot,row in by.items():
   path=folder/row['file'];n=row['frames'];fps=row.get('fps',60);vals=binary(path,b'LSM1',n,21,fps,87);quaternions(vals,87,21,path)
   if abs(row['duration']-(n-1)/fps)>1e-6:fail(mode+' duration mismatch')
   if slot not in ground or len(ground[slot]['clearance'])!=n:fail(mode+' missing actual source ground '+slot)
   g=ground[slot]['clearance']
   if any(not math.isfinite(x) or x<0 or x>100 for x in g):fail(mode+' invalid actual ground')
   maximum=max(maximum,max(g))
   if slot not in weapons or len(weapons[slot]['frames'])!=n:fail(mode+' missing actual source mounts '+slot)
  for slot in core:
   row=by[slot];f=fingers['tracks'].get(slot)
   if f is None:fail(mode+' missing exact core fingers '+slot)
   vals=binary(folder/f['file'],b'LSF1',row['frames'],len(fingers['bones']),60,len(fingers['bones'])*4);quaternions(vals,len(fingers['bones'])*4,len(fingers['bones']),f['file'])
  for slot,windows in hits.items():
   end=-1
   for a,b,m in windows:
    if not 0<=a<b<=by[slot]['duration'] or a<end or not 0<m<=10:fail(mode+' invalid local hit interval '+slot)
    end=b
  ids={clip['id'] for clip in audio['clips']}
  for clip in audio['clips']:
   path=ROOT/registered[mode]['audio_root']/clip['file']
   with wave.open(str(path),'rb') as w:
    if w.getsampwidth()!=2 or not 1<=w.getnchannels()<=2 or w.getnframes()<1:fail(mode+' unsupported PCM')
  for slot,cues in audio['cues'].items():
   if slot not in by:fail(mode+' cue for unpublished slot')
   previous=-1
   for cue in cues:
    time=cue['time']
    if not previous<=time<=by[slot]['duration'] or time<0:fail(mode+' unsorted/out-of-bounds cue')
    previous=time
    if cue['clip'] not in ids or any(v not in ids for v in cue.get('variants',[])):fail(mode+' unresolved audio reference')
  for prop in props:
   data=load(folder/prop['file'])
   if not data.get('source_geometry_normalized') or 'source_mount_formula' not in data:fail(mode+' missing exact root normalization')
   if data.get('role')=='sheath' and data.get('mounting_available') is not False:fail('Unproven sheath mount enabled')
  for slot,track in skin['tracks'].items():
   if slot not in by or track['frames']!=by[slot]['frames']:fail(mode+' own palette body timing mismatch')
   binary(folder/track['file'],b'LSK1',track['frames'],track['bone_count'],60,track['bone_count']*16)
  # A missing original own-animation trigger is an explicit bind fallback,
  # never a reason to fabricate a source animation or claim native carry state.
  fallback=skin.get('bind_fallbacks',{})
  if mode=='greatsword':
   timeline=load(ROOT/'research/multiweapon/greatsword/timeline-decoded.json')['actions']
   from mhw_prop_skin_tracks import phases,deformation,identity
   prop=load(folder/skin['props'][next(iter(skin['props']))]['file'])
   if max(abs(a-b) for matrix in deformation(prop,{}) for a,b in zip(matrix,identity()))>2e-4:fail('GS original bind palette mismatch')
   for slot in ('20144','20145','20245'):
    record=fallback.get(slot);row=by[slot]
    if not record or record['frames']!=row['frames'] or record['source_slot']!=row['source_slot']:fail('GS held bind fallback missing '+slot)
    if phases(timeline[str(row['source_slot'])])!=([],[]) or slot in skin['tracks']:fail('GS original no-trigger evidence changed '+slot)
    if not record['original_trigger_absent'] or record['generated_own_track'] or record['native_carry_state_verified']:fail('GS fabricated own trigger '+slot)
  # 100 complete source tours visit all declared variants and trigger more
  # than one source PCM. This does not replace native controller/host tests.
  next_variant={};visited=set();heard=set();triggers=0;seconds=0;commands=set()
  for round_ in range(100):
   for command in moves['demo']:
    if command not in moves['commands']:fail(mode+' demo uses absent command')
    slots=moves['commands'][command];at=next_variant.get(command,0);slot=str(slots[at%len(slots)]);next_variant[command]=at+1;visited.add(slot);commands.add(command);seconds+=by[slot]['duration']
    for cue in audio['cues'].get(slot,[]):
     if cue.get('kind')=='stop':continue
     variants=cue.get('variants',[cue['clip']]);heard.add(variants[round_%len(variants)]);triggers+=1
  if len(heard)<2:fail(mode+' single-source/silent full tour')
  absent=sorted(set(moves['commands'])-commands)
  results.append(dict(mode=mode,clips=len(catalog),core_slots=len(core),fingers=len(fingers['tracks']),pcm=len(ids),own_skin_tracks=len(skin['tracks']),
   explicit_bind_fallbacks=len(fallback),
   max_original_sole_clearance=maximum,source_tour_rounds=100,source_tour_seconds=seconds,visited_core_slots=len(visited),heard_source_pcm=len(heard),tour_cues=triggers,
   commands_outside_tour=absent,keyboard_defaults_verified=moves.get('keyboard_defaults',{}).get('verified',False)))
  print({k:v for k,v in results[-1].items() if k not in ('source_tour_seconds','commands_outside_tour')})
 report=dict(schema_version=1,modes=results,game_launched=False,scope='Source data closure and tour coverage; native controller, host/rendering and aesthetics tested separately',hashes_computed=False)
 path=ROOT/'research/multiweapon/final-source-runtime-validation.json';path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
 print('Final six-mode source data closure passed')
if __name__=='__main__':main()
