"""Decode original MHW long-sword Wwise media into the unified PCM chain.

Resource source is selected from the numerically merged original chunk overlay.
Labels follow the user's 22-entry table, provisional where its author was unsure.
Cue timing is a presentation timeline, not decoded MHW hit/event control.
"""
from pathlib import Path
import json, struct, subprocess, wave, array, math
ROOT = Path(__file__).resolve().parents[1]
LABELS = {1:'小居合',3:'登龙突刺命中起跳',4:'纳刀入鞘',5:'无刃大居合',6:'白刃大居合',
          7:'黄红刃大居合',9:'气刃斩',10:'气刃斩',11:'登龙落刀',12:'气刃斩',
          13:'见切成功',14:'见切成功提示',15:'气刃斩',16:'气刃斩',17:'气刃斩',18:'气刃斩',
          19:'纳刀入鞘',20:'纳刀入鞘',21:'气刃斩',22:'黄红刃大居合'}


def main():
    source=ROOT/'research/mhw-original/extracted/sound/wwise/Windows/wp03_swo_epvsp.nbnk'
    data=source.read_bytes();chunks={};at=0
    while at < len(data):
        tag,size=struct.unpack_from('<4sI',data,at);chunks[tag]=data[at+8:at+8+size];at+=8+size
    records=list(struct.iter_unpack('<III',chunks[b'DIDX']))
    output=ROOT/'assets/audio';output.mkdir(parents=True,exist_ok=True)
    wem=ROOT/'research/mhw-original/audio-wem';wem.mkdir(parents=True,exist_ok=True)
    exe=ROOT/'local/tools/vgmstream/vgmstream-cli.exe'
    clips=[];by_index={};decoded=[]
    for index,(media,offset,size) in enumerate(records,1):
        stream=wem/f'{media}.wem';stream.write_bytes(chunks[b'DATA'][offset:offset+size])
        target=output/f'{media}.wav'
        run=subprocess.run([str(exe),'-o',str(target),str(stream)],capture_output=True,text=True)
        if run.returncode:raise RuntimeError(run.stderr or run.stdout)
        with wave.open(str(target),'rb') as w:
            raw=w.readframes(w.getnframes());samples=array.array('h',raw)
            if w.getsampwidth()!=2 or w.getnchannels()>2: raise ValueError('Expected PCM16 mono/stereo')
            peak=max(abs(v) for v in samples)/32768
            rms=math.sqrt(sum(v*v for v in samples)/len(samples))/32768
            info=dict(index=index,id=str(media),file=target.name,label=LABELS.get(index,f'原太刀片段{index}（用途待定）'),
                      channels=w.getnchannels(),sample_rate=w.getframerate(),frames=w.getnframes(),
                      seconds=w.getnframes()/w.getframerate(),peak=peak,rms=rms,source=source.relative_to(ROOT).as_posix())
        decoded.append(info);by_index[index]=str(media)
        # Balance quiet iais in PCM player without modifying original media bytes.
        clips.append(dict(id=str(media),file=target.name,label=info['label']))
    cues={}
    def cue(slot,entries):
        cues[str(slot)]=[dict(time=t,clip=by_index[i],gain=g) for t,i,g in entries]
    # Known original slots retain the existing long-sword command identity.
    # Native event decoding remains a separate task; these are explicit cue values.
    cue(104,[(.27,9,.75)]);cue(105,[(.31,10,.75)])
    cue(106,[(.25,12,.75),(.73,15,.65),(1.04,16,.65)])
    cue(107,[(.37,17,.8)]);cue(108,[(.35,18,.8)])
    cue(127,[(.25,13,.8),(.55,14,.75)])
    cue(306,[(.60,19,.8)])
    cue(307,[(.13,1,1.5)])
    cue(308,[(.16,6,1.4)])
    # Actual full original slot170 has 245 frames; final timing must be game checked.
    cue(171,[(.0,3,.8)])
    cue(172,[(.80,11,.8)])
    composition=ROOT/'research/mhw-original/helmbreaker-composition.json'
    if composition.is_file():
        phases=json.loads(composition.read_text(encoding='utf-8'))['phases']
        starts={p['slot']:p['output_start'] for p in phases}
        cue(1000,[(starts[171]/60,3,.8),((starts[172]+48)/60,11,.8)])
    manifest=dict(schema_version=1,source='Original MHW chunkG0 long-sword bank, user-owned local installation',
                  mapping_evidence='User-provided 22-entry weapon SFX table; labels may be provisional',
                  timing_evidence='Explicit showcase timeline; native MHW event-to-animation timing not decoded',
                  clips=clips,cues=cues)
    (output/'cues.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    (ROOT/'research/mhw-original/audio-decoded.json').write_text(json.dumps(decoded,ensure_ascii=False,indent=2),encoding='utf-8')
    print('decoded',len(decoded),'PCM WAV; cue slots',len(cues),'types',len({c['clip'] for rows in cues.values() for c in rows}))


if __name__=='__main__':main()
