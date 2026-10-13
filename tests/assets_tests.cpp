#include "../native/module.cpp"
#include <iostream>
int main(int argc,char** argv){
 try{
  if(argc!=2)throw std::runtime_error("Expected actual package source root");
  packageRoot=std::filesystem::path(argv[1]);ReadAssets();ReadAudio();
  if(characterProfiles.size()!=2||!characterProfiles.contains("chr_0016_laevat")||!characterProfiles.contains("chr_0003_endminf"))
   throw std::runtime_error("Both actual character profiles are required");
  if(!sourceRestReady||!sourcePalmsReady)throw std::runtime_error("Source palm/rest mapping absent");
  if(!controller.clips.contains(1000)||!controller.clips.contains(306))throw std::runtime_error("Helmbreaker/sheathe source absent");
  if(controller.moves.size()<10||gripTracks.empty()||groundTracks.empty())throw std::runtime_error("Real derived motion data absent");
  if(!soundReady||soundClips.size()<8)throw std::runtime_error("Multiple decoded original PCM cues required");
  const auto& helmbreaker=controller.clips.at(1000);
  if(helmbreaker.frames.size()<150)throw std::runtime_error("Full source Helmbreaker duration missing");
  for(const auto& [command,slots]:controller.moves)for(int slot:slots)
   if(!controller.clips.contains(slot))throw std::runtime_error("Command references absent source");
  const auto& laevatain=characterProfiles.at("chr_0016_laevat");
  if(!laevatain.contains("visual_weapon"))throw std::runtime_error("Independent Laevatain blade definition absent");
  // Run the actual automatic timeline against published motions. This catches
  // short segments returning to idle and repeating spirit 1/sound 1 forever.
  controller.start(configuredIdleSlot);autoDemo=true;demoStep=0;demoClock=0;demoDelay=.6f;
  std::set<int> visited;std::set<std::string> heard;AudioTimeline timeline=soundTimeline;
  for(float elapsed=0;elapsed<DemoDuration()*2+1;elapsed+=1.f/120){
   TickDemo(1.f/120);controller.tick(1.f/120);visited.insert(controller.slot);
   timeline.Update(controller.motionSerial,controller.slot,controller.seconds,true,
    [&](const AudioCue& cue){heard.insert(cue.clip);});
  }
  for(int required:{104,105,106,107,108,127,306,307,308,1000})
   if(!visited.contains(required))throw std::runtime_error("Automatic showcase skips slot "+std::to_string(required));
  for(const char* required:{"endfield_swing_1","endfield_swing_2","endfield_swing_3","35004545","280637586"})
   if(!heard.contains(required))throw std::runtime_error(std::string("Automatic showcase skips sound ")+required);
  controller.stop();autoDemo=false;
  std::cout<<"Loaded "<<controller.clips.size()<<" real clips, "<<gripTracks.size()<<" grip tracks, "
   <<groundTracks.size()<<" ground tracks, "<<soundClips.size()<<" PCM sounds and both character profiles; "
   <<visited.size()<<" automatic motion slots, "<<heard.size()<<" distinct triggered sounds\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
