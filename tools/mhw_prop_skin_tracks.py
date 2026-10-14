"""Original TIML weapon-motion phases -> own normalized prop skin matrices.

No target body palette is substituted. The exact source animation keys remain
traceable; native controller interpolation/unknown carry state is qualified.
"""
from pathlib import Path
import json,math,struct
ROOT=Path(__file__).resolve().parents[1]
MODES={'longsword':('03',0),'sword_shield':('01',10000),'greatsword':('00',20000),'lance':('06',30000),'light_bowgun':('13',40000),'bow':('11',50000)}
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def dump(p,v):p.write_text(json.dumps(v,ensure_ascii=False,separators=(',',':')),encoding='utf-8')
def identity():return [1.,0,0,0,0,1.,0,0,0,0,1.,0,0,0,0,1.]
def multiply(a,b):return [sum(a[r+k*4]*b[k+c*4] for k in range(4)) for c in range(4) for r in range(4)]
def trs(row):
 x,y,z,w=row['rotation_xyzw'];p=row['position'];s=row.get('scale',[1,1,1]);n=math.sqrt(x*x+y*y+z*z+w*w);x/=n;y/=n;z/=n;w/=n
 return [(1-2*(y*y+z*z))*s[0],2*(x*y+z*w)*s[0],2*(x*z-y*w)*s[0],0,
  2*(x*y-z*w)*s[1],(1-2*(x*x+z*z))*s[1],2*(y*z+x*w)*s[1],0,
  2*(x*z+y*w)*s[2],2*(y*z-x*w)*s[2],(1-2*(x*x+y*y))*s[2],0,*p,1]
def phases(timeline):
 result=[];interpolation=[]
 for typ in timeline.get('types',[]):
  if typ['name']!='PlMotionCommon':continue
  for prop in typ['transforms']:
   if prop['name']=='mWeaponMotionNo':result.extend(prop['keys'])
   if prop['name']=='mWeaponMotionInterpolationTime':interpolation.extend(prop['keys'])
 return sorted(result,key=lambda k:k['frame']),interpolation
def deformation(prop,sample):
 bones=prop['rig_bones'];by={b['name']:i for i,b in enumerate(bones)};worlds={}
 def world(i):
  if i in worlds:return worlds[i]
  bone=bones[i];function=str(int(bone['name'].split('_')[-1]));basis=trs(sample[function]) if function in sample else identity()
  local=multiply(trs(bone),basis);parent=bone['parent'];worlds[i]=multiply(world(by[parent]),local) if parent else local
  return worlds[i]
 return [multiply(world(i),prop['bindposes'][i]) for i in range(len(bones))]
def own_sample(track,frame):
 rows=track['frames'];loop=track.get('loop_frame',-1)
 if frame>=len(rows) and loop>=0:frame=loop+(frame-loop)%(len(rows)-loop)
 return rows[min(frame,len(rows)-1)]

def main():
 summary=[]
 for mode,(number,offset) in MODES.items():
  folder=ROOT/f'assets/modes/{mode}';props=load(folder/'props/index.json')['props'];main=next(p for p in props if p.get('role','main')=='main')
  prop=load(folder/main['file']);weapons=load(folder/'weapon_rig_tracks.json')['tracks'];timeline=load(ROOT/f'research/multiweapon/{mode}/timeline-decoded.json')['actions']
  candidates=sorted({key.split(':')[0] for key in weapons if key.startswith('wg'+number+'_')})
  bank='wg'+number+'_00' if 'wg'+number+'_00' in candidates else candidates[0] if len(candidates)==1 else None
  if bank is None:raise ValueError('Primary non-shield own bank is ambiguous: '+str(candidates))
  catalog=load(folder/'catalog.json');published={};unresolved=[];bonecount=len(prop['rig_bones']);out=folder/'skin';out.mkdir(exist_ok=True)
  for row in catalog:
   source=row.get('source_slot');td=timeline.get(str(source))
   if not td:continue
   keys,interp=phases(td)
   if not keys:continue
   matched={str(int(key['value'])):weapons.get(f'{bank}:{int(key["value"])}') for key in keys if int(key['value'])>=0}
   missing=[key for key,track in matched.items() if track is None or track.get('needs_parent')]
   if missing:
    unresolved.append(dict(slot=row['slot'],reason='Primary own bank state absent/NeedsParent; no replacement invented',states=missing));continue
   frames=[];selections=[]
   for f in range(row['frames']):
    original=row.get('source_frame_start',0)+f;active=[k for k in keys if k['frame']<=original]
    if not active or int(active[-1]['value'])<0:
     matrices=deformation(prop,{});selection=dict(own_slot=None,reason='Explicit local rest context; native carry state undefined')
    else:
     key=active[-1];own=int(key['value']);at=max(0,int(original-key['frame']));sample=own_sample(matched[str(own)],at)
     matrices=deformation(prop,sample);selection=dict(own_slot=own,own_frame=at,trigger_frame=key['frame'])
    if any(not math.isfinite(x) for m in matrices for x in m):raise ValueError('Nonfinite original own palette')
    frames.extend(x for matrix in matrices for x in matrix);selections.append(selection)
   file=f'skin/skin_{row["slot"]:05d}.lsk';(folder/file).write_bytes(struct.pack('<4sIII',b'LSK1',row['frames'],bonecount,60)+struct.pack('<%df'%len(frames),*frames))
   published[str(row['slot'])]=dict(file=file,prop_id=prop['id'],frames=row['frames'],fps=60,bone_count=bonecount,
    source_slot=source,source_trigger_keys=keys,source_interpolation_keys=interp,
    own_bank=bank,own_states=sorted(matched),
    frame_map=selections,source_keys_unchanged=True,
    limitation='Exact source own-animation samples with original loop/clamp; native transition interpolation and negative carry state are not replayed')
  if mode=='bow':
   for output,parts in [(51000,[(50160,0,208),(50161,0,100)]),(51001,[(50125,0,0)]*61)]:
    rows=[];maps=[]
    for source,start,end in parts:
     record=published[str(source)];raw=(folder/record['file']).read_bytes();count,bones,fps=struct.unpack_from('<III',raw,4);stride=bones*64
     rows.append(raw[16+start*stride:16+(end+1)*stride]);maps.extend(record['frame_map'][start:end+1])
    n=len(maps);file=f'skin/skin_{output:05d}.lsk';(folder/file).write_bytes(struct.pack('<4sIII',b'LSK1',n,bonecount,60)+b''.join(rows))
    published[str(output)]=dict(file=file,prop_id=prop['id'],frames=n,fps=60,bone_count=bonecount,
      source_phases=parts,frame_map=maps,source_keys_unchanged=True,limitation='Same explicit body composite/frozen source pose map, no additional own deformation')
  manifest=dict(schema_version=1,coordinate='Column-major Unity Y-up root-normalized own palette; animatedModel * originalInverseBind',
    props={prop['id']:dict(file=main['file'],bone_names=[b['name'] for b in prop['rig_bones']],bindposes=prop['bindposes'],
      skin_bone_indices=prop['skin_bone_indices'],skin_weights=prop['skin_weights'])},tracks=published,unresolved=unresolved,
    source_evidence='Original body TIML PlMotionCommon.mWeaponMotionNo key -> original primary non-shield bank slot (_00 or sole existing bank) -> exact own MOD3 bind/pose palette',
    absence_policy='Slots without original mWeaponMotionNo have no explicit own-animation trigger; module displays normalized original bind pose. Native retained/carry animation state is unknown, not certified static.',
    mounting_selection_native_verified=False,game_launched=False)
  dump(folder/'source_skin_tracks.json',manifest);summary.append(dict(mode=mode,tracks=len(published),unresolved=len(unresolved),bones=bonecount))
  print(summary[-1])
 dump(ROOT/'research/multiweapon/source-own-skin-audit.json',dict(schema_version=1,modes=summary,hashes_computed=False,game_launched=False))
 # GS charge bodies have no original own-animation trigger. Preserve the
 # explicit bind fallback evidence instead of manufacturing an LSK animation.
 from mhw_verify_greatsword_bind_fallback import main as verify_gs_fallback
 verify_gs_fallback()
if __name__=='__main__':main()
