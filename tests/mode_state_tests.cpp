#include "../native/mode_state.h"
#include "test_support.h"
#include <iostream>
int main(){try{
 using namespace LS;ModeState state;Controller c;c.active=true;c.moves={{Command::Spirit,{104,106,108,109}},{Command::Reload,{102}},{Command::Foresight,{140}}};
 state.Reset("longsword");c.slot=109;c.motionSerial=1;state.Tick(.1f,c);state.Hit(c);state.Hit(c);CHECK(state.level==1&&state.energy==20);
 c.motionSerial=2;state.Tick(.1f,c);state.Hit(c);CHECK(state.level==2);state.Tick(1,c);CHECK(state.energy<30);
 state.Reset("light_bowgun");for(int i=0;i<6;++i)CHECK(state.ReleaseProjectile());CHECK(!state.ReleaseProjectile()&&state.ammo==0);
 c.slot=102;++c.motionSerial;state.Tick(.1f,c);CHECK(state.ammo==6&&state.Maximum()==6);
 state.Reset("bow");CHECK(state.ReleaseProjectile()&&state.energy==90);state.Tick(.1f,c);CHECK(state.energy>90);
 state.Reset("lance");c.slot=140;std::string event;for(int i=0;i<41;++i){auto next=state.DummyAttack(.1f,c,true);if(!next.empty())event=next;}CHECK(event=="counter"&&state.blocks==1);
 for(int i=0;i<1000;++i){state.Tick(.1f,c);CHECK(state.Value()>=0&&state.Value()<=state.Maximum());}
 CHECK(Throws([&]{state.Tick(-1,c);}));std::cout<<"local resources, ammo, promotion once per motion and independent simulated counter passed\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
