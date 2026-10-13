#include "../native/character_binding.h"
#include <iostream>
#include <stdexcept>
using namespace LS::CharacterBinding;
void Require(bool result,const char* text){if(!result)throw std::runtime_error(text);}
template<class F> void Reject(F f,const char* text){bool rejected=false;try{f();}catch(const std::runtime_error&){rejected=true;}Require(rejected,text);}
int main(){
 try {
  // The soldier appears first in traversal. Palette identity and complete
  // coverage, then the shallow authored main root, determine the result.
  std::vector<SkeletonCandidate> roots{{"Soldier_Root/Rush01/P_wpn/Root/Bip001",5,21,true},
   {"Root/Bip001",2,21,true},{"Weapon/Root/Bip001",3,21,false}};
  Require(SelectPrimarySkeleton(roots,"Root/Bip001")==1,"main body lost to first same-name soldier");
  Require(SelectPrimarySkeleton(roots)==1,"unconfigured shallow complete body selection");
  roots.push_back({"Other/Root/Bip001",2,21,true});
  Reject([&]{SelectPrimarySkeleton(roots);},"ambiguous roots must fail");
  Require(SelectPrimarySkeleton(roots,"Other/Root/Bip001")==3,"explicit complete palette path");
  Reject([&]{SelectPrimarySkeleton({{"Root/Bip001",2,20,true}});},"incomplete palette");
  Reject([&]{SelectPrimarySkeleton({{"Root/Bip001",2,21,false}});},"unskinned helper skeleton");
  Reject([&]{SelectPrimarySkeleton({});},"missing body skeleton");
  std::cout<<"Character skeleton identity tests passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
