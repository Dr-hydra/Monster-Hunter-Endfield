#include "../native/practice.h"
#include "test_support.h"
using namespace LS;
int main(){try{
 Practice practice;practice.windows[100]={{.2f,.5f,1},{.7f,.9f,2}};
 PracticeTarget snapshot{1,"target",{0,0,0},.1f,777,1};practice.Refresh({snapshot},1000);CHECK(practice.targets.at(1).hp==1000&&snapshot.hp==777);
 practice.Update(1,100,.1f,{-1,-1,0},{-1,1,0},100);CHECK(practice.hits==0);
 // Fast blade sweep crosses a target between render frames.
 practice.Update(1,100,.4f,{1,-1,0},{1,1,0},100);CHECK(practice.hits==1&&practice.targets.at(1).hp==900);
 practice.Update(1,100,.45f,{0,-1,0},{0,1,0},100);CHECK(practice.hits==1);
 practice.Refresh({snapshot},1000);CHECK(practice.targets.at(1).hp==900);
 practice.Update(1,100,.8f,{0,-1,0},{0,1,0},100);CHECK(practice.hits==2&&practice.targets.at(1).hp==700&&practice.total==300);
 practice.Update(2,100,.3f,{0,-1,0},{0,1,0},100);CHECK(practice.hits==3&&practice.targets.at(1).hp==600);
 // Suspension cannot sweep a blade from its stale previous location.
 practice.Suspend();practice.Update(3,100,.3f,{2,-1,0},{2,1,0},100);CHECK(practice.hits==3);
 practice.Update(4,100,.3f,{0,-1,0},{0,1,0},9000);CHECK(practice.targets.at(1).hp==0&&practice.lastDamage==600&&practice.total==1000);
 practice.Update(5,100,.3f,{0,-1,0},{0,1,0},100);CHECK(practice.hits==4);practice.Refresh({snapshot},1000);CHECK(practice.targets.at(1).hp==0);
 CHECK(Throws([&]{practice.Refresh({snapshot},-1);}));practice.Clear();CHECK(practice.targets.empty()&&practice.hits==0&&practice.total==0&&practice.lastTarget.empty());
 CHECK(Near(SegmentDistance({1,1,0},{0,0,0},{2,0,0}),1));
 std::cout<<"practice: independent HP, swept blade, per-window deduplication and stop passed\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
