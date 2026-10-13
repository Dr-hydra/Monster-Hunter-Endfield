#pragma once
#include "core.h"

namespace LS {
struct AudioCue{float time=0;std::string clip;float gain=1;};
class AudioTimeline{
 bool valid_=false,mutePending_=false;uint64_t motion_=0;int slot_=0;float seconds_=0;size_t next_=0;
public:
 std::map<int,std::vector<AudioCue>> cues;
 void Reset(){valid_=false;mutePending_=false;motion_=0;slot_=0;seconds_=0;next_=0;}
 // Muting consumes future time in Update; re-enabling never replays old cues.
 void Mute(){mutePending_=true;} // PCM ownership remains with AudioPlayer
 template<class Emit>void Update(uint64_t motion,int slot,float seconds,bool enabled,Emit emit){
  if(!std::isfinite(seconds)||seconds<0){Reset();return;}
  const bool changed=!valid_||motion!=motion_||slot!=slot_||seconds<seconds_;
  const bool discard=changed?false:mutePending_;
  if(changed){valid_=true;motion_=motion;slot_=slot;next_=0;}
  auto found=cues.find(slot);if(found!=cues.end()){
   const auto& track=found->second;
   while(next_<track.size()&&track[next_].time<=seconds+1e-6f){const auto& cue=track[next_++];if(enabled&&!discard)emit(cue);}
  }
  seconds_=seconds;mutePending_=false;
 }
};
} // namespace LS
