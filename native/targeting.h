#pragma once
#include "retarget.h"
#include <functional>
#include <optional>
#include <tuple>

namespace LS {
// A uid alone is not an identity: destroyed/spawned entities can reuse it.
// The read-only snapshot producer owns generation and increments it on reuse.
struct EntityKey {
 uint64_t id=0;uint32_t generation=0;
 bool operator==(const EntityKey&)const=default;
 bool operator<(const EntityKey& other)const{return std::tie(id,generation)<std::tie(other.id,other.generation);}
};
struct HurtCapsule {
 Vec3 a{},b{};float radius=0;uint32_t part=0;int priority=0;
 bool weakPointKnown=false,weakPoint=false;
};
struct CombatTarget {
 EntityKey key{};std::string name;Vec3 position{},aimPoint{};
 bool alive=true,hostile=true,approximateGeometry=false;
 std::vector<HurtCapsule> hurt;
};
// Return the first scene obstruction fraction on [from,to]. An empty function
// is a caller-declared clear scene. No entity/HP/network handle lives here.
using SceneObstruction=std::function<bool(Vec3 from,Vec3 to,float& fraction)>;
inline bool TargetObstructed(Vec3 from,Vec3 to,const SceneObstruction& obstruction){
 if(!obstruction)return false;
 float fraction=1;
 if(!obstruction(from,to,fraction))return false;
 return !std::isfinite(fraction)||fraction<1.f-1e-5f;
}
enum class TargetCameraMode {FocusCamera,TargetCamera};
struct TargetLockSettings {
 TargetCameraMode mode=TargetCameraMode::FocusCamera;
 float acquireRange=30,releaseRange=40,fovDegrees=120,occlusionGrace=.35f;
};
struct TargetingView {
 Vec3 origin{},cameraPosition{},forward{0,0,1},up{0,1,0};
};
struct CameraFocus {
 EntityKey key{};Vec3 point{};bool persistent=false;
};
// This selects a camera subject. It intentionally has no actor-facing output:
// Monster Hunter camera selection must not turn a melee swing into homing.
class TargetLock {
 std::optional<EntityKey> selected_;Vec3 selectedPoint_{};float occludedFor_=0;
 struct Candidate {const CombatTarget* target;float score,yaw,distance;};
 bool SettingsValid()const{
  return std::isfinite(settings.acquireRange)&&settings.acquireRange>0&&
   std::isfinite(settings.releaseRange)&&settings.releaseRange>=settings.acquireRange&&
   std::isfinite(settings.fovDegrees)&&settings.fovDegrees>0&&settings.fovDegrees<=360&&
   std::isfinite(settings.occlusionGrace)&&settings.occlusionGrace>=0;
 }
 static bool ViewValid(const TargetingView& view){
  using namespace Retarget;
  return Finite(view.origin)&&Finite(view.cameraPosition)&&Finite(view.forward)&&Finite(view.up)&&
   Len(view.forward)>1e-6f&&Len(Cross(view.up,view.forward))>1e-6f;
 }
 static bool TargetValid(const CombatTarget& target){
  return target.key.id&&target.alive&&target.hostile&&Finite(target.position)&&Finite(target.aimPoint);
 }
 std::vector<Candidate> Candidates(const std::vector<CombatTarget>& targets,const TargetingView& view,
                                    const SceneObstruction& obstruction)const{
  using namespace Retarget;std::vector<Candidate> out;
  if(!SettingsValid()||!ViewValid(view))return out;
  const auto forward=Normalize(view.forward),right=Normalize(Cross(view.up,forward));
  constexpr float pi=3.14159265358979323846f;
  const float minimumDot=std::cos(settings.fovDegrees*pi/360.f);
  std::set<EntityKey> seen;
  for(const auto& target:targets){
   if(!TargetValid(target)||!seen.insert(target.key).second)continue;
   const float distance=Len(Sub(target.position,view.origin));
   if(distance>settings.acquireRange)continue;
   const auto relative=Sub(target.aimPoint,view.cameraPosition);const float cameraDistance=Len(relative);
   const float dot=cameraDistance>1e-6f?Dot(Scale(relative,1/cameraDistance),forward):1.f;
   if(dot<minimumDot-1e-6f||TargetObstructed(view.cameraPosition,target.aimPoint,obstruction))continue;
   out.push_back({&target,2*(1-dot)+distance/settings.acquireRange,
                  std::atan2(Dot(relative,right),Dot(relative,forward)),distance});
  }
  return out;
 }
 void Select(const Candidate& candidate){selected_=candidate.target->key;selectedPoint_=candidate.target->aimPoint;occludedFor_=0;}
public:
 TargetLockSettings settings;
 void Release(){selected_.reset();selectedPoint_={};occludedFor_=0;}
 std::optional<EntityKey> Selected()const{return selected_;}
 float OccludedFor()const{return occludedFor_;}
 bool Acquire(const std::vector<CombatTarget>& targets,const TargetingView& view,const SceneObstruction& obstruction={}){
  auto candidates=Candidates(targets,view,obstruction);
  if(candidates.empty()){Release();return false;}
  std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){
   return std::tie(a.score,a.distance,a.target->key)<std::tie(b.score,b.distance,b.target->key);
  });
  Select(candidates.front());return true;
 }
 bool Cycle(int direction,const std::vector<CombatTarget>& targets,const TargetingView& view,
            const SceneObstruction& obstruction={}){
  auto candidates=Candidates(targets,view,obstruction);
  if(candidates.empty()){Release();return false;}
  std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){
   return std::tie(a.yaw,a.distance,a.target->key)<std::tie(b.yaw,b.distance,b.target->key);
  });
  size_t next=direction<0?candidates.size()-1:0;
  for(size_t i=0;i<candidates.size();++i)if(selected_&&candidates[i].target->key==*selected_){
   next=direction<0?(i+candidates.size()-1)%candidates.size():(i+1)%candidates.size();break;
  }
  Select(candidates[next]);return true;
 }
 void Update(float dt,const std::vector<CombatTarget>& targets,const TargetingView& view,
             const SceneObstruction& obstruction={}){
  using namespace Retarget;
  if(!selected_)return;
  if(!std::isfinite(dt)||dt<0||!SettingsValid()||!ViewValid(view)){Release();return;}
  const CombatTarget* found=nullptr;
  for(const auto& target:targets)if(target.key==*selected_){found=&target;break;}
  if(!found||!TargetValid(*found)||Len(Sub(found->position,view.origin))>settings.releaseRange){Release();return;}
  selectedPoint_=found->aimPoint;
  if(TargetObstructed(view.cameraPosition,selectedPoint_,obstruction)){
   occludedFor_+=dt;if(occludedFor_>settings.occlusionGrace+1e-6f)Release();
  }else occludedFor_=0;
 }
 std::optional<CameraFocus> Camera(bool focusPressed=false)const{
  if(!selected_||(settings.mode==TargetCameraMode::FocusCamera&&!focusPressed))return {};
  return CameraFocus{*selected_,selectedPoint_,settings.mode==TargetCameraMode::TargetCamera};
 }
};
} // namespace LS
