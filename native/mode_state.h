#pragma once
#include "core.h"

namespace LS {
// Deliberately local practice rules; neither native combat fields nor a
// networking interface is part of this object. Not a replay of MHW damage.
class ModeState {
public:
 std::string mode="longsword";
 float energy=0,charge=0;int level=0,ammo=6;uint64_t blocks=0;
 std::string feedback;
 void Reset(const std::string& value){mode=value;energy=mode=="longsword"?0.f:100.f;charge=0;level=0;ammo=6;blocks=0;feedback.clear();lastMotion_=0;promoted_=false;pulse_=0;}
 std::string Label()const{return mode=="longsword"?"本地气刃槽":mode=="light_bowgun"?"本地弹匣":mode=="greatsword"?"本地蓄力":"本地耐力";}
 float Value()const{return mode=="light_bowgun"?float(ammo):mode=="greatsword"?charge:energy;}
 float Maximum()const{return mode=="light_bowgun"?6.f:mode=="greatsword"?3.f:100.f;}
 void Tick(float dt,const Controller& c){
  if(!std::isfinite(dt)||dt<0||dt>1)throw std::runtime_error("Invalid mode-state timestep");
  charge=std::clamp(c.heldSeconds()/1.85f*3,0.f,3.f);
  if(lastMotion_!=c.motionSerial){lastMotion_=c.motionSerial;promoted_=false;
   if(In(c,Command::Reload))ammo=6;
  }
  if(mode=="longsword")energy=std::max(0.f,energy-dt*.4f);
  else if(mode!="light_bowgun"&&mode!="greatsword"){
   const bool consuming=c.holding(Command::Guard)||c.holding(Command::Charge);
   energy=std::clamp(energy+dt*(consuming?-12.f:18.f),0.f,100.f);
  }
 }
 bool ReleaseProjectile(){
  if(mode=="light_bowgun"){if(ammo<=0){feedback="本地弹匣空，请装填";return false;}--ammo;}
  if(mode=="bow")energy=std::max(0.f,energy-10);
  return true;
 }
 void Hit(const Controller& c){
  feedback="本地模拟命中";
  if(mode=="longsword"){
   energy=std::min(100.f,energy+10);
   auto spirit=c.moves.find(Command::Spirit);
   if(!promoted_&&spirit!=c.moves.end()&&!spirit->second.empty()&&c.slot==spirit->second.back()){level=std::min(3,level+1);promoted_=true;}
  }
 }
 // Dummy attacks are explicit periodic simulated events. They do not read or
 // block incoming damage to the player's real character.
 std::string DummyAttack(float dt,const Controller& c,bool nearby){
  if(!nearby){pulse_=0;return {};}
  pulse_+=dt;if(pulse_<4)return {};pulse_=std::fmod(pulse_,4.f);
  if(c.holding(Command::Guard)){++blocks;feedback="练习目标：模拟格挡成功";return "guard";}
  if(In(c,Command::Foresight)||In(c,Command::Counter)||In(c,Command::IaiSpirit)){
   ++blocks;feedback="练习目标：模拟反击成功";return "counter";
  }
  feedback="练习目标：模拟攻击（不扣真实血量）";return {};
 }
 bool In(const Controller& c,Command command)const{auto i=c.moves.find(command);return i!=c.moves.end()&&std::find(i->second.begin(),i->second.end(),c.slot)!=i->second.end();}
private:
 uint64_t lastMotion_=0;bool promoted_=false;float pulse_=0;
};
} // namespace LS
