#pragma once
#include "practice.h"
#include <functional>

namespace LS {
struct LocalProjectile{uint64_t id=0;Vec3 position{},previous{},velocity{};float age=0,lifetime=3,damage=0;bool bow=false;};
// Independent presentation/near-field training only. No entity, skill or
// networking handle exists in this type; a scene blocker is read-only.
class LocalProjectiles{
 uint64_t next_=0;
public:
 std::vector<LocalProjectile> active;
 void Clear(){active.clear();}
 bool Launch(Vec3 origin,Vec3 direction,float speed,float damage,bool bow){
  if(!Finite(origin)||!Finite(direction)||!std::isfinite(speed)||speed<=0||speed>200||!std::isfinite(damage)||damage<0||Retarget::Len(direction)<1e-5f||active.size()>=128)return false;
  active.push_back({++next_,origin,origin,Retarget::Scale(Retarget::Normalize(direction),speed),0,3,damage,bow});return true;
 }
 template<class Block,class Hit>void Update(float dt,Practice& practice,Block blocked,Hit hit){
  if(!std::isfinite(dt)||dt<0||dt>1)throw std::runtime_error("Invalid local projectile timestep");
  for(auto it=active.begin();it!=active.end();){auto& projectile=*it;const auto from=projectile.position;
   const auto to=Retarget::Add(from,Retarget::Scale(projectile.velocity,dt));projectile.previous=from;
   float fraction=1;bool obstruction=blocked(from,to,fraction);fraction=std::clamp(fraction,0.f,1.f);
   const auto end=Lerp(from,to,fraction);float nearest=std::numeric_limits<float>::infinity();uint32_t target=0;
   const auto segment=Retarget::Sub(end,from);const float lengthSquared=Retarget::Dot(segment,segment);
   for(const auto&[id,t]:practice.targets){if(t.hp<=0||SegmentDistance(t.position,from,end)>t.radius)continue;
    const auto along=lengthSquared>1e-9f?std::clamp(Retarget::Dot(Retarget::Sub(t.position,from),segment)/lengthSquared,0.f,1.f):0;
    if(along<nearest){nearest=along;target=id;}}
   if(target){if(practice.Strike(target,projectile.damage))hit(projectile,target);it=active.erase(it);continue;}
   projectile.position=end;projectile.age+=dt;
   if(obstruction||projectile.age>=projectile.lifetime)it=active.erase(it);else ++it;
  }
 }
};
} // namespace LS
