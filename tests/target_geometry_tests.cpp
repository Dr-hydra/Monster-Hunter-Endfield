#include "../native/target_geometry.h"
#include "../native/combat_collision.h"
#include "test_support.h"
using namespace LS;
int main(){try{
 const Vec3 center{4,6,8},extents{.5f,2,.75f};const auto capsule=BoundsHurtCapsule(center,extents,7);
 CHECK(capsule&&capsule->part==7&&Near(capsule->a.y,4)&&Near(capsule->b.y,8));
 CHECK(Near(capsule->radius,std::sqrt(.25f+.5625f)));
 for(float x:{-1.f,1.f})for(float y:{-1.f,1.f})for(float z:{-1.f,1.f}){
  const Vec3 corner{center.x+x*extents.x,center.y+y*extents.y,center.z+z*extents.z};
  CHECK(CombatGeometry::ClosestSegments(corner,corner,capsule->a,capsule->b).distance<=capsule->radius+.00001f);
 }
 CHECK(!BoundsHurtCapsule({},{}));CHECK(!BoundsHurtCapsule({}, {1,-1,1}));CHECK(!BoundsHurtCapsule({}, {1,1,41}));
 CHECK(!BoundsHurtCapsule({std::numeric_limits<float>::quiet_NaN(),0,0},{1,1,1}));
 TargetIdentities ids;ids.Begin();CHECK(ids.Observe(1,100)==1);CHECK(ids.Observe(1,100)==1);
 CHECK(ids.Observe(0,100)==0&&ids.Observe(2,0)==0);ids.End();
 ids.Begin();CHECK(ids.Observe(1,100)==1);ids.End();
 ids.Begin();CHECK(ids.Observe(1,101)==2);ids.End();
 ids.Begin();ids.End();ids.Begin();CHECK(ids.Observe(1,101)==3);ids.End();
 ids.Begin();CHECK(ids.Observe(1,101,10000,2)==4);ids.End();
 ids.Begin();CHECK(ids.Observe(1,101,10000,2)==4);ids.End();
 ids.Begin();CHECK(ids.Observe(1,101,10000,3)==5);ids.End();
 ids.Begin();CHECK(ids.Observe(1,101,10001,3)==6);ids.End();
 // Clearing the live cache must not reuse an older identity: delayed effects
 // and snapshots can still refer to a previous incarnation of this ID.
 ids.Clear();ids.Begin();CHECK(ids.Observe(1,101,10001,3)>6);ids.End();
 std::cout<<"Target geometry: approximate renderer box coverage, invalid bounds and generation/pointer/server reincarnation passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
