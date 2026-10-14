"""Mix actual exported native PCM events and encode source mesh tour previews.

Uses native ReadPcmWav/BalanceShowcaseSound PCM dumped by source_demo_timeline.
Preview has no world attenuation, hardware-device timing or native Wwise RTPC.
"""
from pathlib import Path
import argparse,json,math,shutil,subprocess,sys,wave
import numpy as np
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'local/python-packages'))
import imageio_ffmpeg
MODES=['longsword','sword_shield','greatsword','lance','light_bowgun','bow']
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def dump(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2),encoding='utf-8')
def rounded(values):return np.where(values>=0,np.floor(values+.5),np.ceil(values-.5))
def mix(folder,timeline):
 rate=48000;duration=math.ceil((timeline['samples'][-1]['time']+.1)*10)/10;length=round(duration*rate);out=np.zeros((length,2),dtype=np.float64)
 clips={id:np.fromfile(folder/info['file'],dtype='<i2').reshape(-1,2).astype(np.float64) for id,info in timeline['pcm_clips'].items()}
 active=[];rendered=[];peak_voices=0;evictions=0;events=timeline['audio_events']
 def finish(voice,end,fade=0):
  stop=min(voice['natural_end'],end);voice['end']=min(voice['natural_end'],stop+fade);voice['fade_out']=voice['end']-stop;rendered.append(voice)
 for event in events:
  now=event['time'];expired=[v for v in active if v['natural_end']<=now]
  for v in expired:finish(v,v['natural_end']);active.remove(v)
  if event['kind']=='stop':
   for v in list(active):
    if v['group']==event['group']:finish(v,now,event.get('fade_out',event.get('fade',0)));active.remove(v)
   continue
  looping=event.get('loop') or event['kind'] in ('loop','start')
  if looping:
   for v in list(active):
    if v['loop'] and v['group']==event['group']:finish(v,now);active.remove(v)
  if len(active)>=16:
   lowest=min(active,key=lambda v:v['priority'])
   if lowest['priority']>=event.get('priority',0):continue
   finish(lowest,now);active.remove(lowest);evictions+=1
  clip=clips[event['clip']];active.append(dict(start=now,clip=event['clip'],group=event['group'],gain=event['gain']*.65,
   loop=looping,priority=event.get('priority',0),fade_in=event.get('fade_in',event.get('fade',0)),
   natural_end=duration if looping else now+len(clip)/rate));peak_voices=max(peak_voices,len(active))
 for voice in active:finish(voice,min(duration,voice['natural_end']))
 for voice in rendered:
  begin=int(round(voice['start']*rate));end=min(length,int(round(voice['end']*rate)));n=end-begin
  if n<=0:continue
  clip=clips[voice['clip']];index=np.arange(n,dtype=np.int64);fade_frames=int(voice['fade_in']*rate)
  if voice['loop'] and fade_frames:
   # Native loop has one fading intro prefix, then a nonfading main loop.
   offsets=np.where(index<fade_frames,index,np.maximum(0,index-fade_frames))%len(clip);envelope=np.minimum(1,index/max(1,fade_frames))
  elif voice['loop']:offsets=index%len(clip);envelope=np.ones(n)
  else:offsets=np.minimum(index,len(clip)-1);envelope=np.minimum(1,index/max(1,fade_frames)) if fade_frames else np.ones(n)
  if voice['fade_out']:
   tail=int(voice['fade_out']*rate);envelope*=np.minimum(1,np.maximum(0,n-index)/max(1,tail))
  values=np.clip(rounded(clip[offsets]*voice['gain']*envelope[:,None]),-32768,32767);out[begin:end]+=values
 peak=float(np.max(np.abs(out)));samples=np.clip(rounded(out),-32768,32767).astype('<i2');path=folder/'source-demo.wav'
 with wave.open(str(path),'wb') as file:file.setnchannels(2);file.setsampwidth(2);file.setframerate(rate);file.writeframes(samples.tobytes())
 metadata=dict(seconds=duration,events=len(events),distinct_pcm=len(clips),peak_voices=peak_voices,evictions=evictions,
  master_volume=.65,frames=length,peak_before_output_clip=peak/32768,
  loop_start_events=sum(e.get('loop',False) or e['kind'] in ('loop','start') for e in events),
  loop_stop_events=sum(e['kind']=='stop' for e in events),
  source_release_time_preserved=all('fade_in' in e and 'fade_out' in e for e in events),
  evidence='Actual C++ ReadPcmWav+BalanceShowcaseSound samples, native120Hz AudioTimeline dispatch/variant choices, bounded16 voices and grouped fades',
  limitation='Offline time-domain mixer; no world pan/attenuation, device scheduling or Wwise RTPC/conditional graph emulation')
 dump(folder/'audio-preview-evidence.json',metadata);return path,metadata
def main():
 p=argparse.ArgumentParser();p.add_argument('--audio-only',action='store_true');p.add_argument('--mode',action='append');p.add_argument('--publish',action='store_true');args=p.parse_args();reports=[]
 publish=ROOT/'assets/previews/source-tours'
 if args.publish:publish.mkdir(parents=True,exist_ok=True)
 for mode in args.mode or MODES:
  folder=ROOT/f'research/multiweapon/{mode}/source-tour';timeline=load(folder/'timeline.json');wav,report=mix(folder,timeline)
  if not args.audio_only:
   pngs=list((folder/'frames').glob('*.png'))
   if len(pngs)!=len(timeline['samples']):raise ValueError(mode+' incomplete source mesh rendering')
   ffmpeg=imageio_ffmpeg.get_ffmpeg_exe();movie=folder/'source-demo.mp4';title=mode.upper()+' | ORIGINAL SOURCE MESH + PCM | OFFLINE'
   cmd=[ffmpeg,'-y','-hide_banner','-loglevel','error','-framerate','10','-i',str(folder/'frames/%05d.png'),'-i',str(wav),
    '-vf',"drawbox=x=0:y=ih-32:w=iw:h=32:color=black@0.55:t=fill,drawtext=text='"+title+"':fontcolor=white:fontsize=12:x=10:y=h-22",
    '-c:v','libx264','-preset','fast','-crf','23','-pix_fmt','yuv420p','-c:a','aac','-b:a','160k','-shortest',str(movie)]
   run=subprocess.run(cmd,capture_output=True,text=True)
   if run.returncode:raise RuntimeError(run.stderr[-3000:])
   # Source-derived preview outputs stay local; packaging can opt in without
   # making raw extraction data a ZIP or Git dependency.
   report.update(video='source-demo.mp4',video_bytes=movie.stat().st_size,source_mesh_frames=len(pngs))
   if args.publish:
    destination=publish/(mode+'.mp4');shutil.copyfile(movie,destination)
    label=dict(longsword='太刀',sword_shield='片手剑',greatsword='大剑',lance='长枪',light_bowgun='轻弩',bow='弓')[mode]
    report.update(id=mode,label=label+'：怪猎源猎人＋原武器·离线有声参考',file='previews/source-tours/'+mode+'.mp4',fps=10,
      game_launched=False,visual_quality_certified=False,
      limitations='源猎人及原武器兼容材质渲染；无原衣物物理、未证实的鞘/箭装配或 Wwise 条件图仿真；不是终末地角色的实机效果')
  reports.append(dict(mode=mode,**report));print(mode,'PCM preview',round(report['seconds'],1),'s',report['distinct_pcm'],'sounds','video',report.get('video_bytes',0),flush=True)
 if args.publish:
  if args.audio_only:raise ValueError('--publish needs encoded source reference videos')
  if args.mode and (publish/'index.json').exists():
   previous=load(publish/'index.json')['modes'];updated={r['mode']:r for r in previous};updated.update({r['mode']:r for r in reports});reports=[updated[m] for m in MODES if m in updated]
  dump(publish/'index.json',dict(schema_version=1,modes=reports,evidence='Actual source meshes and native unified PCM controller timeline; reference, not target-game validation',game_launched=False,server_started=False,hashes_computed=False))
 dump(ROOT/'research/multiweapon/source-tour-preview-index.json',dict(schema_version=1,modes=reports,game_launched=False,server_started=False,hashes_computed=False))
if __name__=='__main__':main()
