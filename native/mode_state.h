#pragma once
#include "core.h"
#include "combat_rules.h"
#include <deque>
#include <tuple>

namespace LS {
struct ProjectileRelease {
 bool accepted=false;
 uint64_t motionSerial=0;
 int sourceSlot=0,tier=0;
 float staminaCost=0;
 ProjectileTier parameters;
};
// Independent teaching state. Presentation leaves combatEnabled false;
// correlated combat never obtains resource rewards from demonstration poses.
class ModeState {
public:
 std::string mode="longsword";
 float energy=0,charge=0;int level=0,ammo=6;uint64_t blocks=0;
 float levelSeconds=0;
 std::string feedback;
 CombatRuleSet rules;
 bool combatEnabled=false;
 void Reset(const std::string& value){mode=value;energy=mode=="longsword"?0.f:rules.staminaMax;charge=0;level=0;ammo=rules.ammoCapacity;blocks=0;feedback.clear();lastMotion_=0;promoted_=false;pulse_=0;
  autoGauge_=0;upgradeNoDecay_=0;iaiTimed_=false;levelSeconds=0;bowChainAge_=1000;bowTier_=0;previousSlot_=0;previousSeconds_=0;previousDuration_=0;reloadCommitted_=false;
  foresightEvaded_=false;foresightReady_=false;foresightGauge_=false;iaiOriginalLevel_=0;iaiRefunded_=false;helmConnected_=false;
  hits_.clear();incoming_.clear();incomingOrder_.clear();ResetBranches();}
 void Configure(CombatRuleSet value){if(!value.Valid())throw std::runtime_error("Invalid local combat rules");rules=std::move(value);combatEnabled=true;Reset(mode);}
 std::string Label()const{return mode=="longsword"?"本地气刃槽":mode=="light_bowgun"?"本地弹匣":mode=="greatsword"?"本地蓄力":"本地耐力";}
 float Value()const{return mode=="light_bowgun"?float(ammo):mode=="greatsword"?charge:energy;}
 float Maximum()const{return mode=="light_bowgun"?float(rules.ammoCapacity):mode=="greatsword"?3.f:rules.staminaMax;}
 int BowTier()const{return bowTier_;}
 bool ForesightReady()const{return foresightReady_;}
 bool HelmConnected()const{return helmConnected_;}
 int HelmOriginalLevel()const{return helmOriginalLevel_;}
 void SuspendCombatTracking(){lastMotion_=0;previousSlot_=0;previousSeconds_=previousDuration_=0;reloadCommitted_=false;promoted_=false;
  foresightEvaded_=foresightReady_=foresightGauge_=iaiRefunded_=iaiTimed_=helmConnected_=false;
  hits_.clear();incoming_.clear();incomingOrder_.clear();pulse_=0;charge=0;bowTier_=0;bowChainAge_=1000;ResetBranches();}
 bool TrueChargeQualified()const{return trueChargeQualified_;}
 std::tuple<uint32_t,uint32_t,uint32_t> TrueChargeFirstPart()const{return trueChargeFirstPart_;}
 bool ChargeExhausted(const Controller& c)const{return combatEnabled&&mode=="bow"&&c.holding(Command::Charge)&&energy<=0;}
 // Called before Controller clears its hold timer on release. The expected
 // next serial prevents a cancelled/replaced release from borrowing charge.
 int SelectRelease(Command command,int proposed,float heldSeconds,const Controller& c){
  if(!combatEnabled||mode!="bow"||command!=Command::Charge)return proposed;
  const auto* rule=rules.Find(proposed);
  if(!std::isfinite(heldSeconds)||heldSeconds<0||energy+1e-5f<rules.bowShotCost||!rule||!rule->projectile){releaseSerial_=0;return c.idleSlot;}
  releaseSerial_=c.motionSerial+1;releaseSlot_=proposed;releaseHeldSeconds_=heldSeconds;
  releaseTier_=ProjectileTierFor(*rule,heldSeconds);releaseCost_=rules.bowShotCost;
  releaseParameters_=rule->projectileTiers.empty()?ProjectileTier{}:rule->projectileTiers[size_t(releaseTier_-1)];return proposed;
 }
 CombatOutcome ResolveInput(Command command,const Controller& c)const{
  CombatOutcome out;if(!combatEnabled||!c.active)return out;
  const auto* rule=rules.Find(c.slot);if(!rule)return out;
  for(const auto& branch:rule->timedInputs)if(branch.command==CommandName(command)){
   out.followupSlot=branch.window.Contains(c.seconds)?branch.timedSlot:branch.normalSlot;
   out.accepted=out.followupSlot>0;out.transition=out.accepted?CombatTransition::PerfectRush:CombatTransition::None;return out;
  }
  return out;
 }
 // Invoke at the original input edge, before Controller buffers it. A press
 // outside the timing window must not become precise while waiting to play.
 CombatOutcome CaptureInput(Command command,const Controller& c){
  const auto result=ResolveInput(command,c);
  capturedInputs_[command]={c.motionSerial,result.followupSlot};return result;
 }
 bool CanTransition(Command command,const Controller& c)const{
  if(!combatEnabled||!c.active)return false;
  const auto* rule=rules.Find(c.slot);if(!rule)return false;
  for(const auto& branch:rule->timedInputs)if(branch.command==CommandName(command)&&branch.InputGateContains(c.seconds))return true;
  return false;
 }
 int DamageLevel(const Controller& c)const{
  int sourceLevel=level;const auto* rule=rules.Find(c.slot);
  if(lastMotion_==c.motionSerial&&rule){if(rule->iaiSpirit)sourceLevel=iaiOriginalLevel_;else if(rule->helmFinish)sourceLevel=helmOriginalLevel_;}
  return std::clamp(sourceLevel,0,3);
 }
 float DamageMultiplier(const Controller& c)const{
  return mode=="longsword"?rules.levelDamageMultipliers[DamageLevel(c)]:1;
 }
 // Apply before local HP subtraction. Source MV/100 is a separate factor
 // supplied by the contact's COL node; this is not the complete MHW formula.
 float ContactMultiplier(const Controller& c,const CombatEvent& event)const{
  float value=DamageMultiplier(c);const auto* rule=rules.Find(c.slot);
  if(combatEnabled&&mode=="greatsword"&&c.active&&event.kind==CombatEventKind::OutgoingHit&&event.targetId&&std::isfinite(event.seconds)&&event.seconds<=c.seconds+1e-4f&&lastMotion_==c.motionSerial&&event.motionSerial==c.motionSerial&&event.slot==c.slot&&
     rule&&rule->trueCharge&&rule->weakFinishScope=="motion_has_weakpoint_first_hit"&&trueChargeQualified_&&
     event.windowId==rule->trueChargeFinalWindow&&rule->HitWindow(event.windowId,event.seconds))value*=rule->weakFinishMultiplier;
  return value;
 }
 bool CreditDelayedHelmPulse(int sourceSlot,size_t pulseWindow,int originalLevel){
  const auto* rule=rules.Find(sourceSlot);
  if(!combatEnabled||mode!="longsword"||!rule||!rule->helmFinish||pulseWindow>=rule->outgoing.size()||originalLevel<1||originalLevel>3)return false;
  autoGauge_=originalLevel==3?rules.redHelmGaugeSeconds:rules.helmGaugeSeconds;feedback="登龙延迟命中";return true;
 }
 bool Allows(Command command,const Controller& c)const{
  if(!combatEnabled)return true;
  if(mode=="longsword"){
   if((command==Command::Sheathe||command==Command::Foresight)&&c.resting())return false;
   if((command==Command::Iai||command==Command::IaiSpirit)&&!c.sheathed())return false;
   if(command==Command::Helm)return level>0;
   if(command==Command::Spirit){
    if(foresightReady_&&In(c,Command::Foresight))return true;
    auto moves=c.moves.find(command);if(moves==c.moves.end()||moves->second.empty())return false;
    int next=moves->second.front();auto at=std::find(moves->second.begin(),moves->second.end(),c.slot);
    if(at!=moves->second.end()&&std::next(at)!=moves->second.end())next=*std::next(at);
    const auto* rule=rules.Find(next);return !rule||energy+1e-5f>=rule->gaugeCost;
   }
  }
  if(mode=="light_bowgun"&&(command==Command::Fire||command==Command::Slash))return ammo>0;
  if(mode=="bow"){
   if(command==Command::Fire||command==Command::Release||command==Command::Spirit||command==Command::Thrust)return energy+1e-5f>=rules.bowShotCost;
   if(command==Command::Charge)return energy>0;
   if(command==Command::Dodge||command==Command::Backstep)return energy+1e-5f>=rules.bowDodgeCost;
  }
  if(command==Command::Guard||command==Command::Counter)return energy>0;
  return true;
 }
 int SelectSlot(Command command,int proposed,const Controller& c)const{
  if(!combatEnabled)return proposed;
  if(auto captured=capturedInputs_.find(command);captured!=capturedInputs_.end()){
   if(captured->second.first==c.motionSerial&&captured->second.second)return captured->second.second;
   // A stale/cancelled edge or non-timed original edge keeps normal routing.
  }else {const auto branch=ResolveInput(command,c);if(branch.followupSlot)return branch.followupSlot;}
  if(mode!="longsword")return proposed;
  if(command==Command::IaiSpirit)return 308+std::clamp(level,0,3);
  if(command==Command::Helm)return 170;
  if(command==Command::Spirit&&foresightReady_&&In(c,Command::Foresight))return 109;
  return proposed;
 }
 void Tick(float dt,const Controller& c){
  if(!std::isfinite(dt)||dt<0||dt>1)throw std::runtime_error("Invalid mode-state timestep");
  charge=std::clamp(c.heldSeconds()/1.85f*3,0.f,3.f);
  if(!combatEnabled){
   if(lastMotion_!=c.motionSerial){lastMotion_=c.motionSerial;promoted_=false;if(In(c,Command::Reload))ammo=rules.ammoCapacity;}
   if(mode=="longsword")energy=std::max(0.f,energy-dt*rules.gaugeDecay);
   else if(mode!="light_bowgun"&&mode!="greatsword")energy=std::clamp(energy+dt*((c.holding(Command::Guard)||c.holding(Command::Charge))?-rules.chargeDrain:rules.staminaRegen),0.f,rules.staminaMax);
   return;
  }
  // Cancellation does not refill. Observe the configured marker, or a
  // completed clip's natural return to idle; never refill at reload entry.
  const auto* previous=rules.Find(previousSlot_);
  if(previous&&!reloadCommitted_&&(previous->reload||previous->continuousReload)&&c.motionSerial!=lastMotion_&&c.resting()&&previousSeconds_+dt+1e-4f>=previousDuration_){ammo=rules.ammoCapacity;reloadCommitted_=true;}
  ObserveMotion(c);
  const auto* rule=rules.Find(c.slot);
  if(rule&&(rule->reload||rule->continuousReload)&&rule->reloadAt>=0&&!reloadCommitted_&&c.seconds+1e-5f>=rule->reloadAt){ammo=rules.ammoCapacity;reloadCommitted_=true;}
  if(rule&&rule->continuousReload&&c.motionSerial==lastMotion_&&c.seconds+1e-5f<previousSeconds_)reloadCommitted_=false;
  if(mode=="longsword"){
   const float automatic=std::min(dt,autoGauge_);autoGauge_=std::max(0.f,autoGauge_-dt);
   if(level>0){if(levelSeconds<=0)levelSeconds=rules.levelDurations[std::clamp(level,0,3)];levelSeconds-=dt;
    if(levelSeconds<=0){level=std::max(0,level-1);levelSeconds=level?rules.levelDurations[level]:0;}}
   const float protectedDecay=std::min(dt,upgradeNoDecay_);upgradeNoDecay_=std::max(0.f,upgradeNoDecay_-dt);
   energy=std::clamp(energy+automatic*rules.gaugeAutoGain-(dt-std::max(automatic,protectedDecay))*rules.gaugeDecay,0.f,100.f);
  }else if(mode!="light_bowgun"&&mode!="greatsword"){
   // Normal guard pays on a hit. Only bow charge steadily drains here;
   // unresolved Power Guard is not manufactured from a normal guard clip.
   const bool consuming=mode=="bow"&&c.holding(Command::Charge);
   energy=std::clamp(energy+dt*(consuming?-rules.chargeDrain:rules.staminaRegen),0.f,rules.staminaMax);
  }
  if(mode=="bow"){bowChainAge_+=dt;if(bowChainAge_>rules.bowChainSeconds&&!c.holding(Command::Charge))bowTier_=0;}
  previousSlot_=c.slot;previousSeconds_=c.seconds;auto clip=c.clips.find(c.slot);previousDuration_=clip==c.clips.end()?0:clip->second.duration();
 }
 bool ReleaseProjectile(){
  // Anonymous presentation emission has no motion identity and therefore
  // cannot spend or advance correlated combat resources.
  if(combatEnabled)return false;
  if(mode=="light_bowgun"){if(ammo<=0){feedback="本地弹匣空，请装填";return false;}--ammo;}
  if(mode=="bow"){
   if(combatEnabled&&energy+1e-5f<rules.bowShotCost){feedback="本地耐力不足";return false;}
   energy=std::max(0.f,energy-rules.bowShotCost);if(combatEnabled){bowTier_=std::min(3,std::max(1,bowTier_+1));bowChainAge_=0;}
  }
  return true;
 }
 ProjectileRelease PeekProjectile(const Controller& c)const{
  ProjectileRelease shot;shot.motionSerial=c.motionSerial;shot.sourceSlot=c.slot;
  const auto* rule=rules.Find(c.slot);
  if(!combatEnabled||!c.active||!rule||!rule->projectile||releasedProjectileSerial_==c.motionSerial)return shot;
  if(mode=="bow"){
   const bool captured=releaseSerial_==c.motionSerial&&releaseSlot_==c.slot;
   const float cost=captured?releaseCost_:rules.bowShotCost;
   if(energy+1e-5f<cost)return shot;
   shot.staminaCost=cost;
   shot.tier=captured?releaseTier_:ProjectileTierFor(*rule,0);
   if(captured)shot.parameters=releaseParameters_;
   else if(!rule->projectileTiers.empty())shot.parameters=rule->projectileTiers[size_t(shot.tier-1)];
  }else if(mode=="light_bowgun"){
   if(ammo<=0)return shot;
   if(!rule->projectileTiers.empty())shot.parameters=rule->projectileTiers.front();
  }else return shot;
  shot.accepted=true;return shot;
 }
 ProjectileRelease ReleaseProjectile(const Controller& c){
  auto shot=PeekProjectile(c);if(!shot.accepted){
   if(combatEnabled&&mode=="bow"&&energy+1e-5f<rules.bowShotCost)feedback="本地耐力不足";
   if(combatEnabled&&mode=="light_bowgun"&&ammo<=0)feedback="本地弹匣空，请装填";
   return shot;
  }
  ObserveMotion(c);
  if(mode=="bow"){energy=std::max(0.f,energy-shot.staminaCost);bowTier_=shot.tier;bowChainAge_=0;}
  else if(mode=="light_bowgun")--ammo;
  releasedProjectileSerial_=c.motionSerial;releaseSerial_=0;return shot;
 }
 CombatOutcome Consume(const CombatEvent& event,const Controller& c){
  CombatOutcome out;
  if(!combatEnabled||!c.active||event.motionSerial!=c.motionSerial||event.slot!=c.slot||!std::isfinite(event.seconds)||event.seconds<0||
     !std::isfinite(event.damage)||event.damage<0||event.seconds>c.seconds+1e-4f)return out;
  const auto* rule=rules.Find(c.slot);if(!rule)return out;
  ObserveMotion(c);
  if(event.kind==CombatEventKind::Miss){out.accepted=true;feedback="本地模拟未命中";return out;}
  if(event.kind==CombatEventKind::OutgoingHit){
   if(!event.targetId||(!rule->projectile&&!rule->HitWindow(event.windowId,event.seconds)))return out;
   if(!hits_.insert({event.targetId,event.generation,event.windowId,event.contactSerial}).second)return out;
   out.accepted=true;out.hit=true;out.sourceWindowKnown=rule->hitTimingScope=="source_col_timl";feedback="本地模拟命中";
   if(mode=="greatsword"&&rule->trueCharge&&rule->weakFinishScope=="motion_has_weakpoint_first_hit"){
    if(event.windowId==rule->trueChargeFirstWindow&&event.weakPointKnown&&event.weakPoint&&!trueChargeFinalProcessed_&&event.seconds<rule->outgoing[rule->trueChargeFinalWindow].begin){
     if(!trueChargeQualified_)trueChargeFirstPart_={event.targetId,event.generation,event.partId};
     trueChargeQualified_=true;
    }
    if(event.windowId==rule->trueChargeFinalWindow){trueChargeFinalProcessed_=true;if(trueChargeQualified_)out.damageMultiplier=rule->weakFinishMultiplier;}
   }
   if(mode=="sword_shield"&&rule->hitFollowupSlot&&!rushFollowed_){
    rushFollowed_=true;out.followupSlot=rule->hitFollowupSlot;out.transition=CombatTransition::PerfectRush;
   }
   if(mode=="longsword"){
    if(rule->foresight){
     if(foresightEvaded_&&foresightGauge_){energy=100;foresightReady_=true;out.counterReady=true;out.cue="counter";feedback="见切避开攻击并回刀命中";}
    }else if(rule->iai)autoGauge_=iaiTimed_?rules.iaiTimedGaugeSeconds:rules.iaiGaugeSeconds;
    else if(rule->helmFinish)autoGauge_=helmOriginalLevel_==3?rules.redHelmGaugeSeconds:rules.helmGaugeSeconds;
    else if(!rule->iaiSpirit)energy=std::min(100.f,energy+rule->Gain(event.windowId));
    if(rule->roundslash&&!promoted_){level=std::min(3,level+1);levelSeconds=rules.levelDurations[level];upgradeNoDecay_=rules.gaugeUpgradeNoDecaySeconds;promoted_=true;}
    if(rule->helmThrust&&!helmConnected_&&level>0){helmConnected_=true;out.followupSlot=rules.helmFollowupSlot;out.startSeconds=rules.helmFollowupSeconds;out.transition=CombatTransition::Helm;}
   }
   return out;
  }
  if(!event.attackSerial||!event.targetId)return out;
  const IncomingIdentity incoming{event.targetId,event.generation,event.attackSerial};
  if(!incoming_.insert(incoming).second)return out;
  incomingOrder_.push_back(incoming);if(incomingOrder_.size()>512){incoming_.erase(incomingOrder_.front());incomingOrder_.pop_front();}
  out.accepted=true;
  if(rule->foresight&&event.dodgeable&&foresightGauge_&&rule->DefenseWindow(event.seconds)){
   foresightEvaded_=true;out.evaded=true;feedback="见切避开攻击，需回刀命中";return out;
  }
  if(rule->iai&&event.dodgeable&&rule->DefenseWindow(event.seconds)){
   // The Notes establish a timing/regen bonus, not complete damage immunity.
   // Only a later source outgoing contact starts the extended regeneration.
   iaiTimed_=true;feedback="居合时机成立，回刀命中可延长回复（教学时窗）";return out;
  }
  if(rule->iaiSpirit&&event.dodgeable&&rule->DefenseWindow(event.seconds)){
   out.evaded=true;out.counter=true;out.cue="counter";if(!iaiRefunded_){level=iaiOriginalLevel_;levelSeconds=iaiOriginalSeconds_;iaiRefunded_=true;}++blocks;feedback="居合气刃时机成功，保留气刃等级";return out;
  }
  const bool heldCounter=rule->lanceCounter&&c.holding(Command::Counter);
  const bool defending=(rule->guard&&c.holding(Command::Guard))||(rule->lanceCounter&&(heldCounter||rule->DefenseWindow(event.seconds)));
  if(defending&&event.guardable&&event.fromFront&&energy+1e-5f>=rules.guardHitCost){
   energy=std::max(0.f,energy-rules.guardHitCost);out.blocked=true;out.counter=rule->lanceCounter;++blocks;out.cue=out.counter?"counter":"guard";
   if(heldCounter&&!counterReleased_&&rule->counterShortSlot&&rule->counterFullSlot){
    out.followupSlot=c.heldSeconds()+1e-5f>=rule->counterFullSeconds?rule->counterFullSlot:rule->counterShortSlot;
    out.transition=CombatTransition::Counter;counterReleased_=true;
   }
   feedback=out.counter?"本地模拟格挡后反击":"本地模拟正面格挡";return out;
  }
  feedback="本地模拟受击（不扣真实血量）";return out;
 }
 // Kept for presentation consumers. Combat callers must use Consume with
 // actual target/window/time identity; anonymous Hit cannot award rewards.
 void Hit(const Controller& c){
  if(combatEnabled)return;
  feedback="本地模拟命中";if(mode=="longsword"){
   energy=std::min(100.f,energy+10);auto spirit=c.moves.find(Command::Spirit);
   if(!promoted_&&spirit!=c.moves.end()&&!spirit->second.empty()&&c.slot==spirit->second.back()){level=std::min(3,level+1);promoted_=true;}
  }
 }
 CombatOutcome DummyAttackOutcome(float dt,const Controller& c,bool nearby,CombatEvent& event){
  event={};
  if(!std::isfinite(dt)||dt<0||dt>1)throw std::runtime_error("Invalid dummy attack timestep");
  if(!nearby){pulse_=0;return {};}
  pulse_+=dt;if(pulse_<4)return {};pulse_=std::fmod(pulse_,4.f);
  if(!combatEnabled){feedback="练习目标：模拟攻击（不扣真实血量）";return {};}
  event.kind=CombatEventKind::IncomingAttack;event.motionSerial=c.motionSerial;event.slot=c.slot;event.seconds=c.seconds;event.targetId=0xfffffffe;
  // Automatic dummy and manual attack requests are separate local streams.
  // Keep their serial domains disjoint while sharing the actual dummy ID.
  event.attackSerial=(uint64_t(1)<<63)|++dummySerial_;
  return Consume(event,c);
 }
 std::string DummyAttack(float dt,const Controller& c,bool nearby){
  CombatEvent event;return DummyAttackOutcome(dt,c,nearby,event).cue;
 }
 bool In(const Controller& c,Command command)const{auto i=c.moves.find(command);return i!=c.moves.end()&&std::find(i->second.begin(),i->second.end(),c.slot)!=i->second.end();}
private:
 using HitIdentity=std::tuple<uint32_t,uint32_t,size_t,uint64_t>;
 using IncomingIdentity=std::tuple<uint32_t,uint32_t,uint64_t>;
 std::set<HitIdentity> hits_;std::set<IncomingIdentity> incoming_;std::deque<IncomingIdentity> incomingOrder_;
 uint64_t lastMotion_=0,dummySerial_=0;bool promoted_=false;float pulse_=0,autoGauge_=0,upgradeNoDecay_=0,bowChainAge_=1000;int bowTier_=0;
 int previousSlot_=0;float previousSeconds_=0,previousDuration_=0;bool reloadCommitted_=false;
 bool foresightEvaded_=false,foresightReady_=false,foresightGauge_=false,iaiRefunded_=false,iaiTimed_=false,helmConnected_=false;int iaiOriginalLevel_=0,helmOriginalLevel_=0;float iaiOriginalSeconds_=0;
 bool trueChargeQualified_=false,trueChargeFinalProcessed_=false,counterReleased_=false,rushFollowed_=false;
 std::tuple<uint32_t,uint32_t,uint32_t> trueChargeFirstPart_{};
 uint64_t releasedProjectileSerial_=0,releaseSerial_=0;float releaseHeldSeconds_=0,releaseCost_=0;int releaseSlot_=0,releaseTier_=0;ProjectileTier releaseParameters_;
 std::map<Command,std::pair<uint64_t,int>> capturedInputs_;
 void ResetBranches(){trueChargeQualified_=trueChargeFinalProcessed_=counterReleased_=rushFollowed_=false;trueChargeFirstPart_={};releasedProjectileSerial_=releaseSerial_=0;releaseHeldSeconds_=releaseCost_=0;releaseSlot_=releaseTier_=0;releaseParameters_={};capturedInputs_.clear();}
 int ProjectileTierFor(const MotionCombatRule& rule,float heldSeconds)const{
  const int maximum=rule.projectileTiers.empty()?std::max(3,int(rules.bowChargeSeconds.size())):int(rule.projectileTiers.size());
  int charged=1;for(size_t i=0;i<rules.bowChargeSeconds.size();++i)if(heldSeconds+1e-5f>=rules.bowChargeSeconds[i])charged=int(i)+1;
  const int chained=bowChainAge_<=rules.bowChainSeconds?bowTier_+1:1;
  return std::clamp(std::max(charged,chained),1,maximum);
 }
 void ObserveMotion(const Controller& c){
  if(lastMotion_==c.motionSerial)return;lastMotion_=c.motionSerial;promoted_=false;hits_.clear();reloadCommitted_=false;
  foresightEvaded_=false;foresightReady_=false;foresightGauge_=false;iaiRefunded_=false;iaiTimed_=false;helmConnected_=false;
  trueChargeQualified_=trueChargeFinalProcessed_=counterReleased_=rushFollowed_=false;trueChargeFirstPart_={};
  const auto* rule=rules.Find(c.slot);if(!rule)return;
  if(mode=="longsword"){
   if(rule->foresight){foresightGauge_=energy+1e-5f>=rules.foresightMinimumGauge;energy=0;}
   if(rule->iaiSpirit){iaiOriginalLevel_=std::clamp(level,0,3);iaiOriginalSeconds_=levelSeconds;level=std::max(0,level-rules.gaugeLevelCost);levelSeconds=level?rules.levelDurations[level]:0;}
   if(rule->helmFinish){helmOriginalLevel_=level;level=std::max(0,level-rules.gaugeLevelCost);levelSeconds=level?rules.levelDurations[level]:0;}
   if(rule->spiritBlade)energy=std::max(0.f,energy-rule->gaugeCost);
  }
  if(mode=="bow"&&(In(c,Command::Dodge)||In(c,Command::Backstep))){energy=std::max(0.f,energy-rules.bowDodgeCost);if(bowTier_>0){int maximum=3;for(const auto&[slot,row]:rules.motions)maximum=std::max(maximum,int(row.projectileTiers.size()));bowTier_=std::min(maximum,bowTier_+1);bowChainAge_=0;}}
 }
};
} // namespace LS
