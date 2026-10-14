#include "../native/combat_collision.h"
#include "test_support.h"
#include <random>
using namespace LS;
using CombatGeometry::SweepCapsules;
namespace {
CombatTarget Target(uint64_t id,Vec3 center,float radius=.1f,uint32_t generation=1){
 CombatTarget target;target.key={id,generation};target.position=target.aimPoint=center;target.name="target";
 target.hurt={{center,center,radius,0,0}};return target;
}
WeaponCapsule Blade(float x,uint32_t lane=0){return {{x,-1,0},{x,1,0},.025f,lane};}
void GeometryAndContinuousSweep(){
 using namespace CombatGeometry;
 CHECK(Near(ClosestSegments({0,0,0},{2,0,0},{1,1,0},{1,1,0}).distance,1));
 CHECK(Near(ClosestSegments({0,0,0},{2,0,0},{0,1,0},{2,1,0}).distance,1));
 CHECK(Near(ClosestSegments({-1,0,0},{1,0,0},{0,-1,0},{0,1,0}).distance,0));
 const HurtCapsule thin{{0,0,0},{0,0,0},.001f,7,0};
 const auto fast=SweepCapsules(Blade(-10),Blade(10),thin);CHECK(fast);
 CHECK(std::abs(fast->fraction-(10-.026f)/20)<.00002f);CHECK(Near(Length(fast->normal),1));
 CHECK(!SweepCapsules(Blade(-10),Blade(10),{{0,0,1},{0,0,1},.1f,0,0}));
 // A tetrahedral broadphase can overlap while the actual twisting blade misses.
 const WeaponCapsule twist0{{-1,0,0},{1,0,0},.005f},twist1{{0,-1,1},{0,1,1},.005f};
 CHECK(!SweepCapsules(twist0,twist1,{{.3f,-.3f,.5f},{.3f,-.3f,.5f},.005f}));
 CHECK(SweepCapsules(twist0,twist1,{{.3f,.3f,.5f},{.3f,.3f,.5f},.005f}));
 const HurtCapsule moving0{{-2,0,0},{-2,0,0},.1f},moving1{{2,0,0},{2,0,0},.1f};
 const auto moving=SweepCapsules(Blade(0),Blade(0),moving0,moving1);CHECK(moving&&moving->fraction>.46f&&moving->fraction<.48f);
 // Non-translating (turning) capsule uses both evolving endpoint trajectories.
 CHECK(SweepCapsules({{0,.5f,0},{0,.5f,0},.02f},{{0,.5f,0},{0,.5f,0},.02f},
  {{-2,0,0},{-2,1,0},.1f},{{2,1,0},{2,0,0},.1f}));
 CHECK(!SweepCapsules({{}, {},-1},Blade(0),thin));
 // Dense reference only asserts definite contacts; it cannot certify misses.
 std::mt19937 random(5489);std::uniform_real_distribution<float> distribution(-2,2);
 for(int n=0;n<120;++n){
  const WeaponCapsule a{{distribution(random),distribution(random),distribution(random)},{distribution(random),distribution(random),distribution(random)},.03f};
  const WeaponCapsule b{{distribution(random),distribution(random),distribution(random)},{distribution(random),distribution(random),distribution(random)},.03f};
  const HurtCapsule h{{distribution(random),distribution(random),distribution(random)},{distribution(random),distribution(random),distribution(random)},.12f};
  bool definitelyHit=false;
  for(int frame=0;frame<=2000&&!definitelyHit;++frame){const auto w=Interpolate(a,b,float(frame)/2000);
   definitelyHit=ClosestSegments(w.base,w.tip,h.a,h.b).distance<w.radius+h.radius-.001f;}
  const auto sweep=SweepCapsules(a,b,h);CHECK(!definitelyHit||sweep);
  if(sweep){const auto w=Interpolate(a,b,sweep->fraction);CHECK(ClosestSegments(w.base,w.tip,h.a,h.b).distance<=w.radius+h.radius+.00021f);}
 }
}
void WindowsDeduplicationAndParts(){
 CombatCollision collision;auto target=Target(1,{});target.approximateGeometry=true;
 target.hurt.push_back({{0,0,0},{0,0,0},.1f,7,10});
 const std::vector<CollisionWindow> windows{{.2f,.3f,2,90,0,2,42},{.7f,.9f,3,91,0,3,43}};
 CHECK(collision.Update(1,101,0,Blade(-1),windows,{target},{},5).empty());
 const auto contacts=collision.Update(1,101,.5f,Blade(1),windows,{target},{},5);
 CHECK(contacts.size()==1&&contacts[0].part==7&&contacts[0].strike==90&&contacts[0].bank==2&&contacts[0].node==42);
 CHECK(contacts[0].seconds>=.2f&&contacts[0].seconds<.3f&&contacts[0].multiplier==2&&contacts[0].approximateGeometry);
 CHECK(collision.Update(1,101,.55f,Blade(0),windows,{target},{},5).empty());
 CHECK(collision.Update(1,101,.8f,Blade(0),windows,{target},{},5).size()==1);
 CHECK(collision.Update(1,101,.85f,Blade(0),windows,{target},{},5).empty());
 // Replaying a new motion while already overlapping is seeded, then can hit.
 CHECK(collision.Update(2,101,.22f,Blade(0),windows,{target},{},5).empty());
 CHECK(collision.Update(2,101,.23f,Blade(0),windows,{target},{},5).size()==1);
 CHECK(collision.Update(2,101,.23f,Blade(0),windows,{target},{},5).empty());
 target.key.generation=2;
 CHECK(collision.Update(2,101,.24f,Blade(0),windows,{target},{},5).empty());
 CHECK(collision.Update(2,101,.25f,Blade(0),windows,{target},{},5).size()==1);
 // Distinct hit windows in one render interval stay distinct and time-sorted.
 collision.Suspend();const std::vector<CollisionWindow> two{{.4f,.5f,1,2},{.1f,.2f,1,1}};
 CHECK(collision.Update(3,10,0,Blade(0),two,{target},{},5).empty());
 const auto both=collision.Update(3,10,.6f,Blade(0),two,{target},{},5);CHECK(both.size()==2&&both[0].strike==1&&both[1].strike==2);
 // A shared pulse across blade and shield lanes is one strike per entity.
 collision.Suspend();const std::vector<CollisionWindow> shared{{0,1,1,50,0},{0,1,1,50,1}};
 collision.Update(4,10,0,std::vector<WeaponCapsule>{Blade(0),Blade(0,1)},shared,{target},{},5);
 CHECK(collision.Update(4,10,.1f,std::vector<WeaponCapsule>{Blade(0),Blade(0,1)},shared,{target},{},5).size()==1);
}
void ReadOnlyMovingTargetsAndDiscontinuities(){
 CombatCollision collision;const std::vector<CollisionWindow> windows{{0,2,1,1}};
 auto target=Target(1,{-2,0,0});collision.Update(1,1,0,Blade(0),windows,{target},{},5);
 target=Target(1,{2,0,0});CHECK(collision.Update(1,1,.1f,Blade(0),windows,{target},{},5).size()==1);
 collision.Suspend();target=Target(1,{});collision.Update(2,1,0,Blade(-1),windows,{target},{},5);
 const SceneObstruction wall=[](Vec3,Vec3,float& fraction){fraction=.5f;return true;};
 CHECK(collision.Update(2,1,.1f,Blade(1),windows,{target},{},5,wall).empty());
 collision.Suspend();collision.Update(3,1,0,Blade(-1),windows,{target},{3,0,0},1);
 CHECK(collision.Update(3,1,.1f,Blade(1),windows,{target},{3,0,0},1).empty());
 collision.Suspend();collision.settings.teleportDistance=2;collision.Update(4,1,0,Blade(-2),windows,{target},{},5);
 CHECK(collision.Update(4,1,.1f,Blade(2),windows,{target},{},5).empty());
 CHECK(collision.Update(4,1,.2f,Blade(0),windows,{target},{},5).size()==1);
 CHECK(collision.Update(4,1,.15f,Blade(0),windows,{target},{},5).empty());
 CHECK(collision.Update(4,1,.16f,Blade(0),windows,{target},{},5).size()==1);
 CHECK(collision.Update(4,1,.17f,Blade(0),windows,{target},{20,0,0},30).empty());
 collision.Suspend();collision.settings.maximumFrameGap=.2f;collision.Update(5,1,0,Blade(-1),windows,{target},{},5);
 CHECK(collision.Update(5,1,.5f,Blade(1),windows,{target},{},5).empty());
 target.alive=false;CHECK(collision.Update(5,1,.6f,Blade(0),windows,{target},{},5).empty());
 target.alive=true;CHECK(collision.Update(5,1,.7f,Blade(0),windows,{target},{},5).empty());
 CHECK(collision.Update(5,1,.8f,Blade(0),windows,{target},{},5).size()==1);
 CHECK(collision.Update(5,1,std::numeric_limits<float>::quiet_NaN(),Blade(0),windows,{target},{},5).empty());
 CHECK(collision.Update(5,1,.9f,Blade(0),windows,{target},{},5).empty());
}
void ProjectileEntryAndWalls(){
 const auto near=Target(1,{0,0,2},.1f),large=Target(2,{0,0,3},2);
 auto hit=SweepProjectile({0,0,0},{0,0,10},.025f,{near,large});
 CHECK(hit&&hit->key.id==2&&hit->fraction>.09f&&hit->fraction<.1f);
 const SceneObstruction before=[](Vec3,Vec3,float& f){f=.08f;return true;};
 CHECK(!SweepProjectile({0,0,0},{0,0,10},.025f,{near,large},before));
 const SceneObstruction same=[](Vec3,Vec3,float& f){f=.0975f;return true;};
 CHECK(!SweepProjectile({0,0,0},{0,0,10},.025f,{near,large},same));
 const SceneObstruction behind=[](Vec3,Vec3,float& f){f=.4f;return true;};
 CHECK(SweepProjectile({0,0,0},{0,0,10},.025f,{near,large},behind)->key.id==2);
 CHECK(!SweepProjectile({0,0,0},{0,0,10},.025f,{near},{},1));
 hit=SweepProjectile({0,0,0},{0,0,10},.025f,{near},{},3);CHECK(hit&&std::abs(hit->fraction-.1875f)<.00005f);
 auto dead=near;dead.alive=false;CHECK(!SweepProjectile({0,0,0},{0,0,10},.025f,{dead}));
 dead=near;dead.hostile=false;CHECK(!SweepProjectile({0,0,0},{0,0,10},.025f,{dead}));
 auto part=near;part.hurt.push_back({part.position,part.position,.1f,9,5});
 hit=SweepProjectile({0,0,0},{0,0,10},.025f,{part});CHECK(hit&&hit->part==9);
 CHECK(!SweepProjectile({0,0,0},{0,0,10},-1,{near}));
 CHECK(!SweepProjectile({0,0,0},{0,0,10},.1f,{near},{},0));
}
}
int main(){try{GeometryAndContinuousSweep();WindowsDeduplicationAndParts();ReadOnlyMovingTargetsAndDiscontinuities();ProjectileEntryAndWalls();
 std::cout<<"Combat geometry: 3D continuous weapon/monster sweeps, source windows, generation/strike dedup, part entry, range/walls and projectile TOI passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
