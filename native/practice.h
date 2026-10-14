#pragma once
#include "retarget.h"
#include "combat_collision.h"
#include <set>
#include <deque>

namespace LS {
struct HitWindow{float begin=0,end=0,multiplier=1;};
struct PracticeTarget{
 uint32_t id=0;std::string name;Vec3 position{};float radius=.65f,hp=1000,distance=0;
 uint32_t generation=0;std::vector<HurtCapsule> hurt;bool approximateGeometry=true;
 uint64_t serverId=0,serverBattleInstId=0;uint32_t serverGeneration=0;
 float maximumHP=1000;
};
struct PracticeHit {
 uint32_t targetId=0,generation=0;uint64_t motionSerial=0;int slot=0;float seconds=0;
 uint32_t windowId=0,partId=0;float damage=0;Vec3 point{};
 uint64_t serverId=0,serverBattleInstId=0;uint32_t serverGeneration=0;
 bool weakPointKnown=false,weakPoint=false;
 uint64_t contactSerial=0;
};
inline float SegmentDistance(Vec3 point,Vec3 a,Vec3 b){
 using namespace Retarget;const auto segment=Sub(b,a);const float square=Dot(segment,segment);
 const float t=square>1e-12f?std::clamp(Dot(Sub(point,a),segment)/square,0.f,1.f):0;
 return Len(Sub(point,Add(a,Scale(segment,t))));
}
class Practice{
 using AcceptedIdentity=std::tuple<uint64_t,uint32_t,uint32_t,uint32_t,uint64_t>;
 std::set<AcceptedIdentity> accepted_;std::deque<AcceptedIdentity> acceptedOrder_;
 bool previous_=false;uint64_t motion_=0;int slot_=0;float seconds_=0;Vec3 grip_{},tip_{};
 std::set<std::pair<uint32_t,size_t>> struck_;
 std::vector<WeaponCapsule> cachedWeapons_;std::vector<CombatTarget> cachedTargets_;Vec3 cachedOrigin_{};uint64_t cachedMotion_=0;
 uint64_t attemptMotion_=0;bool attempted_=false,attemptHit_=false,attemptFinished_=false;
public:
 std::function<float(const PracticeHit&)> contactDamage;
 std::function<void(const PracticeHit&)> acceptedHit;
 std::map<int,std::vector<HitWindow>> windows;
 std::map<int,std::vector<CollisionWindow>> authoredWindows;
 CombatCollision collision;
 std::vector<PracticeHit> events;
 std::map<uint32_t,PracticeTarget> targets;
 uint64_t hits=0,attempts=0,misses=0;double total=0;float lastDamage=0;std::string lastTarget;
 void Clear(){targets.clear();accepted_.clear();acceptedOrder_.clear();hits=attempts=misses=0;total=0;lastDamage=0;lastTarget.clear();Suspend();}
 void Suspend(){previous_=false;struck_.clear();collision.Suspend();events.clear();cachedWeapons_.clear();cachedTargets_.clear();cachedMotion_=0;attemptMotion_=0;attempted_=attemptHit_=attemptFinished_=false;}
 void BeginFrame(){events.clear();}
 void Refresh(const std::vector<PracticeTarget>& snapshot,float maximumHP){
  if(!std::isfinite(maximumHP)||maximumHP<=0)throw std::runtime_error("Invalid simulated maximum HP");
  std::map<uint32_t,PracticeTarget> next;
  for(const auto& source:snapshot){if(!source.id||!Finite(source.position)||!std::isfinite(source.radius)||source.radius<=0||!std::isfinite(source.distance)||source.distance<0)continue;
   auto target=source;target.maximumHP=maximumHP;auto old=targets.find(target.id);target.hp=old==targets.end()||old->second.generation!=target.generation?maximumHP:std::clamp(old->second.hp,0.f,maximumHP);next.emplace(target.id,std::move(target));}
  targets=std::move(next);
 }
 bool Strike(uint32_t id,float damage){
  if(!std::isfinite(damage)||damage<=0)return false;auto found=targets.find(id);if(found==targets.end()||found->second.hp<=0)return false;
  auto& target=found->second;const auto amount=std::min(target.hp,damage);target.hp-=amount;lastDamage=amount;lastTarget=target.name;total+=amount;++hits;return true;
 }
 std::vector<CombatTarget> Geometry()const{
  std::vector<CombatTarget> result;result.reserve(targets.size());
  for(const auto&[id,t]:targets){CombatTarget target;
   target.key={id,t.generation};target.name=t.name;target.position=target.aimPoint=t.position;
   target.alive=t.hp>0;target.approximateGeometry=t.approximateGeometry;target.hurt=t.hurt;
   if(target.hurt.empty())target.hurt.push_back({t.position,t.position,t.radius,0,0});
   result.push_back(std::move(target));
  }return result;
 }
 bool ApplyContact(const CombatContact& contact,uint64_t motion,int slot,float damage,bool evaluateDamage=false){
  if(contact.key.id>UINT32_MAX)return false;
  const auto id=uint32_t(contact.key.id);auto found=targets.find(id);
  if(found==targets.end()||found->second.generation!=contact.key.generation||!std::isfinite(contact.multiplier)||contact.multiplier<=0)return false;
  const auto& t=found->second;
  PracticeHit hit{id,t.generation,motion,slot,contact.seconds,contact.strike?uint32_t(contact.strike-1):uint32_t(contact.window),contact.part,damage,
    contact.point,t.serverId,t.serverBattleInstId,t.serverGeneration,contact.weakPointKnown,contact.weakPoint,contact.contactSerial};
  const AcceptedIdentity key{motion,id,t.generation,hit.windowId,contact.contactSerial};
  if(accepted_.contains(key))return false;
  if(evaluateDamage&&contactDamage)damage=contactDamage(hit);
  if(!Strike(id,damage*contact.multiplier))return false;
  accepted_.insert(key);acceptedOrder_.push_back(key);
  while(acceptedOrder_.size()>4096){accepted_.erase(acceptedOrder_.front());acceptedOrder_.pop_front();}
  hit.damage=lastDamage;events.push_back(hit);if(acceptedHit)acceptedHit(hit);return true;
 }
 void UpdateCombat(uint64_t motion,int slot,float seconds,const std::vector<WeaponCapsule>& weapons,
                   Vec3 origin,float maximumRange,float damage,const SceneObstruction& blocked={}){
  auto active=authoredWindows.find(slot);
  static const std::vector<CollisionWindow> empty;
  const auto& windows=active==authoredWindows.end()?empty:active->second;auto geometry=Geometry();
  if(motion!=cachedMotion_&&!cachedWeapons_.empty()&&seconds<=collision.settings.maximumFrameGap)
   collision.Update(motion,slot,0,cachedWeapons_,empty,cachedTargets_,cachedOrigin_,maximumRange,blocked);
  if(attemptMotion_!=motion){if(attempted_&&!attemptHit_&&!attemptFinished_)++misses;
   attemptMotion_=motion;attempted_=attemptHit_=attemptFinished_=false;}
  if(!windows.empty()&&!attempted_){attempted_=true;++attempts;}
  const auto contacts=collision.Update(motion,slot,seconds,weapons,windows,geometry,origin,maximumRange,blocked);
  for(const auto& contact:contacts)if(ApplyContact(contact,motion,slot,damage,true))attemptHit_=true;
  float end=0;for(const auto& window:windows)end=std::max(end,window.end);
  if(attempted_&&!attemptFinished_&&seconds>=end){attemptFinished_=true;if(!attemptHit_)++misses;}
  cachedWeapons_=weapons;cachedTargets_=std::move(geometry);cachedOrigin_=origin;cachedMotion_=motion;
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
