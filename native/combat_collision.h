#pragma once
#include "targeting.h"

namespace LS {
struct WeaponCapsule {
 Vec3 base{},tip{};float radius=.04f;uint32_t lane=0;
};
// strike groups authored hit pulses; multiple shapes/lanes belonging to the
// same pulse may share it. Zero receives a stable per-window fallback identity.
// bank/node retain source COL/TIML identity, never a guessed damage value.
struct CollisionWindow {
 float begin=0,end=0,multiplier=1;uint64_t strike=0;uint32_t lane=0,bank=0,node=0;
};
struct CombatContact {
 EntityKey key{};uint32_t part=0;uint64_t strike=0;uint32_t lane=0,bank=0,node=0;
 size_t window=0;float seconds=0,fraction=0,multiplier=1;
 Vec3 point{},normal{};bool approximateGeometry=false;
 bool weakPointKnown=false,weakPoint=false;
 uint64_t contactSerial=0;
};
struct CollisionSettings {
 float contactTolerance=.0001f,maximumFrameGap=2,teleportDistance=32;
 bool checkOriginOcclusion=true;
};

namespace CombatGeometry {
using Retarget::Add;using Retarget::Sub;using Retarget::Scale;
inline double Dot(Vec3 a,Vec3 b){return double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z;}
inline float Length(Vec3 a){return float(std::sqrt(Dot(a,a)));}
inline bool PointValid(Vec3 value){return Finite(value)&&std::max({std::abs(value.x),std::abs(value.y),std::abs(value.z)})<=1e7f;}
inline bool Valid(const WeaponCapsule& value){return PointValid(value.base)&&PointValid(value.tip)&&std::isfinite(value.radius)&&value.radius>=0&&value.radius<=10000;}
inline bool Valid(const HurtCapsule& value){return PointValid(value.a)&&PointValid(value.b)&&std::isfinite(value.radius)&&value.radius>0&&value.radius<=10000;}
struct ClosestPair {Vec3 first{},second{};float distance=0;};
inline ClosestPair Pair(Vec3 p,Vec3 q){return {p,q,Length(Sub(p,q))};}
inline ClosestPair ClosestSegments(Vec3 p1,Vec3 q1,Vec3 p2,Vec3 q2){
 const auto d1=Sub(q1,p1),d2=Sub(q2,p2),r=Sub(p1,p2);
 const double a=Dot(d1,d1),e=Dot(d2,d2),f=Dot(d2,r);double s=0,t=0;
 constexpr double epsilon=1e-18;
 if(a<=epsilon&&e<=epsilon)return Pair(p1,p2);
 if(a<=epsilon)t=std::clamp(f/e,0.,1.);
 else{
  const double c=Dot(d1,r);
  if(e<=epsilon)s=std::clamp(-c/a,0.,1.);
  else{
   const double b=Dot(d1,d2),denominator=a*e-b*b;
   if(denominator>epsilon*a*e)s=std::clamp((b*f-c*e)/denominator,0.,1.);
   t=(b*s+f)/e;
   if(t<0){t=0;s=std::clamp(-c/a,0.,1.);}
   else if(t>1){t=1;s=std::clamp((b-c)/a,0.,1.);}
  }
 }
 return Pair(Add(p1,Scale(d1,float(s))),Add(p2,Scale(d2,float(t))));
}
inline Vec3 ClosestTriangle(Vec3 p,Vec3 a,Vec3 b,Vec3 c){
 const auto ab=Sub(b,a),ac=Sub(c,a),ap=Sub(p,a);
 const double area2=Dot(Retarget::Cross(ab,ac),Retarget::Cross(ab,ac));
 if(area2<=1e-18){
  const auto x=ClosestSegments(p,p,a,b),y=ClosestSegments(p,p,b,c),z=ClosestSegments(p,p,c,a);
  return x.distance<=y.distance&&x.distance<=z.distance?x.second:(y.distance<=z.distance?y.second:z.second);
 }
 const double d1=Dot(ab,ap),d2=Dot(ac,ap);if(d1<=0&&d2<=0)return a;
 const auto bp=Sub(p,b);const double d3=Dot(ab,bp),d4=Dot(ac,bp);if(d3>=0&&d4<=d3)return b;
 const double vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0)return Add(a,Scale(ab,float(d1/(d1-d3))));
 const auto cp=Sub(p,c);const double d5=Dot(ab,cp),d6=Dot(ac,cp);if(d6>=0&&d5<=d6)return c;
 const double vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0)return Add(a,Scale(ac,float(d2/(d2-d6))));
 const double va=d3*d6-d5*d4;if(va<=0&&d4-d3>=0&&d5-d6>=0)return Add(b,Scale(Sub(c,b),float((d4-d3)/((d4-d3)+(d5-d6)))));
 const double inv=1/(va+vb+vc);return Add(a,Add(Scale(ab,float(vb*inv)),Scale(ac,float(vc*inv))));
}
inline float SegmentTriangleDistance(Vec3 p,Vec3 q,Vec3 a,Vec3 b,Vec3 c){
 float result=std::min(Length(Sub(p,ClosestTriangle(p,a,b,c))),Length(Sub(q,ClosestTriangle(q,a,b,c))));
 for(const auto& edge:std::array<std::pair<Vec3,Vec3>,3>{{{a,b},{b,c},{c,a}}})
  result=std::min(result,ClosestSegments(p,q,edge.first,edge.second).distance);
 const auto normal=Retarget::Cross(Sub(b,a),Sub(c,a));const double denominator=Dot(normal,Sub(q,p));
 if(std::abs(denominator)>1e-18){
  const double t=Dot(normal,Sub(a,p))/denominator;
  if(t>=0&&t<=1){const auto point=Lerp(p,q,float(t));result=std::min(result,Length(Sub(point,ClosestTriangle(point,a,b,c))));}
 }
 return result;
}
inline bool InsideTetra(Vec3 p,const std::array<Vec3,4>& v){
 const double volume=Dot(Sub(v[1],v[0]),Retarget::Cross(Sub(v[2],v[0]),Sub(v[3],v[0])));
 if(std::abs(volume)<1e-15)return false;
 constexpr int faces[4][4]={{0,1,2,3},{0,1,3,2},{0,2,3,1},{1,2,3,0}};
 for(const auto& face:faces){const auto a=v[face[0]],normal=Retarget::Cross(Sub(v[face[1]],a),Sub(v[face[2]],a));
  const double inside=Dot(normal,Sub(v[face[3]],a)),test=Dot(normal,Sub(p,a));
  if((inside>0&&test<0)||(inside<0&&test>0))return false;
 }
 return true;
}
// Every interpolated blade segment is inside this tetrahedral convex hull.
// Its distance gives a conservative lower bound, including twisting blades;
// the triangles alone are not taken as actual blade hits.
inline float HullSegmentDistance(const WeaponCapsule& from,const WeaponCapsule& to,const HurtCapsule& target){
 const std::array<Vec3,4> vertices{from.base,from.tip,to.base,to.tip};
 if(InsideTetra(target.a,vertices)||InsideTetra(target.b,vertices))return 0;
 float result=std::numeric_limits<float>::infinity();
 constexpr int faces[4][3]={{0,1,2},{0,1,3},{0,2,3},{1,2,3}};
 for(const auto& face:faces)result=std::min(result,SegmentTriangleDistance(target.a,target.b,vertices[face[0]],vertices[face[1]],vertices[face[2]]));
 return result;
}
struct Bounds {Vec3 minimum{},maximum{};};
inline Bounds Bounding(Vec3 a,Vec3 b,Vec3 c,Vec3 d){
 return {{std::min({a.x,b.x,c.x,d.x}),std::min({a.y,b.y,c.y,d.y}),std::min({a.z,b.z,c.z,d.z})},
         {std::max({a.x,b.x,c.x,d.x}),std::max({a.y,b.y,c.y,d.y}),std::max({a.z,b.z,c.z,d.z})}};
}
inline float BoundsDistance(const Bounds& a,const Bounds& b){
 return Length({std::max({0.f,a.minimum.x-b.maximum.x,b.minimum.x-a.maximum.x}),
                std::max({0.f,a.minimum.y-b.maximum.y,b.minimum.y-a.maximum.y}),
                std::max({0.f,a.minimum.z-b.maximum.z,b.minimum.z-a.maximum.z})});
}
inline WeaponCapsule Interpolate(const WeaponCapsule& a,const WeaponCapsule& b,float t){return {Lerp(a.base,b.base,t),Lerp(a.tip,b.tip,t),a.radius+(b.radius-a.radius)*t,b.lane};}
inline HurtCapsule Interpolate(const HurtCapsule& a,const HurtCapsule& b,float t){return {Lerp(a.a,b.a,t),Lerp(a.b,b.b,t),a.radius+(b.radius-a.radius)*t,b.part,b.priority,b.weakPointKnown,b.weakPoint};}
struct CapsuleSweep {
 float fraction=0;Vec3 weaponPoint{},targetPoint{},normal{};float separation=0;
};
inline std::optional<CapsuleSweep> SweepCapsules(WeaponCapsule from,WeaponCapsule to,HurtCapsule targetFrom,HurtCapsule targetTo,float tolerance=.0001f){
 if(!Valid(from)||!Valid(to)||!Valid(targetFrom)||!Valid(targetTo)||!std::isfinite(tolerance)||tolerance<=0)return {};
 Vec3 carried{};
 // Ordinary hurt capsules translate with a monster. Removing that motion
 // makes a moving-target collision exact instead of testing only its endpoint.
 const auto targetDelta=Sub(targetTo.a,targetFrom.a);
 if(Length(Sub(targetDelta,Sub(targetTo.b,targetFrom.b)))<1e-6f){
  carried=targetDelta;to.base=Sub(to.base,carried);to.tip=Sub(to.tip,carried);targetTo.a=targetFrom.a;targetTo.b=targetFrom.b;
 }
 const float speed=std::max(Length(Sub(to.base,from.base)),Length(Sub(to.tip,from.tip)))+
  std::max(Length(Sub(targetTo.a,targetFrom.a)),Length(Sub(targetTo.b,targetFrom.b)))+
  std::abs(to.radius-from.radius)+std::abs(targetTo.radius-targetFrom.radius);
 const auto sample=[&](float t){const auto w=Interpolate(from,to,t);const auto h=Interpolate(targetFrom,targetTo,t);
  const auto pair=ClosestSegments(w.base,w.tip,h.a,h.b);return std::pair{pair,pair.distance-w.radius-h.radius};};
 const auto make=[&](float t){const auto [pair,gap]=sample(t);const auto w=Interpolate(from,to,t);const auto h=Interpolate(targetFrom,targetTo,t);
  const auto delta=Sub(pair.first,pair.second);Vec3 normal{0,1,0};
  if(pair.distance>1e-8f)normal=Scale(delta,1/pair.distance);
  const auto shift=Scale(carried,t);
  return CapsuleSweep{t,Add(Sub(pair.first,Scale(normal,w.radius)),shift),Add(Add(pair.second,Scale(normal,h.radius)),shift),normal,gap};};
 struct Interval {float lo,hi;unsigned depth;};std::vector<Interval> pending{{0,1,0}};
 const bool fixedAxis=Length(Sub(targetTo.a,targetFrom.a))<1e-8f&&Length(Sub(targetTo.b,targetFrom.b))<1e-8f;
 while(!pending.empty()){
  const auto interval=pending.back();pending.pop_back();const float middle=(interval.lo+interval.hi)*.5f;
  const auto w0=Interpolate(from,to,interval.lo),w1=Interpolate(from,to,interval.hi);
  const auto h0=Interpolate(targetFrom,targetTo,interval.lo),h1=Interpolate(targetFrom,targetTo,interval.hi);
  const float radius=std::max(w0.radius,w1.radius)+std::max(h0.radius,h1.radius);
  if(BoundsDistance(Bounding(w0.base,w0.tip,w1.base,w1.tip),Bounding(h0.a,h0.b,h1.a,h1.b))-radius>tolerance)continue;
  if(fixedAxis&&HullSegmentDistance(w0,w1,h0)-radius>tolerance)continue;
  const auto [first,firstGap]=sample(interval.lo);(void)first;
  if(firstGap<=tolerance)return make(interval.lo);
  const auto [center,centerGap]=sample(middle);(void)center;
  if(centerGap-speed*(interval.hi-interval.lo)*.5f>tolerance)continue;
  if(speed*(interval.hi-interval.lo)<=tolerance||interval.depth>=40||middle==interval.lo||middle==interval.hi){
   const auto [last,lastGap]=sample(interval.hi);(void)last;
   float at=interval.lo,best=firstGap;if(centerGap<best){at=middle;best=centerGap;}if(lastGap<best){at=interval.hi;best=lastGap;}
   if(best<=2*tolerance)return make(at);
   continue;
  }
  // Depth first, earlier interval first: the resulting contact is an entry
  // time, not a target center projection or a coarse render-frame sample.
  pending.push_back({middle,interval.hi,interval.depth+1});pending.push_back({interval.lo,middle,interval.depth+1});
 }
 return {};
}
inline std::optional<CapsuleSweep> SweepCapsules(const WeaponCapsule& from,const WeaponCapsule& to,const HurtCapsule& target,float tolerance=.0001f){return SweepCapsules(from,to,target,target,tolerance);}
inline bool Better(float time,int priority,uint32_t part,float oldTime,int oldPriority,uint32_t oldPart){
 constexpr float tie=.00001f;
 if(time<oldTime-tie)return true;
 if(std::abs(time-oldTime)<=tie)return priority>oldPriority||(priority==oldPriority&&part<oldPart);
 return false;
}
} // namespace CombatGeometry

// Stateful melee contacts only: the caller decides whether to apply simulated
// HP, source conditions, feedback, or optional local extension callbacks.
class CombatCollision {
 bool previous_=false;uint64_t motion_=0;int slot_=0;float seconds_=0;Vec3 origin_{};
 std::map<uint32_t,WeaponCapsule> weapons_;
 std::map<EntityKey,CombatTarget> targets_;
 std::set<std::pair<EntityKey,uint64_t>> struck_;
 void Seed(uint64_t motion,int slot,float seconds,const std::vector<WeaponCapsule>& weapons,
           const std::vector<CombatTarget>& targets,Vec3 origin){
  previous_=true;motion_=motion;slot_=slot;seconds_=seconds;origin_=origin;weapons_.clear();targets_.clear();
  for(const auto& w:weapons)if(CombatGeometry::Valid(w))weapons_.emplace(w.lane,w);
  for(const auto& target:targets)if(target.key.id&&target.alive&&target.hostile&&CombatGeometry::PointValid(target.position)&&CombatGeometry::PointValid(target.aimPoint))targets_.emplace(target.key,target);
 }
 bool SettingsValid()const{return std::isfinite(settings.contactTolerance)&&settings.contactTolerance>0&&
  std::isfinite(settings.maximumFrameGap)&&settings.maximumFrameGap>0&&std::isfinite(settings.teleportDistance)&&settings.teleportDistance>0;}
public:
 CollisionSettings settings;
 void Suspend(){previous_=false;weapons_.clear();targets_.clear();struck_.clear();}
 std::vector<CombatContact> Update(uint64_t motion,int slot,float seconds,const WeaponCapsule& weapon,
   const std::vector<CollisionWindow>& windows,const std::vector<CombatTarget>& targets,Vec3 origin,
   float maximumRange,const SceneObstruction& obstruction={}){
  return Update(motion,slot,seconds,std::vector<WeaponCapsule>{weapon},windows,targets,origin,maximumRange,obstruction);
 }
 std::vector<CombatContact> Update(uint64_t motion,int slot,float seconds,const std::vector<WeaponCapsule>& weapons,
   const std::vector<CollisionWindow>& windows,const std::vector<CombatTarget>& targets,Vec3 origin,
   float maximumRange,const SceneObstruction& obstruction={}){
  using namespace CombatGeometry;std::vector<CombatContact> candidates;
  if(!motion||!std::isfinite(seconds)||seconds<0||!PointValid(origin)||!std::isfinite(maximumRange)||maximumRange<=0||!SettingsValid()||
    weapons.empty()||std::any_of(weapons.begin(),weapons.end(),[](const auto& w){return !Valid(w);})){Suspend();return candidates;}
  bool continuous=previous_&&motion==motion_&&slot==slot_&&seconds>=seconds_&&seconds-seconds_<=settings.maximumFrameGap&&Length(Sub(origin,origin_))<=settings.teleportDistance;
  std::set<uint32_t> lanes;
  for(const auto& weapon:weapons){
   if(!lanes.insert(weapon.lane).second){Suspend();return candidates;}
   const auto old=weapons_.find(weapon.lane);
   if(old!=weapons_.end()&&std::max(Length(Sub(weapon.base,old->second.base)),Length(Sub(weapon.tip,old->second.tip)))>settings.teleportDistance)continuous=false;
  }
  if(!continuous){struck_.clear();Seed(motion,slot,seconds,weapons,targets,origin);return candidates;}
  if(seconds==seconds_){Seed(motion,slot,seconds,weapons,targets,origin);return candidates;}
  const float duration=seconds-seconds_;
  for(size_t windowIndex=0;windowIndex<windows.size();++windowIndex){const auto& window=windows[windowIndex];
   if(!std::isfinite(window.begin)||!std::isfinite(window.end)||window.begin<0||window.end<=window.begin||
      !std::isfinite(window.multiplier)||window.multiplier<=0||window.end<=seconds_||window.begin>seconds)continue;
   const uint64_t strike=window.strike?window.strike:((uint64_t(1)<<63)|uint64_t(windowIndex+1));
   const float begin=std::max(seconds_,window.begin),end=std::min(seconds,window.end);
   if(end<begin)continue;
   const float clipFrom=(begin-seconds_)/duration,clipTo=(end-seconds_)/duration;
   const WeaponCapsule* weapon=nullptr;
   for(const auto& source:weapons)if(source.lane==window.lane){weapon=&source;break;}
   const auto oldWeapon=weapons_.find(window.lane);if(!weapon||oldWeapon==weapons_.end())continue;
   const auto w0=Interpolate(oldWeapon->second,*weapon,clipFrom),w1=Interpolate(oldWeapon->second,*weapon,clipTo);
   std::set<EntityKey> seen;
   for(const auto& target:targets){
    if(!target.key.id||!target.alive||!target.hostile||!seen.insert(target.key).second||struck_.contains({target.key,strike}))continue;
    const auto oldTarget=targets_.find(target.key);if(oldTarget==targets_.end())continue;
    std::optional<CombatContact> best;int bestPriority=0;std::set<uint32_t> parts;
    for(const auto& hurt:target.hurt){
     if(!Valid(hurt)||!parts.insert(hurt.part).second)continue;
     const HurtCapsule* oldHurt=nullptr;
     for(const auto& source:oldTarget->second.hurt)if(source.part==hurt.part&&Valid(source)){oldHurt=&source;break;}
     if(!oldHurt||std::max(Length(Sub(hurt.a,oldHurt->a)),Length(Sub(hurt.b,oldHurt->b)))>settings.teleportDistance)continue;
     const auto h0=Interpolate(*oldHurt,hurt,clipFrom),h1=Interpolate(*oldHurt,hurt,clipTo);
     const auto collision=SweepCapsules(w0,w1,h0,h1,settings.contactTolerance);if(!collision)continue;
     const float sampleTime=begin+(end-begin)*collision->fraction;
     // Source flags are half-open; touching only after their off marker is not a hit.
     if(sampleTime>=window.end&&end==window.end)continue;
     const float frameFraction=(sampleTime-seconds_)/duration;const auto atOrigin=Lerp(origin_,origin,frameFraction);
     if(Length(Sub(collision->targetPoint,atOrigin))>maximumRange)continue;
     const auto atWeapon=Interpolate(w0,w1,collision->fraction);
     if(TargetObstructed(atWeapon.base,collision->targetPoint,obstruction)||
        (settings.checkOriginOcclusion&&TargetObstructed(atOrigin,collision->targetPoint,obstruction)))continue;
     CombatContact contact{target.key,hurt.part,strike,window.lane,window.bank,window.node,windowIndex,
      sampleTime,frameFraction,window.multiplier,collision->targetPoint,collision->normal,target.approximateGeometry,hurt.weakPointKnown,hurt.weakPoint};
     if(!best||Better(contact.seconds,hurt.priority,hurt.part,best->seconds,bestPriority,best->part)){best=contact;bestPriority=hurt.priority;}
    }
    if(best)candidates.push_back(*best);
   }
  }
  std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return std::tie(a.seconds,a.key,a.strike,a.part)<std::tie(b.seconds,b.key,b.strike,b.part);});
  std::vector<CombatContact> contacts;contacts.reserve(candidates.size());
  for(const auto& contact:candidates)if(struck_.insert({contact.key,contact.strike}).second)contacts.push_back(contact);
  Seed(motion,slot,seconds,weapons,targets,origin);return contacts;
 }
};

// The scene wall and every hurt part compete by entry time. A projectile born
// outside a hurt capsule is never awarded a hit merely for aiming at its root.
inline std::optional<CombatContact> SweepProjectile(Vec3 from,Vec3 to,float radius,const std::vector<CombatTarget>& targets,
 const SceneObstruction& obstruction={},float maximumRange=std::numeric_limits<float>::infinity()){
 using namespace CombatGeometry;
 if(!PointValid(from)||!PointValid(to)||!std::isfinite(radius)||radius<0||radius>10000||std::isnan(maximumRange)||maximumRange<=0)return {};
 const float originalDistance=Length(Sub(to,from));float rangeFraction=1;
 if(originalDistance>maximumRange){rangeFraction=maximumRange/originalDistance;to=Lerp(from,to,rangeFraction);}
 const float distance=Length(Sub(to,from));
 const float wallTolerance=2e-4f/std::max(distance,1e-4f)+1e-6f;
 float blockedAt=std::numeric_limits<float>::infinity();
 if(obstruction){float fraction=1;if(obstruction(from,to,fraction))blockedAt=std::isfinite(fraction)?std::clamp(fraction,0.f,1.f):0;}
 std::optional<CombatContact> best;int bestPriority=0;std::set<EntityKey> seen;
 for(const auto& target:targets){
  if(!target.key.id||!target.alive||!target.hostile||!seen.insert(target.key).second)continue;
  std::set<uint32_t> parts;
  for(const auto& hurt:target.hurt){
   if(!Valid(hurt)||!parts.insert(hurt.part).second)continue;
   const auto collision=SweepCapsules({from,from,radius,0},{to,to,radius,0},hurt);if(!collision)continue;
   if(collision->fraction>=blockedAt-wallTolerance)continue;
   CombatContact contact;contact.key=target.key;contact.part=hurt.part;contact.fraction=collision->fraction*rangeFraction;
   contact.point=collision->targetPoint;contact.normal=collision->normal;contact.approximateGeometry=target.approximateGeometry;
   contact.weakPointKnown=hurt.weakPointKnown;contact.weakPoint=hurt.weakPoint;
   if(!best||Better(contact.fraction,hurt.priority,hurt.part,best->fraction,bestPriority,best->part)||
      (std::abs(contact.fraction-best->fraction)<=1e-5f&&hurt.priority==bestPriority&&hurt.part==best->part&&contact.key<best->key)){
    best=contact;bestPriority=hurt.priority;
   }
  }
 }
 return best;
}
} // namespace LS
