#include "../native/targeting.h"
#include "test_support.h"
using namespace LS;
namespace {
CombatTarget Target(uint64_t id,Vec3 point,uint32_t generation=1){CombatTarget t;t.key={id,generation};t.name="target";t.position=t.aimPoint=point;return t;}
void EligibilityAndStableSelection(){
 TargetLock lock;TargetingView view;std::vector<CombatTarget> targets{
  Target(1,{0,0,9}),Target(2,{1,0,4}),Target(3,{0,0,-1}),Target(4,{0,0,31}),
  Target(5,{0,0,1}),Target(6,{0,0,2}),Target(7,{0,0,3})};
 targets[4].alive=false;targets[5].hostile=false;targets[6].key.id=0;
 CHECK(lock.Acquire(targets,view));CHECK(lock.Selected()==std::optional<EntityKey>({2,1}));
 std::reverse(targets.begin(),targets.end());CHECK(lock.Acquire(targets,view));CHECK(lock.Selected()->id==2);
 CHECK(!lock.Camera());const auto focus=lock.Camera(true);CHECK(focus&&focus->key.id==2&&!focus->persistent);
 lock.settings.mode=TargetCameraMode::TargetCamera;CHECK(lock.Camera()&&lock.Camera()->persistent);
 const auto before=lock.Selected();view.forward={0,0,-1};lock.Update(.1f,targets,view);
 // Retained camera selection is not forced back into the acquisition FOV.
 CHECK(lock.Selected()==before);
 targets.erase(std::remove_if(targets.begin(),targets.end(),[](const auto& t){return t.key.id==2;}),targets.end());
 lock.Update(.1f,targets,view);CHECK(!lock.Selected()&&!lock.Camera(true));
 // No implicit switch to another enemy when the selected entity disappears.
 CHECK(!lock.Selected());
}
void CyclingAndGeneration(){
 TargetLock lock;TargetingView view;std::vector<CombatTarget> targets{Target(3,{4,0,8}),Target(1,{-4,0,8}),Target(2,{0,0,8})};
 CHECK(lock.Acquire(targets,view));CHECK(lock.Selected()->id==2);
 CHECK(lock.Cycle(1,targets,view));CHECK(lock.Selected()->id==3);
 CHECK(lock.Cycle(1,targets,view));CHECK(lock.Selected()->id==1);
 CHECK(lock.Cycle(-1,targets,view));CHECK(lock.Selected()->id==3);
 std::reverse(targets.begin(),targets.end());CHECK(lock.Cycle(-1,targets,view));CHECK(lock.Selected()->id==2);
 for(auto& t:targets)if(t.key.id==2)t.key.generation=2;
 lock.Update(.01f,targets,view);CHECK(!lock.Selected());
 CHECK(lock.Acquire(targets,view));CHECK(lock.Selected()==std::optional<EntityKey>({2,2}));
 for(auto& t:targets)if(t.key.id==2)t.alive=false;
 lock.Update(.01f,targets,view);CHECK(!lock.Selected());
}
void OcclusionRangeAndInvalidInputs(){
 TargetLock lock;TargetingView view;auto target=Target(1,{0,0,5});
 const SceneObstruction wall=[](Vec3,Vec3,float& fraction){fraction=.5f;return true;};
 CHECK(!lock.Acquire({target},view,wall));CHECK(lock.Acquire({target},view));
 lock.Update(.2f,{target},view,wall);CHECK(lock.Selected()&&Near(lock.OccludedFor(),.2f));
 lock.Update(.1f,{target},view);CHECK(lock.Selected()&&lock.OccludedFor()==0);
 lock.Update(.2f,{target},view,wall);lock.Update(.16f,{target},view,wall);CHECK(!lock.Selected());
 CHECK(lock.Acquire({target},view));target.position.z=35;target.aimPoint=target.position;
 lock.Update(.1f,{target},view);CHECK(lock.Selected());target.position.z=41;target.aimPoint=target.position;
 lock.Update(.1f,{target},view);CHECK(!lock.Selected());
 target=Target(1,{0,0,5});CHECK(lock.Acquire({target},view));
 lock.Update(-1,{target},view);CHECK(!lock.Selected());
 view.forward={};CHECK(!lock.Acquire({target},view));view.forward={0,0,1};
 target.aimPoint.x=std::numeric_limits<float>::quiet_NaN();CHECK(!lock.Acquire({target},view));
 target=Target(1,{0,0,5});lock.settings.releaseRange=1;CHECK(!lock.Acquire({target},view));
 lock.settings.releaseRange=40;lock.settings.fovDegrees=30;CHECK(!lock.Acquire({Target(2,{5,0,5})},view));
 const SceneObstruction bad=[](Vec3,Vec3,float& f){f=std::numeric_limits<float>::quiet_NaN();return true;};
 CHECK(!lock.Acquire({target},view,bad));
 const SceneObstruction endpoint=[](Vec3,Vec3,float& f){f=1;return true;};CHECK(lock.Acquire({target},view,endpoint));
 lock.Release();CHECK(!lock.Selected());
}
}
int main(){try{EligibilityAndStableSelection();CyclingAndGeneration();OcclusionRangeAndInvalidInputs();
 std::cout<<"Target selection: identity/generation, camera modes, deterministic cycling, eligibility and release passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
