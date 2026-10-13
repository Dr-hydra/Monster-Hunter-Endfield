#include "../native/retarget.h"
#include "../native/grip.h"
#include "../native/grounding.h"
#include "test_support.h"
#include <numbers>
using namespace LS;
int main(int argc,char** argv){try{
 CHECK(argc==2);
 using namespace Retarget;
 auto rotation=FromTo({1,0,0},{0,1,0});auto turned=Rotate(rotation,{1,0,0});CHECK(Near(turned.y,1)&&Near(turned.x,0));
 auto opposite=Rotate(FromTo({1,0,0},{-1,0,0}),{1,0,0});CHECK(Near(opposite.x,-1));CHECK(Throws([]{FromTo({},{1,0,0});}));
 const auto basis=SourceToTargetBodyBasis({1,0,0},{0,1,0},{0,0,-1},{0,1,0});CHECK(Near(Rotate(basis,{1,0,0}).z,-1));
 const auto targetRest=Quat{0,0,std::sin(-.4f),std::cos(-.4f)};
 const auto aligned=AlignedBoneRest(targetRest,Rotate(targetRest,{1,0,0}),{1,0,0},{});CHECK(Near(Rotate(aligned,{1,0,0}).x,1));
 const auto hand=CarryRestAlignment(aligned,targetRest,targetRest);CHECK(std::abs(LS::Dot(hand,Quat{}))>.99999f);
 const auto targetPalm=PalmBasis({0,1,0},{1,0,0}),sourcePalm=PalmBasis({1,0,0},{0,-1,0});
 const auto palmAligned=AlignedPalmRest({},sourcePalm,targetPalm,{});CHECK(Near(Rotate(palmAligned,{0,1,0}).x,1));
 const auto delta=FromTo({0,1,0},{1,0,0});const auto mapped=ApplySourceDelta(delta,{},basis);
 CHECK(Len(Sub(Rotate(mapped,Rotate(basis,{0,1,0})),Rotate(basis,{1,0,0})))<1e-4f);
 auto arm=SolveArm({0,0,0},{1,0,0},{.5f,.5f,0},.8f,.6f,{0,1,0});CHECK(Near(Len(arm.elbow),.8f));CHECK(Near(Len(Sub(arm.hand,arm.elbow)),.6f));CHECK(Near(arm.hand.x,1));CHECK(arm.elbow.y>0);
 arm=SolveArm({}, {8,0,0}, {.5f,.5f,0},.8f,.6f,{0,1,0});CHECK(arm.clamped&&arm.hand.x<1.4f);CHECK(Near(Len(Sub(arm.hand,arm.elbow)),.6f));
 arm=SolveArm({}, {}, {.5f,0,0},.8f,.6f,{1,0,0});CHECK(Finite(arm.elbow)&&Finite(arm.hand));CHECK(Near(Len(arm.elbow),.8f));
 CHECK(Walkable({{0,.2f,0},{0,1,0},.45f},0));CHECK(!Walkable({{0,0,0},{1,0,0},.3f},0));CHECK(!Walkable({{0,-2,0},{0,1,0},1},0));
 const std::array<Vec3,4> feet{{{0,.1f,0},{0,.2f,0},{1,.3f,0},{1,.2f,0}}};const std::array<float,4> floor{0,0,.1f,.1f};
 CHECK(Near(FloorCorrection(feet,floor,.04f,0),-.06f));CHECK(Near(FloorCorrection(feet,floor,.04f,.7f),.64f));
 GroundTrack ground{60,{0,1,2}};CHECK(Near(ground.Sample(.5f/60),.5f));CHECK(Near(ground.Sample(9),2));CHECK(Throws([&]{ground.Sample(std::numeric_limits<float>::quiet_NaN());}));
 GripTrack grip{60,{{{0,0,0},0},{{1,0,0},1}}};CHECK(Near(grip.Sample(.5f/60).weight,.5f));CHECK(Near(grip.Sample(.5f/60).offset.x,.5f));
 const auto fixtures=std::filesystem::path(argv[1])/"build/runtime-restore/grip-fixtures";std::filesystem::create_directories(fixtures);const auto path=fixtures/"calibrated.lsg";
 {std::ofstream file(path,std::ios::binary);file.write("LSG1",4);const uint32_t dimensions[]{2,60,4};file.write(reinterpret_cast<const char*>(dimensions),12);const float frames[]{.1f,.2f,.3f,0,.4f,.5f,.6f,1};file.write(reinterpret_cast<const char*>(frames),32);}
 const auto loaded=ReadGrip(path,2);CHECK(Near(loaded.Sample(.5f/60).offset.y,.35f));CHECK(Throws([&]{ReadGrip(path,3);}));
 {std::ofstream file(path,std::ios::binary|std::ios::app);file.put('x');}CHECK(Throws([&]{ReadGrip(path,2);}));
 std::cout<<"retarget: anatomical frames, world deltas, no-stretch IK, floor clearance passed\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
