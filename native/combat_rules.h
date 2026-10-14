#pragma once
#include <cmath>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace LS {
enum class CombatEventKind { OutgoingHit, IncomingAttack, Miss };
enum class CombatTransition { None, Helm, Counter, WeakFinish, PerfectRush };
// Identity/generation and attack serial are supplied by the local contact
// detector. This common message contains no native entity or network handle.
struct CombatEvent {
 CombatEventKind kind=CombatEventKind::OutgoingHit;
 uint64_t motionSerial=0,attackSerial=0;
 uint64_t contactSerial=0;
 int slot=0;
 float seconds=0,damage=0;
 uint32_t targetId=0,generation=0,partId=0;
 size_t windowId=0;
 bool guardable=true,fromFront=true,dodgeable=true;
 // Set only by an explicitly marked local part or verified native hitbox data.
 // An enclosing renderer/capsule is never evidence of a weak point.
 bool weakPointKnown=false,weakPoint=false;
};
struct CombatOutcome {
 bool accepted=false,hit=false,blocked=false,counter=false,evaded=false;
 bool sourceWindowKnown=false,counterReady=false;
 int followupSlot=0;
 CombatTransition transition=CombatTransition::None;
 float damageMultiplier=1;
 float startSeconds=0;
 std::string cue;
};
struct CombatTimeWindow {
 float begin=0,end=0;
 bool Contains(float seconds)const{return std::isfinite(seconds)&&seconds>=begin&&seconds<end;}
 bool Valid()const{return std::isfinite(begin)&&std::isfinite(end)&&begin>=0&&end>begin;}
};
struct TimedInputBranch {
 std::string command;
 CombatTimeWindow window;
 int timedSlot=0,normalSlot=0;
 float inputGateBegin=-1,inputGateEnd=-1;
 bool HasInputGate()const{return inputGateBegin>=0;}
 bool InputGateContains(float seconds)const{return HasInputGate()&&CombatTimeWindow{inputGateBegin,inputGateEnd}.Contains(seconds);}
};
struct ProjectileTier {
 int arrows=1,shellId=0;
 float motionValue=0,speed=0,range=0,radius=0;
};
struct MotionCombatRule {
 std::vector<CombatTimeWindow> outgoing,defensive;
 // These roles are facts from Hunter's Notes/COL. Numeric resource amounts
 // and unverified defensive frame windows belong to teaching parameters.
 bool spiritBlade=false,roundslash=false,foresight=false,iai=false,iaiSpirit=false;
 bool helmThrust=false,helmFinish=false,guard=false,lanceCounter=false,tackle=false;
 bool reload=false,continuousReload=false,projectile=false;
 int counterShortSlot=0,counterFullSlot=0;
 int completionFollowupSlot=0;
 int hitFollowupSlot=0;
 float counterFullSeconds=0;
 bool trueCharge=false;
 size_t trueChargeFirstWindow=0,trueChargeFinalWindow=1;
 float weakFinishMultiplier=1;
 std::string weakFinishScope="unknown";
 std::vector<TimedInputBranch> timedInputs;
 std::vector<ProjectileTier> projectileTiers;
 std::vector<float> projectileMarkers;
 std::vector<float> motionValueByWindow;
 std::vector<std::array<float,4>> motionValueByLevelByWindow;
 float gaugeGain=0,gaugeCost=0;
 std::vector<float> gaugeGainByWindow;
 float reloadAt=-1;
 std::string hitTimingScope="source_col_timl",defensiveTimingScope="unknown";
 bool HitWindow(size_t index,float seconds)const{
  return index<outgoing.size()&&outgoing[index].Valid()&&outgoing[index].Contains(seconds);
 }
 bool DefenseWindow(float seconds)const{
  for(const auto& window:defensive)if(window.Valid()&&window.Contains(seconds))return true;
  return false;
 }
 float Gain(size_t window)const{return window<gaugeGainByWindow.size()?gaugeGainByWindow[window]:gaugeGain;}
 float MotionValue(size_t window,int level=0)const{
  if(window<motionValueByLevelByWindow.size())return motionValueByLevelByWindow[window][size_t(std::clamp(level,0,3))];
  return window<motionValueByWindow.size()?motionValueByWindow[window]:100.f;
 }
};
struct CombatRuleSet {
 std::map<int,MotionCombatRule> motions;
 float staminaMax=100,staminaRegen=18,chargeDrain=12,guardHitCost=15;
 float bowShotCost=10,bowDodgeCost=15,gaugeDecay=.4f,gaugeAutoGain=5,gaugeAutoSeconds=15;
 int ammoCapacity=6;
 float bowChainSeconds=1.5f;
 std::vector<float> bowChargeSeconds;
 float foresightMinimumGauge=10,specialSheatheSeconds=3.5f,specialSheatheReadySeconds=73.f/60;
 int gaugeLevelCost=1;
 std::array<float,4> levelDurations{0,195,135,60};
 float helmGaugeSeconds=10,redHelmGaugeSeconds=15,iaiGaugeSeconds=15;
 float iaiTimedGaugeSeconds=30,gaugeUpgradeNoDecaySeconds=10;
 std::array<float,4> levelDamageMultipliers{1,1.05f,1.10f,1.20f};
 int helmFollowupSlot=1000;
 float helmFollowupSeconds=1.5f;
 // Normative logical conditions and teaching numbers are separate: these
 // parameters do not claim MHW motion values, Focus or original stamina.
 std::string numericScope="local_teaching_parameters";
 const MotionCombatRule* Find(int slot)const{auto at=motions.find(slot);return at==motions.end()?nullptr:&at->second;}
 bool Valid()const{
  const float positive[]{staminaMax,staminaRegen,chargeDrain,guardHitCost,bowShotCost,bowDodgeCost,gaugeAutoSeconds,bowChainSeconds};
  for(float value:positive)if(!std::isfinite(value)||value<=0)return false;
  if(!std::isfinite(gaugeDecay)||gaugeDecay<0||!std::isfinite(gaugeAutoGain)||gaugeAutoGain<0||ammoCapacity<=0||ammoCapacity>1000||
     !std::isfinite(helmFollowupSeconds)||helmFollowupSeconds<0)return false;
  if(!std::isfinite(foresightMinimumGauge)||foresightMinimumGauge<0||foresightMinimumGauge>100||gaugeLevelCost<1||gaugeLevelCost>3||
     !std::isfinite(specialSheatheSeconds)||specialSheatheSeconds<=0||!std::isfinite(specialSheatheReadySeconds)||specialSheatheReadySeconds<0)return false;
  if(!std::isfinite(levelDurations[0])||levelDurations[0]!=0)return false;
  for(float value:{levelDurations[1],levelDurations[2],levelDurations[3],helmGaugeSeconds,redHelmGaugeSeconds,iaiGaugeSeconds,iaiTimedGaugeSeconds,gaugeUpgradeNoDecaySeconds})if(!std::isfinite(value)||value<=0)return false;
  if(levelDamageMultipliers[0]!=1)return false;
  for(float value:levelDamageMultipliers)if(!std::isfinite(value)||value<=0)return false;
  float previousCharge=-1;
  for(float value:bowChargeSeconds){if(!std::isfinite(value)||value<0||value<=previousCharge)return false;previousCharge=value;}
  for(const auto&[slot,rule]:motions){
   if(slot<0||!std::isfinite(rule.gaugeGain)||rule.gaugeGain<0||!std::isfinite(rule.gaugeCost)||rule.gaugeCost<0||
      !std::isfinite(rule.reloadAt)||rule.reloadAt<-1)return false;
   for(const auto& w:rule.outgoing)if(!w.Valid())return false;
   for(const auto& w:rule.defensive)if(!w.Valid())return false;
   if(!rule.gaugeGainByWindow.empty()&&rule.gaugeGainByWindow.size()!=rule.outgoing.size())return false;
   if(!rule.motionValueByWindow.empty()&&rule.motionValueByWindow.size()!=rule.outgoing.size())return false;
   if(!rule.motionValueByLevelByWindow.empty()&&rule.motionValueByLevelByWindow.size()!=rule.outgoing.size())return false;
   for(float value:rule.gaugeGainByWindow)if(!std::isfinite(value)||value<0)return false;
   for(float value:rule.motionValueByWindow)if(!std::isfinite(value)||value<0)return false;
   for(const auto& row:rule.motionValueByLevelByWindow)for(float value:row)if(!std::isfinite(value)||value<0)return false;
   float previousMarker=-1;
   for(float value:rule.projectileMarkers){if(!rule.projectile||!std::isfinite(value)||value<0||value<=previousMarker)return false;previousMarker=value;}
   if(rule.counterShortSlot<0||rule.counterFullSlot<0||rule.completionFollowupSlot<0||rule.hitFollowupSlot<0||!std::isfinite(rule.counterFullSeconds)||rule.counterFullSeconds<0||
      !std::isfinite(rule.weakFinishMultiplier)||rule.weakFinishMultiplier<=0)return false;
   if((rule.counterShortSlot||rule.counterFullSlot)&&(!rule.lanceCounter||!rule.counterShortSlot||!rule.counterFullSlot))return false;
   if(rule.trueCharge&&(rule.trueChargeFirstWindow>=rule.outgoing.size()||rule.trueChargeFinalWindow>=rule.outgoing.size()||rule.trueChargeFirstWindow==rule.trueChargeFinalWindow))return false;
   if(rule.weakFinishScope!="unknown"&&rule.weakFinishScope!="motion_has_weakpoint_first_hit")return false;
   for(const auto& branch:rule.timedInputs){
    if(branch.command.empty()||!branch.window.Valid()||branch.timedSlot<=0||branch.normalSlot<0)return false;
    if(branch.inputGateBegin!=-1||branch.inputGateEnd!=-1){const CombatTimeWindow gate{branch.inputGateBegin,branch.inputGateEnd};
     if(!gate.Valid()||gate.begin>branch.window.begin||gate.end<branch.window.end)return false;}
   }
   for(const auto& tier:rule.projectileTiers){if(tier.arrows<=0||tier.arrows>64||tier.shellId<0)return false;
    for(float value:{tier.motionValue,tier.speed,tier.range,tier.radius})if(!std::isfinite(value)||value<0)return false;}
  }
  return true;
 }
};
} // namespace LS
