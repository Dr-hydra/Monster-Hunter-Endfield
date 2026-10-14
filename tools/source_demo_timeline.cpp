// Offline source-preview exporter: same native demo controller and PCM cues.
// No host/game/server APIs are supplied or invoked.
#include "../native/module.cpp"
#include <iostream>
int main(int argc,char**argv){try{
 if(argc!=4)throw std::runtime_error("Usage: source_demo_timeline packageRoot mode out.json");
 packageRoot=std::filesystem::path(argv[1]);ReadAssets();ReadMode(argv[2]);controller.start(configuredIdleSlot);
 autoDemo=true;demoStep=0;demoClock=0;demoDelay=.6f;demoReturnStarted=false;demoHeld=false;demoChargeLevel=0;
 AudioTimeline timeline=soundTimeline;Json rows=Json::array(),sounds=Json::array();size_t round=0;float tail=0;
 for(float elapsed=0;elapsed<240;elapsed+=1.f/120){
  const size_t before=demoStep;if(!round)TickDemo(1.f/120);controller.tick(1.f/120);
  if(!round&&before!=0&&demoStep==0){++round;tail=controller.clips.at(controller.slot).duration()+.7f;}
  if(round){tail-=1.f/120;if(tail<=0)break;}
  if(rows.empty()||int(elapsed*10)>rows.back().at("frame").get<int>())rows.push_back({{"time",elapsed},{"frame",int(elapsed*10)},
   {"slot",controller.slot},{"seconds",controller.seconds},{"motion",controller.motionSerial},{"demo_step",demoStep},{"weapons_visible",!controller.normalSheathed()},
   {"hip",{controller.output.hip.x,controller.output.hip.y,controller.output.hip.z}}});
  const auto emit=[&](const AudioCue&cue){sounds.push_back({{"time",elapsed},{"clip",cue.clip},{"kind",cue.kind},{"group",cue.group},
    {"loop",cue.loop},{"gain",cue.gain},{"fade",cue.fade},{"fade_in",cue.fadeIn},{"fade_out",cue.fadeOut},{"priority",cue.priority},{"motion",controller.motionSerial},{"slot",controller.slot},{"source_time",controller.seconds}});};
  timeline.Update(controller.motionSerial,controller.slot,controller.seconds,true,emit);
  if(loadedMode=="longsword")timeline.Condition("counter",true,emit);
 }
 if(round!=1||rows.empty())throw std::runtime_error("Source tour did not complete");
 const auto destination=std::filesystem::path(argv[3]);const auto pcmRoot=destination.parent_path()/"pcm";std::filesystem::create_directories(pcmRoot);
 Json pcm=Json::object();std::set<std::string>used;for(const auto&event:sounds)if(event.at("kind")!="stop")used.insert(event.at("clip").get<std::string>());
 for(const auto&id:used){const auto&clip=soundClips.at(id);const auto path=pcmRoot/(id+".pcm");std::ofstream data{path,std::ios::binary};data.write(reinterpret_cast<const char*>(clip.samples.data()),std::streamsize(clip.samples.size()*2));
  if(!data)throw std::runtime_error("Cannot write native PCM");pcm[id]={{"file","pcm/"+id+".pcm"},{"sample_rate",clip.sampleRate},{"channels",clip.channels},{"frames",clip.samples.size()/clip.channels},{"balance","Actual native ReadPcmWav+BalanceShowcaseSound samples"}};}
 Json result{{"schema_version",1},{"mode",loadedMode},{"fps",10},{"data_root",modeDefinitions.at(loadedMode).at("data_root")},
  {"audio_root",modeDefinitions.at(loadedMode).at("audio_root")},{"samples",rows},{"audio_events",sounds},{"pcm_clips",pcm},
  {"evidence","Actual native TickDemo/Controller timeline and unified AudioTimeline triggers; original source meshes rendered separately"},
  {"game_launched",false},{"server_started",false}};
 std::ofstream file{std::filesystem::path(argv[3])};file<<result.dump();if(!file)throw std::runtime_error("Cannot write source timeline");
 std::cout<<loadedMode<<": "<<rows.size()<<" actual controller samples, "<<sounds.size()<<" source PCM events\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
