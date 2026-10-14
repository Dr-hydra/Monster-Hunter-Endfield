#pragma once
#include "practice.h"
#include <deque>

namespace LS {
struct DelayedHitRule {
 uint32_t triggerWindow=0;float begin=0,end=0;
 std::vector<uint32_t> pulseWindows;std::vector<float> offsets;
 std::vector<std::array<float,4>> motionValues;
 float radius=1,stickSeconds=.5f;
 bool authoredTriggerGeometry=false;
 bool Valid()const{
  if(!std::isfinite(begin)||!std::isfinite(end)||begin<0||end<=begin||pulseWindows.empty()||pulseWindows.size()>32||
     pulseWindows.size()!=offsets.size()||!std::isfinite(radius)||radius<=0||!std::isfinite(stickSeconds)||stickSeconds<0)return false;
  std::set<uint32_t> ids;float previous=-1;
  if(!motionValues.empty()&&motionValues.size()!=offsets.size())return false;
  for(const auto& levels:motionValues)for(float value:levels)if(!std::isfinite(value)||value<0)return false;
  for(size_t i=0;i<offsets.size();++i){if(!std::isfinite(offsets[i])||offsets[i]<0||offsets[i]<=previous||offsets[i]>10||!ids.insert(pulseWindows[i]).second)return false;previous=offsets[i];}
  return true;
 }
};
struct DelayedPulse {PracticeHit hit;int originalLevel=0;};
// W03P supplies the pulse offsets. The current host contact anchor is local
// and explicitly approximate until native COL bone attachments are decoded.
class DelayedHits {
 using Identity=std::tuple<uint64_t,uint32_t,uint32_t,uint32_t>;
 struct Pending {PracticeHit seed;DelayedHitRule rule;Vec3 local{},anchor{};float elapsed=0;size_t next=0;int level=0;float firstAdvance=-1;};
 std::deque<Pending> pending_;std::set<Identity> accepted_;std::deque<Identity> acceptedOrder_;
public:
 uint64_t scheduled=0,pulses=0,misses=0,discarded=0;
 size_t Active()const{return pending_.size();}
 size_t RetainedIdentities()const{return accepted_.size();}
 void Clear(){pending_.clear();accepted_.clear();acceptedOrder_.clear();scheduled=pulses=misses=discarded=0;}
 bool Schedule(const PracticeHit& seed,const DelayedHitRule& rule,const CombatTarget& target,int originalLevel,float elapsed=0,float firstAdvance=-1){
  if(!rule.Valid()||!seed.motionSerial||seed.targetId!=target.key.id||seed.generation!=target.key.generation||!target.alive||
     !Finite(seed.point)||!Finite(target.aimPoint)||!std::isfinite(seed.damage)||seed.damage<=0||!std::isfinite(seed.seconds)||
     seed.seconds<rule.begin||seed.seconds>=rule.end||seed.windowId!=rule.triggerWindow||!std::isfinite(elapsed)||elapsed<0||elapsed>.25f||!std::isfinite(firstAdvance)||firstAdvance<-1||firstAdvance>.25f||pending_.size()>=128)return false;
  const Identity key{seed.motionSerial,seed.targetId,seed.generation,seed.windowId};if(!accepted_.insert(key).second)return false;
  acceptedOrder_.push_back(key);while(acceptedOrder_.size()>4096){accepted_.erase(acceptedOrder_.front());acceptedOrder_.pop_front();}
  pending_.push_back({seed,rule,Retarget::Sub(seed.point,target.aimPoint),seed.point,elapsed,0,std::clamp(originalLevel,0,3),firstAdvance});++scheduled;return true;
 }
 std::vector<DelayedPulse> Update(float dt,const std::vector<CombatTarget>& targets,const SceneObstruction& blocked={}){
  if(!std::isfinite(dt)||dt<0||dt>.25f)throw std::runtime_error("Invalid delayed-hit timestep");
  std::vector<DelayedPulse> result;
  for(auto p=pending_.begin();p!=pending_.end();){
   const auto found=std::find_if(targets.begin(),targets.end(),[&](const auto& t){return t.key.id==p->seed.targetId&&t.key.generation==p->seed.generation&&t.alive;});
   if(found==targets.end()){++discarded;p=pending_.erase(p);continue;}
   p->elapsed+=p->firstAdvance>=0?std::min(dt,p->firstAdvance):dt;p->firstAdvance=-1;
   if(p->elapsed<=p->rule.offsets.front()+p->rule.stickSeconds)p->anchor=Retarget::Add(found->aimPoint,p->local);
   while(p->next<p->rule.offsets.size()&&p->elapsed+1e-6f>=p->rule.offsets[p->next]){
    bool contact=false;for(const auto& shape:found->hurt){if(!Finite(shape.a)||!Finite(shape.b)||!std::isfinite(shape.radius)||shape.radius<=0)continue;
     const auto pair=CombatGeometry::ClosestSegments(p->anchor,p->anchor,shape.a,shape.b);
     if(pair.distance>p->rule.radius+shape.radius||TargetObstructed(p->anchor,pair.second,blocked))continue;contact=true;break;}
    if(contact){auto hit=p->seed;hit.windowId=p->rule.pulseWindows[p->next];hit.seconds=p->seed.seconds+p->rule.offsets[p->next];hit.point=p->anchor;
     if(!p->rule.motionValues.empty())hit.damage*=p->rule.motionValues[p->next][p->level]/100.f;
     if(hit.damage>0){result.push_back({hit,p->level});++pulses;}
    }else ++misses;
    ++p->next;
   }
   if(p->next==p->rule.offsets.size())p=pending_.erase(p);else ++p;
  }return result;
 }
};
} // namespace LS
