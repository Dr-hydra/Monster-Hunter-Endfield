"""Read current special-weapon config strings, exact Wwise routes and PCM.

Only targeted effective VFS SkillData/BuffData and selected SFX bank/media
ranges are read. No game process, audio event, damage or networking is used.
Bank graph references are read through wwiser, not integer coincidence scans.
"""
from __future__ import annotations
from pathlib import Path
import argparse,importlib.util,json,re,struct,subprocess,sys,types,wave,zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'local/python-deps/endfield-swings'))
sys.path.insert(0,str(ROOT/'local/python-packages'))
spec=importlib.util.spec_from_file_location('weapon_audio_base',ROOT/'tools/endfield_extract_swings.py')
BASE=importlib.util.module_from_spec(spec);sys.modules[spec.name]=BASE;spec.loader.exec_module(BASE)

REQUESTS={
 'laevat_fire_pad':'au_chr_0016_laevat_ultimate_skill_pad',
 'laevat_fire_end':'au_chr_0016_laevat_ultimate_skill_end',
 'laevat_ultimate_start':'au_chr_0016_laevat_ultimate_skill',
 'laevat_fire_slash_1':'au_chr_0016_laevat_ult_attack1',
 'laevat_fire_slash_2':'au_chr_0016_laevat_ult_attack2',
 'laevat_fire_slash_3':'au_chr_0016_laevat_ult_attack3',
 'laevat_fire_slash_4':'au_chr_0016_laevat_ult_attack4',
 'typhoea_bow_charge':'au_chr_0034_typhoea_power_attack_pre',
 'typhoea_bow_shot':'au_chr_0034_typhoea_power_attack_shot',
 'typhoea_bow_shot_1':'au_chr_0034_typhoea_attack1',
 'typhoea_bow_shot_2':'au_chr_0034_typhoea_attack2',
 'typhoea_bow_shot_3':'au_chr_0034_typhoea_attack3',
 'typhoea_bow_shot_4':'au_chr_0034_typhoea_attack4',
 'typhoea_bow_shot_5':'au_chr_0034_typhoea_attack5',
 'typhoea_bow_end_1':'au_chr_0034_typhoea_attack1_end',
 'typhoea_bow_end_2':'au_chr_0034_typhoea_attack2_end',
 'typhoea_bow_end_3':'au_chr_0034_typhoea_attack3_end',
}

def dump(path,value):
 path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8')

def prefixed_events(raw):
 result=[]
 for match in re.finditer(rb'au_[a-zA-Z0-9_]+',raw):
  at=match.start()
  if at<4:continue
  n=struct.unpack_from('<I',raw,at-4)[0]
  if not 8<=n<=200:continue
  value=raw[at:at+n]
  if re.fullmatch(rb'au_[a-zA-Z0-9_]+',value):result.append(dict(offset=at,length=n,event=value.decode('ascii')))
 return result

def graph_from_xml(path,event_id):
 import xml.etree.ElementTree as ET
 tree=ET.parse(path);nodes={}
 for obj in tree.iter('object'):
  field=obj.find('./field[@name="ulID"]')
  if field is None or not obj.get('name','').startswith('CAk'):continue
  ident=int(field.get('value'));fields=list(obj.iter('field'))
  refs=[int(f.get('value')) for f in fields if f.get('name') in ('ulActionID','idExt','ulChildID','ulPlayID') and f.get('value','').isdigit() and int(f.get('value'))]
  media=[int(f.get('value')) for f in fields if f.get('name')=='sourceID']
  nodes[ident]=dict(id=ident,type=obj.get('name'),refs=sorted(set(refs)),media=media,
   loop=[f.get('value') for f in fields if 'LoopCount' in f.get('name','')],
   action_type=[f.get('valuefmt',f.get('value')) for f in fields if f.get('name')=='ulActionType'],
   container_modes=[f.get('valuefmt',f.get('value')) for f in fields if f.get('name') in ('eMode','bIsContinuous','eContinuousPlaybackMode')],
   conditions=[dict(name=f.get('name'),value=f.get('value')) for f in fields if any(s in f.get('name','').lower() for s in ('switch','state','rtpc'))])
 if event_id not in nodes or nodes[event_id]['type']!='CAkEvent':raise ValueError('Per-event bank XML has no matching Event')
 seen=set();missing=set();media=set();pending=[event_id]
 while pending:
  ident=pending.pop()
  if ident in seen:continue
  seen.add(ident)
  if ident not in nodes:missing.add(ident);continue
  media.update(nodes[ident]['media']);pending.extend(nodes[ident]['refs'])
 if missing:raise ValueError('Unresolved precise HIRC references '+str(sorted(missing)))
 return dict(media_ids=sorted(media),nodes=[nodes[i] for i in sorted(seen)],
  routing_evidence='wwiser v150 Event ulActionID -> Action idExt -> container ulChildID/ulPlayID -> Sound sourceID; exact named references only')

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--game',type=Path,default=Path('G:/Arknights Endfield'));parser.add_argument('--be',type=Path,default=Path('G:/Better Endfield'));args=parser.parse_args()
 game=args.game.resolve();be=args.be.resolve();out=ROOT/'research/endfield-special-audio';out.mkdir(exist_ok=True)
 sys.path.insert(0,str(be/'scripts'))
 helper=BASE.load('mhe_weapon_audio_helper',be/'scripts/RefreshEndfieldResourceInputs.py');gen=BASE.load('mhe_weapon_audio_gen',be/'scripts/GenerateResourceManifests.py')
 gen.sha256_bytes=lambda _: 'not-computed';gen.sha256_file=lambda _: 'not-computed'
 stub=types.ModuleType('config');stub.get_game_dir=lambda:str(game);sys.modules['config']=stub
 unpacker=BASE.load('mhe_weapon_audio_vfs',be/'toolchains/EndfieldUnpacker/decrypt_vfs.py');unpacker.crc32=lambda data:(zlib.crc32(data)+2**31)%2**32-2**31
 roots=[(layer,game/'Endfield_Data'/layer/'VFS') for layer in ('StreamingAssets','Persistent')]
 pattern=re.compile(r'^(?:Bundles/Windows/manifest\.hgmmap|.*\.pck|Json/(?:SkillData|BuffData)/.*(?:laevat|typhoea).*\.json)$',re.I)
 records=helper.select_overlay(unpacker,roots,pattern);chunks=helper.build_chunk_index(roots)
 discovery=[];provenance={name:[] for name in REQUESTS.values()};manifest=None
 for record in records:
  relative=record.relative_path
  if relative.casefold().endswith('manifest.hgmmap'):manifest=BASE.manifest_identity(BASE.read_record(record,chunks,unpacker));continue
  if relative.casefold().endswith('.pck'):continue
  raw=BASE.read_record(record,chunks,unpacker);events=prefixed_events(raw)
  cache=be/'inputs/1.5.3/Windows/json'/Path(relative).relative_to('Json')
  row=dict(path=relative,layer=record.layer,block=record.block,chunk=record.chunk,bytes=len(raw),events=events,
    direct_byte_match_to_be_cache=cache.is_file() and cache.read_bytes()==raw)
  discovery.append(row)
  if events:(out/'inputs'/relative).parent.mkdir(parents=True,exist_ok=True);(out/'inputs'/relative).write_bytes(raw)
  for event in events:
   if event['event'] in provenance:provenance[event['event']].append(dict(**event,source=relative,layer=record.layer,chunk=record.chunk))
 if manifest is None or not discovery:raise ValueError('Current manifest or targeted effective configs unavailable')
 dump(out/'current-config-event-discovery.json',dict(schema_version=1,manifest_identity=manifest,configs=discovery,hashes_computed=False))
 packages=[]
 for record in records:
  if not record.relative_path.casefold().endswith('.pck'):continue
  if record.offset!=0 or record.encrypted:raise ValueError('PCK is not direct/unencrypted; explicit range reader required')
  packages.append((record,gen.parse_pck(chunks[record.chunk],game)))
 report=[]
 for key,event in REQUESTS.items():
  if not provenance[event]:raise ValueError('No exact current config string for '+event)
  event_id=gen.wwise_id(event);candidates=[(r,i,b) for r,i in packages for b in i.banks if b.file_id==event_id and b.language=='s']
  if not candidates:
   report.append(dict(key=key,event=event,event_id=event_id,source_config=provenance[event],status='missing-current-sfx-bank'));continue
  candidates.sort(key=lambda x:(len(x[1].banks),x[0].layer=='Persistent'),reverse=True);record,index,bank=candidates[0]
  payload=gen.read_pck_payload(index,bank);parts=dict(gen.iter_chunks(payload));version,bank_id=struct.unpack_from('<II',parts[b'BKHD'])
  if version!=150 or bank_id!=event_id:raise ValueError('Bank version/ID mismatch')
  bank_path=out/'banks'/f'{event_id}.bnk';bank_path.parent.mkdir(exist_ok=True);bank_path.write_bytes(payload)
  xml=bank_path.with_suffix('.bnk.xml')
  run=subprocess.run([sys.executable,str(ROOT/'local/tools/wwiser-source/wwiser-master/wwiser.py'),'-d','xml',str(bank_path)],capture_output=True,text=True)
  if run.returncode:raise RuntimeError(run.stdout[-800:]+run.stderr[-800:])
  graph=graph_from_xml(xml,event_id);media=[];missing_media=[]
  for ident in graph['media_ids']:
   matches=[(r,i,m) for r,i in packages if Path(r.relative_path).parent.as_posix().casefold()==Path(record.relative_path).parent.as_posix().casefold() for m in i.media if m.file_id==ident and m.language=='s']
   if len(matches)!=1:
    all_matches=[dict(package=r.relative_path,layer=r.layer,language=m.language,offset=m.offset,bytes=m.size)
     for r,i in packages for m in i.media if m.file_id==ident]
    missing_media.append(dict(id=ident,matching_namespace_count=len(matches),other_package_occurrences=all_matches,
     reason='Referenced HIRC Sound has no unique media in the current matching SFX namespace; no cross-namespace substitution'))
    continue
   mr,mi,entry=matches[0];wem=gen.read_pck_payload(mi,entry);wem_path=out/'wem'/f'{ident}.wem';wem_path.parent.mkdir(exist_ok=True);wem_path.write_bytes(wem);wav_path=out/'wav'/f'{ident}.wav';wav_path.parent.mkdir(exist_ok=True)
   run=subprocess.run([str(ROOT/'local/tools/vgmstream/vgmstream-cli.exe'),'-i','-o',str(wav_path),str(wem_path)],capture_output=True,text=True)
   (out/'wav'/f'{ident}.decode.log').write_text(run.stdout+run.stderr,encoding='utf-8')
   if run.returncode:raise RuntimeError('Special weapon PCM decode failed '+str(ident))
   with wave.open(str(wav_path),'rb') as w:
    if w.getsampwidth()!=2 or not 1<=w.getnchannels()<=2 or w.getnframes()<1:raise ValueError('Unexpected special-weapon PCM format')
    pcm=dict(sample_rate=w.getframerate(),channels=w.getnchannels(),frames=w.getnframes(),seconds=w.getnframes()/w.getframerate())
   media.append(dict(id=ident,file='wav/'+wav_path.name,**pcm,package=mr.relative_path,chunk=mi.source,offset=entry.offset,bytes=entry.size))
  result=dict(key=key,event=event,event_id=event_id,source_config=provenance[event],bank_version=version,graph=graph,
   package=record.relative_path,layer=record.layer,chunk=index.source,bank_offset=bank.offset,bank_bytes=bank.size,media=media,
   status='decoded' if media and not missing_media else 'partial-current-media-gap' if media else 'missing-current-media' if missing_media else 'source-stop-event-no-new-media',missing_media=missing_media)
  report.append(result);print(key,'resolved',len(media),'PCM',[round(m['seconds'],3) for m in media],'gaps',[m['id'] for m in missing_media],flush=True)
 dump(out/'mapping.json',dict(schema_version=1,manifest_identity=manifest,effective_order=['StreamingAssets','Persistent'],events=report,
   evidence='Current length-prefixed config event string plus matching SFX Bank/Event/HIRC/sourceID and current matching media package',
   game_launched=False,server_started=False,game_audio_events_called=False,hashes_computed=False))
 print('Special source events',len(report),'PCM references',sum(len(e.get('media',[])) for e in report),flush=True)

if __name__=='__main__':main()
