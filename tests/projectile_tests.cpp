#include "../native/projectiles.h"
#include "test_support.h"
#include <iostream>
int main(){try{
 LS::Practice practice;practice.Refresh({{1,"near",{0,0,1},.2f,100,1},{2,"far",{0,0,2},.2f,100,2}},100);
 LS::LocalProjectiles arrows;CHECK(arrows.Launch({0,0,0},{0,0,1},30,25,true));int contacts=0;
 const auto clear=[](LS::Vec3,LS::Vec3,float&){return false;};
 arrows.Update(.1f,practice,clear,[&](const LS::LocalProjectile&,uint32_t id){CHECK(id==1);++contacts;});
 CHECK(contacts==1&&arrows.active.empty()&&practice.targets.at(1).hp==75&&practice.targets.at(2).hp==100);
 CHECK(arrows.Launch({0,0,0},{0,0,1},30,25,false));
 arrows.Update(.1f,practice,[](LS::Vec3,LS::Vec3,float& fraction){fraction=.1f;return true;},[&](const LS::LocalProjectile&,uint32_t){++contacts;});
 CHECK(contacts==1&&arrows.active.empty());
 CHECK(!arrows.Launch({},{},30,25,true));CHECK(!arrows.Launch({}, {0,0,1},std::numeric_limits<float>::quiet_NaN(),25,true));
 for(int i=0;i<128;++i)CHECK(arrows.Launch({5,0,0},{0,0,1},1,10,true));CHECK(!arrows.Launch({}, {0,0,1},1,10,true));
 for(int i=0;i<4;++i)arrows.Update(1,practice,clear,[](const LS::LocalProjectile&,uint32_t){});CHECK(arrows.active.empty());
 CHECK(Throws([&]{arrows.Update(-1,practice,clear,[](const LS::LocalProjectile&,uint32_t){});}));arrows.Clear();
 std::cout<<"Independent projectiles: nearest local target, scene blocker, lifetime, capacity and invalid input passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
