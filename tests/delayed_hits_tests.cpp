#include "../native/delayed_hits.h"
#include "../native/target_geometry.h"
#include "test_support.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>

using namespace LS;
namespace {
DelayedHitRule SourceRule(const std::filesystem::path& root){
 std::ifstream input(root/"assets/modes/longsword/combat_rules.json");CHECK(bool(input));nlohmann::json j;input>>j;
 const auto& source=j.at("delayed_hit"),row=source.at("triggers").at("1000").at(0),shell=source.at("shell_geometry");
 CHECK(source.at("hit_limit")==7);CHECK(Near(source.at("first_delay").get<float>(),.4f));
 CHECK(Near(source.at("between_delay").get<float>(),.05f)&&Near(source.at("last_delay").get<float>(),.05f));
 DelayedHitRule r;r.triggerWindow=row.at("window_id").get<uint32_t>();r.begin=row.at("begin").get<float>();r.end=row.at("end").get<float>();
 r.pulseWindows=row.at("pulse_window_ids").get<std::vector<uint32_t>>();r.offsets=row.at("pulse_offsets").get<std::vector<float>>();
 r.radius=shell.at("native_attack_range").get<float>()*shell.at("range_scale").get<float>()*shell.at("shell_hitbox_multiplier").get<float>();
 r.stickSeconds=shell.at("modifiers").at("HitStickTime").get<float>();r.authoredTriggerGeometry=row.at("trigger_contact_geometry_known").get<bool>();
 CHECK(r.Valid()&&r.offsets.size()==7&&!r.authoredTriggerGeometry);return r;
}
CombatTarget Target(uint32_t generation=2){CombatTarget t;t.key={17,generation};t.hurt={{{0,0,0},{0,0,0},.1f,3,0}};return t;}
PracticeHit Seed(const DelayedHitRule& r,uint64_t motion=42,uint32_t generation=2){
 PracticeHit hit;hit.targetId=17;hit.generation=generation;hit.motionSerial=motion;hit.slot=1000;
 hit.seconds=r.begin+.001f;hit.windowId=r.triggerWindow;hit.partId=3;hit.damage=10;hit.serverId=UINT64_C(0x100000001);
 hit.serverBattleInstId=UINT64_C(0x200000002);hit.serverGeneration=3;return hit;
}
void TimingAndIdentity(const DelayedHitRule& r){
 DelayedHits d;auto target=Target();const auto seed=Seed(r);CHECK(d.Schedule(seed,r,target,3));CHECK(!d.Schedule(seed,r,target,3));
 CHECK(d.Update(.25f,{target}).empty());CHECK(d.Update(.14f,{target}).empty());
 auto first=d.Update(.01f,{target});CHECK(first.size()==1&&first.front().hit.windowId==r.pulseWindows.front());
 CHECK(first.front().originalLevel==3&&first.front().hit.motionSerial==seed.motionSerial&&first.front().hit.serverId==seed.serverId);
 CHECK(first.front().hit.serverBattleInstId==seed.serverBattleInstId&&first.front().hit.serverGeneration==seed.serverGeneration);
 size_t emitted=1;for(size_t i=1;i<7;++i){auto next=d.Update(.05f,{target});CHECK(next.size()==1);
  CHECK(next.front().hit.windowId==r.pulseWindows[i]&&Near(next.front().hit.seconds,seed.seconds+r.offsets[i]));++emitted;}
 CHECK(emitted==7&&d.pulses==7&&d.Active()==0&&!d.Schedule(seed,r,target,3));
 auto changed=target;changed.key.generation=3;auto nextSeed=Seed(r,42,3);
 CHECK(d.Schedule(nextSeed,r,changed,2));CHECK(d.Update(.25f,{target}).empty()&&d.Active()==0&&d.discarded==1);
 // A different generation is not a replay of an older target's hit.
 CHECK(d.Schedule(Seed(r,43,3),r,changed,2));CHECK(d.Update(.25f,{}).empty()&&d.Active()==0&&d.discarded==2);
 changed.alive=false;CHECK(!d.Schedule(Seed(r,44,3),r,changed,2));
}
void BirthAndStop(const DelayedHitRule& r){
 DelayedHits d;auto t=Target();CHECK(d.Schedule(Seed(r),r,t,1,0,0));
 CHECK(d.Update(.25f,{t}).empty()); // newborn frame's supplied advance is zero
 CHECK(d.Update(.25f,{t}).empty());CHECK(d.Update(.14f,{t}).empty());
 CHECK(d.Update(0,{t}).empty()&&d.Active()==1); // a zero source step stays frozen
 CHECK(d.Update(.01f,{t}).size()==1);d.Clear();
 CHECK(d.Active()==0&&d.pulses==0&&d.scheduled==0&&d.Update(.25f,{t}).empty());
 CHECK(d.Schedule(Seed(r),r,t,1));t.alive=false;CHECK(d.Update(0,{t}).empty()&&d.Active()==0&&d.discarded==1);
}
void MovingAndBlocked(const DelayedHitRule& r){
 DelayedHits d;auto t=Target();CHECK(d.Schedule(Seed(r),r,t,1));CHECK(d.Update(.25f,{t}).empty());
 t.aimPoint=t.position={2,0,0};t.hurt.front().a=t.hurt.front().b={2,0,0};
 auto hit=d.Update(.15f,{t});CHECK(hit.size()==1&&Near(hit.front().hit.point.x,2));
 // Hurt geometry can move away from the attachment/aim reference; a pulse
 // must independently intersect it, even though the entity is still alive.
 t.hurt.front().a=t.hurt.front().b={5,0,0};CHECK(d.Update(.05f,{t}).empty()&&d.misses==1);
 t.hurt.front().a=t.hurt.front().b={2.5f,0,0};
 const SceneObstruction wall=[](Vec3,Vec3,float& fraction){fraction=.5f;return true;};
 CHECK(d.Update(.05f,{t},wall).empty()&&d.misses==2);
 CHECK(d.Update(.05f,{t}).size()==1);
 // The source rule itself remains sticky for all seven near pulses. Test a
 // longer local schedule separately to observe anchor expiry and escape.
 auto longRule=r;longRule.offsets={.4f,1.0f};longRule.pulseWindows={20,21};
 DelayedHits expired;t=Target();CHECK(expired.Schedule(Seed(longRule),longRule,t,1));
 CHECK(expired.Update(.25f,{t}).empty());CHECK(expired.Update(.15f,{t}).size()==1);
 CHECK(expired.Update(.25f,{t}).empty());CHECK(expired.Update(.25f,{t}).empty());
 t.aimPoint=t.position={3,0,0};t.hurt.front().a=t.hurt.front().b={3,0,0};
 CHECK(expired.Update(.1f,{t}).empty()&&expired.misses==1&&expired.Active()==0);
}
void IndependentHp(const DelayedHitRule& r){
 Practice practice;PracticeTarget target;target.id=17;target.generation=2;target.radius=.1f;target.hurt=Target().hurt;
 practice.Refresh({target},25);DelayedHits d;CHECK(d.Schedule(Seed(r),r,practice.Geometry().front(),3));
 for(int i=0;i<60&&d.Active();++i){for(const auto& pulse:d.Update(1.f/60,practice.Geometry())){
   CombatContact c;c.key={pulse.hit.targetId,pulse.hit.generation};c.part=pulse.hit.partId;c.point=pulse.hit.point;
   c.strike=uint64_t(pulse.hit.windowId)+1;c.seconds=pulse.hit.seconds;
   CHECK(practice.ApplyContact(c,pulse.hit.motionSerial,pulse.hit.slot,pulse.hit.damage));}}
 CHECK(practice.hits==3&&Near(practice.targets.at(17).hp,0)&&Near(float(practice.total),25));
 CHECK(d.pulses==3&&d.discarded==1&&d.Active()==0);
}
void Validation(const DelayedHitRule& r){
 auto t=Target();DelayedHits d;const auto seed=Seed(r);const auto nan=std::numeric_limits<float>::quiet_NaN();
 CHECK(Throws([&]{d.Update(-.001f,{t});}));CHECK(Throws([&]{d.Update(.251f,{t});}));CHECK(Throws([&]{d.Update(nan,{t});}));
 CHECK(!d.Schedule(seed,r,t,1,nan)&&!d.Schedule(seed,r,t,1,0,nan));
 CHECK(!d.Schedule(seed,r,t,1,0,.251f));auto bad=r;bad.offsets[1]=bad.offsets[0];CHECK(!d.Schedule(seed,bad,t,1));
 auto wrong=seed;wrong.generation=3;CHECK(!d.Schedule(wrong,r,t,1));wrong=seed;wrong.seconds=r.end;CHECK(!d.Schedule(wrong,r,t,1));
 wrong=seed;wrong.damage=nan;CHECK(!d.Schedule(wrong,r,t,1));wrong=seed;wrong.point.x=nan;CHECK(!d.Schedule(wrong,r,t,1));
}
void BoundedSourceCycles(const DelayedHitRule& r){
 const auto begin=std::chrono::steady_clock::now();DelayedHits capacity;auto t=Target();size_t pendingPeak=0;
 for(uint64_t i=1;i<=128;++i){CHECK(capacity.Schedule(Seed(r,i),r,t,1));pendingPeak=std::max(pendingPeak,capacity.Active());}
 CHECK(!capacity.Schedule(Seed(r,129),r,t,1)&&capacity.Active()==128);
 CHECK(capacity.Update(.25f,{t}).empty());CHECK(capacity.Update(.25f,{t}).size()==384);CHECK(capacity.Update(.25f,{t}).size()==512);
 CHECK(capacity.Active()==0&&capacity.pulses==896);
 DelayedHits ledger;TargetIdentities identities;size_t ledgerPeak=0,identityPeak=0;uint32_t initialGeneration=0;
 constexpr uint32_t cycles=20000;
 for(uint32_t i=1;i<=cycles;++i){
  identities.Begin();const auto generation=identities.Observe(i,uintptr_t(i)+1,UINT64_C(0x100000000)+i,1);identities.End();
  if(i==1)initialGeneration=generation;identityPeak=std::max(identityPeak,identities.Size());CHECK(identities.Size()<=4096);
  t=Target(generation);CHECK(ledger.Schedule(Seed(r,i,generation),r,t,3));
  ledgerPeak=std::max(ledgerPeak,ledger.RetainedIdentities());CHECK(ledger.RetainedIdentities()<=4096);
  CHECK(ledger.Update(.25f,{t}).empty());CHECK(ledger.Update(.25f,{t}).size()==3);CHECK(ledger.Update(.25f,{t}).size()==4);
 }
 CHECK(ledger.Active()==0&&ledger.pulses==uint64_t(cycles)*7&&ledgerPeak==4096&&identityPeak==4096);
 identities.Begin();CHECK(identities.Observe(1,2,UINT64_C(0x100000001),1)!=initialGeneration);identities.End();
 // A recent source remains suppressed; the bounded ledger evicts the oldest.
 t=Target(cycles);CHECK(!ledger.Schedule(Seed(r,cycles,cycles),r,t,3));
 t=Target(1);CHECK(ledger.Schedule(Seed(r,1,1),r,t,3));
 const auto wallMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
 std::cout<<"source cycles="<<cycles<<", pending peak="<<pendingPeak<<", delayed ledger peak="<<ledgerPeak
          <<", target identity peak="<<identityPeak<<", measured wall ms="<<wallMs<<'\n';
}
}
int main(int argc,char** argv){try{CHECK(argc==2);const auto rule=SourceRule(argv[1]);TimingAndIdentity(rule);BirthAndStop(rule);
 MovingAndBlocked(rule);IndependentHp(rule);Validation(rule);BoundedSourceCycles(rule);
 std::cout<<"delayed hits: original seven offsets, identity, birth, geometry, HP, bounds and source cycles passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
