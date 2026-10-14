#include "../native/module.cpp"
#include "test_support.h"
#include <thread>

// Exercise the real asset loader and module entry points with an in-memory
// managed object graph. No game, backend or entity HP writer is used.
namespace {
Mat Identity(){Mat m;for(int i=0;i<4;++i)m.at(i,i)=1;return m;}
struct Object {
 bool alive=true,valid=true,enabled=true,matrixAvailable=true;
 uint32_t uid=17,generation=5;uint64_t server=12345,battle=76543;
 Vec3 position{};Quat rotation{};Mat world=Identity();Object* model=nullptr;Object* parent=nullptr;
 struct Bounds{Vec3 center,extents;} bounds{{0,1,3},{.3f,1,.4f}};
 std::vector<void*> renderers;
};
Object root,camera,enemy,model,renderer,hand;
std::vector<void*> enemies;
int count=0;unsigned invokes=0,writes=0;
int containerTag=0,unorderedTag=0,listTag=0;
enum Field { Container=1,Items,ListItems,ListSize };
void* __cdecl ObjectClass(void*){return &listTag;}
void* __cdecl FindField(const void*,const char* name){
 if(std::string(name)=="m_items")return reinterpret_cast<void*>(Items);
 if(std::string(name)=="_items")return reinterpret_cast<void*>(ListItems);
 if(std::string(name)=="_size")return reinterpret_cast<void*>(ListSize);
 return nullptr;
}
void* BE_CALL ReadField(void*,const void* field,void*){
 switch(reinterpret_cast<uintptr_t>(field)){
 case Container:return &containerTag;case Items:return &listTag;
 case ListItems:return &enemies;case ListSize:count=int(enemies.size());return &count;
 default:return nullptr;}
}
void* BE_CALL Unbox(void*,void* value){return value;}
void BE_CALL Free(void*,uint32_t){}
void* BE_CALL Invoke(void*,const void* token,void* self,void** args,void** exception){
 ++invokes;*exception=nullptr;auto* o=static_cast<Object*>(self);
 switch(Index(reinterpret_cast<uintptr_t>(token)-1)){
 case AllEnemies:return &unorderedTag;
 case ArrayLength:{static int size;size=int(static_cast<std::vector<void*>*>(self)->size());return &size;}
 case ArrayValue:return static_cast<std::vector<void*>*>(self)->at(*static_cast<int*>(args[0]));
 case Valid:return &o->valid;case AliveEntity:return &o->alive;
 case EntityUid:return &o->uid;case EntityServerId:return &o->server;
 case EntityServerGeneration:return &o->generation;case EntityServerBattleId:return &o->battle;
 case EntityPosition:case WorldPosition:return &o->position;
 case EntityName:return nullptr;
 case ModelComponent:return self;case ModelGo:return o->model;
 case GetComponents:return &o->renderers;
 case RendererEnabled:return &o->enabled;case RendererBounds:return &o->bounds;
 case WorldMatrix:if(o&&o->matrixAvailable)return &o->world;break;
 case LocalRotation:case Rotation:return &o->rotation;
 case SetLocalRotation:o->rotation=*static_cast<Quat*>(args[0]);++writes;return nullptr;
 case SetWorldPosition:o->position=*static_cast<Vec3*>(args[0]);o->world.at(0,3)=o->position.x;o->world.at(1,3)=o->position.y;o->world.at(2,3)=o->position.z;return nullptr;
 case Parent:return o->parent;
 case Alive:{static bool alive;const auto* value=static_cast<Object*>(args[0]);alive=value&&value->alive;return &alive;}
 default:break;
 }
 *exception=reinterpret_cast<void*>(1);return nullptr;
}
BE_Result BE_CALL Reply(void*,const char*,BE_Result result,const char* body){CHECK(Json::parse(body).is_object());return result;}
Json ReadJson(const std::filesystem::path& path){std::ifstream input(path);CHECK(bool(input));Json j;input>>j;return j;}
CombatEvent Hit(const Controller& c,size_t window=0){
 const auto& range=localMode.rules.motions.at(c.slot).outgoing.at(window);
 CombatEvent event;event.motionSerial=c.motionSerial;event.slot=c.slot;
 event.seconds=(range.begin+range.end)*.5f;event.windowId=window;event.targetId=17;event.generation=1;event.damage=10;return event;
}
void LoadContracts(){
 for(const char* mode:{"longsword","sword_shield","greatsword","lance","light_bowgun","bow"}){
  ReadMode(mode);const auto manifest=ReadJson(activeAssetRoot/L"combat_rules.json");
  CHECK(!localMode.combatEnabled&&controller.combatAllowed&&controller.combatSelectSlot);
  size_t enemyWindows=0,excluded=0;
  for(auto rows=manifest.at("authored_windows").begin();rows!=manifest.at("authored_windows").end();++rows){
   const int slot=std::stoi(rows.key());size_t expected=0;
   for(const auto& row:rows.value()){
    const auto role=row.at("contact_role").get<std::string>();
    if(role=="ally_only"||role=="delayed_effect"){++excluded;continue;}
    ++expected;++enemyWindows;const auto& loaded=practice.authoredWindows.at(slot).at(expected-1);
    CHECK(Near(loaded.begin,row.at("begin").get<float>())&&Near(loaded.end,row.at("end").get<float>()));
    CHECK(loaded.strike==row.at("window_id").get<uint64_t>()+1);
    CHECK(loaded.lane==(role=="shield"?1U:role=="body"?2U:0U));
    CHECK(Near(localMode.rules.motions.at(slot).Gain(row.at("window_id").get<size_t>()),
      manifest.at("motions").at(rows.key()).at("gauge_gain_by_window").at(row.at("window_id").get<size_t>()).get<float>()));
   }
   const auto loaded=practice.authoredWindows.find(slot);
   CHECK((loaded==practice.authoredWindows.end()?0:loaded->second.size())==expected);
  }
  CHECK(enemyWindows>0);if(std::string(mode)=="longsword")CHECK(excluded>0);
  for(auto rows=manifest.at("motions").begin();rows!=manifest.at("motions").end();++rows){
   const auto& loaded=localMode.rules.motions.at(std::stoi(rows.key()));
   CHECK(loaded.motionValueByWindow==rows.value().at("motion_value_by_window").get<std::vector<float>>());
   if(rows.value().contains("timed_inputs")){
    CHECK(loaded.timedInputs.size()==rows.value().at("timed_inputs").size());
    for(size_t i=0;i<loaded.timedInputs.size();++i){const auto& branch=loaded.timedInputs[i];const auto& source=rows.value().at("timed_inputs").at(i);
     CHECK(Near(branch.inputGateBegin,source.at("input_gate_begin").get<float>())&&Near(branch.inputGateEnd,source.at("input_gate_end").get<float>()));
    }
    auto invalidGate=manifest;invalidGate["motions"][rows.key()]["timed_inputs"][0]["input_gate_end"]=.01f;
    CHECK(Throws([&]{ReadCombatAssets(invalidGate,mode,controller.clips);}));
   }
   if(rows.value().contains("projectile_tiers")){
    CHECK(loaded.projectileTiers.size()==rows.value().at("projectile_tiers").size());
    CHECK(loaded.projectileMarkers==rows.value().at("projectile_markers").get<std::vector<float>>());
    auto invalidArrow=manifest;invalidArrow["motions"][rows.key()]["projectile_tiers"][0]["arrows"]=0;
    CHECK(Throws([&]{ReadCombatAssets(invalidArrow,mode,controller.clips);}));
   }
  }
  auto bad=manifest;bad["mode"]="invalid";CHECK(Throws([&]{ReadCombatAssets(bad,mode,controller.clips);}));
 }
 ReadMode("longsword");auto manifest=ReadJson(activeAssetRoot/L"combat_rules.json");
 const auto& p=manifest.at("source_parameters");
 CHECK(Near(localMode.rules.foresightMinimumGauge,p.at("foresight_minimum_gauge").get<float>()));
 CHECK(Near(controller.specialSheatheSeconds,p.at("special_sheathe_seconds").get<float>()));
 for(int i=1;i<=3;++i)CHECK(Near(localMode.rules.levelDurations[i],p.at("level_durations").at(i).get<float>()));
 CHECK(localMode.rules.gaugeLevelCost==p.at("gauge_level_cost").get<int>());
 const auto sourceLoaded=ReadCombatAssets(manifest,"longsword",controller.clips);
 CHECK(sourceLoaded.delayedRules.at(1000).size()==1&&sourceLoaded.delayedRules.at(1000).front().pulseWindows.size()==7);
 CHECK(Near(sourceLoaded.delayedRules.at(1000).front().radius,1)&&Near(sourceLoaded.delayedRules.at(1000).front().offsets.front(),.4f));
 auto wrongPulse=manifest;
 wrongPulse["delayed_hit"]["triggers"]["1000"][0]["pulse_window_ids"][0]=1;
 CHECK(Throws([&]{ReadCombatAssets(wrongPulse,"longsword",controller.clips);}));
 // Non-default fixture detects a silently ignored source-parameter section.
 manifest["source_parameters"]["foresight_minimum_gauge"]=23.5f;
 manifest["source_parameters"]["level_durations"][2]=47.25f;
 const auto altered=ReadCombatAssets(manifest,"longsword",controller.clips);
 CHECK(Near(altered.rules.foresightMinimumGauge,23.5f)&&Near(altered.rules.levelDurations[2],47.25f));
 manifest["source_parameters"]["gauge_level_cost"]=0;
 CHECK(Throws([&]{ReadCombatAssets(manifest,"longsword",controller.clips);}));
}
void ManualCombat(){
 ReadMode("longsword");localMode.Reset("longsword");localMode.combatEnabled=true;
 CHECK(controller.start(configuredIdleSlot));controller.input(Command::Helm);CHECK(controller.resting());
 localMode.level=1;controller.input(Command::Helm);CHECK(controller.slot==170);
 localMode.Tick(0,controller);const auto serial=controller.motionSerial;
 controller.tick(controller.clips.at(170).duration()+.05f);localMode.Tick(.05f,controller);
 CHECK(controller.resting()&&controller.motionSerial!=serial&&localMode.level==1&&!localMode.HelmConnected());
 CHECK(controller.start(configuredIdleSlot));localMode.Reset("longsword");localMode.combatEnabled=true;localMode.level=2;
 controller.input(Command::Helm);CHECK(controller.slot==170);auto hit=Hit(controller);
 controller.tick(hit.seconds);localMode.Tick(0,controller);auto follow=localMode.Consume(hit,controller);
 CHECK(follow.accepted&&follow.followupSlot==1000&&Near(follow.startSeconds,1.5f));
 CHECK(!localMode.Consume(hit,controller).accepted);
 CHECK(controller.transitionToSlot(follow.followupSlot,follow.startSeconds,Command::Helm));
 CHECK(controller.slot==1000&&Near(controller.seconds,1.5f));localMode.Tick(0,controller);CHECK(localMode.level==1);
 for(int level=0;level<=3;++level){
  CHECK(controller.start(configuredIdleSlot));localMode.Reset("longsword");localMode.combatEnabled=true;
  localMode.level=level;localMode.levelSeconds=level?localMode.rules.levelDurations[level]-.5f:0;
  CHECK(controller.transitionToSlot(306,80.f/60,Command::Sheathe));controller.input(Command::IaiSpirit);
  CHECK(controller.slot==308+level);localMode.Tick(0,controller);CHECK(localMode.level==std::max(0,level-1));
  auto incoming=Hit(controller);incoming.kind=CombatEventKind::IncomingAttack;incoming.attackSerial=99;
  incoming.seconds=.05f;controller.tick(.05f);const auto outcome=localMode.Consume(incoming,controller);
  CHECK(outcome.evaded&&outcome.counter&&localMode.level==level);
 }
 // An automatic source tour explicitly bypasses resource gates.
 CHECK(controller.start(configuredIdleSlot));localMode.Reset("longsword");localMode.combatEnabled=false;
 controller.input(Command::Helm);CHECK(controller.slot==1000&&Near(controller.seconds,0));controller.stop();
}
void HardwareCameraControls(){
 ReadMode("longsword");Configure("{\"mode\":\"longsword\",\"keyboard_preset\":\"custom\",\"keys\":{\"reload\":81,\"aim\":16}}");
 const auto pressed=[](int key){return key>0;};
 CHECK(config.lock_key=='Q'&&config.focus_key==VK_CONTROL&&config.cycle_key==VK_TAB);
 CHECK(CameraControlDown(config.lock_key,pressed)&&CameraControlDown(config.focus_key,pressed)&&CameraControlDown(config.cycle_key,pressed));
 CHECK(KeyBitsFrom([](int key){return key=='Q';})==0);
 CHECK(KeyBitsFrom([](int key){return key==VK_SHIFT;})==SheatheBit);
 CHECK(!CameraControlDown(VK_SHIFT,pressed));
 Configure("{\"mode\":\"longsword\",\"keyboard_preset\":\"custom\",\"keys\":{\"attack\":81}}");
 CHECK(!CameraControlDown('Q',pressed)&&KeyBitsFrom([](int key){return key=='Q';})==SlashBit);
 Configure("{\"mode\":\"longsword\",\"keyboard_preset\":\"custom\",\"spirit_alt_key\":9}");
 CHECK(!CameraControlDown(VK_TAB,pressed)&&KeyBitsFrom([](int key){return key==VK_TAB;})==SpiritBit);
 ReadMode("bow");Configure("{\"mode\":\"bow\",\"keyboard_preset\":\"custom\",\"keys\":{\"reload\":81,\"spirit\":17}}");
 CHECK(!CameraControlDown('Q',pressed)&&CameraControlDown(VK_CONTROL,pressed));
 CHECK(KeyBitsFrom([](int key){return key=='Q';})==ReloadBit&&KeyBitsFrom([](int key){return key==VK_CONTROL;})==0);
 // Actions reserve the current toggle, F9 and Esc. Camera controls also
 // reserve F8; a custom F10 toggle leaves F8 available to an action.
 CHECK(Throws([]{Configure("{\"toggle_key\":119,\"keyboard_preset\":\"custom\",\"keys\":{\"attack\":119}}");}));
 CHECK(Throws([]{Configure("{\"toggle_key\":120,\"keyboard_preset\":\"custom\",\"keys\":{\"attack\":119}}");}));
 CHECK(Throws([]{Configure("{\"toggle_key\":120,\"lock_key\":119}");}));
 CHECK(Throws([]{Configure("{\"spirit_key\":119}");}));
 CHECK(Throws([]{Configure("{\"spirit_alt_key\":119}");}));
 CHECK(Throws([]{Configure("{\"focus_key\":81}");}));
 Configure("{\"mode\":\"bow\",\"toggle_key\":121,\"keyboard_preset\":\"custom\",\"keys\":{\"attack\":119}}");
 CHECK(KeyBitsFrom([](int key){return key==VK_F8;})==SlashBit);
 Configure("{}");ReadMode("longsword");
}
void SwordShieldPhysicalTiming(){
 ReadMode("sword_shield");localMode.Reset("sword_shield");localMode.combatEnabled=true;
 const float originalFraction=controller.transitionFraction;
 const float originalBuffer=controller.bufferSeconds;
 std::vector<std::pair<int,TimedInputBranch>> stages;
 for(const auto&[slot,rule]:localMode.rules.motions)for(const auto& branch:rule.timedInputs)stages.push_back({slot,branch});
 CHECK(stages.size()>=3);
 for(const auto&[slot,branch]:stages){
  CHECK(branch.command=="special"&&branch.normalSlot>0&&branch.HasInputGate());
  CHECK(controller.start(configuredIdleSlot));CHECK(controller.transitionToSlot(slot,(branch.window.begin+branch.window.end)*.5f));
  controller.transitionFraction=originalFraction;controller.bufferSeconds=originalBuffer;
  controller.hardwareInput(SlashBit,SlashBit);CHECK(controller.slot==branch.timedSlot); // Published defaults must actually permit the source Just phase.
  for(const auto&[time,expected]:std::vector<std::pair<float,int>>{{branch.window.begin,branch.timedSlot},{branch.window.end,branch.normalSlot},{std::max(0.f,branch.window.begin-.01f),branch.normalSlot}}){
   CHECK(controller.start(configuredIdleSlot));CHECK(controller.transitionToSlot(slot,time));controller.transitionFraction=0;
   controller.hardwareInput(SlashBit,SlashBit);CHECK(controller.slot==expected);
  }
  CHECK(branch.inputGateBegin>.02f);
  CHECK(controller.start(configuredIdleSlot));CHECK(controller.transitionToSlot(slot,branch.inputGateBegin-.01f));controller.transitionFraction=1;
  controller.hardwareInput(SlashBit,SlashBit);CHECK(controller.slot==slot&&controller.buffered());
  controller.tick(.011f);CHECK(controller.slot==branch.normalSlot); // Bit 28 permits the captured normal branch before Just opens.
  CHECK(controller.transitionToSlot(slot,branch.inputGateEnd));CHECK(!localMode.CanTransition(Command::Special,controller));
  // The original physical edge falls outside the Just interval; allowing
  // the buffered dispatch only inside the interval must retain normal MV.
  CHECK(branch.window.begin>.02f);
  const float edge=branch.window.begin-.01f,dispatch=(branch.window.begin+branch.window.end)*.5f;
  const auto sourceGate=controller.combatCanTransition;controller.combatCanTransition={};
  CHECK(controller.start(configuredIdleSlot));CHECK(controller.transitionToSlot(slot,edge));
  controller.transitionFraction=dispatch/controller.clips.at(slot).duration();controller.bufferSeconds=1;
  controller.hardwareInput(SlashBit,SlashBit);CHECK(controller.slot==slot&&controller.buffered());
  controller.tick(dispatch-edge+.001f);CHECK(controller.slot==branch.normalSlot);
  controller.combatCanTransition=sourceGate;
  // A new edge in a new motion can qualify; the old serial cannot carry it.
  CHECK(controller.transitionToSlot(slot,dispatch));controller.transitionFraction=0;
  controller.hardwareInput(SlashBit,SlashBit);CHECK(controller.slot==branch.timedSlot);
 }
 controller.transitionFraction=originalFraction;ReadMode("longsword");
}
void LiveSnapshots(){
 practice.Clear();targetIdentities.Clear();actor={};actor.root=&root;actor.cameraTransform=&camera;
 enemy.position={0,0,3};enemy.model=&model;model.renderers={&renderer};enemies={&enemy};
 practiceReady=true;practiceObjectClass=ObjectClass;practiceFindField=FindField;
 worldContainer.field_info=reinterpret_cast<const void*>(Container);rendererClass.type_object=&renderer;
 CHECK(QueryPracticeTargets());const auto first=practice.targets.at(17).generation;
 CHECK(first>0&&practice.targets.at(17).hurt.size()==1&&practice.targets.at(17).hurt.front().part==1);
 CHECK(Near(practice.targets.at(17).hurt.front().a.y,0)&&Near(practice.targets.at(17).hurt.front().b.y,2));
 CHECK(practice.targets.at(17).approximateGeometry);CHECK(practice.Strike(17,37));
 CHECK(QueryPracticeTargets()&&Near(practice.targets.at(17).hp,config.practice_hp-37));
 ++enemy.generation;CHECK(QueryPracticeTargets());CHECK(practice.targets.at(17).generation!=first&&Near(practice.targets.at(17).hp,config.practice_hp));
 const auto second=practice.targets.at(17).generation;enemies.clear();CHECK(QueryPracticeTargets()&&practice.targets.empty());
 enemies={&enemy};CHECK(QueryPracticeTargets()&&practice.targets.at(17).generation!=second);
 renderer.bounds.extents={0,1,.4f};CHECK(QueryPracticeTargets());CHECK(practice.targets.at(17).hurt.front().part==0);
 renderer.bounds.extents={.3f,1,.4f};enemy.alive=false;CHECK(QueryPracticeTargets()&&practice.targets.empty());enemy.alive=true;
}
void ActualTrainingPump(){
 ReadMode("longsword");config.local_practice=true;config.practice_target="monsters";config.target_lock=false;
 config.root_motion=false;config.sound_enabled=false;config.speed=1;practiceResetRequest=false;autoDemo=false;
 actor.root=&root;actor.bones.resize(21);for(auto& bone:actor.bones)bone.transform=&root;
 actor.bones[12].transform=&hand;actor.order.clear();hand.position={0,1,2};hand.world=Identity();hand.world.at(1,3)=1;hand.world.at(2,3)=2;
 actor.bladeCalibrated=false;practiceBladeAxis={0,0,1};
 practice.Clear();localMode.Reset("longsword");localMode.combatEnabled=true;localMode.energy=0;
 const int slot=101;auto& rule=localMode.rules.motions.at(slot);rule.outgoing={{0,.003f}};rule.gaugeGainByWindow={10};
 rule.motionValueByWindow={100};rule.motionValueByLevelByWindow.clear();
 practice.authoredWindows[slot]={{0,.003f,1,1,0,0,0}};
 CHECK(controller.start(configuredIdleSlot));CHECK(controller.transitionToSlot(slot));pumpedMotionSerial=0;
 CHECK(AdvanceShowcaseFrame(1.f/30));CHECK(practice.hits==1&&Near(practice.targets.at(17).hp,config.practice_hp-config.practice_damage));
 CHECK(localMode.energy>9&&localMode.energy<=10);
 // Repeating the frame cannot replay a closed early window.
 CHECK(AdvanceShowcaseFrame(1.f/30)&&practice.hits==1);
 // A newly visible target is sampled at its birth position, never swept from
 // an unrelated old entity that shared the UID.
 practice.Clear();enemies.clear();localMode.Reset("longsword");localMode.combatEnabled=true;
 rule.outgoing={{0,.2f}};practice.authoredWindows[slot]={{0,.2f,1,1,0,0,0}};
 CHECK(controller.start(configuredIdleSlot));CHECK(controller.transitionToSlot(slot));pumpedMotionSerial=0;
 CHECK(AdvanceShowcaseFrame(.02f)&&practice.hits==0);
 enemies={&enemy};renderer.bounds.center={5,1,3};enemy.position={5,0,3};
 CHECK(AdvanceShowcaseFrame(.02f)&&practice.hits==0);
 renderer.bounds.center={0,1,3};enemy.position={0,0,3};
 CHECK(AdvanceShowcaseFrame(.02f)&&practice.hits==1);
 // Source marker at the end of a substep emits an unmoved projectile. Its
 // first real movement occurs after birth; source identity uses the marker.
 ReadMode("light_bowgun");config.target_lock=false;practice.Clear();localProjectiles.Clear();enemies.clear();
 localMode.Reset("light_bowgun");localMode.combatEnabled=true;projectileProp=Json::object();
 const int fire=controller.moves.at(Command::Fire).front();projectileEvents[fire]={.01f};
 CHECK(controller.start(configuredIdleSlot));CHECK(controller.transitionToSlot(fire));controller.tick(.01f);
 projectileMotion=std::numeric_limits<uint64_t>::max();PumpTrainingStep(.01f);
 CHECK(localProjectiles.active.size()==1&&Near(localProjectiles.active.front().age,0));
 CHECK(Near(localProjectiles.active.front().sourceSeconds,.01f)&&Close(localProjectiles.active.front().position,hand.position));
 controller.tick(.01f);PumpTrainingStep(.01f);CHECK(localProjectiles.active.size()==1&&Near(localProjectiles.active.front().age,.01f));
 CHECK(localMode.ammo==localMode.rules.ammoCapacity-1);
 localProjectiles.Clear();config.local_practice=false;config.target_lock=true;
 enemies={&enemy};ReadMode("longsword");renderer.bounds.center={0,1,3};
}
void FourWeaponDamageAndRelease(){
 config.local_practice=true;config.practice_target="monsters";config.target_lock=false;
 config.root_motion=false;config.sound_enabled=false;config.speed=1;practiceResetRequest=false;autoDemo=false;
 actor.root=&root;actor.bones.resize(21);for(auto& bone:actor.bones)bone.transform=&root;
 actor.bones[8].transform=&hand;actor.bones[12].transform=&hand;actor.order.clear();
 actor.bladeCalibrated=false;practiceBladeAxis={0,0,1};actor.scale=1;
 hand.position={0,1,2};hand.world=Identity();hand.world.at(1,3)=1;hand.world.at(2,3)=2;
 enemy.position={0,0,3};renderer.bounds.center={0,1,3};renderer.bounds.extents={.3f,1,.4f};enemies={&enemy};
 ReadMode("sword_shield");practiceBladeAxis={0,0,1};std::vector<std::pair<int,MotionCombatRule>> rushFinishes;
 for(const auto&[slot,rule]:localMode.rules.motions)if(rule.hitFollowupSlot)rushFinishes.push_back({slot,rule});
 CHECK(rushFinishes.size()==2);
 for(const auto&[slot,rule]:rushFinishes){
  CHECK(slot==10319||slot==10341);
  CHECK(Near(rule.MotionValue(0),slot==10319?45.f:65.f));
  CHECK(Near(rule.MotionValue(1),slot==10319?8.f:13.f));
  const auto& authored=practice.authoredWindows.at(slot);
  const auto weapon=std::find_if(authored.begin(),authored.end(),[](const auto& window){return window.lane==0;});CHECK(weapon!=authored.end());
  CHECK(weapon->strike==1&&std::count_if(authored.begin(),authored.end(),[](const auto& window){return window.lane==0;})==2);
  const auto contactTime=weapon->begin+.0005f;
  practice.Clear();localMode.Reset("sword_shield");localMode.combatEnabled=true;enemies.clear();
  CHECK(controller.start(configuredIdleSlot));CHECK(controller.transitionToSlot(slot,contactTime-.001f));PumpTrainingStep(0);controller.tick(.001f);PumpTrainingStep(.001f);
  CHECK(controller.slot==slot&&practice.hits==0); // A miss never attaches the rush finisher.
  // Primary constraint thrust and the later grazing contact each retain
  // their own source MV; either legitimate contact can attach the finish.
  for(const auto& hitWindow:authored)if(hitWindow.lane==0){
   practice.Clear();localMode.Reset("sword_shield");localMode.combatEnabled=true;enemies={&enemy};
   CHECK(controller.start(configuredIdleSlot));CHECK(controller.transitionToSlot(slot,hitWindow.begin-.0005f));PumpTrainingStep(0);controller.tick(.001f);PumpTrainingStep(.001f);
   CHECK(practice.hits==1&&controller.slot==rule.hitFollowupSlot&&controller.motionCommand()==Command::Special);
   CHECK(practice.events.size()==1&&practice.events.front().windowId==hitWindow.strike-1);
   const float sourceDamage=config.practice_damage*rule.MotionValue(size_t(hitWindow.strike-1))/100;
   CHECK(Near(practice.events.front().damage,sourceDamage,.002f)&&Near(practice.targets.at(17).hp,config.practice_hp-sourceDamage,.002f));
   const auto& attached=localMode.rules.motions.at(rule.hitFollowupSlot);CHECK(attached.completionFollowupSlot>0);
   controller.tick(controller.clips.at(rule.hitFollowupSlot).duration()+.001f);CHECK(controller.slot==attached.completionFollowupSlot);
  }
 }
 ReadMode("greatsword");practiceBladeAxis={0,0,1};std::vector<int> trueCharges;
 for(const auto&[slot,rule]:localMode.rules.motions)if(rule.trueCharge)trueCharges.push_back(slot);
 CHECK(trueCharges.size()>=3);
 for(int slot:trueCharges)for(bool marked:{false,true}){
  practice.Clear();localMode.Reset("greatsword");localMode.combatEnabled=true;
  const auto rule=localMode.rules.motions.at(slot);const auto first=rule.trueChargeFirstWindow,finish=rule.trueChargeFinalWindow;
  CHECK(rule.motionValueByWindow.size()==rule.outgoing.size());
  config.practice_target=marked?"dummy":"monsters";
  if(marked){
   PracticeTarget target;target.id=0xfffffffe;target.name="explicit marked training part";target.position={0,1,3};target.radius=.4f;target.distance=3;target.generation=1;
   HurtCapsule upper{{0,.8f,3},{0,1.3f,3},.4f,9,1};upper.weakPointKnown=upper.weakPoint=true;target.hurt={upper};
   practice.Refresh({target},config.practice_hp);
  }
  CHECK(controller.start(configuredIdleSlot));CHECK(controller.transitionToSlot(slot,rule.outgoing[first].begin-.0005f));PumpTrainingStep(0);controller.tick(.001f);PumpTrainingStep(.001f);
  CHECK(practice.events.size()==1&&localMode.TrueChargeQualified()==marked);
  const auto target=practice.events.front().targetId;
  CHECK(practice.events.front().weakPointKnown==marked);
  const float firstDamage=config.practice_damage*rule.MotionValue(first)/100;
  CHECK(Near(float(practice.total),firstDamage));
  const float finalTime=rule.outgoing[finish].begin+.0005f;
  while(controller.seconds<finalTime-1e-6f){const float delta=std::min(.1f,finalTime-controller.seconds);controller.tick(delta);PumpTrainingStep(delta);}
  const float finalDamage=config.practice_damage*rule.MotionValue(finish)/100*(marked?rule.weakFinishMultiplier:1);
  CHECK(Near(float(practice.total),firstDamage+finalDamage,.002f)&&Near(practice.targets.at(target).hp,config.practice_hp-firstDamage-finalDamage,.002f));
  const auto hitCount=practice.hits;PumpTrainingStep(0);CHECK(practice.hits==hitCount); // No replay damage.
  CHECK(controller.transitionToSlot(configuredIdleSlot));localMode.Tick(0,controller);CHECK(!localMode.TrueChargeQualified());
 }
 ReadMode("lance");config.practice_target="dummy";std::vector<std::pair<int,MotionCombatRule>> stances;
 for(const auto&[slot,rule]:localMode.rules.motions)if(rule.lanceCounter&&rule.counterShortSlot)stances.push_back({slot,rule});
 CHECK(stances.size()==1);const auto stance=stances.front();
 for(int holdCase=0;holdCase<3;++holdCase){const bool full=holdCase>0;
  practice.Clear();localMode.Reset("lance");localMode.combatEnabled=true;CHECK(controller.start(configuredIdleSlot));
  controller.held(Command::Counter,true);CHECK(controller.slot==stance.first&&controller.holding(Command::Counter));
  controller.tick(full?stance.second.counterFullSeconds+.02f:.1f);
  if(holdCase==2){
   controller.tick(controller.clips.at(stance.first).duration()+.1f);
   CHECK(controller.holding(Command::Counter)&&controller.seconds>=150.f/60&&controller.seconds<controller.clips.at(stance.first).duration());
  }
  practiceAttackRequest=true;practiceAttackFromFront=true;practiceAttackGuardable=true;PumpTrainingStep(0);
  CHECK(std::any_of(practice.targets.at(0xfffffffe).hurt.begin(),practice.targets.at(0xfffffffe).hurt.end(),[](const auto& part){return part.weakPointKnown&&part.weakPoint;}));
  CHECK(controller.slot==(full?stance.second.counterFullSlot:stance.second.counterShortSlot)&&!controller.holding(Command::Counter));
  CHECK(localMode.blocks==1&&Near(localMode.energy,localMode.rules.staminaMax-localMode.rules.guardHitCost));
  controller.held(Command::Counter,true);CHECK(!controller.holding(Command::Counter)); // Continued held key cannot restart after auto-release.
  controller.held(Command::Counter,false);
 }
 practice.Clear();localMode.Reset("lance");localMode.combatEnabled=true;CHECK(controller.start(configuredIdleSlot));controller.held(Command::Counter,true);
 for(int i=0;i<42&&localMode.blocks==0;++i){controller.tick(.1f);PumpTrainingStep(.1f);}
 CHECK(localMode.blocks==1&&controller.slot==stance.second.counterFullSlot&&!controller.holding(Command::Counter));
 CHECK(Near(localMode.energy,localMode.rules.staminaMax-localMode.rules.guardHitCost)); // Periodic dummy attack uses the same automatic-release outcome path.
 controller.held(Command::Counter,false);
 ReadMode("bow");config.practice_target="monsters";const int fire=controller.moves.at(Command::Fire).front();
 const auto bow=localMode.rules.motions.at(fire);CHECK(bow.projectileTiers.size()==4&&!bow.projectileMarkers.empty());
 const int powerSlot=controller.moves.at(Command::Spirit).front();const auto powerRule=localMode.rules.motions.at(powerSlot);
 CHECK(powerSlot!=fire&&powerRule.projectileTiers.size()==4&&!powerRule.projectileMarkers.empty());
 for(bool power:{false,true})for(int tier=1;tier<=4;++tier){
  practice.Clear();localProjectiles.Clear();localMode.Reset("bow");localMode.combatEnabled=true;
  CHECK(controller.start(configuredIdleSlot));
  if(!power||tier>1){
   controller.held(Command::Charge,true);controller.tick(localMode.rules.bowChargeSeconds.at(size_t((power?tier-1:tier)-1))+.001f);
   controller.held(Command::Charge,false);CHECK(controller.slot==fire&&!controller.holding(Command::Charge));
   if(power){
    controller.tick(bow.projectileMarkers.back()+.001f);projectileMotion=std::numeric_limits<uint64_t>::max();TickProjectiles(0);
    CHECK(localMode.BowTier()==tier-1);localProjectiles.Clear();
    controller.tick(std::max(0.f,controller.clips.at(fire).duration()*controller.transitionFraction-controller.seconds)+.001f);
   }
  }
  if(power){controller.input(Command::Spirit);CHECK(controller.slot==powerSlot);}
  CHECK(QueryPracticeTargets());const auto shotRule=power?powerRule:bow;const auto sourceSlot=power?powerSlot:fire;
  const auto energy=localMode.energy;controller.tick(shotRule.projectileMarkers.back()+.001f);projectileMotion=std::numeric_limits<uint64_t>::max();
  TickProjectiles(0);const auto expected=size_t(shotRule.projectileTiers[size_t(tier-1)].arrows)*shotRule.projectileMarkers.size();
  CHECK(localProjectiles.active.size()==expected&&localMode.BowTier()==tier&&Near(localMode.energy,energy-localMode.rules.bowShotCost));
  float emitted=0;for(const auto& shot:localProjectiles.active){
   CHECK(shot.slot==sourceSlot&&shot.motionSerial==controller.motionSerial);
   CHECK(Near(shot.damage,config.practice_damage*shotRule.projectileTiers[size_t(tier-1)].motionValue/100));emitted+=shot.damage;
   const auto& source=shotRule.projectileTiers[size_t(tier-1)];
   CHECK(Near(Retarget::Len(shot.velocity),source.speed));
   CHECK(Near(shot.range,source.range)&&Near(shot.radius,source.radius));
   CHECK(Near(shot.lifetime,source.range/source.speed));
  }
  TickProjectiles(0);CHECK(localProjectiles.active.size()==expected&&Near(localMode.energy,energy-localMode.rules.bowShotCost));
  for(int i=0;i<20&&!localProjectiles.active.empty();++i)TickProjectiles(.1f);
  CHECK(practice.hits==expected&&Near(float(practice.total),emitted,.01f)&&Near(practice.targets.at(17).hp,config.practice_hp-emitted,.01f));
 }
 for(size_t remaining:{0U,2U}){
  practice.Clear();localProjectiles.Clear();localMode.Reset("bow");localMode.combatEnabled=true;CHECK(QueryPracticeTargets());
  for(size_t i=0;i<LocalProjectiles::Capacity-remaining;++i)CHECK(localProjectiles.Launch({100,100,100},{1,0,0},30,0,false));
  CHECK(controller.start(configuredIdleSlot));controller.held(Command::Charge,true);controller.tick(.001f);controller.held(Command::Charge,false);
  projectileMotion=std::numeric_limits<uint64_t>::max();TickProjectiles(0);
  if(remaining==0){CHECK(localProjectiles.active.size()==LocalProjectiles::Capacity&&Near(localMode.energy,localMode.rules.staminaMax)&&localMode.BowTier()==0);}
  else{CHECK(localProjectiles.active.size()==LocalProjectiles::Capacity-1&&Near(localMode.energy,localMode.rules.staminaMax-localMode.rules.bowShotCost)&&localMode.BowTier()==1);}
 }
 localProjectiles.Clear();practice.Clear();ReadMode("longsword");config.local_practice=false;config.target_lock=true;
}
void CameraLifecycle(){
 camera.rotation={};root.matrixAvailable=true;camera.matrixAvailable=true;
 config.target_lock=true;config.target_camera="target";CHECK(QueryPracticeTargets());lockRequest=true;
 const auto original=camera.rotation;TickTargetCamera(.02f);CHECK(targetLock.Selected()&&ownedCamera&&writes>0);
 initialized=ready=true;bridge.reply=Reply;const auto before=invokes;
 CHECK(Message("test","{\"action\":\"target_release\"}")==BE_Result_Ok&&targetReleaseRequest);
 CHECK(Message("test","{\"action\":\"pause\"}")==BE_Result_Ok&&paused);
 CHECK(Message("test","{\"action\":\"resume\"}")==BE_Result_Ok&&!paused);
 CHECK(invokes==before);TickTargetCamera(.02f);CHECK(!targetLock.Selected()&&!ownedCamera&&Close(camera.rotation,original));
 lockRequest=true;TickTargetCamera(.02f);CHECK(ownedCamera);
 root.matrixAvailable=false;TickTargetCamera(.02f);CHECK(!ownedCamera&&!targetLock.Selected()&&Close(camera.rotation,original));root.matrixAvailable=true;
 lockRequest=true;TickTargetCamera(.02f);CHECK(ownedCamera);++enemy.generation;CHECK(QueryPracticeTargets());
 TickTargetCamera(.02f);CHECK(!ownedCamera&&!targetLock.Selected()&&Close(camera.rotation,original));
 lockRequest=true;TickTargetCamera(.02f);CHECK(ownedCamera);const Quat foreign{.2f,0,0,.9797959f};camera.rotation=foreign;
 targetReleaseRequest=true;TickTargetCamera(.02f);CHECK(!ownedCamera&&Close(camera.rotation,foreign));
 const auto calls=invokes;unityThread=GetCurrentThreadId();std::thread worker([]{Frame(.1f);});worker.join();CHECK(invokes==calls);
 lockRequest=true;camera.rotation={};TickTargetCamera(.02f);CHECK(ownedCamera);
 CHECK(Message("test","{\"action\":\"stop\"}")==BE_Result_Ok&&stopRequest&&ownedCamera);
 // Restoration is performed on the owner thread; the message only queues it.
 Cleanup();CHECK(!ownedCamera&&!targetLock.Selected()&&!controller.active&&Close(camera.rotation,Quat{}));
 initialized=ready=false;bridge={};unityThread=0;
}
void PresentationPreview(){
 ReadMode("longsword");config.enabled=true;config.local_practice=true;config.combat_rules=true;config.practice_target="monsters";
 config.root_motion=false;config.target_lock=false;config.sound_enabled=false;practiceResetRequest=false;autoDemo=false;paused=hudEditing=false;
 practice.Clear();CHECK(QueryPracticeTargets());localMode.Reset("longsword");localMode.combatEnabled=true;
 CHECK(controller.start(configuredIdleSlot));CHECK(ManualCombatActive());
 combatFollowupSlot=1000;CHECK(localProjectiles.Launch({0,0,0},{0,0,1},30,10,false));
 CHECK(BeginPresentationPreview(1000)&&presentationPreview&&!ManualCombatActive());
 CHECK(!localMode.combatEnabled&&combatFollowupSlot==0&&localProjectiles.active.empty());
 const auto hp=practice.targets.at(17).hp;CHECK(AdvanceShowcaseFrame(.1f));
 CHECK(Near(practice.targets.at(17).hp,hp)&&localMode.energy==0&&localMode.level==0&&delayedHits.Active()==0);
 CHECK(TakeManualControl()&&!presentationPreview&&ManualCombatActive()&&controller.resting()&&controller.idleSlot==configuredIdleSlot);
 controller.input(Command::Helm);CHECK(controller.resting()); // level zero cannot inherit the preview's combo
 // Preserve a real continuous melee sample across the manual→demo boundary:
 // the source window opens on the next step, but presentation must not hit.
 practiceBladeAxis={0,0,1};const int melee=controller.moves.at(Command::Slash).front();
 const auto& meleeWindows=practice.authoredWindows.at(melee);
 const auto window=std::find_if(meleeWindows.begin(),meleeWindows.end(),[](const auto& w){return w.lane==0&&w.begin>.002f;});CHECK(window!=meleeWindows.end());
 localMode.combatEnabled=false;CHECK(controller.transitionToSlot(melee,window->begin-.001f));PumpTrainingStep(0);
 const auto beforeDemoHP=practice.targets.at(17).hp;const auto beforeDemoHits=practice.hits;
 autoDemo=true;controller.tick(.002f);PumpTrainingStep(.002f);
 CHECK(Near(practice.targets.at(17).hp,beforeDemoHP)&&practice.hits==beforeDemoHits);
 CHECK(TakeManualControl()&&!autoDemo);
 autoDemo=true;controller.transitionToSlot(1000,1.5f);localMode.level=3;localMode.energy=100;
 CHECK(!ManualCombatActive()&&TakeManualControl()&&controller.resting()&&localMode.level==0&&localMode.energy==0);
 paused=true;CHECK(!ManualCombatActive());paused=false;hudEditing=true;CHECK(!ManualCombatActive());hudEditing=false;
 ReadMode("bow");localMode.Reset("bow");localMode.combatEnabled=false;autoDemo=true;practice.Clear();localProjectiles.Clear();CHECK(QueryPracticeTargets());
 const auto bowHP=practice.targets.at(17).hp;const int fire=controller.moves.at(Command::Fire).front();
 CHECK(controller.start(configuredIdleSlot));controller.input(Command::Fire);CHECK(controller.slot==fire);
 controller.tick(localMode.rules.motions.at(fire).projectileMarkers.back()+.001f);projectileMotion=std::numeric_limits<uint64_t>::max();TickProjectiles(0);
 CHECK(!localProjectiles.active.empty());for(const auto& shot:localProjectiles.active)CHECK(shot.damage==0);
 for(int i=0;i<10&&!localProjectiles.active.empty();++i){controller.tick(.1f);PumpTrainingStep(.1f);}
 CHECK(Near(practice.targets.at(17).hp,bowHP)&&practice.hits==0);
 autoDemo=false;localProjectiles.Clear();ReadMode("longsword");
 config.local_practice=false;config.target_lock=true;
}
}
int main(int argc,char** argv){try{
 CHECK(argc==2);packageRoot=std::filesystem::path(argv[1]);ReadAssets();LoadContracts();ManualCombat();SwordShieldPhysicalTiming();HardwareCameraControls();
 BE_HostApiV1 mock{};mock.runtime_invoke=Invoke;mock.object_unbox=Unbox;mock.field_get_value_object=ReadField;mock.gchandle_free=Free;api=&mock;
 for(size_t i=0;i<std::size(methods);++i)methods[i].r.method_info=reinterpret_cast<const void*>(i+1);
 LiveSnapshots();ActualTrainingPump();FourWeaponDamageAndRelease();PresentationPreview();CHECK(controller.start(configuredIdleSlot));CameraLifecycle();api=nullptr;
 std::cout<<"combat integration: six modes, manual dispatch, camera keys, source substeps, projectile birth, live snapshots and camera lifecycle passed (simulated host)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
