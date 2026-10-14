"""Source TIML requests -> WWEV event records -> Wwise HIRC -> decoded PCM.

Unknown WWEV condition fields remain visible. Native variants/layers are retained
as provenance; one-shot cues only use resolved original event descendants. No
game sound calls, synthesized substitute or invented timing is used.
"""
from pathlib import Path
import array,collections,json,math,struct,subprocess,sys,wave,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'research/mhw-longsword/20261012/tooling/mhw-anim-tools'))
from core.formats.lmt.reader import read_lmt_file
from core.formats.timl.reader import read_timl_data_bytes
from core.formats.timl.semantics import format_timeline_parameter_label as TL,format_datatype_hash_label as PROP
MODES={'sword_shield':('01','one',10000),'greatsword':('00','two',20000),'lance':('06','lan',30000),
       'light_bowgun':('13','lbg',40000),'bow':('11','bow',50000),'longsword':('03','swo',0)}

def dump(path,value):
    path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(value,ensure_ascii=False,separators=(',',':')),encoding='utf-8')

def read_wwev(path):
    raw=path.read_bytes();magic,version,count=struct.unpack_from('<III',raw)
    if magic!=int.from_bytes(b'WWEV','little') or version!=19:raise ValueError('Unqualified WWEV layout '+str(path))
    start=len(raw)-count*129
    if not 16<=start<=500:raise ValueError('Unqualified variable WWEV prefix')
    records=[]
    for i in range(count):
        at=start+i*129;req,flag,event,switch=struct.unpack_from('<HHII',raw,at)
        records.append(dict(req=req,flag=flag,event=event,switch_group=switch,source=path.relative_to(ROOT).as_posix(),source_record=i,offset=at,
          raw_condition_hex=raw[at+8:at+129].hex(),layout_evidence='WWEV v19 count-delimited 129-byte records; first fields independently match bank Event ulID; unknown conditional fields retained'))
    return records

def request_markers(action,raw):
    if not action.header.timl_offset:return []
    doc=read_timl_data_bytes(raw,data_offset=action.header.timl_offset);result=[]
    for t in doc.types:
        name=TL(t.timeline_parameter_hash)
        if not name.startswith('nWwiseTimeline::EventGroup') and not name.endswith('::EventLoop'):continue
        fields={PROP(x.datatype_hash):x.keyframes for x in t.transforms}
        def value(prop,f,default=0):
            ks=fields.get(prop,[]);valid=[k for k in ks if k.frame_timing<=f]
            return valid[-1].value if valid else default
        trigger=fields.get('mTrigger',[]) if 'EventGroup' in name else fields.get('mFlag',[])
        for key in trigger:
            for i,ch in enumerate('ABCDEFGHIJKLMNOPQRSTUVWXYZ'):
                if int(key.value)&(1<<i):
                    req=int(value('ReqNo '+ch,key.frame_timing,-1))
                    if req<=0:continue
                    result.append(dict(frame=key.frame_timing,time=key.frame_timing/60,request=req,timeline=name,channel=ch,
                      kind='loop' if 'EventLoop' in name else 'oneshot',release_ms=int(value('ReleaseTime '+ch,key.frame_timing,0)),
                      target_unit=int(value('TargetUnitNo',key.frame_timing,-1)),evidence='Embedded original LMT TIML ReqNo and bitmask trigger key'))
    return result

def load_banks():
    banks=ROOT/'research/multiweapon/extracted/sound/wwise/Windows';nodes={};media={};audits=[]
    for bank in sorted(banks.glob('*.nbnk')):
        xml=bank.with_suffix('.nbnk.xml')
        if not xml.is_file():
            run=subprocess.run([sys.executable,str(ROOT/'local/tools/wwiser-source/wwiser-master/wwiser.py'),'-d','xml',str(bank)],capture_output=True,text=True)
            if run.returncode:raise RuntimeError(run.stdout[-1000:]+run.stderr[-1000:])
        tree=ET.parse(xml)
        raw=bank.read_bytes();chunks={};at=0
        while at<len(raw):
            tag,size=struct.unpack_from('<4sI',raw,at);chunks[tag]=raw[at+8:at+8+size];at+=8+size
        for ident,off,size in struct.iter_unpack('<III',chunks.get(b'DIDX',b'')):
            if off+size>len(chunks.get(b'DATA',b'')):raise ValueError('DIDX exceeds bank DATA')
            media[ident]=dict(source=bank.relative_to(ROOT).as_posix(),data=chunks[b'DATA'][off:off+size])
        added=0
        for obj in tree.iter('object'):
            field=obj.find('./field[@name="ulID"]')
            if field is None or not obj.get('name','').startswith('CAk'):continue
            ident=int(field.get('value'));fields=list(obj.iter('field'))
            refs=[int(f.get('value')) for f in fields if f.get('name') in ('ulActionID','idExt','ulChildID','ulPlayID') and f.get('value','').isdigit() and int(f.get('value'))]
            source=[int(f.get('value')) for f in fields if f.get('name')=='sourceID']
            props=[]
            for p in obj.iter('object'):
                if p.get('name')=='AkPropBundle':
                    pid=p.find('./field[@name="pID"]');val=p.find('./field[@name="pValue"]')
                    if pid is not None and val is not None:props.append(dict(id=int(pid.get('value')),value=float(val.get('value')),label=pid.get('valuefmt')))
            nodes[ident]=dict(id=ident,type=obj.get('name'),source_bank=bank.relative_to(ROOT).as_posix(),refs=sorted(set(refs)),media=source,properties=props,
              mode=[f.get('valuefmt') for f in fields if f.get('name')=='eMode'],loop=[f.get('value') for f in fields if 'LoopCount' in f.get('name','')],
              conditions=[dict(name=f.get('name'),value=f.get('value'),type=f.get('type')) for f in fields if any(s in f.get('name','').lower() for s in ('switch','state','rtpc'))])
            added+=1
        audits.append(dict(bank=bank.name,xml_version=tree.getroot().get('version'),nodes=added,embedded_media=len(chunks.get(b'DIDX',b''))//12))
    return nodes,media,audits

def resolve(nodes,root):
    seen=set();found=set();missing=set();trace=[]
    def visit(ident):
        if ident in seen:return
        seen.add(ident)
        if ident not in nodes:missing.add(ident);return
        node=nodes[ident];trace.append(node);found.update(node['media'])
        for child in node['refs']:visit(child)
    visit(root)
    return sorted(found),trace,sorted(missing)

def main():
    nodes,media,bank_audit=load_banks();extracted=ROOT/'research/multiweapon/extracted'
    events=[]
    for path in (extracted/'sound/pl').glob('*.wwev'):events+=read_wwev(path)
    all_results=[]
    for mode,(num,cat,offset) in MODES.items():
        lmtpath=(ROOT/'research/mhw-original/extracted' if mode=='longsword' else extracted)/f'hm/wp/wp{num}/mot/w{num}_00/w{num}_00.lmt'
        lmt=read_lmt_file(lmtpath);raw=lmtpath.read_bytes();audio=ROOT/f'assets/modes/{mode}/audio';audio.mkdir(parents=True,exist_ok=True)
        matching=[e for e in events if f'hm_wp{num}_' in e['source'] or (mode=='light_bowgun' and 'hm_wp_bowgun_' in e['source'])]
        by_request=collections.defaultdict(list)
        for e in matching:by_request[e['req']].append(e)
        source_events=[];cues={};used=set();missing=[];graphs={};markers={}
        for action in lmt.actions:
            marks=request_markers(action,raw);markers[str(offset+action.id)]=marks;rows=[]
            for marker in marks:
                req=marker['request'];selected=[];records=[]
                for event in by_request.get(req,[]):
                    ids,trace,unresolved=resolve(nodes,event['event'])
                    valid=[i for i in ids if i in media];selected+=valid
                    record=dict(**event,media=ids,missing_nodes=unresolved,missing_media=[i for i in ids if i not in media])
                    records.append(record);graphs[str(event['event'])]=trace
                selected=sorted(set(selected))
                if not selected:
                    missing.append(dict(slot=offset+action.id,**marker,events=records,reason='source request has no resolved embedded playable descendant'))
                    continue
                used.update(selected);ids=[f'{mode}_{i}' for i in selected]
                # Multiple HIRC events/conditional branches are preserved. Playback
                # selects a proven descendant; it does not claim native state mix.
                rows.append(dict(time=marker['time'],clip=ids[0],variants=ids,gain=.75,group=mode+'_request_'+str(req),
                  kind=marker['kind'],loop=marker['kind']=='loop',fade=max(.025,marker['release_ms']/1000),priority=2,
                  fade_in=0.,fade_out=marker['release_ms']/1000,
                  source_request=req,source_timeline=marker['timeline'],source_frame=marker['frame'],condition='always',
                  confidence='source_timing_and_event_descendant',evidence=marker['evidence'],
                  variant_limitation='WWEV conditional selection and Wwise RTPC mix are preserved in source graph, not fully emulated'))
                source_events.append(dict(slot=offset+action.id,marker=marker,events=records,resolved_media=selected))
            if rows:cues[str(offset+action.id)]=rows
        clips=[];decoded=[]
        wemdir=ROOT/f'research/multiweapon/{mode}/wem';wemdir.mkdir(parents=True,exist_ok=True)
        for ident in sorted(used):
            stream=wemdir/f'{ident}.wem';stream.write_bytes(media[ident]['data']);target=audio/f'{ident}.wav'
            metadata=subprocess.run([str(ROOT/'local/tools/vgmstream/vgmstream-cli.exe'),'-I','-m',str(stream)],capture_output=True,text=True)
            run=subprocess.run([str(ROOT/'local/tools/vgmstream/vgmstream-cli.exe'),'-i','-o',str(target),str(stream)],capture_output=True,text=True)
            if run.returncode:raise RuntimeError(run.stdout[-1000:]+run.stderr[-1000:])
            with wave.open(str(target),'rb') as w:
                values=array.array('h',w.readframes(w.getnframes()))
                if w.getsampwidth()!=2 or not 1<=w.getnchannels()<=2:raise ValueError('Unified player requires PCM16 mono/stereo')
                peak=max((abs(s) for s in values),default=0)/32768;rms=math.sqrt(sum(s*s for s in values)/max(1,len(values)))/32768
                reqs=sorted({e['marker']['request'] for e in source_events if ident in e['resolved_media']})
                info=dict(id=f'{mode}_{ident}',file=target.name,label=f'{mode} 原声 / Req '+','.join(map(str,reqs)),
                   source=media[ident]['source'],media_id=ident,requests=reqs,sample_rate=w.getframerate(),channels=w.getnchannels(),frames=w.getnframes(),
                   seconds=w.getnframes()/w.getframerate(),peak=peak,rms=rms,silence=rms<1e-5,truncated=False,metadata=metadata.stdout[-2000:])
                decoded.append(info);clips.append({k:info[k] for k in ('id','file','label','source','requests','seconds','peak','rms')})
        manifest=dict(schema_version=2,mode=mode,source=lmtpath.relative_to(ROOT).as_posix(),
          mapping_evidence='Original LMT TIML bitmask/ReqNo -> WWEV v19 event ID -> wwiser Wwise v120 HIRC descendant -> embedded DIDX/DATA PCM',
          timing_evidence='Original TIML trigger frames /60; no guessed timings',clips=clips,cues=cues,
          limitation='Only resolved source requests play; original unknown condition/RTPC/loop details are recorded for replay fidelity',
          missing_source_requests=missing)
        dump(audio/'cues.json',manifest)
        dump(ROOT/f'research/multiweapon/{mode}/audio-decoded.json',decoded)
        dump(ROOT/f'research/multiweapon/{mode}/audio-source-events.json',dict(schema_version=1,events=source_events,graphs=graphs,markers=markers,missing=missing))
        summary=dict(mode=mode,clips=len(clips),cue_slots=len(cues),cues=sum(len(v) for v in cues.values()),missing_requests=len(missing),silent=sum(x['silence'] for x in decoded),pcm_bytes=sum((audio/x['file']).stat().st_size for x in clips))
        all_results.append(summary);print(summary,flush=True)
    dump(ROOT/'research/multiweapon/audio-audit.json',dict(schema_version=1,bank_audit=bank_audit,modes=all_results,hashes_computed=False,game_launched=False))

if __name__=='__main__':main()
