#pragma once
#include "core.h"

namespace LS {
struct AudioCue{float time=0;std::string clip;float gain=1;
 std::string kind="oneshot",group="weapon",condition;bool loop=false;int priority=0;
 float fade=0;std::vector<std::string> variants;float fadeIn=-1,fadeOut=-1;};
class AudioTimeline{
 bool valid_=false,mutePending_=false;uint64_t motion_=0;int slot_=0;float seconds_=0;size_t next_=0;
 std::map<std::string,AudioCue> loops_;std::set<size_t> conditionEmitted_;
 template<class Emit>void Dispatch(AudioCue cue,Emit emit){
  if(!cue.variants.empty())cue.clip=cue.variants[size_t(motion_)%cue.variants.size()];
  if(cue.kind=="stop"){loops_.erase(cue.group);emit(cue);return;}
  if(cue.loop||cue.kind=="loop"||cue.kind=="start"){
   cue.loop=true;const bool continuing=loops_.contains(cue.group)&&loops_.at(cue.group).clip==cue.clip;
   loops_[cue.group]=cue;if(continuing)return;
  }emit(cue);
 }
public:
 std::map<int,std::vector<AudioCue>> cues;
 void Reset(){valid_=false;mutePending_=false;motion_=0;slot_=0;seconds_=0;next_=0;loops_.clear();conditionEmitted_.clear();}
 // Muting consumes future time in Update; re-enabling never replays old cues.
 void Mute(){mutePending_=true;} // PCM ownership remains with AudioPlayer
 template<class Emit>void Update(uint64_t motion,int slot,float seconds,bool enabled,Emit emit){
  if(!std::isfinite(seconds)||seconds<0){Reset();return;}
  const bool changed=!valid_||motion!=motion_||slot!=slot_;
  const bool cycle=!changed&&seconds<seconds_;
  const bool discard=changed?false:mutePending_;
  if(changed){
   for(const auto&[group,loop]:loops_){auto stop=loop;stop.kind="stop";stop.loop=false;stop.fade=loop.fadeOut>=0?loop.fadeOut:.04f;emit(stop);}
   loops_.clear();conditionEmitted_.clear();valid_=true;motion_=motion;slot_=slot;next_=0;
  }
  // Repeating a source body loop consumes its timed one-shots again, while
  // an already owned sustained PCM voice keeps playing without a restart.
  if(cycle){next_=0;conditionEmitted_.clear();}
  auto found=cues.find(slot);if(found!=cues.end()){
   const auto& track=found->second;
   while(next_<track.size()&&track[next_].time<=seconds+1e-6f){const auto& cue=track[next_++];if(cue.condition.empty()||cue.condition=="always")
    Dispatch(cue,[&](const AudioCue& value){if(enabled&&!discard)emit(value);});}
  }
  seconds_=seconds;mutePending_=false;
 }
 template<class Emit>void Condition(const std::string& event,bool enabled,Emit emit){
  if(!valid_||!enabled||event.empty())return;auto found=cues.find(slot_);if(found==cues.end())return;
  for(size_t i=0;i<found->second.size();++i){const auto& cue=found->second[i];
   if(cue.condition==event&&cue.time<=seconds_+1e-6f&&conditionEmitted_.insert(i).second)Dispatch(cue,emit);}
 }
 template<class Emit>void ResumeLoops(bool enabled,Emit emit){if(enabled)for(const auto&[group,loop]:loops_)emit(loop);}
};
} // namespace LS
