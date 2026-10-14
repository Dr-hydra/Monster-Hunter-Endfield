#pragma once
#include "retarget.h"
#include <set>

namespace LS {
struct HitWindow{float begin=0,end=0,multiplier=1;};
struct PracticeTarget{uint32_t id=0;std::string name;Vec3 position{};float radius=.65f,hp=1000,distance=0;};
inline float SegmentDistance(Vec3 point,Vec3 a,Vec3 b){
 using namespace Retarget;const auto segment=Sub(b,a);const float square=Dot(segment,segment);
 const float t=square>1e-12f?std::clamp(Dot(Sub(point,a),segment)/square,0.f,1.f):0;
 return Len(Sub(point,Add(a,Scale(segment,t))));
}
class Practice{
 bool previous_=false;uint64_t motion_=0;int slot_=0;float seconds_=0;Vec3 grip_{},tip_{};
 std::set<std::pair<uint32_t,size_t>> struck_;
public:
 std::map<int,std::vector<HitWindow>> windows;
 std::map<uint32_t,PracticeTarget> targets;
 uint64_t hits=0;double total=0;float lastDamage=0;std::string lastTarget;
 void Clear(){targets.clear();hits=0;total=0;lastDamage=0;lastTarget.clear();Suspend();}
 void Suspend(){previous_=false;struck_.clear();}
 void Refresh(const std::vector<PracticeTarget>& snapshot,float maximumHP){
  if(!std::isfinite(maximumHP)||maximumHP<=0)throw std::runtime_error("Invalid simulated maximum HP");
  std::map<uint32_t,PracticeTarget> next;
  for(const auto& source:snapshot){if(!source.id||!Finite(source.position)||!std::isfinite(source.radius)||source.radius<=0||!std::isfinite(source.distance)||source.distance<0)continue;
   auto target=source;auto old=targets.find(target.id);target.hp=old==targets.end()?maximumHP:std::clamp(old->second.hp,0.f,maximumHP);next.emplace(target.id,std::move(target));}
  targets=std::move(next);
 }
 bool Strike(uint32_t id,float damage){
  if(!std::isfinite(damage)||damage<=0)return false;auto found=targets.find(id);if(found==targets.end()||found->second.hp<=0)return false;
  auto& target=found->second;const auto amount=std::min(target.hp,damage);target.hp-=amount;lastDamage=amount;lastTarget=target.name;total+=amount;++hits;return true;
 }
 void Update(uint64_t motion,int slot,float seconds,Vec3 grip,Vec3 tip,float damage){
  using namespace Retarget;
  if(!std::isfinite(seconds)||seconds<0||!Finite(grip)||!Finite(tip)||!std::isfinite(damage)||damage<0){Suspend();return;}
  const bool continuous=previous_&&motion==motion_&&slot==slot_&&seconds>=seconds_;
  if(!continuous)struck_.clear();
  const float from=continuous?seconds_:seconds;auto active=windows.find(slot);
  if(active!=windows.end())for(size_t w=0;w<active->second.size();++w){const auto& window=active->second[w];
   if(window.end<=from||window.begin>seconds||!std::isfinite(window.multiplier)||window.multiplier<=0)continue;
   const float begin=std::max(from,window.begin),end=std::min(seconds,window.end);
   const float delta=seconds-from;const float a=delta>1e-7f?(begin-from)/delta:1,b=delta>1e-7f?(end-from)/delta:1;
   const auto gripA=continuous?Lerp(grip_,grip,a):grip,gripB=continuous?Lerp(grip_,grip,b):grip;
   const auto tipA=continuous?Lerp(tip_,tip,a):tip,tipB=continuous?Lerp(tip_,tip,b):tip;
   const float travel=std::max(Len(Sub(gripB,gripA)),Len(Sub(tipB,tipA)));
   for(auto& [uid,target]:targets){if(target.hp<=0||struck_.contains({uid,w}))continue;
    const int samples=std::clamp(int(std::ceil(travel/std::max(.01f,target.radius*.4f))),1,128);bool hit=false;
    for(int s=0;s<=samples&&!hit;++s){const float t=float(s)/samples;hit=SegmentDistance(target.position,Lerp(gripA,gripB,t),Lerp(tipA,tipB,t))<=target.radius;}
    if(hit){struck_.insert({uid,w});const float amount=std::min(target.hp,damage*window.multiplier);target.hp-=amount;lastDamage=amount;lastTarget=target.name;total+=amount;++hits;}
   }
  }
  previous_=true;motion_=motion;slot_=slot;seconds_=seconds;grip_=grip;tip_=tip;
 }
};
} // namespace LS
