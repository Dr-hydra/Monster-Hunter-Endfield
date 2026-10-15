#include "../native/module.cpp"
#include "test_support.h"
// The real runtime module is compiled with a narrow fake managed API. No
// game process, backend, packet sender or actual entity damage is invoked.
namespace {
struct FakeTransform{bool alive=true;LS::Quat rotation{};LS::Vec3 position{};bool enabled=true;Mat world{};FakeTransform* parent=nullptr;};
FakeTransform* equipmentGo=nullptr;
FakeTransform* currentModel=nullptr;
unsigned freedPins=0,rendererWrites=0,nextPin=10;
unsigned terrainCalls=0;
Mat Identity(){Mat value;for(int i=0;i<4;++i)value.at(i,i)=1;return value;}
BE_Result BE_CALL FakeResolve(void*,const BE_MethodDescriptorV1* descriptor,BE_ResolvedMethodV1* result){
 if(std::string(descriptor->class_name)=="GameObject"&&std::string(descriptor->method_name)=="get_transform"){*result={};result->method_info=reinterpret_cast<const void*>(0xf00d);return BE_Result_Ok;}return BE_Result_NotFound;
}
void BE_CALL FakeFree(void*,uint32_t){++freedPins;}
uint32_t BE_CALL FakePin(void*,void* object,int){return object?++nextPin:0;}
void* BE_CALL FakeUnbox(void*,void* value){return value;}
void* BE_CALL FakeInvoke(void*,const void* token,void* object,void** args,void** exception){
 *exception=nullptr;if(reinterpret_cast<uintptr_t>(token)==0xf00d)return object;
 const auto index=Index(reinterpret_cast<uintptr_t>(token)-1);auto transform=static_cast<FakeTransform*>(object);
 switch(index){
  case AliveEntity:return &transform->alive;
  case ModelComponent:return object;
  case ModelGo:return currentModel;
  case Alive:{static bool alive;const auto candidate=static_cast<FakeTransform*>(args[0]);alive=candidate&&candidate->alive;return &alive;}
  case LocalRotation:return &transform->rotation;
  case LocalPosition:return &transform->position;
  case SetLocalRotation:transform->rotation=*static_cast<LS::Quat*>(args[0]);return nullptr;
  case SetLocalPosition:transform->position=*static_cast<LS::Vec3*>(args[0]);return nullptr;
  case RendererEnabled:return &transform->enabled;
  case RendererSetEnabled:transform->enabled=*static_cast<bool*>(args[0]);++rendererWrites;return nullptr;
  case WorldMatrix:return &transform->world;
  case Parent:return transform->parent;
  case WeaponViewPart:{static bool found=true;*static_cast<void**>(args[1])=equipmentGo;return &found;}
  case WeaponModel:return equipmentGo;
  case TerrainRaycast:{++terrainCalls;auto* payload=static_cast<unsigned char*>(args[2]);
   const Vec3 point{1,2,3},normal{0,1,0};const float distance=.5f;static bool found=true;
   std::memcpy(payload+8,&point,sizeof(point));std::memcpy(payload+24,&normal,sizeof(normal));std::memcpy(payload+52,&distance,sizeof(distance));return &found;}
  default:*exception=reinterpret_cast<void*>(1);return nullptr;
 }
}
BE_Result BE_CALL Reply(void*,const char*,BE_Result result,const char* body){CHECK(nlohmann::json::parse(body).is_object());return result;}
}
int main(int argc,char** argv){try{
 RaycastHitLayout layout;
 CHECK(layout.Configure(64,16,16,28,44)&&layout.point==0&&layout.normal==12&&layout.distance==28);
 CHECK(layout.Configure(80,24,32,48,76)&&layout.point==8&&layout.normal==24&&layout.distance==52);
 CHECK(!layout.Configure(129,16,16,28,44)&&!layout);
 CHECK(!layout.Configure(64,16,16,24,44)&&!layout); // overlapping vector fields
 CHECK(!layout.Configure(64,16,16,28,78)&&!layout); // misaligned/out of bounds
 CHECK(!layout.Configure(32,16,16,28,48)&&!layout);
 CHECK(!layout.Configure(64,16,-1,28,44)&&!layout);
 CHECK(NativeHookFlagsMatch(0x40,false)&&!NativeHookFlagsMatch(0x10,false));
 CHECK(NativeHookFlagsMatch(0x10,true)&&!NativeHookFlagsMatch(0x400,false));
 for(const auto* raw:{"{\"schema_version\":0}","{\"schema_version\":-1}","{\"schema_version\":1.5}","{\"schema_version\":4294967297}",
  "{\"toggle_key\":119.5}","{\"toggle_key\":27}","{\"toggle_key\":120}","{\"toggle_key\":255}","{\"toggle_key\":4294967415}"})CHECK(Throws([&]{Configure(raw);}));
 CHECK(argc==2);CHECK(std::filesystem::exists(std::filesystem::path(argv[1])/"native/module.cpp"));
 // Repeated early shutdown checks the overlay's message queue creation.
 for(int i=0;i<4;++i){PracticeHud hud;hud.Start();hud.Update(false,"local simulation");hud.Stop();}
 Mat matrix;matrix.at(0,1)=-3;matrix.at(1,0)=2;matrix.at(2,2)=4;matrix.at(3,3)=1;matrix.at(0,3)=5;matrix.at(1,3)=6;matrix.at(2,3)=7;
 const auto product=Multiply(matrix,matrix.inverse());for(int r=0;r<4;++r)for(int c=0;c<4;++c)CHECK(Near(product.at(r,c),r==c?1.f:0.f));
 CHECK(Close(matrix.inverse().point(matrix.point({1,2,3})),LS::Vec3{1,2,3}));CHECK(Near(LS::Rotate(matrix.rotation(),{1,0,0}).y,1));CHECK(Throws([]{Mat{}.inverse();}));
 BE_HostApiV1 mock{};mock.runtime_invoke=FakeInvoke;mock.object_unbox=FakeUnbox;mock.resolve_method=FakeResolve;mock.gchandle_free=FakeFree;mock.gchandle_new=FakePin;api=&mock;
 for(size_t i=0;i<std::size(methods);++i)methods[i].r.method_info=reinterpret_cast<const void*>(i+1);
 actor.terrainMask=1;CHECK(terrainHitLayout.Configure(80,24,32,48,76));TerrainHit terrain;
 CHECK(TerrainQuery({0,3,0},{0,-1,0},1,0,terrain)&&Close(terrain.point,Vec3{1,2,3})&&Close(terrain.normal,Vec3{0,1,0})&&Near(terrain.distance,.5f)&&terrainCalls==1);
 CHECK(!terrainHitLayout.Configure(256,24,32,48,76));CHECK(!TerrainQuery({0,3,0},{0,-1,0},1,0,terrain)&&terrainCalls==1);actor={};
 FakeTransform entity,model,replacement;actor.entity=&entity;actor.model=&model;currentModel=&model;
 CHECK(ActorIdentityIsCurrent(&entity));currentModel=&replacement;
 CHECK(!ActorIdentityIsCurrent(&entity)); // old pooled GO can still be alive
 currentModel=&model;entity.alive=false;CHECK(!ActorIdentityIsCurrent(&entity));
 actor={};currentModel=nullptr;
 FakeTransform bone;bone.rotation={0,0,.6f,.8f};bone.position={1,2,3};Binding binding;binding.transform=&bone;binding.written=binding.positionWritten=true;binding.lastRotation=bone.rotation;binding.lastPosition=bone.position;binding.previousRotation={};binding.previousPosition={4,5,6};
 actor.bones={binding};RestoreOwned();CHECK(Close(bone.rotation,LS::Quat{})&&Close(bone.position,LS::Vec3{4,5,6}));CHECK(!actor.bones[0].written&&!actor.bones[0].positionWritten);
 bone.rotation={.6f,0,0,.8f};bone.position={9,9,9};actor.bones={binding};RestoreOwned();CHECK(Near(bone.rotation.x,.6f)&&bone.position.x==9); // foreign writes survive cleanup
 bone.position=binding.lastPosition;auto positionOnly=binding;positionOnly.written=false;actor.bones={positionOnly};RestoreOwned();CHECK(Close(bone.position,binding.previousPosition)&&Near(bone.rotation.x,.6f));
 bone.alive=false;actor.bones={binding};RestoreOwned();CHECK(!actor.bones[0].written);actor={};
 FakeTransform renderer;renderer.enabled=false;actor.hiddenRenderers={{&renderer,3,true,true}};actor.hiddenEquipment=&renderer;RestoreEquipmentVisibility();CHECK(renderer.enabled&&rendererWrites==1&&freedPins==1&&actor.hiddenRenderers.empty()&&!actor.hiddenEquipment);
 actor.hiddenRenderers={{&renderer,4,true,true}};RestoreEquipmentVisibility();CHECK(rendererWrites==1&&freedPins==2); // a foreign re-enable remains untouched
 renderer.alive=false;actor.hiddenRenderers={{&renderer,5,true,true}};RestoreEquipmentVisibility();CHECK(rendererWrites==1&&freedPins==3);
 FakeTransform socket,equipment;socket.world=Identity();equipment.world=Identity();equipment.world.at(0,3)=.07f;equipment.parent=&socket;equipmentGo=&equipment;
 actor.weaponHeld=true;actor.weapon=&equipment;actor.sockets.resize(1);actor.sockets[0].binding.transform=&socket;actor.weaponModel.transform=&equipment;actor.weaponModel.parent=&socket;actor.bladeCalibrated=true;actor.bladeRootAxis={0,1,0};
 CaptureWeaponModel();CHECK(Near(actor.modelInSocketPosition.x,.07f));const auto mount=actor.modelInSocketRotation;
 // A later native animation rotates the prefab root. It is not a new
 // equipment identity or authored installation and must not contaminate
 // the showcase mount captured for this model.
 equipment.world.at(0,0)=0;equipment.world.at(1,0)=1;equipment.world.at(0,1)=-1;equipment.world.at(1,1)=0;
 CaptureWeaponModel();CHECK(Close(actor.modelInSocketRotation,mount));
 FakeTransform newParent;newParent.world=Identity();equipment.parent=&newParent;CaptureWeaponModel();CHECK(actor.weaponModel.parent==&newParent&&!Close(actor.modelInSocketRotation,mount));
 // Mesh-axis uncertainty only limits practice collision; a confirmed
 // native mount must still transport the display weapon with the hand.
 FakeTransform hand;hand.world=Identity();hand.world.at(0,3)=2;actor.bones.resize(13);actor.bones[12].transform=&hand;actor.sockets[0].binding.parent=&hand;actor.bladeCalibrated=false;
 ApplyWeaponSockets();CHECK(Near(equipment.position.x,2.07f));RestoreOwned();CHECK(Near(equipment.position.x,0));actor={};equipmentGo=nullptr;
 const auto before=config.speed;CHECK(ConfigurationChanged("{\"speed\":0}")==BE_Result_InvalidArgument&&config.speed==before);CHECK(ConfigurationChanged("{\"speed\":1.25,\"sound_enabled\":false,\"local_practice\":true}")==BE_Result_Ok&&config.speed==1.25f);
 CHECK(Initialize(nullptr,"{}")==BE_Result_InvalidArgument);CHECK(Message("test","{\"action\":\"status\"}")==BE_Result_NotReady);
 initialized=true;ready=false;bridge.reply=Reply;CHECK(Message("test","{\"action\":\"status\"}")==BE_Result_Ok);CHECK(Message("test","{\"action\":\"demo\"}")==BE_Result_NotReady);CHECK(Message("test","malformed")==BE_Result_InvalidArgument);
 // Saving volume or loop policy must preserve the active demonstration.
 loadedMode="longsword";autoDemo=true;demoStep=3;demoClock=.2f;demoElapsed=7;demoDelay=.8f;
 Configure("{\"sound_volume\":0.4,\"demo_loop\":false}");CHECK(autoDemo&&demoStep==3&&Near(demoClock,.2f)&&Near(demoElapsed,7)&&!config.demo_loop);
 Configure("{\"demo_sequence\":\"signature\"}");CHECK(demoStep==0&&Near(demoElapsed,0)&&demoSequence.size()>0);autoDemo=false;
 suppressInput=true;CHECK(Message("test","{\"action\":\"stop\"}")==BE_Result_Ok&&!suppressInput);initialized=false;bridge={};api=nullptr;
 CHECK(BetterEndfield_GetThirdPartyModuleV1()->version==1);std::cout<<"runtime: ownership cleanup, validation, host messages, HUD lifecycle passed (simulated host)\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
