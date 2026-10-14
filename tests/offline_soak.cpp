#include "../native/module.cpp"
#include <psapi.h>
#include <iostream>
int main(int argc,char**argv){try{
 if(argc!=3)throw std::runtime_error("offline_soak <project root> <report.json>");
 packageRoot=std::filesystem::path(argv[1]);ReadAssets();Json report{{"scope","CPU and asset state only; no Unity/game/server"},{"modes",Json::array()}};
 const auto memory=[](){PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m)))throw std::runtime_error("Memory measurement unavailable");return size_t(m.PrivateUsage);};
 for(const auto&mode:std::array<std::string,6>{"longsword","sword_shield","greatsword","lance","light_bowgun","bow"}){
  ReadMode(mode);controller.start(configuredIdleSlot);autoDemo=true;demoStep=0;demoClock=0;demoDelay=.6f;demoReturnStarted=false;demoHeld=false;demoChargeLevel=0;
  const auto before=memory();const auto start=std::chrono::steady_clock::now();size_t events=0,steps=0;std::set<int> visited;
  for(float seconds=0;seconds<1800;seconds+=1.f/60){TickDemo(1.f/60);controller.tick(1.f/60);visited.insert(controller.slot);
   localMode.Tick(1.f/60,controller);soundTimeline.Update(controller.motionSerial,controller.slot,controller.seconds,true,[&](const AudioCue& c){if(c.kind!="stop")++events;});
   if(!controller.active||!Finite(controller.output.hip)||controller.output.rotations.size()!=21)throw std::runtime_error(mode+": state invalid during soak");++steps;
  }
  const auto after=memory();const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
  if(events<100||visited.size()<5||after>before+64*1024*1024)throw std::runtime_error(mode+": bounded state/performance check failed");
  report["modes"].push_back({{"id",mode},{"simulated_seconds",1800},{"steps",steps},{"motions",visited.size()},{"cue_dispatches",events},{"cpu_seconds",elapsed},{"mean_cpu_step_us",elapsed*1e6/steps},{"private_before",before},{"private_after",after},{"unity_rendering_measured",false}});
 }
 for(int pass=0;pass<3;++pass)for(const auto&mode:std::array<std::string,6>{"longsword","bow","lance","greatsword","light_bowgun","sword_shield"})ReadMode(mode);
 report["asset_switches"]=24;report["passed"]=true;std::ofstream output(argv[2]);output<<report.dump(2);std::cout<<"Six modes:30 virtual minutes each, bounded resources and24 loaded asset switches passed\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
