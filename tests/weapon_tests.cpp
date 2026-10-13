#include "../native/weapon_pose.h"
#include "test_support.h"
using namespace LS;
int main(){try{
 const auto plus=WeaponPose::InferAxis({0,-.1f,0},{0,1.5f,0},{0,0,0},.08f);CHECK(plus.length>1.4f&&Near(plus.axis.y,1));
 const auto minus=WeaponPose::InferAxis({0,-1.5f,0},{0,.1f,0},{0,0,0},.08f);CHECK(Near(minus.axis.y,-1));
 CHECK(WeaponPose::InferAxis({-1,0,0},{1,0,0},{0,0,0},.1f).length==0);CHECK(WeaponPose::InferAxis({0,0,0},{0,.1f,0},{0,0,0},1).length==0);
 WeaponPose::SourceTrack track{60,{{{}, {0,0,0},0},{{}, {1,0,0},1}}};CHECK(Near(track.Sample(.5f/60).supportOffset.x,.5f));CHECK(Near(track.Sample(10).supportWeight,1));
 WeaponPose::SourceBlend blend;auto first=blend.Sample(1,0,.1f,track.frames[0]);CHECK(first.supportWeight==0);
 auto next=blend.Sample(2,.05f,.1f,track.frames[1]);CHECK(Near(next.supportWeight,.5f));next=blend.Sample(2,.1f,.1f,track.frames[1]);CHECK(Near(next.supportWeight,1));
 CHECK(Throws([&]{track.Sample(std::numeric_limits<float>::infinity());}));blend.Reset();CHECK(Near(blend.Sample(3,0,.1f,track.frames[1]).supportWeight,1));
 std::cout<<"weapon: measured local axis and hand-only source tracks passed\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
