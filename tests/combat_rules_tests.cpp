#include "../native/mode_state.h"
#include "test_support.h"
#include <iostream>

namespace {
using namespace LS;
Clip ClipFor(int slot,float duration=2){Clip clip;clip.slot=slot;clip.fps=60;for(int i=0;i<=int(duration*60);++i){Pose pose;pose.rotations.resize(21);pose.hip={float(i)/60,0,0};clip.frames.push_back(pose);}return clip;}
Controller MakeController(){Controller c;for(int slot:{1,3,101,104,106,108,109,140,170,306,307,308,309,310,311,1000,30121,30128,40102,40107,40082,50125,50145})c.clips[slot]=ClipFor(slot,slot==1000?7.f:2.f);CHECK(c.start());return c;}
CombatRuleSet MakeRules(){CombatRuleSet rules;
 for(int slot:{101,104,106,108,109,140,170,307,308,309,310,311,1000})rules.motions[slot].outgoing={{.8f,1.3f}};
 rules.motions[101].gaugeGain=10;for(int slot:{104,106,108}){rules.motions[slot].spiritBlade=true;rules.motions[slot].gaugeCost=15;}
 rules.motions[109].spiritBlade=true;rules.motions[109].roundslash=true;
 auto& foresight=rules.motions[140];foresight.foresight=true;foresight.defensive={{0,.75f}};foresight.defensiveTimingScope="local_simulation";
 rules.motions[170].helmThrust=true;rules.motions[1000].helmFinish=true;rules.motions[307].iai=true;
 for(int slot:{308,309,310,311}){auto& rule=rules.motions[slot];rule.iaiSpirit=true;rule.defensive={{0,.15f}};rule.defensiveTimingScope="local_simulation";}
 rules.motions[30128].guard=true;rules.motions[30121].lanceCounter=true;rules.motions[30121].defensive={{0,.25f}};
 rules.motions[40102].reload=true;rules.motions[40102].reloadAt=2;
 rules.motions[40107].continuousReload=true;rules.motions[40107].reloadAt=2;
 rules.motions[40082].projectile=true;rules.motions[50125].projectile=true;rules.motions[50145]={};return rules;
}
void Move(Controller& c,int slot,float seconds){CHECK(c.transitionToSlot(slot,seconds));}
CombatEvent Hit(const Controller& c,size_t window=0,uint32_t target=7){CombatEvent e;e.motionSerial=c.motionSerial;e.slot=c.slot;e.seconds=c.seconds;e.targetId=target;e.generation=1;e.windowId=window;e.damage=25;return e;}
CombatEvent Incoming(const Controller& c,uint64_t serial=1){auto e=Hit(c);e.kind=CombatEventKind::IncomingAttack;e.attackSerial=serial;return e;}
void SourceHitsAndForesight(){
 auto c=MakeController();ModeState state;state.Configure(MakeRules());state.Reset("longsword");
 Move(c,101,.5f);CHECK(!state.Consume(Hit(c),c).accepted&&state.energy==0);
 c.seconds=.9f;auto hit=Hit(c);CHECK(state.Consume(hit,c).hit&&state.energy==10);CHECK(!state.Consume(hit,c).accepted&&state.energy==10);
 hit.generation=2;CHECK(state.Consume(hit,c).hit&&state.energy==20); // Reused entity ID has a different lifetime.
 hit.motionSerial--;CHECK(!state.Consume(hit,c).accepted);hit=Hit(c);hit.seconds=c.seconds+.1f;CHECK(!state.Consume(hit,c).accepted);
 hit=Hit(c);hit.damage=std::numeric_limits<float>::quiet_NaN();CHECK(!state.Consume(hit,c).accepted);
 state.energy=30;Move(c,140,.1f);state.Tick(0,c);CHECK(state.energy==0);
 auto attack=Incoming(c);auto dodge=state.Consume(attack,c);CHECK(dodge.evaded&&!dodge.counter&&dodge.cue.empty());CHECK(!state.Consume(attack,c).accepted);
 c.seconds=.9f;auto success=state.Consume(Hit(c),c);CHECK(success.hit&&success.counterReady&&success.cue=="counter"&&state.energy==100&&state.level==0);
 CHECK(state.SelectSlot(Command::Spirit,104,c)==109);
 Move(c,109,.9f);CHECK(state.Consume(Hit(c),c).hit&&state.level==1);CHECK(state.Consume(Hit(c,0,8),c).hit&&state.level==1);
 state.energy=30;Move(c,140,.9f);state.Tick(0,c);auto miss=Incoming(c,2);CHECK(!state.Consume(miss,c).evaded);
 CHECK(state.Consume(Hit(c),c).hit&&!state.ForesightReady()&&state.energy==0); // Hit alone is not a successful foresight.
 state.energy=0;Move(c,140,.1f);state.Tick(0,c);CHECK(!state.Consume(Incoming(c,3),c).evaded);
 // A repeated attack cannot become a new successful counter after a motion transition.
 Move(c,140,.1f);state.energy=30;state.Tick(0,c);CHECK(!state.Consume(Incoming(c,3),c).accepted);
}
void IaiAndHelm(){
 auto c=MakeController();ModeState state;state.Configure(MakeRules());
 for(int level=0;level<=3;++level){state.level=level;CHECK(state.SelectSlot(Command::IaiSpirit,311,c)==308+level);}
 state.level=3;Move(c,311,.05f);state.Tick(0,c);CHECK(state.level==2);
 auto success=state.Consume(Incoming(c,10),c);CHECK(success.counter&&success.evaded&&state.level==3);
 CHECK(!state.Consume(Incoming(c,10),c).accepted&&state.level==3);
 state.level=2;Move(c,310,.2f);state.Tick(0,c);CHECK(state.level==1&&!state.Consume(Incoming(c,11),c).counter&&state.level==1);
 state.level=0;CHECK(!state.Allows(Command::Helm,c));state.level=2;CHECK(state.Allows(Command::Helm,c)&&state.SelectSlot(Command::Helm,1000,c)==170);
 Move(c,170,.5f);CombatEvent miss=Hit(c);miss.kind=CombatEventKind::Miss;CHECK(state.Consume(miss,c).accepted&&!state.HelmConnected());
 CHECK(!state.Consume(Hit(c),c).accepted);c.seconds=.9f;auto lift=state.Consume(Hit(c),c);CHECK(lift.followupSlot==1000&&Near(lift.startSeconds,1.5f)&&state.level==2);
 CHECK(state.Consume(Hit(c,0,8),c).followupSlot==0);Move(c,1000,1.5f);state.Tick(0,c);CHECK(state.level==1);
 state.energy=0;Move(c,307,.9f);CHECK(state.Consume(Hit(c),c).hit);state.Tick(1,c);CHECK(state.energy>0); // Actual Iai hit enables periodic gauge filling.
}
void GuardsAndResources(){
 auto c=MakeController();ModeState state;state.Configure(MakeRules());state.Reset("lance");
 c.longswordRules=false;c.moves[Command::Guard]={30128};c.holdPolicies[Command::Guard]={15,false,{}};
 c.held(Command::Guard,true);c.tick(.2f);state.Tick(.2f,c);CHECK(state.energy==100);
 auto attack=Incoming(c,20);attack.fromFront=false;CHECK(!state.Consume(attack,c).blocked&&state.energy==100);
 attack.attackSerial=21;attack.fromFront=true;attack.guardable=false;CHECK(!state.Consume(attack,c).blocked);
 attack.attackSerial=22;attack.guardable=true;CHECK(state.Consume(attack,c).blocked&&state.energy==85);
 state.energy=5;attack.attackSerial=23;CHECK(!state.Consume(attack,c).blocked&&state.energy==5);
 c.held(Command::Guard,false);Move(c,30121,.5f);state.energy=100;CHECK(!state.Consume(Incoming(c,24),c).counter);
 Move(c,30121,.1f);CHECK(state.Consume(Incoming(c,25),c).counter);
 state.Reset("light_bowgun");state.ammo=0;Move(c,40102,0);state.Tick(0,c);CHECK(state.ammo==0&&!state.ReleaseProjectile());
 c.seconds=.2f;state.Tick(.2f,c);Move(c,40082,0);state.Tick(.1f,c);CHECK(state.ammo==0); // Cancellation is not a refill.
 Move(c,40102,0);state.Tick(0,c);c.seconds=1.95f;state.Tick(.1f,c);CHECK(state.ammo==0);Move(c,1,0);state.Tick(.1f,c);CHECK(state.ammo==6);
 CHECK(!state.ReleaseProjectile());
 for(int i=0;i<6;++i){Move(c,40082,0);CHECK(state.ReleaseProjectile(c).accepted);}Move(c,40082,0);CHECK(!state.ReleaseProjectile(c).accepted);
 state.Reset("bow");state.energy=5;Move(c,50125,0);CHECK(!state.ReleaseProjectile(c).accepted&&state.energy==5&&!state.Allows(Command::Fire,c));
 state.energy=100;CHECK(!state.ReleaseProjectile());CHECK(state.ReleaseProjectile(c).accepted&&state.BowTier()==1);Move(c,50125,0);CHECK(state.ReleaseProjectile(c).accepted&&state.BowTier()==2);
 c.moves[Command::Dodge]={50145};Move(c,50145,0);state.Tick(0,c);CHECK(state.BowTier()==3&&state.energy==65);
 state.Tick(1,c);state.Tick(1,c);CHECK(state.BowTier()==0);
 auto bad=MakeRules();bad.motions[101].outgoing={{2,1}};CHECK(!bad.Valid()&&Throws([&]{state.Configure(bad);}));
 CHECK(Throws([&]{state.Tick(-1,c);}));CHECK(Throws([&]{state.DummyAttack(-1,c,true);}));
}
void DodgeabilityAndSourceParameters(){
 auto c=MakeController();ModeState s;s.Configure(MakeRules());s.Reset("longsword");
 s.energy=30;Move(c,140,.05f);s.Tick(0,c);auto immune=Incoming(c,101);immune.dodgeable=false;
 CHECK(!s.Consume(immune,c).evaded&&!s.ForesightReady());
 s.Reset("longsword");s.level=3;Move(c,311,.05f);s.Tick(0,c);immune=Incoming(c,102);immune.dodgeable=false;
 CHECK(!s.Consume(immune,c).counter&&s.level==2&&Near(s.DamageMultiplier(c),1.2f));
 Move(c,101,.9f);s.Tick(0,c);CHECK(Near(s.DamageMultiplier(c),1.1f));
 s.Reset("longsword");s.level=3;Move(c,1000,1.5f);s.Tick(0,c);CHECK(s.level==2&&Near(s.DamageMultiplier(c),1.2f));
 s.Reset("longsword");s.energy=37;Move(c,109,.9f);CHECK(s.Consume(Hit(c),c).hit&&s.level==1);
 for(int i=0;i<10;++i)s.Tick(1,c);CHECK(Near(s.energy,37)&&Near(s.levelSeconds,185));
 s.Tick(1,c);CHECK(Near(s.energy,36.6f)&&Near(s.levelSeconds,184));
 s.Reset("longsword");Move(c,307,.9f);CHECK(s.Consume(Hit(c),c).hit);
 for(int i=0;i<20;++i)s.Tick(1,c);const auto normal=s.energy;
 s.Reset("longsword");s.rules.motions[307].defensive={{0,.15f}};s.rules.motions[307].defensiveTimingScope="local_simulation";
 Move(c,307,.05f);const auto timing=s.Consume(Incoming(c,103),c);
 CHECK(timing.accepted&&!timing.evaded&&!timing.blocked&&!timing.counter);s.Tick(1,c);CHECK(s.energy==0);
 c.seconds=.9f;CHECK(s.Consume(Hit(c),c).hit);for(int i=0;i<20;++i)s.Tick(1,c);CHECK(s.energy>normal+20);
 auto bad=MakeRules();bad.levelDamageMultipliers[2]=std::numeric_limits<float>::quiet_NaN();CHECK(!bad.Valid());
 bad=MakeRules();bad.iaiTimedGaugeSeconds=0;CHECK(!bad.Valid());bad=MakeRules();bad.gaugeUpgradeNoDecaySeconds=0;CHECK(!bad.Valid());
 // Guard pays only for a successful incoming contact, never for holding.
 s.Reset("lance");c.longswordRules=false;c.moves[Command::Guard]={30128};c.holdPolicies[Command::Guard]={15,false,{}};
 CHECK(c.start());c.held(Command::Guard,true);s.energy=45;s.Tick(1,c);CHECK(s.energy>=45);
}
void FourWeaponConditions(){
 auto c=MakeController();c.longswordRules=false;
 for(int slot:{20146,30120,30122,30123,10341,10342,10345,10347,10348,10349})c.clips[slot]=ClipFor(slot,3);
 auto rules=MakeRules();
 auto& gs=rules.motions[20146];gs.trueCharge=true;gs.outgoing={{37.f/60,43.f/60},{101.f/60,113.f/60}};
 gs.weakFinishScope="motion_has_weakpoint_first_hit";gs.weakFinishMultiplier=1.2f;
 auto& counter=rules.motions[30120];counter.lanceCounter=true;counter.counterShortSlot=30122;counter.counterFullSlot=30123;counter.counterFullSeconds=1;
 rules.motions[10345].timedInputs={{"slash",{.5f,.7f},10347,10348}};
 rules.motions[10347].timedInputs={{"slash",{.8f,1.f},10349,10348}};
 rules.bowChargeSeconds={0,.5f,1.f};
 rules.motions[50125].projectileTiers={{1,4,10},{2,4,15},{3,4,20}};
 ModeState s;s.Configure(rules);s.Reset("greatsword");
 Move(c,20146,.65f);auto first=Hit(c);first.weakPoint=true;first.partId=9;
 CHECK(s.Consume(first,c).hit&&!s.TrueChargeQualified()); // A bool without its data provenance is insufficient.
 first.weakPointKnown=true;CHECK(!s.Consume(first,c).accepted&&!s.TrueChargeQualified()); // Rewriting a replay cannot earn qualification.
 c.seconds=1.75f;auto finish=Hit(c,1);CHECK(Near(s.ContactMultiplier(c,finish),1));CHECK(Near(s.Consume(finish,c).damageMultiplier,1));
 Move(c,20146,.65f);first=Hit(c);first.weakPointKnown=first.weakPoint=true;first.partId=9;
 CHECK(s.Consume(first,c).hit&&s.TrueChargeQualified());CHECK(s.TrueChargeFirstPart()==std::make_tuple(7u,1u,9u));
 auto futureFinish=Hit(c,1);futureFinish.seconds=1.75f;CHECK(Near(s.ContactMultiplier(c,futureFinish),1));
 futureFinish.kind=CombatEventKind::IncomingAttack;CHECK(Near(s.ContactMultiplier(c,futureFinish),1));
 c.seconds=1.75f;finish=Hit(c,1,8);finish.generation=2;finish.partId=10;
 CHECK(Near(s.ContactMultiplier(c,finish),1.2f)&&Near(s.Consume(finish,c).damageMultiplier,1.2f)); // Source action flag also empowers a different final contact.
 Move(c,101,.9f);s.Tick(0,c);CHECK(!s.TrueChargeQualified());Move(c,20146,1.75f);s.Tick(0,c);
 CHECK(Near(s.ContactMultiplier(c,Hit(c,1)),1)); // Cancel/replay cannot borrow the former action's first hit.
 first=Hit(c);first.seconds=.65f;first.weakPointKnown=first.weakPoint=true;CHECK(s.Consume(first,c).hit&&s.TrueChargeQualified());
 CHECK(Near(s.ContactMultiplier(c,Hit(c,1)),1.2f)); // Chronologically sorted first/finish contacts in one frame retain source hit times.
 Move(c,20146,1.75f);CHECK(s.Consume(Hit(c,1),c).hit&&!s.TrueChargeQualified());
 first=Hit(c);first.seconds=.65f;first.weakPointKnown=first.weakPoint=true;CHECK(s.Consume(first,c).hit&&!s.TrueChargeQualified()); // First arriving after an already processed finish cannot retroactively empower it.
 s.SuspendCombatTracking();CHECK(!s.TrueChargeQualified());

 s.Reset("lance");c.moves[Command::Counter]={30120};c.holdPolicies[Command::Counter]={12,true,{{0,30122},{1,30123}}};
 CHECK(c.start());c.held(Command::Counter,true);c.tick(.3f);s.Tick(0,c);
 auto attack=Incoming(c,400);attack.fromFront=false;CHECK(!s.Consume(attack,c).blocked);
 attack.attackSerial=401;attack.fromFront=true;attack.guardable=false;CHECK(!s.Consume(attack,c).blocked);
 attack.attackSerial=402;attack.guardable=true;const auto shortRelease=s.Consume(attack,c);
 CHECK(shortRelease.blocked&&shortRelease.transition==CombatTransition::Counter&&shortRelease.followupSlot==30122);
 CHECK(!s.Consume(attack,c).accepted);attack.attackSerial=403;CHECK(s.Consume(attack,c).followupSlot==0); // One hold requests one automatic release.
 CHECK(c.start());c.held(Command::Counter,true);c.tick(1);c.tick(.1f);s.Tick(0,c);
 CHECK(s.Consume(Incoming(c,404),c).followupSlot==30123);
 CHECK(c.start());c.held(Command::Counter,true);c.tick(.3f);s.energy=5;
 CHECK(!s.Consume(Incoming(c,405),c).blocked);c.held(Command::Counter,false);
 CHECK(s.Consume(Incoming(c,406),c).followupSlot==0); // Cancelled stance cannot release a counter.
 s.Reset("lance");CHECK(c.start());c.held(Command::Counter,true);c.tick(1.1f);
 CombatEvent dummy;CombatOutcome automatic;
 for(int i=0;i<4;++i)automatic=s.DummyAttackOutcome(1,c,true,dummy);
 CHECK(automatic.blocked&&automatic.followupSlot==30123&&automatic.transition==CombatTransition::Counter);
 CHECK(dummy.kind==CombatEventKind::IncomingAttack&&dummy.motionSerial==c.motionSerial&&dummy.targetId==0xfffffffe&&dummy.attackSerial>0);
 CHECK(!s.Consume(dummy,c).accepted);CHECK(!s.DummyAttackOutcome(1,c,false,dummy).accepted&&dummy.attackSerial==0);
 ModeState mixed;mixed.Configure(rules);mixed.Reset("lance");CHECK(c.start());c.held(Command::Counter,true);c.tick(1.1f);
 auto manual=Incoming(c,1);manual.targetId=0xfffffffe;CHECK(mixed.Consume(manual,c).blocked);
 for(int i=0;i<4;++i)automatic=mixed.DummyAttackOutcome(1,c,true,dummy);
 CHECK(automatic.accepted&&automatic.blocked&&mixed.blocks==2&&dummy.attackSerial!=manual.attackSerial&&(dummy.attackSerial>>63)==1);
 CHECK(!mixed.Consume(dummy,c).accepted&&!mixed.Consume(manual,c).accepted);

 s.Reset("sword_shield");Move(c,10345,.49f);CHECK(s.SelectSlot(Command::Slash,10348,c)==10348);
 c.seconds=.5f;const auto perfect=s.ResolveInput(Command::Slash,c);CHECK(perfect.transition==CombatTransition::PerfectRush&&perfect.followupSlot==10347);
 c.seconds=.7f;CHECK(s.SelectSlot(Command::Slash,10348,c)==10348); // Half-open source/teaching timing boundary.
 Move(c,10347,.85f);CHECK(s.SelectSlot(Command::Slash,10348,c)==10349);
 Move(c,101,.6f);CHECK(s.SelectSlot(Command::Slash,10348,c)==10348); // Cancelled chain lacks timing branch.
 Move(c,10345,.6f);CHECK(!s.ResolveInput(Command::Thrust,c).accepted);
 Move(c,10345,.49f);s.CaptureInput(Command::Slash,c);c.seconds=.6f;CHECK(s.SelectSlot(Command::Slash,10348,c)==10348);
 s.CaptureInput(Command::Slash,c);c.seconds=.75f;CHECK(s.SelectSlot(Command::Slash,10348,c)==10347); // Buffered precise edge keeps its actual timestamp.
 Move(c,10347,.85f);CHECK(s.SelectSlot(Command::Slash,10348,c)==10348); // Reusing a cancelled earlier edge cannot precise the next action.
 s.CaptureInput(Command::Slash,c);CHECK(s.SelectSlot(Command::Slash,10348,c)==10349);
 s.rules.motions[10341].outgoing={{.2f,.3f}};s.rules.motions[10341].hitFollowupSlot=10342;
 Move(c,10341,.25f);auto emptyRush=Hit(c);emptyRush.kind=CombatEventKind::Miss;
 CHECK(s.Consume(emptyRush,c).accepted&&s.Consume(emptyRush,c).followupSlot==0);
 auto finishRush=s.Consume(Hit(c),c);CHECK(finishRush.transition==CombatTransition::PerfectRush&&finishRush.followupSlot==10342);
 CHECK(s.Consume(Hit(c,0,8),c).followupSlot==0);CHECK(!s.Consume(Hit(c),c).accepted);
 Move(c,101,.9f);Move(c,10341,.25f);CHECK(s.Consume(Hit(c),c).followupSlot==10342); // A new motion can attach once again.

 s.Reset("bow");Move(c,1,0);const int release=s.SelectRelease(Command::Charge,50125,1.1f,c);CHECK(release==50125);
 Move(c,release,0);const auto peek=s.PeekProjectile(c);CHECK(peek.accepted&&peek.tier==3&&peek.parameters.arrows==3&&Near(s.energy,100)&&s.BowTier()==0);
 const auto arrows=s.ReleaseProjectile(c);CHECK(arrows.accepted&&arrows.tier==3&&arrows.parameters.arrows==3&&Near(arrows.parameters.motionValue,20)&&Near(s.energy,90));
 CHECK(!s.ReleaseProjectile(c).accepted&&Near(s.energy,90));
 // The physical release freezes both the type (source slot) and its tier
 // parameters, even when the marker occurs after the source table changes.
 s.Reset("bow");Move(c,1,0);s.SelectRelease(Command::Charge,50125,.6f,c);
 rules.motions[50125].projectileTiers[1].arrows=9;
 s.rules.motions[50125].projectileTiers[1].arrows=9;
 s.rules.bowShotCost=99;
 Move(c,50125,0);const auto frozen=s.ReleaseProjectile(c);
 CHECK(frozen.accepted&&frozen.sourceSlot==50125&&frozen.tier==2&&frozen.parameters.arrows==2&&Near(frozen.parameters.motionValue,15)&&Near(frozen.staminaCost,10));
 CHECK(!s.ReleaseProjectile(c).accepted&&Near(s.energy,90));
 s.rules.motions[50125].projectileTiers[1].arrows=2;
 s.rules.bowShotCost=10;
 s.energy=5;Move(c,50125,0);CHECK(!s.ReleaseProjectile(c).accepted&&Near(s.energy,5));
 CHECK(s.SelectRelease(Command::Charge,50125,1.2f,c)==c.idleSlot);
 s.Reset("bow");Move(c,1,0);s.SelectRelease(Command::Charge,50125,1.2f,c);Move(c,101,0);s.Tick(0,c);Move(c,50125,0);
 CHECK(s.ReleaseProjectile(c).parameters.arrows==1); // Replaced release cannot reuse a stale charge snapshot.
 s.Reset("bow");Move(c,1,0);s.SelectRelease(Command::Charge,50125,1.2f,c);
 s.rules.motions[40082].projectileTiers={{1,8,7},{2,8,14},{3,8,21}};
 Move(c,40082,0);const auto otherType=s.ReleaseProjectile(c);
 CHECK(otherType.accepted&&otherType.tier==1&&otherType.parameters.shellId==8&&Near(otherType.parameters.motionValue,7));
 // Four source tiers, ordinary/power chain increments, exact source threshold,
 // and an expired chain all use the emitted snapshot rather than only HUD.
 s.rules.bowChargeSeconds={0,.5f,1.f,1.5f};
 s.rules.motions[50125].projectileTiers={{1,4,10},{2,4,15},{3,4,20},{4,4,25}};
 for(int tier=1;tier<=4;++tier){
  s.Reset("bow");Move(c,1,0);s.SelectRelease(Command::Charge,50125,s.rules.bowChargeSeconds[size_t(tier-1)],c);
  Move(c,50125,0);const auto shot=s.ReleaseProjectile(c);
  CHECK(shot.accepted&&shot.tier==tier&&shot.parameters.arrows==tier&&Near(shot.parameters.motionValue,5.f*tier+5)&&Near(s.energy,90));
 }
 s.Reset("bow");Move(c,50125,0);CHECK(s.ReleaseProjectile(c).tier==1);Move(c,50125,0);CHECK(s.ReleaseProjectile(c).tier==2);
 s.Tick(1,c);s.Tick(1,c);Move(c,50125,0);CHECK(s.ReleaseProjectile(c).tier==1);
 s.Tick(0,c);auto arrow=Hit(c);arrow.contactSerial=1;CHECK(s.Consume(arrow,c).hit);
 CHECK(!s.Consume(arrow,c).accepted);arrow.contactSerial=2;CHECK(s.Consume(arrow,c).hit);
 arrow.contactSerial=3;CHECK(s.Consume(arrow,c).hit);CHECK(!s.ReleaseProjectile(c).accepted); // Distinct arrows do not charge again.
 c.moves[Command::Charge]={50145};c.holdPolicies[Command::Charge]={10,true,{}};CHECK(c.start());c.held(Command::Charge,true);s.energy=1;s.Tick(1,c);
 CHECK(s.ChargeExhausted(c)&&s.SelectRelease(Command::Charge,50125,1,c)==c.idleSlot);
 auto bad=rules;bad.motions[20146].trueChargeFinalWindow=9;CHECK(!bad.Valid());
 bad=rules;bad.motions[30120].counterFullSlot=0;CHECK(!bad.Valid());bad=rules;bad.bowChargeSeconds={0,.5f,.4f};CHECK(!bad.Valid());
 bad=rules;bad.motions[50125].projectileTiers[0].arrows=0;CHECK(!bad.Valid());
 bad=rules;bad.motions[20146].motionValueByWindow={1};CHECK(!bad.Valid());
 bad=rules;bad.motions[50125].projectileMarkers={.1f,.1f};CHECK(!bad.Valid());
 bad=rules;bad.motions[101].projectileMarkers={.1f};CHECK(!bad.Valid());
 auto levelRules=rules;levelRules.motions[101].motionValueByWindow={28};
 levelRules.motions[101].motionValueByLevelByWindow={{{20,25,30,35}}};CHECK(levelRules.Valid());
 CHECK(Near(levelRules.motions[101].MotionValue(0,2),30)&&Near(levelRules.motions[101].MotionValue(0,99),35));
 bad=levelRules;bad.motions[101].motionValueByLevelByWindow[0][2]=std::numeric_limits<float>::quiet_NaN();CHECK(!bad.Valid());
 bad=levelRules;bad.motions[101].motionValueByLevelByWindow.push_back({1,2,3,4});CHECK(!bad.Valid());
 s.Reset("sword_shield");s.rules.motions[10345].timedInputs[0].inputGateBegin=.4f;s.rules.motions[10345].timedInputs[0].inputGateEnd=.8f;
 Move(c,10345,.4f);CHECK(s.CanTransition(Command::Slash,c)&&!s.CanTransition(Command::Thrust,c));
 c.seconds=.8f;CHECK(!s.CanTransition(Command::Slash,c));
 bad=s.rules;bad.motions[10345].timedInputs[0].inputGateEnd=.6f;CHECK(!bad.Valid());
}
}
int main(){try{SourceHitsAndForesight();IaiAndHelm();GuardsAndResources();DodgeabilityAndSourceParameters();FourWeaponConditions();std::cout<<"Correlated rules, four-weapon branches, explicit weakpoint causality, cancellation, replay and projectile snapshots passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
