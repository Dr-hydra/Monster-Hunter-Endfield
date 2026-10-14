"""Finish source-derived runtime metadata without rebaking original motion keys.

Composite events follow the published source frame map, including original loop
release tails. Empty native guard/aim timelines remain silent. Keyboard presets
use the primary-source mouse5/mouse3 physical presets; weapon derivations carry
their separate Hunter's Notes/resource evidence.
"""
from pathlib import Path
import importlib.util, json, math
from mhw_keyboard_presets import binding_metadata
from mhw_control_mappings import correct_control_metadata
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('publish',ROOT/'tools/mhw_multi_publish.py')
PUB=importlib.util.module_from_spec(spec);spec.loader.exec_module(PUB)
MODES=['longsword','sword_shield','greatsword','lance','light_bowgun','bow']
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def dump(p,v):PUB.dump(p,v)

def map_events(audio,output,stage_map,fps=60):
    cues=[];previous_slot=None;live={}
    # Map every native trigger once, not once for every repeated freeze/blend
    # sample. Fractional source key times preserve sub-frame native timing.
    sources=set(row['slot'] for row in stage_map)
    for slot in sources:
        for cue in audio['cues'].get(str(slot),[]):
            frame=float(cue.get('source_frame',cue['time']*fps));integer=math.floor(frame+1e-6)
            matches=[i for i,row in enumerate(stage_map) if row['slot']==slot and row['frame']==integer]
            if not matches:continue
            copied=dict(cue,time=(matches[0]+frame-integer)/fps,
                composed_source_slot=slot,composed_source_frame=frame,
                composition_evidence='Exact published source slot/frame map; original trigger frame unchanged')
            cues.append(copied)
    # A source loop belongs to its original animation phase. End it at the
    # published phase transition using its recorded release tail rather than
    # letting it run across unrelated follow-up phases.
    phase_changes=[i for i in range(1,len(stage_map)) if stage_map[i]['slot']!=stage_map[i-1]['slot']]
    for i in phase_changes:
        ended=stage_map[i-1]['slot'];ending=i/fps
        groups={c['group']:c for c in cues if c.get('loop') and c['composed_source_slot']==ended and c['time']<ending}
        for group,cue in groups.items():
            cues.append(dict(cue,time=ending,kind='stop',loop=False,
                composition_evidence='Exact published source phase boundary; original TIML release tail retained'))
    audio['cues'][str(output)]=sorted(cues,key=lambda c:c['time'])

def hit_windows(mode,folder,catalog):
    collision=load(ROOT/f'research/multiweapon/{mode}/collision-evidence.json')['actions']
    windows={}
    for row in catalog:
        if 'source_slot' not in row:continue
        slot=row['slot'];source=row['source_slot'];duration=row['duration']
        hits=[]
        for item in collision.get(str(source),[]):
            start=item['start_frame']/60;end=min(duration,item['end_frame']/60)
            if 0<=start<end:hits.append([start,end,1.])
        if hits:
            # Original collision lanes may overlap (blade/shield or several
            # native nodes). Local practice uses their interval union once.
            merged=[]
            for start,end,multiplier in sorted(hits):
                if merged and start<=merged[-1][1]+1e-7:merged[-1][1]=max(merged[-1][1],end)
                else:merged.append([start,end,multiplier])
            windows[str(slot)]=merged
    if mode=='longsword':
        phase=load(ROOT/'research/mhw-original/helmbreaker-composition.json')['stage_map']
        active=[]
        for i,frame in enumerate(phase):
            if any(x['start_frame']<=frame['frame']<x['end_frame'] for x in collision.get(str(frame['slot']),[])):active.append(i)
        if active:
            groups=[];start=last=active[0]
            for i in active[1:]:
                if i>last+1:groups.append([start/60,(last+1)/60,1.]);start=i
                last=i
            groups.append([start/60,min((last+1)/60,(len(phase)-1)/60),1.]);windows['1000']=groups
    dump(folder/'hit_windows.json',dict(schema_version=1,windows=windows,
        evidence='Original COL author nodes joined to TIML CollisionTimelineObject active intervals; overlapping native lanes become an interval union; exclusive source ends clamp to last published frame; composite phase maps unchanged; local practice only, no original damage'))

def main():
    PUB.merge_longsword_audio()
    audio=load(ROOT/'assets/modes/longsword/audio/cues.json')
    phase=load(ROOT/'research/mhw-original/helmbreaker-composition.json')['stage_map']
    map_events(audio,1000,phase);dump(ROOT/'assets/modes/longsword/audio/cues.json',audio)
    audio=load(ROOT/'assets/modes/bow/audio/cues.json')
    phase=[dict(slot=s,frame=f) for s,start,end in [(50160,0,208),(50161,0,100)] for f in range(start,end+1)]
    map_events(audio,51000,phase);dump(ROOT/'assets/modes/bow/audio/cues.json',audio)
    index=load(ROOT/'assets/modes/index.json');by_mode={x['id']:x for x in index['modes']}
    coverage=[]
    for mode in MODES:
        folder=ROOT/f'assets/modes/{mode}';moves=load(folder/'moves.json')
        defaults=binding_metadata(mode);keys=defaults['keys']
        moves['default_keys']=keys
        moves['keyboard_defaults']={name:value for name,value in defaults.items() if name!='keys'}
        if mode=='greatsword':moves['inputs']['slash']='charge'
        if mode!='longsword':correct_control_metadata(mode,moves,by_mode[mode]['offset'])
        dump(folder/'moves.json',moves)
        by_mode[mode]['default_bindings']=defaults
        by_mode[mode]['demo']=moves['demo'];by_mode[mode]['commands']=list(moves['commands'])
        PUB.sanitize_audio(mode)
        audio=load(folder/'audio/cues.json');catalog=load(folder/'catalog.json')
        markers=load(ROOT/f'research/multiweapon/{mode}/audio-source-events.json')['markers']
        for slot,cues in audio['cues'].items():
            for cue in cues:
                if cue.get('confidence')!='source_timing_and_event_descendant':continue
                source_slot=str(cue.get('composed_source_slot',slot))
                matches=[m for m in markers.get(source_slot,[]) if m['request']==cue['source_request']
                    and abs(m['frame']-cue['source_frame'])<1e-6 and m['timeline']==cue['source_timeline']]
                if len(matches)!=1:raise ValueError('Native loop release marker is not uniquely resolved: '+mode+'/'+slot)
                cue['fade_in']=0.;cue['fade_out']=matches[0]['release_ms']/1000
        dump(folder/'audio/cues.json',audio)
        hit_windows(mode,folder,catalog)
        coverage.append(dict(mode=mode,clips=len(audio['clips']),cue_slots=len(audio['cues']),
            cue_count=sum(len(x) for x in audio['cues'].values()),
            command_audio=[dict(command=command,slots=slots,cues=sum(len(audio['cues'].get(str(s),[])) for s in slots),
                source_silent=all(not audio['cues'].get(str(s)) for s in slots)) for command,slots in moves['commands'].items()],
            demo_stages=len(moves['demo']),keyboard_defaults_verified=True))
    dump(ROOT/'assets/modes/index.json',index)
    dump(ROOT/'research/multiweapon/final-runtime-source-coverage.json',dict(schema_version=1,modes=coverage,game_launched=False,hashes_computed=False))
    print(json.dumps(dict(modes=len(coverage),pcm_clips=sum(x['clips'] for x in coverage),
        cues=sum(x['cue_count'] for x in coverage),composite_cues={m:next(x for x in coverage if x['mode']==m)['cue_count'] for m in ('longsword','bow')})))

if __name__=='__main__':main()
