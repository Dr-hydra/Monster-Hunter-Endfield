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
 // Independent arrows retain one authored source window while contributing
 // separate stable contact identities to Practice and its contact observer.
 LS::Practice volley;volley.Refresh({{7,"volley target",{0,0,1},.2f,100,1}},100);std::vector<LS::PracticeHit> received;
 volley.acceptedHit=[&](const LS::PracticeHit& hit){received.push_back(hit);};LS::LocalProjectiles group;CHECK(group.CanLaunch(3));
 for(int i=0;i<3;++i)CHECK(group.Launch({},{0,0,1},30,10,true,42,5,7,.125f));
 const auto finalId=group.active.back().id;group.Update(.1f,volley,clear,[](const LS::LocalProjectile&,uint32_t){});
 CHECK(group.active.empty()&&received.size()==3&&volley.targets.at(7).hp==70);
 for(size_t i=0;i<received.size();++i){const auto& hit=received[i];CHECK(hit.contactSerial>0&&hit.motionSerial==42&&hit.slot==5&&hit.windowId==7&&Near(hit.seconds,.125f));
  for(size_t j=0;j<i;++j)CHECK(hit.contactSerial!=received[j].contactSerial);
  LS::CombatContact replay;replay.key={hit.targetId,hit.generation};replay.strike=8;replay.seconds=hit.seconds;replay.point=hit.point;replay.contactSerial=hit.contactSerial;
  CHECK(!volley.ApplyContact(replay,42,5,10));}
 CHECK(volley.targets.at(7).hp==70&&received.size()==3);group.Clear();CHECK(group.Launch({},{0,0,1},1,0,true));CHECK(group.active.front().id>finalId);group.Clear();
 CHECK(!group.CanLaunch(0)&&!group.CanLaunch(129)&&group.CanLaunch(128));
 for(int i=0;i<126;++i)CHECK(group.Launch({},{0,0,1},1,0,true));CHECK(group.CanLaunch(2)&&!group.CanLaunch(3)&&group.active.size()==126);
 CHECK(group.Launch({},{0,0,1},1,0,true)&&group.Launch({},{0,0,1},1,0,true));CHECK(!group.CanLaunch());group.Clear();
 // A large update must stop its actual sweep at source range, so a target
 // beyond the endpoint cannot be struck before expiry removes the arrow.
 LS::Practice ranged;ranged.Refresh({{9,"beyond range",{0,0,1.5f},.1f,100,1}},100);LS::LocalProjectiles limited;float maximumEnd=0;int beyondHits=0;
 const auto observe=[&](LS::Vec3,LS::Vec3 to,float&){maximumEnd=std::max(maximumEnd,to.z);return false;};
 const auto record=[&](const LS::LocalProjectile&,uint32_t){++beyondHits;};
 CHECK(limited.Launch({},{0,0,1},10,25,true,81,5,7,0,-1,.03f,1));CHECK(Near(limited.active.front().lifetime,.1f)&&Near(limited.active.front().radius,.03f));
 limited.Update(.07f,ranged,observe,record);CHECK(limited.active.size()==1&&Near(limited.active.front().position.z,.7f)&&Near(limited.active.front().traveled,.7f));
 limited.Update(.5f,ranged,observe,record);CHECK(limited.active.empty()&&Near(maximumEnd,1)&&beyondHits==0&&ranged.targets.at(9).hp==100);
 maximumEnd=0;CHECK(limited.Launch({},{0,0,1},10,25,true,82,5,7,0,-1,.03f,2));limited.active.front().lifetime=.05f;
 limited.Update(1,ranged,observe,record);CHECK(limited.active.empty()&&Near(maximumEnd,.5f)&&beyondHits==0);
 CHECK(limited.Launch({5,0,0},{0,0,1},1,0,true,83,5,7,0,-1,.03f,5));
 for(int i=0;i<3;++i)limited.Update(1,ranged,clear,record);CHECK(limited.active.size()==1&&Near(limited.active.front().age,3)&&Near(limited.active.front().lifetime,5));
 for(int i=0;i<2;++i)limited.Update(1,ranged,clear,record);CHECK(limited.active.empty());
 CHECK(limited.Launch({},{0,0,1},10,0,true));CHECK(Near(limited.active.front().lifetime,3)&&Near(limited.active.front().range,30));limited.Clear();
 CHECK(!limited.Launch({},{0,0,1},10,1,true,0,0,0,0,-1,-.01f,1));CHECK(!limited.Launch({},{0,0,1},10,1,true,0,0,0,0,-1,.03f,-1));
 CHECK(!limited.Launch({},{0,0,1},10,1,true,0,0,0,0,-1,.03f,std::numeric_limits<float>::infinity()));
 std::cout<<"Independent projectiles: original source window, per-arrow contacts, group capacity, range/lifetime clipping, scene blocker and invalid input passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
