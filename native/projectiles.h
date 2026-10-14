#pragma once
#include "practice.h"
#include <functional>

namespace LS {
struct LocalProjectile{uint64_t id=0;Vec3 position{},previous{},velocity{};float age=0,lifetime=3,damage=0;bool bow=false;
 uint64_t motionSerial=0;int slot=0;uint32_t windowId=0;float sourceSeconds=0,radius=.02f,firstAdvance=-1,range=0,traveled=0;};
// Independent presentation/near-field training only. No entity, skill or
// networking handle exists in this type; a scene blocker is read-only.
class LocalProjectiles{
 uint64_t next_=0;
public:
 std::vector<LocalProjectile> active;
 static constexpr size_t Capacity=128;
 void Clear(){active.clear();}
 // Preflight the entire source arrow group before launching any member.
 bool CanLaunch(size_t count=1)const{return count>0&&count<=Capacity&&active.size()<=Capacity-count&&count<=std::numeric_limits<uint64_t>::max()-next_;}
 bool Launch(Vec3 origin,Vec3 direction,float speed,float damage,bool bow,uint64_t motion=0,int slot=0,uint32_t window=0,float seconds=0,float firstAdvance=-1,float radius=.02f,float range=0){
  if(!Finite(origin)||!Finite(direction)||!std::isfinite(speed)||speed<=0||speed>200||!std::isfinite(damage)||damage<0||Retarget::Len(direction)<1e-5f||!CanLaunch())return false;
  if(!std::isfinite(firstAdvance)||firstAdvance<-1||firstAdvance>1||!std::isfinite(seconds)||seconds<0||!std::isfinite(radius)||radius<0||radius>10000||!std::isfinite(range)||range<0)return false;
  // Omitted range retains the old three-second API; explicit source range
  // controls both travel distance and lifetime, including ranges over 3s.
  const float travel=range==0?speed*3:range, lifetime=travel/speed;if(!std::isfinite(lifetime)||lifetime<=0)return false;
  active.push_back({++next_,origin,origin,Retarget::Scale(Retarget::Normalize(direction),speed),0,lifetime,damage,bow,motion,slot,window,seconds,radius,firstAdvance,travel,0});return true;
 }
 template<class Block,class Hit>void Update(float dt,Practice& practice,Block blocked,Hit hit){
  if(!std::isfinite(dt)||dt<0||dt>1)throw std::runtime_error("Invalid local projectile timestep");
  for(auto it=active.begin();it!=active.end();){auto& projectile=*it;const auto from=projectile.position;
   const float requested=projectile.firstAdvance>=0?std::min(dt,projectile.firstAdvance):dt;projectile.firstAdvance=-1;
   const float speed=Retarget::Len(projectile.velocity),remainingTime=projectile.lifetime-projectile.age,remainingRange=projectile.range-projectile.traveled;
   if(!std::isfinite(speed)||speed<=0||!std::isfinite(remainingTime)||remainingTime<=0||!std::isfinite(remainingRange)||remainingRange<=0){it=active.erase(it);continue;}
   const float rangeTime=remainingRange/speed;const float advance=std::min({requested,remainingTime,rangeTime});const bool expires=advance>=remainingTime||advance>=rangeTime;
   const auto to=Retarget::Add(from,Retarget::Scale(projectile.velocity,advance));projectile.previous=from;
   auto contact=SweepProjectile(from,to,projectile.radius,practice.Geometry(),blocked,remainingRange);
   if(contact){contact->strike=uint64_t(projectile.windowId)+1;contact->seconds=projectile.sourceSeconds;contact->contactSerial=projectile.id;
    if(practice.ApplyContact(*contact,projectile.motionSerial,projectile.slot,projectile.damage))hit(projectile,uint32_t(contact->key.id));
    it=active.erase(it);continue;}
   float fraction=1;bool obstruction=blocked(from,to,fraction);
   if(!obstruction)fraction=1;if(!std::isfinite(fraction))fraction=0;fraction=std::clamp(fraction,0.f,1.f);
   const auto end=Lerp(from,to,fraction);
   projectile.position=end;projectile.age+=advance;projectile.traveled+=speed*advance*fraction;
   if(obstruction||expires||projectile.age>=projectile.lifetime||projectile.traveled>=projectile.range)it=active.erase(it);else ++it;
  }
 }
};
} // namespace LS
