#include "../native/module.cpp"
#include <iostream>
#include <set>
namespace {
void HardwareControls(const std::string& mode){
 const auto require=[&](bool ok,const char* message){if(!ok)throw std::runtime_error(mode+": "+message);};
 const auto ready=[](){controller.tick(controller.clips.at(controller.slot).duration()*.6f+.01f);};
 if(mode=="greatsword"){
  const std::array<float,3> times{.2f,.8f,1.4f};
  const std::array<int,3> base{20152,20153,20154},strong{20155,20156,20157},truth{20146,20189,20190};
  for(size_t level=0;level<times.size();++level){
   controller.start(configuredIdleSlot);
   for(const auto& stage:std::array<std::pair<int,int>,3>{{{20144,base[level]},{20145,strong[level]},{20245,truth[level]}}}){
    controller.hardwareInput(SlashBit,SlashBit);
    require(controller.slot==stage.first&&controller.holding(Command::Charge),"physical attack did not enter correct charge stage");
    controller.tick(times[level]);controller.hardwareInput(0,0);
    require(controller.slot==stage.second&&!controller.holding(Command::Charge),"physical release did not select charge stage/level");ready();
   }
  }
 }else if(mode=="sword_shield"){
  const bool soundEnabled=config.sound_enabled;config.sound_enabled=false;
  localMode.Reset(mode);localMode.combatEnabled=true;
  const auto rush=[&](bool timed){
   controller.start(configuredIdleSlot);controller.input(Command::Backstep);ready();
   controller.hardwareInput(SlashBit,SlashBit);require(controller.slot==10344,"physical attack did not enter Perfect Rush");controller.hardwareInput(0,0);
   for(int stage=0;stage<3;++stage){
    const auto* rule=localMode.rules.Find(controller.slot);require(rule&&!rule->timedInputs.empty(),"source Perfect Rush phase lacks an input branch");
    const auto branch=rule->timedInputs.front();const auto seconds=(timed?branch.window.begin:branch.inputGateBegin)+.001f;
    controller.tick(std::max(0.f,seconds-controller.seconds));controller.hardwareInput(SlashBit,SlashBit);
    require(controller.slot==(timed?branch.timedSlot:branch.normalSlot),"physical input did not select the source normal/Just branch");controller.hardwareInput(0,0);
   }
   require(controller.slot==(timed?10341:10319),"Perfect Rush did not select the source final thrust");
  };
  for(bool timed:{false,true}){
   rush(timed);const int thrust=controller.slot;
   controller.tick(controller.clips.at(thrust).duration()+.001f);
   require(controller.resting(),"missed Perfect Rush final thrust incorrectly attaches its finisher");
   rush(timed);const auto* rule=localMode.rules.Find(controller.slot);const auto& windows=practice.authoredWindows.at(controller.slot);
   const auto weapon=std::find_if(windows.begin(),windows.end(),[](const auto& window){return window.lane==0;});require(weapon!=windows.end(),"Perfect Rush final thrust has no source weapon window");
   const int attachment=rule->hitFollowupSlot;CombatEvent hit;hit.motionSerial=controller.motionSerial;hit.slot=controller.slot;hit.seconds=(weapon->begin+weapon->end)*.5f;
   hit.windowId=size_t(weapon->strike-1);hit.targetId=17;hit.generation=1;hit.damage=10;
   controller.tick(std::max(0.f,hit.seconds-controller.seconds));const auto outcome=localMode.Consume(hit,controller);
   require(outcome.hit&&outcome.followupSlot==attachment,"valid source final thrust contact did not attach Perfect Rush");ApplyCombatOutcome(hit,outcome);
   require(controller.slot==attachment,"source attachment outcome did not reach the real Controller");
   const int finish=localMode.rules.Find(attachment)->completionFollowupSlot;controller.tick(controller.clips.at(attachment).duration()+.001f);
   require(finish>0&&controller.slot==finish,"attached Perfect Rush omitted its source completion finisher");ready();
   controller.hardwareInput(SlashBit,SlashBit);require(controller.slot==10101,"Perfect Rush completion incorrectly loops into its entry");
  }
  localMode.Reset(mode);localMode.combatEnabled=false;config.sound_enabled=soundEnabled;
 }else if(mode=="light_bowgun"){
  controller.start(configuredIdleSlot);controller.hardwareInput(ReloadBit,ReloadBit);controller.tick(.1f);controller.hardwareInput(0,0);
  require(controller.slot==40102&&!controller.holding(Command::Spirit),"short reload press incorrectly enters held reload");
  controller.start(configuredIdleSlot);controller.hardwareInput(ReloadBit,ReloadBit);
  // Holding reload queues the all-ammunition action after the current source
  // reload reaches its permitted transition window, rather than cutting it.
  for(int i=0;i<400&&!controller.holding(Command::Spirit);++i){controller.tick(.01f);controller.hardwareInput(0,ReloadBit);}
  require(controller.slot==40107&&controller.holding(Command::Spirit),"long reload press does not hold continuous reload");
  const auto serial=controller.motionSerial;controller.tick(controller.clips.at(40107).duration()*2.2f);
  require(controller.motionSerial==serial&&controller.motionAge()>controller.clips.at(40107).duration()*2,"hold loop resets motion age or starts a new motion");
  controller.hardwareInput(0,0);require(!controller.holding(Command::Spirit)&&controller.resting(),"continuous reload survives key release");
  controller.start(configuredIdleSlot);controller.hardwareInput(ReloadBit,ReloadBit);controller.tick(.1f);
  controller.hardwareInput(DodgeBit,ReloadBit|DodgeBit);
  for(int i=0;i<50;++i){controller.tick(.01f);controller.hardwareInput(0,ReloadBit);}
  require(!controller.holding(Command::Spirit),"cancelled reload press restarts held reload");
  controller.stop();controller.start(configuredIdleSlot);controller.hardwareInput(0,0);
  require(controller.resting()&&!controller.holding(Command::Spirit),"held reload survives stop/restart");
 }else if(mode=="bow"){
  controller.start(configuredIdleSlot);controller.hardwareInput(ReloadBit,ReloadBit);require(controller.slot==50164,"coating/arrow handling input has no source action");
  controller.start(configuredIdleSlot);controller.hardwareInput(AimBit,AimBit);require(controller.holding(Command::Aim),"right mouse does not hold aim");
  controller.hardwareInput(SlashBit,AimBit|SlashBit);require(controller.holding(Command::Charge)&&controller.slot==50110,"attack while aiming is swallowed by aim hold");
  controller.tick(.8f);controller.hardwareInput(0,AimBit);require(controller.slot==50125&&!controller.holding(Command::Charge),"attack release while aiming does not fire");
 }
 controller.stop();
}
}
int main(int argc,char** argv){
 try{
  if(argc!=2)throw std::runtime_error("Expected actual package source root");
  packageRoot=std::filesystem::path(argv[1]);ReadAssets();
  std::ifstream rosterInput(packageRoot/"profiles/index.json");Json roster;rosterInput>>roster;
  const auto expected=roster.at("current_source").at("character_ids").get<std::set<std::string>>();
  if(expected.empty()||characterProfiles.size()!=expected.size()||characterProfiles.contains("chr_9000_endmin"))throw std::runtime_error("Full current-source character registry required");
  for(const auto& id:expected)if(!characterProfiles.contains(id))throw std::runtime_error("Current character profile was not loaded: "+id);
  const std::array<std::string,6> requiredModes{"longsword","sword_shield","greatsword","lance","light_bowgun","bow"};
  size_t totalClips=0,totalSounds=0,totalMotions=0;
  for(const auto& mode:requiredModes){
   ReadMode(mode);
   const auto savedKeys=config;
   for(const auto* preset:{"mouse5","mouse3"}){
    const auto raw=Json{{"mode",mode},{"keyboard_preset",preset}}.dump();Configure(raw.c_str());
    const bool ranged=mode=="bow"||mode=="light_bowgun";const int unique=std::string(preset)=="mouse3"?VK_MBUTTON:VK_XBUTTON2;
    const auto pressed=[&](int key){return KeyBitsFrom([&](int candidate){return candidate==key;});};
    if(pressed(unique)!=(ranged?ThrustBit:SpiritBit)||pressed(VK_LBUTTON)!=SlashBit||pressed(VK_SPACE)!=DodgeBit||pressed(VK_SHIFT)!=SheatheBit)
     throw std::runtime_error(mode+": official mouse preset does not map actual key inputs");
    if(ranged&&(pressed(VK_RBUTTON)!=AimBit||pressed(config.keys.at("reload"))!=ReloadBit))throw std::runtime_error(mode+": ranged aim/reload overlaps another action");
   }
   config=savedKeys;
   if(mode=="longsword"){
    const auto custom=Json{{"mode",mode},{"keyboard_preset","custom"},{"keys",{{"attack",VK_SHIFT},{"secondary",VK_XBUTTON2},{"spirit",VK_CONTROL}}}}.dump();Configure(custom.c_str());
    if(KeyBitsFrom([](int key){return key==VK_SHIFT;})!=SlashBit||KeyBitsFrom([](int key){return key==VK_XBUTTON2;})!=ThrustBit)
     throw std::runtime_error("Official auxiliary aliases override custom key assignments");config=savedKeys;
   }
   if(mode=="longsword"){
    if(geometryContactMasks.size()!=2)throw std::runtime_error("Both actual longsword contact model banks required");
    for(const auto&[model,characters]:geometryContactMasks){
     if(characters.size()!=characterProfiles.size())throw std::runtime_error("Geometric contact character coverage incomplete");
     for(const auto&[character,tracks]:characters)if(tracks.size()!=20)throw std::runtime_error("Geometric contact core slot coverage incomplete");
    }
   }else if(!geometryContactMasks.empty())throw std::runtime_error("Longsword geometric contact leaked across modes");
   if(mode=="sword_shield"){
    if(!supportGroundTracks.contains(10011))throw std::runtime_error("Source-verified roll hand support missing");
    const auto& left=supportGroundTracks.at(10011)[0];if(left.Kind(16.f/60)!=1||left.active.Sample(16.f/60)<.5f||left.clearance.Sample(16.f/60)>.001f)
     throw std::runtime_error("Forward roll hand anchor no longer matches original source mesh");
   }else if(!supportGroundTracks.empty())throw std::runtime_error("Hand ground support leaked across modes");
   if(!sourceRestReady||!sourcePalmsReady||controller.clips.empty()||controller.moves.empty()||groundTracks.empty())throw std::runtime_error(mode+": incomplete actual motion assets");
   if(!soundReady||soundClips.size()<3)throw std::runtime_error(mode+": "+soundDetail);
   for(const auto&[command,slots]:controller.moves)for(int slot:slots){
    if(!controller.clips.contains(slot)||!groundTracks.contains(slot))throw std::runtime_error(mode+": command missing source/floor");
    if(!fingerTracks.contains(slot))throw std::runtime_error(mode+": command missing real source fingers");
   }
   for(const auto&[bits,command]:controller.chordBindings)if(!controller.moves.contains(command))throw std::runtime_error(mode+": invalid chord target");
   if((mode=="bow"||mode=="light_bowgun")&&projectileEvents.empty())throw std::runtime_error(mode+": no actual release markers");
   if(mode!="longsword"&&modeProps.empty())throw std::runtime_error(mode+": missing native-source prop geometry");
   HardwareControls(mode);
   controller.start(configuredIdleSlot);autoDemo=true;demoStep=0;demoClock=0;demoDelay=.6f;demoReturnStarted=false;demoHeld=false;demoChargeLevel=0;demoFinalTail=false;
   std::set<int> visited;std::set<std::string> heard;AudioTimeline timeline=soundTimeline;size_t rounds=0,previous=0;
   for(float elapsed=0;elapsed<240&&rounds<2;elapsed+=1.f/120){
    previous=demoStep;TickDemo(1.f/120);controller.tick(1.f/120);visited.insert(controller.slot);
    if(previous!=0&&demoStep==0)++rounds;
    timeline.Update(controller.motionSerial,controller.slot,controller.seconds,true,[&](const AudioCue& cue){if(cue.kind!="stop")heard.insert(cue.clip);});
    if(autoDemo&&mode=="longsword")timeline.Condition("counter",true,[&](const AudioCue& cue){heard.insert(cue.clip);});
   }
   if(rounds<2||visited.size()<4||heard.size()<3)throw std::runtime_error(mode+": full demonstration did not visit varied motions/audio");
   for(const auto command:demoSequence){const auto& slots=controller.moves.at(command);for(int slot:slots)
    if(!visited.contains(slot))throw std::runtime_error(mode+": automatic showcase skips command stage "+std::to_string(slot));}
   if(mode=="longsword")for(int required:{104,105,106,107,108,109,127,128,140,306,307,311,1000})
    if(!visited.contains(required))throw std::runtime_error("Longsword showcase stage skipped");
   // Each shorter tour must finish a single run in the held ordinary-sheath
   // pose, then accept a new user command without restarting the tour.
   for(const auto* selection:{"full","basic","signature"}){
    controller.stop();controller.start(configuredIdleSlot);config.demo_sequence=selection;config.demo_loop=false;SelectDemoSequence();
    autoDemo=true;demoStep=0;demoClock=0;demoDelay=.6f;demoReturnStarted=false;demoFinalTail=false;demoHeld=false;demoChargeLevel=0;
    const auto estimated=DemoDuration();float elapsed=0;
    for(;elapsed<240&&autoDemo;elapsed+=1.f/120){TickDemo(1.f/120);controller.tick(1.f/120);}
    if(mode=="greatsword"&&std::string(selection)=="full"&&std::abs(elapsed-estimated)>.4f)throw std::runtime_error("Greatsword progress duration differs from actual full tour: elapsed="+std::to_string(elapsed)+", estimated="+std::to_string(estimated));
    if(autoDemo||!controller.active||!controller.normalSheathed()||demoHeld)throw std::runtime_error(mode+": single "+selection+" tour did not stop in the sheathed pose");
    const auto next=controller.moves.contains(Command::Slash)?Command::Slash:Command::Fire;
    controller.input(next);controller.tick(1.f/120);if(controller.normalSheathed())throw std::runtime_error(mode+": cannot resume manual control after tour");
   }
   config.demo_sequence="full";config.demo_loop=true;SelectDemoSequence();
   controller.stop();autoDemo=false;totalClips+=controller.clips.size();totalSounds+=soundClips.size();totalMotions+=visited.size();
   std::cout<<mode<<": "<<controller.clips.size()<<" source clips, "<<fingerTracks.size()<<" finger tracks, "<<soundClips.size()<<" PCM, "<<visited.size()<<" demo motions, "<<heard.size()<<" source sounds, 2 rounds\n";
  }
  ReadMode("longsword");const auto oldSlots=controller.clips.size();const auto oldMode=loadedMode;
  if(themeDefinitions.size()!=2)throw std::runtime_error("Both special model sound routes required");
  const bool soundEnabled=config.sound_enabled;config.sound_enabled=false;
  std::vector<AudioCue> sustained;const auto collect=[&](const AudioCue& cue){sustained.push_back(cue);};
  ThemePhase("laevatain_blade","start",0);themeTimeline.ResumeLoops(true,collect);
  if(sustained.size()!=1||sustained.front().group!="theme.laevat.fire")throw std::runtime_error("Visible fire blade ambience lifecycle missing");
  sustained.clear();ThemePhase("laevatain_blade","stop",1);themeTimeline.ResumeLoops(true,collect);
  if(!sustained.empty())throw std::runtime_error("Fire blade ambience survives end phase");
  ThemePhase("laevatain_blade","start",0);themeTimeline.Mute();themeTimeline.Reset();themeTimeline.ResumeLoops(true,collect);
  if(!sustained.empty())throw std::runtime_error("Muted fire blade ambience survives cleanup");config.sound_enabled=soundEnabled;
  const auto invalid="invalid-test";modeDefinitions[invalid]={{"id",invalid},{"data_root","assets/missing-mode"}};
  bool rejected=false;try{ReadMode(invalid);}catch(const std::exception&){rejected=true;}modeDefinitions.erase(invalid);
  if(!rejected||loadedMode!=oldMode||controller.clips.size()!=oldSlots||!soundReady||geometryContactMasks.size()!=2)throw std::runtime_error("Failed mode switch did not restore coherent previous assets");
  // A real host mode request with broken assets must restore configuration
  // and revisions as well as the old loaded asset bank.
  const auto bowDefinition=modeDefinitions.at("bow");modeDefinitions["bow"]["data_root"]="assets/missing-mode";
  const auto previousConfig=config;config.visual_weapon="laevatain_blade";const auto revision=configRevision,applied=appliedConfigRevision;
  const auto previousBridge=bridge;const bool previousInitialized=initialized;initialized=true;
  bridge.reply=[](void*,const char*,BE_Result result,const char*){return result;};
  const auto failure=Message("broken-mode",R"({"action":"set_mode","mode":"bow"})");
  if(failure!=BE_Result_InvalidArgument||config.mode!=oldMode||config.visual_weapon!="laevatain_blade"||configRevision!=revision||appliedConfigRevision!=applied||geometryContactMasks.size()!=2)
   throw std::runtime_error("Failed host mode request changed configuration or old assets");
  modeDefinitions["bow"]=bowDefinition;config=previousConfig;bridge=previousBridge;initialized=previousInitialized;
  std::cout<<"Validated 6 live-loaded asset sets, "<<characterProfiles.size()<<" characters, "<<totalClips<<" source clips, "<<totalSounds<<" PCM and "<<totalMotions<<" demonstrated motions\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
