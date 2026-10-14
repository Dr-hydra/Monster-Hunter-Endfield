"""Publish source-evidenced mode descriptors, core tours and event markers.

Source animation identities and native ActionNo enum identities are separate.
The selected stationary presentation contexts are explicit, and labels whose
native motion/action association is inferred carry that qualification.
"""
from pathlib import Path
import argparse,copy,json,math,shutil,struct
from mhw_control_mappings import correct_control_metadata
ROOT=Path(__file__).resolve().parents[1]
MODES={'sword_shield':('片手剑','01','one',10000,'single_sword'),
       'greatsword':('大剑','00','two',20000,'great_sword'),
       'lance':('长枪','06','lan',30000,'polearm'),
       'light_bowgun':('轻弩','13','lbg',40000,'handgun'),
       'bow':('弓','11','bow',50000,'arts_unit')}

# These are observed source poses, not enum-number arithmetic. The exact native
# control mapping is retained separately for review in each mode evidence file.
PLANS={
 'sword_shield':dict(commands=dict(slash=[101,102,103],thrust=[141],spirit=[118,119,122],guard=[47],backstep=[110],charge=[110],release=[112],special=[344,345,346,341,342],advance=[104],counter=[116],dodge=[9,11],normal_sheathe=[3]),
  labels={101:'劈斩',102:'横斩',103:'剑盾组合',141:'突刺',118:'盾击',119:'反盾击',122:'盾连击终结',47:'举盾防御',116:'防御斩',104:'突进斩',110:'后撤蓄力准备',112:'蓄力跃斩',344:'飞身跃入斩',345:'精准突击剑盾连击',346:'精准突击盾击',341:'精准突击刺入',342:'精准突击脱离'},
  demo=['slash','slash','slash','thrust','spirit','spirit','spirit','guard','counter','advance','backstep','charge','release','special','special','special','special','special','dodge','dodge','normal_sheathe'],hold=dict(charge=dict(frame=100,releases=[dict(seconds=0,slot=112)]),guard=dict(frame=20))),
 'greatsword':dict(commands=dict(slash=[128,106,107],thrust=[147],spirit=[155,156,157],charge=[144],release=[152,153,154],guard=[114],counter=[147],special=[146,189,190],dodge=[9,11],normal_sheathe=[3]),
  labels={128:'纵斩',106:'横扫',107:'上撩',147:'铁山靠',144:'举剑蓄力保持',114:'大剑防御',152:'蓄力斩一级',153:'蓄力斩二级',154:'蓄力斩三级',155:'强蓄力斩一级',156:'强蓄力斩二级',157:'强蓄力斩三级',146:'真蓄力斩一级',189:'真蓄力斩二级',190:'真蓄力斩三级'},
  demo=['slash','slash','slash','thrust','guard','counter']+['charge','release']*9+['spirit','spirit','spirit','special','special','special','dodge','dodge','normal_sheathe'],hold=dict(charge=dict(frame=25,releases=[dict(seconds=0,slot=152),dict(seconds=.65,slot=153),dict(seconds=1.25,slot=154)]),guard=dict(frame=25))),
 'lance':dict(commands=dict(thrust=[105,131,132],slash=[106,133,134],spirit=[107],guard=[128],counter=[121],charge=[138],release=[112],advance=[117],backstep=[14],special=[174],dodge=[12,13,14],normal_sheathe=[3]),
  labels={105:'中段突刺一',131:'中段突刺二',132:'中段突刺三',106:'上段突刺一',133:'上段突刺二',134:'上段突刺三',107:'横扫',128:'持盾防御',121:'蓄力反击突刺',138:'持枪冲锋',112:'冲锋终结突刺',174:'强终结双突刺',117:'持盾前进',14:'后撤步',12:'侧跳左',13:'侧跳右'},
  demo=['thrust','thrust','thrust','slash','slash','slash','spirit','guard','counter','backstep','advance','charge','release','special','dodge','dodge','dodge','normal_sheathe'],hold=dict(guard=dict(frame=15),charge=dict(frame=58,releases=[dict(seconds=0,slot=112)]))),
 'light_bowgun':dict(commands=dict(slash=[82],thrust=[92],spirit=[107],aim=[1],fire=[82,85,88],reload=[102],special=[110],dodge=[9,11],normal_sheathe=[3]),
  labels={1:'持弩瞄准',82:'射击低姿态',85:'射击中姿态',88:'射击高姿态',92:'后坐姿态',102:'装填',107:'连续装填展示',110:'特殊机械动作'},
  demo=['aim','fire','fire','fire','thrust','reload','spirit','dodge','dodge','aim','fire','special','normal_sheathe'],hold=dict(aim=dict(frame=0))),
 'bow':dict(commands=dict(slash=[125],thrust=[136],spirit=[136],aim=[1001],charge=[110],release=[125],fire=[125],special=[1000],backstep=[145],dodge=[145,148],reload=[164],normal_sheathe=[3]),
  labels={1001:'持弓瞄准（源拉弦姿态）',110:'取箭拉弦',125:'松弦射击',136:'刚射',1000:'龙之矢准备与释放',160:'龙之矢准备',161:'龙之矢释放',145:'滑步',148:'滑步收势',164:'取箭'},
  demo=['reload','aim','charge','release','charge','fire','spirit','dodge','dodge','charge','fire','special','normal_sheathe'],hold=dict(charge=dict(frame=86,releases=[dict(seconds=0,slot=125)]),aim=dict(frame=0))),
}

def load(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def dump(path,value):
    path.parent.mkdir(parents=True,exist_ok=True);temp=path.with_suffix(path.suffix+'.tmp')
    temp.write_text(json.dumps(value,ensure_ascii=False,separators=(',',':')),encoding='utf-8');temp.replace(path)

def make_bow_stages():
    folder=ROOT/'assets/modes/bow';preview=ROOT/'preview/modes/bow';catalog=load(folder/'catalog.json');known={r['slot']:r for r in catalog}
    ground=load(folder/'ground_tracks.json');grip=load(folder/'grip_tracks.json');weapons=load(folder/'source_weapon_tracks.json');fingers=load(folder/'finger_tracks.json')
    audio=load(folder/'audio/cues.json');projectiles=load(folder/'projectile_events.json') if (folder/'projectile_events.json').exists() else {'schema_version':1,'tracks':{}}
    def clip(slot):
        raw=(folder/known[slot]['file']).read_bytes();n,b,fps=struct.unpack_from('<III',raw,4)
        stride=b*4+3;values=struct.unpack_from('<%df'%(n*stride),raw,16)
        return [list(values[i*stride:(i+1)*stride]) for i in range(n)]
    def finger_values(slot):
        row=fingers['tracks'].get(str(slot))
        if not row:return None
        if 'file' not in row:return row['frames']
        raw=(folder/row['file']).read_bytes();n,b,fps=struct.unpack_from('<III',raw,4);vals=struct.unpack_from('<%df'%(n*b*4),raw,16)
        return [list(vals[i*b*4:(i+1)*b*4]) for i in range(n)]
    for output_slot,phases,label in [(51000,[(50160,0,208),(50161,0,100)],'Original dragon-piercer preparation/release source stages'),(51001,[(50125,0,0)]*61,'Frozen original full-draw release-entry pose; explicit local aim hold')]:
        samples=[];gp=[];gf=[];wp=[];fp=[];pframes=[];qframes=[];phase_map=[];all_cues=[];events=[];last_xz=None
        for source_slot,start,end in phases:
            rows=clip(source_slot);p=load(preview/f'slot_{source_slot:05d}.json');begin=len(samples);first=rows[start][84:87];shift=[0.,0.]
            if last_xz is not None:shift=[last_xz[0]-first[0],last_xz[1]-first[2]]
            gripfile=folder/grip['tracks'][str(source_slot)];graw=gripfile.read_bytes();gn=struct.unpack_from('<I',graw,4)[0];gvals=struct.unpack_from('<%df'%(gn*4),graw,16);frows=finger_values(source_slot)
            for frame in range(start,end+1):
                row=rows[frame][:];row[84]+=shift[0];row[86]+=shift[1];samples.append(row)
                gp.append(ground['tracks'][str(source_slot)]['clearance'][frame]);gf.extend(gvals[frame*4:frame*4+4]);wp.append(weapons['tracks'][str(source_slot)]['frames'][frame]);fp.append(frows[frame] if frows else [])
                pframes.append(p['positions'][frame]);qframes.append(p['rotations_xyzw'][frame]);phase_map.append(dict(source_slot=source_slot-50000,source_frame=frame))
            last_xz=[samples[-1][84],samples[-1][86]]
            if output_slot==51000:
                for cue in audio['cues'].get(str(source_slot),[]):
                    source_frame=cue['time']*60
                    if start<=source_frame<=end:all_cues.append(dict(cue,time=(begin+source_frame-start)/60))
                for event in projectiles['tracks'].get(str(source_slot),[]):
                    sf=event['time']*60
                    if start<=sf<=end:events.append(dict(event,time=(begin+sf-start)/60))
        n=len(samples);fname=f'clip_{output_slot:05d}.lsm';(folder/fname).write_bytes(struct.pack('<4sIII',b'LSM1',n,21,60)+struct.pack('<%df'%(n*87),*(v for row in samples for v in row)))
        gname=f'grip_{output_slot:05d}.lsg';(folder/gname).write_bytes(struct.pack('<4sIII',b'LSG1',n,60,4)+struct.pack('<%df'%len(gf),*gf))
        ground['tracks'][str(output_slot)]=dict(fps=60,clearance=gp);grip['tracks'][str(output_slot)]=gname;weapons['tracks'][str(output_slot)]=dict(fps=60,frames=wp)
        if fp and fp[0]:
            fcount=len(fp[0])//4;file=f'finger_{output_slot:05d}.lsf';vals=[v for row in fp for v in row]
            (folder/file).write_bytes(struct.pack('<4sIII',b'LSF1',n,fcount,60)+struct.pack('<%df'%len(vals),*vals));fingers['tracks'][str(output_slot)]=dict(file=file,frames=n,fps=60)
        if all_cues:audio['cues'][str(output_slot)]=all_cues
        if events:projectiles['tracks'][str(output_slot)]=events
        row=dict(slot=output_slot,file=fname,frames=n,duration=(n-1)/60,source=label,source_slot=160 if output_slot==51000 else 125,source_frame_start=0,source_frame_end=208 if output_slot==51000 else 0,source_phases=phases,local_control='source phase composition' if output_slot==51000 else 'source pose freeze',rotations='original samples unchanged')
        known[output_slot]=row
        dump(preview/f'slot_{output_slot:05d}.json',dict(bones=p['bones'],positions=pframes,rotations_xyzw=qframes,up_axis='y',fps=60,source=label,phase_map=phase_map))
    dump(folder/'catalog.json',sorted(known.values(),key=lambda x:x['slot']))
    for name,d in [('ground_tracks',ground),('grip_tracks',grip),('source_weapon_tracks',weapons),('finger_tracks',fingers)]:dump(folder/f'{name}.json',d)
    dump(folder/'audio/cues.json',audio);dump(folder/'projectile_events.json',projectiles)

def sanitize_audio(mode):
    folder=ROOT/f'assets/modes/{mode}';audio=folder/'audio/cues.json'
    if not audio.exists():return
    catalog=load(folder/'catalog.json');by_slot={str(r['slot']):r for r in catalog}
    d=load(audio);report=ROOT/f'research/multiweapon/{mode}/unplayable-source-cues.json';inactive=load(report)['cues'] if report.exists() else [];valid={};decoded=load(ROOT/f'research/multiweapon/{mode}/audio-decoded.json');silent={r['id'] for r in decoded if r.get('silence')};clipids={r['id'] for r in d['clips']}-silent
    for slot,rows in d['cues'].items():
        row=by_slot.get(slot)
        for cue in rows:
            reason=None
            if not row:reason='source slot not in effective runtime catalog'
            elif str(row.get('source','')).startswith('Legacy') and cue.get('confidence')!='user-media-table':reason='legacy source version differs from native source TIML'
            elif not 0<=cue['time']<=row['duration']+.000001:reason='native TIML event outside selected presentation source frame range'
            variants=[v for v in cue.get('variants',[cue['clip']]) if v in clipids]
            if not variants and cue.get('kind')!='stop':reason='only native silence/invalid media descendants available'
            if reason:
                item=dict(slot=slot,cue=cue,reason=reason)
                if item not in inactive:inactive.append(item)
                continue
            item=dict(cue,variants=variants,clip=variants[0] if variants else cue['clip']);valid.setdefault(slot,[]).append(item)
    d['cues']={slot:sorted(rows,key=lambda cue:cue['time']) for slot,rows in valid.items()};d['clips']=[r for r in d['clips'] if r['id'] in clipids];d['inactive_runtime_cues']=len(inactive)
    dump(audio,d);dump(ROOT/f'research/multiweapon/{mode}/unplayable-source-cues.json',dict(schema_version=1,cues=inactive,source_timeline_preserved=True))

def finger_binary(folder):
    p=folder/'finger_tracks.json'
    if not p.exists():return
    d=load(p)
    if 'functions' not in d:
        old_bones=d['bones'];selected=[i for i,b in enumerate(old_bones) if b['function']!=64]
        if len(selected)!=len(old_bones):
            for row in d['tracks'].values():
                file=folder/row['file'];raw=file.read_bytes();n,b,fps=struct.unpack_from('<III',raw,4);values=struct.unpack_from('<%df'%(n*b*4),raw,16);new=[values[frame*b*4+i*4+j] for frame in range(n) for i in selected for j in range(4)];file.write_bytes(struct.pack('<4sIII',b'LSF1',n,len(selected),fps)+struct.pack('<%df'%len(new),*new))
            d['bones']=[old_bones[i] for i in selected]
        for bone in d['bones']:
            if bone['function'] in (40,57):bone['name']='left_palm_cup' if bone['function']==40 else 'right_palm_cup'
            bone['semantic_evidence']='Original MOD3 hierarchy;40/57 are palm cup,64 hip child omitted'
        dump(p,d)
        return
    functions=[f for f in d['functions'] if f!=64];selected=[d['functions'].index(f) for f in functions];bones=[];tracks={}
    # Functions use the original MOD3 hand roots/palm landmarks; ambiguous
    # intermediate helper joints retain their function, never a made-up target.
    starts={'left':dict(thumb=31,index=34,middle=37,ring=41,pinky=44),
            'right':dict(thumb=48,index=51,middle=54,ring=58,pinky=61)}
    names={}
    for hand,groups in starts.items():
        for digit,start in groups.items():
            for segment in range(3):names[start+segment]=f'{hand}_{digit}{segment}'
    for f in functions:
        rest=d['rest'][str(f)];name=names.get(f,f'source_function_{f}')
        bones.append(dict(name='left_palm_cup' if f==40 else 'right_palm_cup' if f==57 else name,function=f,hand='left' if f<48 else 'right',rest_position=rest['position'],rest_rotation_xyzw=rest['rotation_xyzw'],semantic_evidence='MOD3 hand hierarchy:40/57 palm-cup parents of ring+pinky;64 hip child excluded'))
    for slot,row in d['tracks'].items():
        frames=row['frames'];file=f'finger_{int(slot):05d}.lsf';values=[frame[i*4+j] for frame in frames for i in selected for j in range(4)]
        (folder/file).write_bytes(struct.pack('<4sIII',b'LSF1',len(frames),len(functions),row['fps'])+struct.pack('<%df'%len(values),*values))
        tracks[slot]=dict(file=file,frames=len(frames),fps=row['fps'])
    dump(folder/'finger_tracks.json',dict(schema_version=1,bones=bones,tracks=tracks,coordinate='world rest quaternion delta xyzw; original source keys plus explicit stationary source context'))

def make_longsword_stage():
    folder=ROOT/'assets/modes/longsword';root=ROOT/'assets';finger_binary(folder)
    catalog=load(folder/'catalog.json');rootrow=next(r for r in load(root/'catalog.json') if r['slot']==1000);existing={r['slot']:r for r in catalog};existing[1000]=dict(rootrow,source_phases=load(ROOT/'research/mhw-original/helmbreaker-composition.json')['phases'])
    shutil.copyfile(root/rootrow['file'],folder/rootrow['file']);phase=load(ROOT/'research/mhw-original/helmbreaker-composition.json')['stage_map']
    f=load(folder/'finger_tracks.json');frames=[];source_frames={}
    for slot in (170,171,172,173):
        row=f['tracks'][str(slot)];raw=(folder/row['file']).read_bytes();n,b,fps=struct.unpack_from('<III',raw,4);values=struct.unpack_from('<%df'%(n*b*4),raw,16);source_frames[slot]=[list(values[i*b*4:(i+1)*b*4]) for i in range(n)]
    for stage in phase:
        sample=source_frames[stage['slot']][stage['frame']][:]
        if 'blend' in stage:
            source=source_frames[172][-1];amount=stage['blend'];result=[]
            for j in range(0,len(sample),4):
                a=source[j:j+4];b=sample[j:j+4];sign=1 if sum(x*y for x,y in zip(a,b))>=0 else -1;q=[(1-amount)*x+amount*sign*y for x,y in zip(a,b)];length=math.sqrt(sum(x*x for x in q));result.extend(x/length for x in q)
            sample=result
        frames.append(sample)
    n=len(frames);b=len(f['bones']);fname='finger_01000.lsf';(folder/fname).write_bytes(struct.pack('<4sIII',b'LSF1',n,b,60)+struct.pack('<%df'%(n*b*4),*(x for row in frames for x in row)));f['tracks']['1000']=dict(file=fname,frames=n,fps=60,source='Exact original stage_map; original source finger poses with same declared transition blend');dump(folder/'finger_tracks.json',f)
    for name in ('ground_tracks','grip_tracks'):
        doc=load(folder/f'{name}.json');old=load(root/f'{name}.json');doc['tracks']['1000']=old['tracks']['1000'];dump(folder/f'{name}.json',doc)
        if name=='grip_tracks':shutil.copyfile(root/doc['tracks']['1000'],folder/doc['tracks']['1000'])
    w=load(folder/'source_weapon_tracks.json');w['tracks']['1000']=dict(fps=60,frames=[w['tracks'][str(s['slot'])]['frames'][s['frame']] for s in phase],source='Original stage_map; no invented blade rotations');dump(folder/'source_weapon_tracks.json',w)
    dump(folder/'catalog.json',sorted(existing.values(),key=lambda r:r['slot']))
    preview=ROOT/'preview/slot_1000.json'
    if preview.exists():shutil.copyfile(preview,ROOT/'preview/modes/longsword/slot_01000.json')

def projectile_tracks(folder,mode,offset):
    path=ROOT/f'research/multiweapon/{mode}/timeline-decoded.json'
    if not path.exists():return
    doc=load(path)['actions'];tracks={}
    # Corroborated by source recoil/release poses, gun muzzle/bow release VFX,
    # and original weapon Wwise request co-occurrence. Native attack/flight is
    # intentionally not invoked; marker is a local presentation dispatch.
    slots={'bow':(125,136,161,162),'light_bowgun':(82,85,88,91,92)}.get(mode,())
    for slot in slots:
        td=doc.get(str(slot))
        if not td:continue
        events=[]
        for t in td['types']:
            if t['name']!='PlMotionCommon':continue
            for f in t['transforms']:
                if f['name']!='mTrigger':continue
                for k in f['keys']:
                    value=int(k['value'])
                    if value&1:
                        events.append(dict(time=k['frame']/60,kind='arrow' if mode=='bow' else 'bullet',source_event=dict(type=t['name'],property=f['name'],value=value,source_slot=slot,source_frame=k['frame']),
                          evidence='Original release/recoil pose + TIML mTrigger bit0 and same-phase source muzzle/bow EffectParameter/Wwise markers',confidence='resource-phase-correspondence',native_bit_handler_reversed=False))
        if events:tracks[str(offset+slot)]=events
    dump(folder/'projectile_events.json',dict(schema_version=1,source='Original source event markers; local cosmetic projectile; no actual MHW or Endfield attack',tracks=tracks))

def merge_longsword_audio():
    out=ROOT/'assets/modes/longsword/audio';fresh=load(out/'cues.json');old=load(ROOT/'assets/audio/cues.json');clips={x['id']:x for x in fresh['clips']}
    remap={};new_media={int(x['id'].split('_')[-1]):x['id'] for x in fresh['clips'] if x['id'].split('_')[-1].isdigit()}
    for clip in old['clips']:
        ident=clip['id'];mapped=new_media.get(int(ident)) if ident.isdigit() else None
        if mapped is None:
            mapped='longsword_'+ident;dst=out/clip['file'];shutil.copyfile(ROOT/'assets/audio'/clip['file'],dst)
            clips[mapped]=dict(clip,id=mapped,source=clip.get('source','Previously confirmed MHW original / locally extracted Endfield ordinary-swing PCM'),mapping_evidence='Existing user-identified long-sword media table retained')
        remap[ident]=mapped
    for slot,rows in old['cues'].items():
        if slot=='127':slot='140'
        if slot=='128':continue
        existing=fresh['cues'].setdefault(slot,[])
        for row in rows:
            if row['clip'].startswith('endfield_swing') and existing:continue
            clip=remap[row['clip']]
            if any((x['clip']==clip or clip in x.get('variants',[])) and abs(x['time']-row['time'])<.10 for x in existing):continue
            item=dict(row,clip=clip,group='longsword_special',kind='oneshot',condition='counter' if slot=='140' else 'always',
                confidence='user-media-table',timing_evidence='Previous explicit showcase cue; native EPV timing not yet associated')
            existing.append(item)
        existing.sort(key=lambda x:x['time'])
    fresh['clips']=list(clips.values());fresh['legacy_special_timing']='Known special media retained; successful counter cue requires local counter or explicit simulated demo event'
    dump(out/'cues.json',fresh)

def main():
    p=argparse.ArgumentParser();p.add_argument('--skip-audio',action='store_true');args=p.parse_args();index=[];coverage=[]
    for mode,(label,num,cat,offset,category) in MODES.items():
        if mode=='bow':
            projectile_tracks(ROOT/'assets/modes/bow',mode,offset);make_bow_stages()
        folder=ROOT/f'assets/modes/{mode}';catalog=load(folder/'catalog.json');known={r['slot']:r for r in catalog};plan=copy.deepcopy(PLANS[mode]);commands={k:[offset+s for s in v] for k,v in plan['commands'].items()}
        for command,slots in commands.items():
            if any(s not in known for s in slots):raise ValueError(f'{mode} {command} missing source motion {slots}')
        holds={}
        for key,row in plan['hold'].items():
            row=copy.deepcopy(row)
            for release in row.get('releases',[]):release['slot']+=offset
            holds[key]=row
        labels={str(offset+s):name for s,name in plan['labels'].items()};labels.update({str(offset+3):'收纳',str(offset+9):'回避',str(offset+11):'回避'})
        moves=dict(schema_version=2,mode=mode,idle=offset+1,source='Original MHW/Iceborne source LMT with original source TIML',
             commands=commands,labels=labels,hold_commands=holds,charge_levels=[.65,1.25,1.85],sheathe_hold_frame=0,
             inputs=dict(slash='charge' if mode in ('bow','greatsword') else 'fire' if mode=='light_bowgun' else 'thrust' if mode=='lance' else 'slash',thrust='spirit' if mode=='bow' else 'slash' if mode=='lance' else 'thrust',spirit='aim' if mode in ('bow','light_bowgun') else 'guard',dodge='dodge',sheathe='normal_sheathe'),
             chords={'3':'special','5':'reload' if mode=='light_bowgun' else 'counter','6':'special','12':'special' if mode=='bow' else 'backstep'},
             demo=plan['demo'],limitation='Showcase independent controller; source motion/action class association uses source pose review plus FSM semantics; native gameplay controller is not replayed',
             source_pose_context='stationary source idle when original layered component holes exist',native_controller_replayed=False)
        moves['chords']={mask:command for mask,command in moves['chords'].items() if command in commands}
        moves['inputs']={key:command for key,command in moves['inputs'].items() if command in commands}
        correct_control_metadata(mode, moves, offset)
        dump(folder/'moves.json',moves)
        core=sorted({s for rows in commands.values() for s in rows}|{offset+1,offset+2,offset+3})
        hit={}
        timelines=load(ROOT/f'research/multiweapon/{mode}/timeline-decoded.json')['actions'];collisions=load(ROOT/f'research/multiweapon/{mode}/collision-evidence.json')['actions']
        for slot in core:
            td=timelines.get(str(slot-offset))
            if not td:continue
            windows=[[r['start_frame']/60,min(known[slot]['duration'],r['end_frame']/60),1.] for r in collisions.get(str(slot-offset),[]) if r['start_frame']/60<known[slot]['duration']]
            if windows:hit[str(slot)]=windows
        dump(folder/'hit_windows.json',dict(schema_version=1,evidence='Original TIML CollisionTimelineObject active intervals reused for independent local practice; not source damage values',windows=hit))
        if mode!='bow':projectile_tracks(folder,mode,offset)
        finger_binary(folder)
        entries=[]
        for command,slots in commands.items():
            for slot in slots:
                row=known[slot];entries.append(dict(command=command,slot=slot,source_slot=row['source_slot'],source_frames=[row.get('source_frame_start',0),row.get('source_frame_end',row['frames']-1)],label=labels.get(str(slot),command),source=row['source'],
                  evidence_level='original-COL-author-name-TIML-node-and-source-pose-review' if str(row['source_slot']) in collisions else 'original-resource-and-source-pose-review',collision_events=collisions.get(str(row['source_slot']),[]),author_move_names=list(dict.fromkeys(x['author_name'] for x in collisions.get(str(row['source_slot']),[]) if x['author_name'])),native_action_mapping_confirmed=False,context=row.get('base_context'),native_control_limit='Author COL names establish attack semantics; native C++ controller linkage is not replayed'))
        dump(folder/'core_moves.json',dict(schema_version=1,mode=mode,entries=entries,source_control_evidence=f'research/multiweapon/{mode}/fsm-references.json',source_blend_evidence=f'research/multiweapon/{mode}/mbd-decoded.json',runtime_core_slots=core))
        index.append(dict(id=mode,label=label,data_root=f'assets/modes/{mode}',audio_root=f'assets/modes/{mode}/audio',offset=offset,weapon_category=category,dominant_hand='left' if mode=='bow' else 'right',demo=plan['demo'],commands=list(commands),status='offline-source-baked',
          detail='原版源动作与事件时序；独立展示控制，原生动作关联和武器安装仍需校准',source=f'hm/wp/wp{num}/mot/w{num}_00/w{num}_00.lmt',
          default_bindings=dict(slash=1,thrust=2,spirit=17,dodge=32,sheathe=69,reload=82,source='MHW keyboard vocabulary; defaults awaiting official/local key table verification',verified=False)))
        coverage.append(dict(mode=mode,all_clips=len(catalog),core_slots=len(core),commands=len(commands),demo_stages=len(plan['demo']),source_physical_verified=False,native_action_mapping_confirmed=False))
    make_longsword_stage()
    # This checked-in descriptor is the current COL-reviewed source mapping,
    # not the old consecutive-slot assumption inherited from the MOD prototype.
    dump(ROOT/'assets/modes/longsword/moves.json',load(ROOT/'tools/data/mhw_longsword_moves.json'))
    if not args.skip_audio:merge_longsword_audio()
    for mode in list(MODES)+['longsword']:sanitize_audio(mode)
    existing=ROOT/'assets/modes/index.json'
    ls=dict(id='longsword',label='太刀',data_root='assets/modes/longsword',audio_root='assets/modes/longsword/audio',finger_root='assets/modes/longsword',offset=0,weapon_category='single_sword',dominant_hand='right',status='original-source-baseline',detail='原版太刀与明确本地登龙下降控制',
       demo=['slash','slash','slash','spirit','spirit','spirit','spirit','spirit','fade','foresight','sheathe','iai','sheathe','iai_spirit','helm','normal_sheathe'])
    dump(existing,dict(schema_version=1,modes=[ls]+index))
    dump(ROOT/'research/multiweapon/core-coverage.json',dict(schema_version=1,modes=coverage,game_launched=False))
    print('Published mode index, source movement descriptors, LSF1 and original local hit/projectile phase markers;',coverage)

if __name__=='__main__':main()
