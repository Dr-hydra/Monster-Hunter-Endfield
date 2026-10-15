"""Publish sampled original actions plus an explicitly cosmetic Helmbreaker.
Does not overwrite hand rotations with a blade direction, replay MHW damage,
or claim that the local descent is the native FSM flight implementation.
"""
from pathlib import Path
import argparse,json,struct,math,shutil,tempfile
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
STAGE=ROOT/'research/mhw-original/baked'
ORDER=['hips','spine','chest','neck','head','left_shoulder','left_upper_arm','left_lower_arm','left_hand',
       'right_shoulder','right_upper_arm','right_lower_arm','right_hand','left_upper_leg','left_lower_leg',
       'left_foot','left_toes','right_upper_leg','right_lower_leg','right_foot','right_toes']
PARENTS=[-1,0,1,2,3,2,5,6,7,2,9,10,11,0,13,14,15,0,17,18,19]


def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def write(path,value):path.write_text(json.dumps(value,ensure_ascii=False,separators=(',',':')),encoding='utf-8')
def quat(q,v):
    x,y,z,w=q;u=np.array([x,y,z]);v=np.array(v)
    return v+2*np.cross(u,np.cross(u,v)+w*v)
def slerp(a,b,t):
    a=np.array(a);b=np.array(b);dot=np.dot(a,b)
    if dot<0:b=-b;dot=-dot
    if dot>.9995:q=a+(b-a)*t
    else:
        angle=math.acos(np.clip(dot,-1,1));q=(math.sin((1-t)*angle)*a+math.sin(t*angle)*b)/math.sin(angle)
    return (q/np.linalg.norm(q)).tolist()


def previous_catalog(root, preserve_existing=False):
    """First publication consumes only freshly baked original sources.

    Reusing local Legacy clips is an explicit upgrade operation, never an input
    needed to bootstrap an original-only catalog on a clean checkout.
    """
    if not preserve_existing:return []
    path=Path(root)/'assets/catalog.json'
    if not path.is_file():raise FileNotFoundError('Explicit Legacy preservation requires an existing local catalog: '+str(path))
    catalog=read(path)
    if not isinstance(catalog,list):raise ValueError('Existing Legacy catalog must be a list')
    retained=[]
    for row in catalog:
        if not isinstance(row,dict):raise ValueError('Invalid existing catalog record')
        if not str(row.get('source','')).startswith('Legacy'):continue
        relative=Path(row.get('file',''))
        if not relative.parts or relative.is_absolute() or '..' in relative.parts or ':' in str(relative):raise ValueError('Invalid Legacy clip path')
        clip=(Path(root)/'assets'/relative).resolve();clip.relative_to((Path(root)/'assets').resolve())
        if not clip.is_file():raise FileNotFoundError('Explicit Legacy input is unavailable: '+str(clip))
        retained.append(row)
    return retained

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--preserve-existing-legacy',action='store_true',help='Upgrade only: retain explicitly labelled local Legacy clips; never needed for first publication')
    args=parser.parse_args();previous=previous_catalog(ROOT,args.preserve_existing_legacy)
    scratch=ROOT/'output/resource-staging';scratch.mkdir(parents=True,exist_ok=True)
    # Earlier publishers mutated baked input, including appending Legacy rows.
    # Compose only in a private copy so a second first-publication run has the
    # same source inputs and cannot inherit publication state from the last run.
    with tempfile.TemporaryDirectory(prefix='baseline-',dir=scratch) as folder:
        stage=Path(folder)/'baked';shutil.copytree(STAGE,stage)
        publish(previous,stage)


def original_catalog(rows):
    return [row for row in rows if row['slot']!=1000 and not str(row.get('source','')).startswith('Legacy')]


def validate_publication_controls(moves,catalog):
    from package import validate_controls
    motions={row['slot']:row for row in catalog};commands=moves.get('commands',{})
    if len(motions)!=len(catalog) or moves.get('idle') not in motions:raise ValueError('Baseline catalog has duplicate slots or missing idle')
    for command,slots in commands.items():
        if not isinstance(slots,list) or not slots or any(slot not in motions for slot in slots):
            raise ValueError('Baseline command references unpublished source: '+command)
    for field in ('experimental_commands','demo'):
        if any(command not in commands for command in moves.get(field,[])):
            raise ValueError('Baseline '+field+' references absent command')
    if any(command not in commands for field in ('inputs','chords') for command in moves.get(field,{}).values()):
        raise ValueError('Baseline input references absent command')
    validate_controls(moves,motions,'baseline')


def publish(previous,stage):
    (ROOT/'assets').mkdir(parents=True,exist_ok=True);(ROOT/'preview').mkdir(parents=True,exist_ok=True)
    assets=stage/'assets';rig=read(assets/'source_rig.json');height=rig['source_rest_hip_height']
    data={s:read(stage/f'preview/slot_{s:03d}.json') for s in (170,171,172,173)}
    clips={}
    for slot in data:
        raw=(assets/f'clip_{slot:03d}.lsm').read_bytes();count=struct.unpack_from('<I',raw,4)[0]
        clips[slot]=np.frombuffer(raw,dtype='<f4',offset=16).reshape(count,87).copy()
    # Resource-phase correspondence: source 170 frame89 nearly equals source
    # 171 frame0; 171 last equals 172 first. These are measured, not numbered guesses.
    phases=[(170,0,89),(171,0,46),(172,0,133),(173,0,114)]
    poses=[];points=[];world_quats=[];stage_map=[];phase_evidence=[];lastxz=None
    peak_frame=max(range(len(data[172]['positions'])),key=lambda f:data[172]['positions'][f][12][1])
    descent_amount=data[172]['positions'][-1][0][1]-data[173]['positions'][0][0][1]
    # Landing begins at the actual source173 hip height. Duration is presentation
    # timing: 0.85 s after the peak wrist/overhead frame, keeping source sample FPS.
    landing_frame=min(133,peak_frame+51)
    for slot,start,end in phases:
        beginning=len(poses);source=data[slot];first=np.array(source['positions'][start][0]);shift=np.zeros(3)
        if lastxz is not None:shift[[0,2]]=lastxz-first[[0,2]]
        for f in range(start,end+1):
            sample=clips[slot][f].copy();p=np.array(source['positions'][f],dtype=float)+shift
            sample[84]=p[0,0];sample[86]=p[0,2]
            if slot==172:
                t=np.clip((f-peak_frame)/max(1,landing_frame-peak_frame),0,1)
                # Root motion is cosmetic: anchor the original lowest ankle/toe
                # to the local stage plane once landing completes. Pose rotations
                # and bone proportions stay unmodified.
                descent=-min(p[j,1] for j in (15,16,19,20))*t*t
                p[:,1]+=descent;sample[85]+=descent
            elif slot==173:
                landing=-min(p[j,1] for j in (15,16,19,20))
                p[:,1]+=landing;sample[85]+=landing
            if slot==173 and f==0:
                # Only leg continuity differs at this source boundary; hands
                # already match to float precision. Six shortest-path samples.
                before=poses[-1];after=sample
                for k in range(1,7):
                    t=k/7;row=np.array(after);q=[]
                    for b in range(21):row[b*4:b*4+4]=slerp(before[b*4:b*4+4],after[b*4:b*4+4],t)
                    row[84:87]=before[84:87]+(after[84:87]-before[84:87])*t
                    # FK in the unmodified source rest geometry; no shrinking arms.
                    rest=np.array([rig['bones'][n]['position'] for n in ORDER]);fk=np.zeros((21,3));fk[0]=[row[84],row[85]+height,row[86]]
                    for b in range(1,21):fk[b]=fk[PARENTS[b]]+quat(row[PARENTS[b]*4:PARENTS[b]*4+4],rest[b]-rest[PARENTS[b]])
                    poses.append(row);points.append(fk.tolist());world_quats.append(None);stage_map.append(dict(slot=173,frame=0,blend=t))
            poses.append(sample);points.append(p.tolist());world_quats.append(source['rotations_xyzw'][f]);stage_map.append(dict(slot=slot,frame=f))
        lastxz=np.array(points[-1][0])[[0,2]]
        phase_evidence.append(dict(slot=slot,start=start,end=end,output_start=beginning,output_end=len(poses)-1))
    # X/Z is globally first-frame relative, as required by LSM runtime.
    first=poses[0][84:87].copy()
    for row in poses:row[84]-=first[0];row[86]-=first[2]
    count=len(poses);raw=np.array(poses,dtype='<f4')
    (assets/'clip_1000.lsm').write_bytes(struct.pack('<4sIII',b'LSM1',count,21,60)+raw.tobytes())
    ground=read(assets/'ground_tracks.json');grip=read(assets/'grip_tracks.json');weapon=read(assets/'source_weapon_tracks.json')
    contacts=[];source_frames=[];clearance=[]
    for i,(row,p) in enumerate(zip(poses,points)):
        p=np.array(p);offset=p[8]-p[12];weight=float(np.clip((.40-np.linalg.norm(offset))/.12,0,1))
        contacts.extend((*offset,weight))
        # world hand rotation is reconstructed from original rest and exported delta.
        from_quat=row[48:52];restq=rig['bones']['right_hand']['rotation']
        x,y,z,w=from_quat;X,Y,Z,W=restq
        q=np.array([w*X+x*W+y*Z-z*Y,w*Y-x*Z+y*W+z*X,w*Z+x*Y-y*X+z*W,w*W-x*X-y*Y-z*Z]);q/=np.linalg.norm(q)
        invq=q.copy();invq[:3]*=-1
        source_frames.append(dict(rotation_xyzw=q.tolist(),support_offset=quat(invq,offset).tolist(),support_weight=weight))
        clearance.append(max(0.,min(p[j,1] for j in (15,16,19,20))))
    (assets/'grip_1000.lsg').write_bytes(struct.pack('<4sIII',b'LSG1',count,60,4)+struct.pack('<%df'%len(contacts),*contacts))
    ground['tracks']['1000']=dict(fps=60,clearance=clearance);grip['tracks']['1000']='grip_1000.lsg';weapon['tracks']['1000']=dict(fps=60,frames=source_frames)
    for name,value in [('ground_tracks',ground),('grip_tracks',grip),('source_weapon_tracks',weapon)]:write(assets/f'{name}.json',value)
    cat=original_catalog(read(assets/'catalog.json'))
    cat.append(dict(slot=1000,file='clip_1000.lsm',frames=count,duration=(count-1)/60,source='Original MHW skeletal stages with explicit local cosmetic descent',experimental=True,
         limitation='Native flight control/mount not reconstructed; no manually fitted wrist/blade rotations'))
    write(assets/'catalog.json',cat)
    write(stage/'preview/slot_1000.json',dict(bones=ORDER,positions=points,up_axis='y',fps=60,experimental=True,
          source='Original source stages, measured posture joins, local showcase descent'))
    moves=read(assets/'moves.json');moves['commands']['helm']=[1000];moves['commands']['fade']=[113];write(assets/'moves.json',moves)
    evidence=dict(phases=phase_evidence,frames=count,duration=(count-1)/60,descent_peak_frame=peak_frame,
        descent_landing_frame=landing_frame,descent_amount_m=descent_amount,descent_evidence='Reference: original172 final hip height minus original173 first hip height; actual local fall anchors the sampled lowest foot to the stage plane from source wrist-height peak; source173 is grounded using sampled feet',
        timing_evidence='Presentation descent 51 frames / 0.85s, consistent in order with user 1:08–1:11 clip; not native FSM gravity/timing measurement',
        hand_rotations='Unmodified original source; no blade direction correction',native_flight_replayed=False,source_mount_verified=False,
        source_states='FSM nodes45->55->56->58->57; ActionNo87->89->90->91->92',
        lmt_phase_assignment='Source pose continuity + original MBD corresponding LMT entries; mapping to native C++ actions remains an inference',
        low_ground_frames=int(sum(c<.02 for c in clearance)),max_source_clearance=float(max(clearance)),stage_map=stage_map)
    # Preserve legacy NeedsParent/alternate slots which do not yet have qualified
    # original context. They are explicitly marked, never presented as originals.
    known={r['slot'] for r in cat}
    kept=[]
    for r in previous:
        if r['slot'] in known or not (ROOT/'assets'/r['file']).is_file():continue
        r=dict(r,experimental=True);cat.append(r);kept.append(r['slot'])
    validate_publication_controls(moves,cat)
    write(ROOT/'research/mhw-original/helmbreaker-composition.json',evidence)
    write(assets/'catalog.json',sorted(cat,key=lambda r:r['slot']))
    for file in assets.iterdir():shutil.copyfile(file,ROOT/'assets'/file.name)
    for file in (stage/'preview').iterdir():shutil.copyfile(file,ROOT/'preview'/file.name)
    # Ground the explicitly retained legacy actions from their existing source FK.
    legacy_slots=[r['slot'] for r in cat if str(r.get('source','')).startswith('Legacy')]
    for slot in legacy_slots:
        path=ROOT/f'preview/slot_{slot:03d}.json'
        if path.is_file():
            legacy=read(path);ground['tracks'][str(slot)]=dict(fps=60,clearance=[max(0.,min(p[j][1] for j in (15,16,19,20))) for p in legacy['positions']])
    write(ROOT/'assets/ground_tracks.json',ground)
    ui=dict(schema_version=1,fps=60,up_axis='y',forward_axis='z',bones=ORDER,clips=[])
    labels={1:'持刀待机',9:'回避',11:'回避',101:'普通斩',102:'普通斩',103:'普通斩',104:'气刃',105:'气刃',106:'气刃',107:'气刃',108:'气刃',127:'见切',128:'见切后续',306:'特殊纳刀',307:'居合',308:'居合气刃',1000:'原版登龙阶段（实验展示控制）'}
    for slot,label in labels.items():
        source=read(ROOT/f'preview/slot_{slot:03d}.json');frames=source['positions'];at=list(range(0,len(frames),3))
        if at[-1]!=len(frames)-1:at.append(len(frames)-1)
        ui['clips'].append(dict(slot=slot,label=label,duration=(len(frames)-1)/60,source_frames=len(frames),frames=[[[round(v,4) for v in p] for p in frames[i]] for i in at]))
    write(ROOT/'assets/ui_preview.json',ui)
    print('Published',len(cat),'clips; legacy context slots',legacy_slots,'Helm frames',count,'seconds',round((count-1)/60,3))


def read_json_ui(path):
    text=path.read_text(encoding='utf-8').split('=',1)[1].strip().rstrip(';');return json.loads(text)


if __name__=='__main__':main()
