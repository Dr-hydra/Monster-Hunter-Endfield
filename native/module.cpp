#include <BetterEndfield/ThirdPartyModule.h>
#include <Windows.h>
#include <nlohmann/json.hpp>
#include "core.h"
#include "character_binding.h"
#include "visual_weapon.h"
#include "retarget.h"
#include "weapon_pose.h"
#include "grip.h"
#include "packed_tracks.h"
#include "contact_mask.h"
#include "fingers.h"
#include "grounding.h"
#include "runtime_contracts.h"
#include "practice.h"
#include "target_geometry.h"
#include "projectiles.h"
#include "mode_state.h"
#include "combat_assets.h"
#include "pose_lease.h"
#include "source_skin.h"
#include "practice_hud.h"
#include "audio.h"
#include "audio_timeline.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#ifdef MHE_MODULE_EXTENSION_HEADER
#include MHE_MODULE_EXTENSION_HEADER
#endif

namespace {
using Json=nlohmann::json;
using namespace LS;
#ifndef MHE_MODULE_ID
#define MHE_MODULE_ID "endfield.longsword.showcase"
#endif
#ifndef MHE_MODULE_FRAME_OWNER
#define MHE_MODULE_FRAME_OWNER "endfield.longsword.showcase.frame"
#endif
#ifndef MHE_MODULE_TOGGLE_DEFAULT
#define MHE_MODULE_TOGGLE_DEFAULT VK_F8
#endif
constexpr char ID[]=MHE_MODULE_ID;
constexpr char FrameOwner[]=MHE_MODULE_FRAME_OWNER;
constexpr int ToggleDefault=MHE_MODULE_TOGGLE_DEFAULT;
constexpr char Game[]="Gameplay.Beyond.dll";
constexpr char Core[]="UnityEngine.CoreModule.dll";
std::recursive_mutex gate;
BE_ThirdPartyHostV1 bridge{};
const BE_HostApiV1* api=nullptr;
std::thread bootstrap;
std::atomic_bool exitBootstrap{false}, suppressInput{false};
bool initialized=false, ready=false, frameHooked=false, inputHooked=false, cleanupPending=false;
DWORD unityThread=0;
std::filesystem::path packageRoot;
std::string detail="等待游戏运行时", phase="waiting";
Controller controller;
AudioPlayer soundPlayer;
AudioTimeline soundTimeline;
AudioTimeline themeTimeline;
Json themeDefinitions=Json::object();std::string activeTheme;uint64_t themeSerial=0,themeBowMotion=0;float themeClock=0;bool themeBowCharging=false;
VisualWeapon::ManagedVisualWeapon visualWeapon;
VisualWeapon::ManagedSourceWeapon sourceWeapon,offhandWeapon;
VisualWeapon::ManagedSourceWeapon practiceDummy;
std::vector<Json> modeProps;
GeometryContactModels geometryContactMasks;
SupportGroundTracks supportGroundTracks;
Json projectileProp;
std::map<std::string,Json> visualWeaponRegistry;
std::map<std::string,PcmClip> soundClips;
bool soundReady=false;
std::string soundDetail="等待音效资源";
std::string lastSound;
std::map<std::string,std::string> soundLabels;
uint64_t soundCueCount=0;
int configuredIdleSlot=1;
std::string loadedMode="longsword",pendingMode;
std::filesystem::path activeAssetRoot;
std::filesystem::path activeAudioRoot;
std::map<std::string,Json> modeDefinitions;
std::map<int,std::string> motionLabels;
Json productCatalog=Json::object();
uint64_t configRevision=0,appliedConfigRevision=0;
bool paused=false,hudEditing=false,resumeSoundLoops=false;
bool hudLayoutChanged=false;
bool demoReturnStarted=false;
LocalProjectiles localProjectiles;
uint64_t projectileMotion=std::numeric_limits<uint64_t>::max();
std::map<int,std::vector<float>> projectileEvents;size_t projectileEventCursor=0;
ProjectileRelease activeProjectileRelease;
float localResource=0;int localAmmo=6;
ModeState localMode;
struct ProjectileVisual {uint64_t id=0;std::unique_ptr<VisualWeapon::ManagedSourceWeapon> mesh;};
std::array<ProjectileVisual,16> projectileVisuals;
uint64_t demoConditionMotion=0;
struct SkinGeometry {std::vector<Vec3> vertices;std::vector<SourceSkin::Indices> indices;std::vector<SourceSkin::Weights> weights;size_t bones=0;};
std::map<std::string,SkinGeometry> skinGeometry;
struct SkinMotion{std::string prop;SourceSkin::Track track;};
std::map<int,SkinMotion> skinMotions;
struct Configuration {
 bool enabled=true, root_motion=true, show_prop=true, sound_enabled=true, local_practice=false, laevatain_blade=true;
 int toggle_key=ToggleDefault, spirit_key=VK_XBUTTON2, spirit_alt_key=VK_XBUTTON2;
 float speed=1,blend_seconds=.12f,buffer_seconds=.3f,sound_volume=.65f,practice_hp=1000,practice_damage=100,grip_strength=1;
 float practice_radius=.65f,practice_height=.9f,practice_blade_length=1.25f;
 std::string mode="longsword",visual_weapon="auto",theme="auto",hud_layout="teaching",effects_quality="full",practice_target="dummy",demo_sequence="full";
 bool demo_loop=true;
 std::string keyboard_preset="mouse5";
 float projectile_speed=30;
 bool target_lock=true,combat_rules=true;
 std::string target_camera="focus";
 int lock_key='Q',focus_key=VK_CONTROL,cycle_key=VK_TAB;
 float target_range=30;
 bool hud_enabled=true,hud_show_derivations=true,hud_show_target=true,hud_show_crosshair=true;
 float hud_scale=1,hud_anchor_x=.02f,hud_anchor_y=.05f,hud_margin=16;
 std::map<std::string,int> keys{{"attack",VK_LBUTTON},{"secondary",VK_RBUTTON},{"spirit",VK_XBUTTON2},
  {"dodge",VK_SPACE},{"sheathe",'E'},{"aim",VK_RBUTTON},{"reload",VK_XBUTTON1}};
 std::map<std::string,std::map<std::string,int>> mode_keys;
 std::map<std::string,float> volumes{{"weapon",1.f},{"impact",1.f},{"charge",1.f},{"fire",1.f},{"voice",0.f}};
} config;
bool toggleRequest=false, stopRequest=false, demoRequest=false, autoDemo=false;
bool presentationPreview=false;
bool ManualCombatActive(){return controller.active&&config.enabled&&config.local_practice&&config.combat_rules&&
 !autoDemo&&!presentationPreview&&!paused&&!hudEditing;}
size_t demoStep=0;float demoClock=0,demoDelay=.6f;
size_t demoChargeLevel=0;bool demoHeld=false;Command demoHeldCommand=Command::Aim;
float demoElapsed=0;bool demoFinalTail=false;
std::vector<Command> commands;
extern std::vector<Command> demoSequence;
int previewRequest=-1;
void ReadMode(const std::string& id);
bool UnderSkeleton(void* transform,void* root);
struct Method { BE_MethodDescriptorV1 d; BE_ResolvedMethodV1 r{}; bool required=true; };
#define M(a,n,c,m,p,r,k) {{a,n,c,m,p,r,k}}
enum Index {MainCharacter,ModelComponent,ModelGo,Template,Valid,AliveEntity,Transform,Name,Alive,
 ChildCount,GetChild,Parent,LocalPosition,SetLocalPosition,Rotation,SetLocalRotation,LocalRotation,
 WorldMatrix,InverseVector,GetComponents,ArrayLength,ArrayValue,Bones,Mesh,Bindposes,
 Tail,CheckInput,ObjectDestroy,GoCtor,AddComponent,SetParent,LineCount,LinePoint,LineWidth,
 LineWorld,ShaderFind,MaterialCtor,RendererMaterial,MaterialColor,FrameCount,
 WeaponComponent,WeaponSetState,WeaponDisablePriority,WeaponPriorityActive,WeaponForceUpdate,
 WorldPosition,SetWorldPosition,MainCamera,AllEnemies,EntityPosition,EntityUid,EntityName,
 TerrainRaycast,TerrainSphereCast,WeaponViewPart,WeaponModel,FilterMesh,MeshBounds,RendererEnabled,RendererSetEnabled,
 RendererBounds,EntityServerId,EntityServerGeneration,EntityServerBattleId,MethodCount};
Method methods[]{
 M(Game,"Beyond.Gameplay.Core","PlayerController","GetMainCharacter",nullptr,nullptr,0),
 M(Game,"Beyond.Gameplay.Core","Entity","get_modelCom",nullptr,nullptr,0),
 M(Game,"Beyond.Gameplay.View","BaseModelComponent","GetModelGo",nullptr,"UnityEngine.GameObject",0),
 M(Game,"Beyond.Gameplay.Core","Entity","get_templateData",nullptr,"Beyond.Gameplay.EntityTemplateData",0),
 M(Game,"Beyond.Gameplay.Core","Entity","IsValid",nullptr,"System.Boolean",0),
 M(Game,"Beyond.Gameplay.Core","Entity","get_alive",nullptr,"System.Boolean",0),
 M(Core,"UnityEngine","Component","get_transform",nullptr,"UnityEngine.Transform",0),
 M(Core,"UnityEngine","Object","get_name",nullptr,"System.String",0),
 M(Core,"UnityEngine","Object","op_Implicit","UnityEngine.Object","System.Boolean",1),
 M(Core,"UnityEngine","Transform","get_childCount",nullptr,"System.Int32",0),
 M(Core,"UnityEngine","Transform","GetChild","System.Int32","UnityEngine.Transform",1),
 M(Core,"UnityEngine","Transform","get_parent",nullptr,"UnityEngine.Transform",0),
 M(Core,"UnityEngine","Transform","get_localPosition",nullptr,"UnityEngine.Vector3",0),
 M(Core,"UnityEngine","Transform","set_localPosition","UnityEngine.Vector3","System.Void",1),
 M(Core,"UnityEngine","Transform","get_rotation",nullptr,"UnityEngine.Quaternion",0),
 M(Core,"UnityEngine","Transform","set_localRotation","UnityEngine.Quaternion","System.Void",1),
 M(Core,"UnityEngine","Transform","get_localRotation",nullptr,"UnityEngine.Quaternion",0),
 M(Core,"UnityEngine","Transform","get_localToWorldMatrix",nullptr,"UnityEngine.Matrix4x4",0),
 M(Core,"UnityEngine","Transform","InverseTransformVector","UnityEngine.Vector3","UnityEngine.Vector3",1),
 M(Core,"UnityEngine","GameObject","GetComponentsInChildren","System.Type|System.Boolean","UnityEngine.Component[]",2),
 M("mscorlib.dll","System","Array","GetLength","System.Int32","System.Int32",1),
 M("mscorlib.dll","System","Array","GetValue","System.Int32","System.Object",1),
 M(Core,"UnityEngine","SkinnedMeshRenderer","get_bones",nullptr,"UnityEngine.Transform[]",0),
 M(Core,"UnityEngine","SkinnedMeshRenderer","get_sharedMesh",nullptr,"UnityEngine.Mesh",0),
 M(Core,"UnityEngine","Mesh","get_bindposes",nullptr,"UnityEngine.Matrix4x4[]",0),
 M(Game,"Beyond.Gameplay.View","CameraManager","TailLateTick","System.Single","System.Void",1),
 M("Input.Beyond.dll","Beyond.Input","InputManager","_CheckInput",nullptr,"System.Void",0),
 M(Core,"UnityEngine","Object","Destroy","UnityEngine.Object","System.Void",1),
 M(Core,"UnityEngine","GameObject",".ctor","System.String","System.Void",1),
 M(Core,"UnityEngine","GameObject","AddComponent","System.Type","UnityEngine.Component",1),
 M(Core,"UnityEngine","Transform","SetParent","UnityEngine.Transform|System.Boolean","System.Void",2),
 M(Core,"UnityEngine","LineRenderer","set_positionCount","System.Int32","System.Void",1),
 M(Core,"UnityEngine","LineRenderer","SetPosition","System.Int32|UnityEngine.Vector3","System.Void",2),
 M(Core,"UnityEngine","LineRenderer","set_widthMultiplier","System.Single","System.Void",1),
 M(Core,"UnityEngine","LineRenderer","set_useWorldSpace","System.Boolean","System.Void",1),
 M(Core,"UnityEngine","Shader","Find","System.String","UnityEngine.Shader",1),
 M(Core,"UnityEngine","Material",".ctor","UnityEngine.Shader","System.Void",1),
 M(Core,"UnityEngine","Renderer","set_sharedMaterial","UnityEngine.Material","System.Void",1),
 M(Core,"UnityEngine","Material","set_color","UnityEngine.Color","System.Void",1),
 M(Core,"UnityEngine","Time","get_frameCount",nullptr,"System.Int32",0),
 M(Game,"Beyond.Gameplay.Core","Entity","get_weaponCom",nullptr,"Beyond.Gameplay.View.WeaponComponent",0),
 M(Game,"Beyond.Gameplay.View","WeaponComponent","SetWeaponState","Beyond.Gameplay.View.WeaponComponent.WeaponState|Beyond.Gameplay.View.WeaponComponent.WeaponStatePriority|System.Boolean","System.Int32",3),
 M(Game,"Beyond.Gameplay.View","WeaponComponent","DisablePriority","Beyond.Gameplay.View.WeaponComponent.WeaponStatePriority|System.Int32|System.Boolean|System.Boolean","System.Void",4),
 M(Game,"Beyond.Gameplay.View","WeaponComponent","IsPriorityActive","Beyond.Gameplay.View.WeaponComponent.WeaponStatePriority","System.Boolean",1),
 M(Game,"Beyond.Gameplay.View","WeaponComponent","ForceUpdateImmediately",nullptr,"System.Void",0),
 M(Core,"UnityEngine","Transform","get_position",nullptr,"UnityEngine.Vector3",0),
 M(Core,"UnityEngine","Transform","set_position","UnityEngine.Vector3","System.Void",1),
 M(Core,"UnityEngine","Camera","get_main",nullptr,"UnityEngine.Camera",0),
 M(Game,"Beyond.Gameplay.Core","ObjectContainer","get_allEnemies",nullptr,nullptr,0),
 M(Game,"Beyond.Gameplay.Core","Entity","get_position",nullptr,"UnityEngine.Vector3",0),
 M(Game,"Beyond.Gameplay.Core","Entity","get_instanceUid",nullptr,"System.UInt32",0),
 M(Game,"Beyond.Gameplay.Core","Entity","get_name",nullptr,"System.String",0),
 M("UnityEngine.PhysicsModule.dll","UnityEngine","Physics","Raycast","UnityEngine.Vector3|UnityEngine.Vector3|UnityEngine.RaycastHit&|System.Single|System.Int32|UnityEngine.QueryTriggerInteraction","System.Boolean",6),
 M("UnityEngine.PhysicsModule.dll","UnityEngine","Physics","SphereCast","UnityEngine.Vector3|System.Single|UnityEngine.Vector3|UnityEngine.RaycastHit&|System.Single|System.Int32|UnityEngine.QueryTriggerInteraction","System.Boolean",7),
 M(Game,"Beyond.Gameplay.View","WeaponComponent","TryGetWeaponViewPart","System.Int32|Beyond.Gameplay.View.WeaponModelViewPart&","System.Boolean",2),
 M(Game,"Beyond.Gameplay.View","BaseModelViewPart","get_model",nullptr,"UnityEngine.GameObject",0),
 M(Core,"UnityEngine","MeshFilter","get_sharedMesh",nullptr,"UnityEngine.Mesh",0),
 M(Core,"UnityEngine","Mesh","get_bounds",nullptr,"UnityEngine.Bounds",0),
 M(Core,"UnityEngine","Renderer","get_enabled",nullptr,"System.Boolean",0),
 M(Core,"UnityEngine","Renderer","set_enabled","System.Boolean","System.Void",1),
 M(Core,"UnityEngine","Renderer","get_bounds",nullptr,"UnityEngine.Bounds",0),
 M(Game,"Beyond.Gameplay.Core","Entity","get_serverId",nullptr,"System.UInt64",0),
 M(Game,"Beyond.Gameplay.Core","Entity","get_serverGeneration",nullptr,"System.UInt32",0),
 M(Game,"Beyond.Gameplay.Core","Entity","get_serverBattleInstId",nullptr,"System.UInt64",0)
};
#undef M
static_assert(std::size(methods)==MethodCount);
BE_ResolvedClassV1 rendererClass{},gameObjectClass{},lineClass{},materialClass{};
BE_ResolvedClassV1 meshFilterClass{},baseRendererClass{};
BE_ResolvedClassV1 grounderClass{};
BE_ResolvedFieldV1 grounderSolver{},groundLayers{};
BE_ResolvedFieldV1 templateId{};
RaycastHitLayout terrainHitLayout;
using MethodFlagsFn=uint32_t(__cdecl*)(const void*,uint32_t*);
MethodFlagsFn methodFlags=nullptr;
bool NativeHookContract(const BE_ResolvedMethodV1& method,bool isStatic=false){
 if(!method.method_info||!method.method_pointer||!methodFlags)return false;
 uint32_t implementation=0;return NativeHookFlagsMatch(methodFlags(method.method_info,&implementation),isStatic);
}
bool GroundDelegateContract(const void* method,const BE_MethodDescriptorV1& expected){
 const auto module=GetModuleHandleW(L"GameAssembly.dll");
 using CountFn=uint32_t(__cdecl*)(const void*);using ParamFn=const void*(__cdecl*)(const void*,uint32_t);
 using ReturnFn=const void*(__cdecl*)(const void*);using NameFn=char*(__cdecl*)(const void*);using FreeFn=void(__cdecl*)(void*);
 auto count=reinterpret_cast<CountFn>(GetProcAddress(module,"il2cpp_method_get_param_count"));
 auto param=reinterpret_cast<ParamFn>(GetProcAddress(module,"il2cpp_method_get_param"));
 auto returns=reinterpret_cast<ReturnFn>(GetProcAddress(module,"il2cpp_method_get_return_type"));
 auto name=reinterpret_cast<NameFn>(GetProcAddress(module,"il2cpp_type_get_name"));
 auto freeMemory=reinterpret_cast<FreeFn>(GetProcAddress(module,"il2cpp_free"));
 uint32_t implementation=0;
 if(!method||!methodFlags||!count||!param||!returns||!name||!freeMemory||!NativeHookFlagsMatch(methodFlags(method,&implementation),false)||count(method)!=expected.parameter_count)return false;
 auto typeMatches=[&](const void* type,const std::string& wanted){if(!type)return false;auto actual=name(type);if(!actual)return false;const bool match=wanted==actual;freeMemory(actual);return match;};
 if(!typeMatches(returns(method),expected.return_type))return false;
 std::string parameters=expected.parameter_types;size_t start=0;
 for(uint32_t i=0;i<expected.parameter_count;++i){const auto end=parameters.find('|',start);if(!typeMatches(param(method,i),parameters.substr(start,end==std::string::npos?end:end-start)))return false;start=end==std::string::npos?parameters.size():end+1;}
 return true;
}
int weaponInFight=-1,weaponPerform=-1;
using TailFn=void(__cdecl*)(void*,float,void*);
using InputFn=void(__cdecl*)(void*,void*);
TailFn nextTail=nullptr; InputFn nextInput=nullptr;

void Log(const std::string& message) { if(initialized&&bridge.log)bridge.log(bridge.context,message.c_str()); }
void* Call(Index id,void* self=nullptr,void** args=nullptr,bool* ok=nullptr) {
 if(ok)*ok=false;
 if(!api||!api->runtime_invoke||!methods[id].r.method_info)return nullptr;
 void* ex=nullptr; void* out=api->runtime_invoke(api->context,methods[id].r.method_info,self,args,&ex);
 if(ok)*ok=ex==nullptr; return ex?nullptr:out;
}
template<class T> bool Value(Index id,void* self,T& out,void** args=nullptr) {
 bool ok=false; auto boxed=Call(id,self,args,&ok);
 if(!ok||!boxed)return false; auto raw=api->object_unbox(api->context,boxed);
 if(!raw)return false; std::memcpy(&out,raw,sizeof(T)); return true;
}
bool Set(Index id,void* self,void** args) {bool ok=false;Call(id,self,args,&ok);return ok;}
template<class T> bool SetValue(Index id,void* self,T v) {void* args[]{&v};return Set(id,self,args);}
bool IsAlive(void* obj) {bool alive=false;void* args[]{obj};return obj&&Value(Alive,nullptr,alive,args)&&alive;}
bool Focused() {DWORD process=0;GetWindowThreadProcessId(GetForegroundWindow(),&process);return process==GetCurrentProcessId();}
std::string Text(void* str) {char out[512]{};if(str&&api->copy_managed_string)api->copy_managed_string(api->context,str,out,sizeof(out));return out;}
std::string ObjectName(void* o) {return Text(Call(Name,o));}
void* RootTransform(void* go) {
 // GameObject and Component have different managed declaring classes.
 static BE_ResolvedMethodV1 get{};
 if(!get.method_info){BE_MethodDescriptorV1 d{Core,"UnityEngine","GameObject","get_transform",nullptr,"UnityEngine.Transform",0};
  if(api->resolve_method(api->context,&d,&get)!=BE_Result_Ok)return nullptr;}
 void* ex=nullptr;auto out=api->runtime_invoke(api->context,get.method_info,go,nullptr,&ex);return ex?nullptr:out;
}
int Count(void* arr) {int dimension=0,n=0;void* args[]{&dimension};return Value(ArrayLength,arr,n,args)&&n>=0&&n<=8192?n:0;}
void* Item(void* arr,int index) {void* args[]{&index};return Call(ArrayValue,arr,args);}

struct Mat {
 float v[16]{};
 float& at(int r,int c){return v[c*4+r];}
 float at(int r,int c)const{return v[c*4+r];}
 Vec3 point()const{return {v[12],v[13],v[14]};}
 Vec3 point(Vec3 p)const{return {at(0,0)*p.x+at(0,1)*p.y+at(0,2)*p.z+at(0,3),
  at(1,0)*p.x+at(1,1)*p.y+at(1,2)*p.z+at(1,3),at(2,0)*p.x+at(2,1)*p.y+at(2,2)*p.z+at(2,3)};}
 Mat inverse()const {
  // Gauss-Jordan is adequate for the small, bind-time-only matrix set.
  double a[4][8]{};for(int r=0;r<4;++r){for(int c=0;c<4;++c)a[r][c]=at(r,c);a[r][r+4]=1;}
  for(int c=0;c<4;++c){int p=c;for(int r=c+1;r<4;++r)if(std::abs(a[r][c])>std::abs(a[p][c]))p=r;
   if(std::abs(a[p][c])<1e-8)throw std::runtime_error("Singular bind matrix");
   for(int j=0;j<8;++j)std::swap(a[p][j],a[c][j]);double div=a[c][c];for(double& x:a[c])x/=div;
   for(int r=0;r<4;++r)if(r!=c){double f=a[r][c];for(int j=0;j<8;++j)a[r][j]-=f*a[c][j];}}
  Mat out;for(int r=0;r<4;++r)for(int c=0;c<4;++c)out.at(r,c)=float(a[r][c+4]);return out;
 }
 Quat rotation()const {
  float m[3][3]{};for(int c=0;c<3;++c){float n=0;for(int r=0;r<3;++r)n+=at(r,c)*at(r,c);n=std::sqrt(n);
   if(n<1e-6f||!std::isfinite(n))throw std::runtime_error("Invalid bind rotation");for(int r=0;r<3;++r)m[r][c]=at(r,c)/n;}
  Quat q;float t=m[0][0]+m[1][1]+m[2][2];
  if(t>0){float s=std::sqrt(t+1)*2;q.w=.25f*s;q.x=(m[2][1]-m[1][2])/s;q.y=(m[0][2]-m[2][0])/s;q.z=(m[1][0]-m[0][1])/s;}
  else if(m[0][0]>m[1][1]&&m[0][0]>m[2][2]){float s=std::sqrt(1+m[0][0]-m[1][1]-m[2][2])*2;q.w=(m[2][1]-m[1][2])/s;q.x=.25f*s;q.y=(m[0][1]+m[1][0])/s;q.z=(m[0][2]+m[2][0])/s;}
  else if(m[1][1]>m[2][2]){float s=std::sqrt(1+m[1][1]-m[0][0]-m[2][2])*2;q.w=(m[0][2]-m[2][0])/s;q.x=(m[0][1]+m[1][0])/s;q.y=.25f*s;q.z=(m[1][2]+m[2][1])/s;}
  else{float s=std::sqrt(1+m[2][2]-m[0][0]-m[1][1])*2;q.w=(m[1][0]-m[0][1])/s;q.x=(m[0][2]+m[2][0])/s;q.y=(m[1][2]+m[2][1])/s;q.z=.25f*s;}return Normalize(q);
 }
};
Mat Multiply(const Mat& a,const Mat& b){Mat out;for(int r=0;r<4;++r)for(int c=0;c<4;++c)for(int k=0;k<4;++k)out.at(r,c)+=a.at(r,k)*b.at(k,c);return out;}
struct Binding {void* transform=nullptr; void* parent=nullptr;Quat restWorld{};Vec3 restLocal{},previousPosition{},lastPosition{};
 Quat previousRotation{},lastRotation{};bool written=false,positionWritten=false;int depth=0;};
struct Socket {Binding binding{};Vec3 handPosition{};Quat handRotation{};int handIndex=12;};
struct SecondarySkinRoot{Binding binding;void* anchor=nullptr;Mat anchorRelative;};
struct FingerBinding{Binding binding;size_t source=0;Quat from{};};
struct FootInfluence{void* transform=nullptr;Vec3 local{};float weight=0;};
using FootProbe=std::vector<FootInfluence>;
struct RendererVisibility {void* renderer=nullptr;uint32_t pin=0;bool previous=true,written=false;};
struct Actor {
 void* entity=nullptr;void* model=nullptr;void* root=nullptr;
 std::string character;
 std::string skeletonPath;
 std::string restRegistration="未校准";
 std::vector<uint32_t> pins;
 std::vector<Binding> bones;
 std::vector<FingerBinding> fingers;uint64_t fingerMotion=std::numeric_limits<uint64_t>::max();
 std::vector<FootProbe> footProbes;
 std::array<std::vector<FootProbe>,2> handProbes;
 std::array<Quat,2> nativeHandRest{};
 std::vector<int> order;
 void* prop=nullptr;void* propMaterial=nullptr;float scale=1,entrySeconds=0;
 Quat sourceBasis{},sourceStageBasis{};bool gripAvailable=true;
 void* weapon=nullptr;int weaponHandle=-1;bool weaponHeld=false;
 std::vector<Socket> sockets;Binding weaponModel{};
 std::vector<SecondarySkinRoot> secondaryRoots;
 void* equippedGo=nullptr;void* hiddenEquipment=nullptr;void* mountSocket=nullptr;
 bool mountCaptured=false;std::string visualReason;
 std::string visualMountName="wep_R";bool sourcePresentation=false,offhandRequested=false,visualSourceAdapter=false;
 void* drawFinger=nullptr;
 uint64_t sourceSkinMotion=0;
 std::vector<RendererVisibility> hiddenRenderers;
 Vec3 bladeRootAxis{},bladeHandAxis{},edgeRootAxis{},modelInSocketPosition{};Quat modelInSocketRotation{};
 WeaponPose::SourceBlend weaponBlend{};WeaponPose::SourceFrame sourceWeaponFrame{};
 bool sourceWeaponActive=false;Quat gripFrameWorld{};
 std::array<bool,2> palmAligned{};
 float bladeLength=0,weaponRetry=0,visualFrameSeconds=0;bool bladeCalibrated=false;
 std::string weaponPoseDetail="等待武器绑定";
 void* camera=nullptr;void* cameraTransform=nullptr;
 uint32_t cameraPin=0,cameraTransformPin=0;
 Vec3 rootPrevious{},rootLast{},cameraPrevious{},cameraLast{};
 bool rootWritten=false,cameraWritten=false;
 float soleOffset=0,groundCorrection=0,clearance=0,clearanceFrom=0,groundBlend=0;
 float groundFloor=0;
 uint64_t groundMotion=std::numeric_limits<uint64_t>::max();
 int terrainMask=0;void* groundRaycast=nullptr;void* groundSphereCast=nullptr;
 const void* groundRaycastMethod=nullptr;const void* groundSphereCastMethod=nullptr;
 std::string groundDetail="等待地面校准";
} actor;
Json profile;
std::map<std::string,Json> characterProfiles;
std::array<Vec3,21> sourceRestPositions{};
std::array<Quat,21> sourceRestRotations{};
float sourceSoleHeight=1.0568615f;
bool sourceRestReady=false;
std::array<Quat,2> sourcePalmFrames{};
bool sourcePalmsReady=false;
std::map<int,GripTrack> gripTracks;
std::map<int,WeaponPose::SourceTrack> sourceWeaponTracks;
std::map<int,GroundTrack> groundTracks;
std::map<int,FingerTrack> fingerTracks;Json sourceFingerBones=Json::array();
void* ReadObjectField(void* object,const char* name);
bool Pin(void* p);
std::string ModelObjectPath(void* transform){std::vector<std::string> parts;auto at=transform;
 for(int depth=0;at&&depth<96;++depth){parts.push_back(ObjectName(at));if(at==actor.root)break;at=Call(Parent,at);}
 std::reverse(parts.begin(),parts.end());std::string result;for(const auto& part:parts){if(!result.empty())result+='/';result+=part;}return result;}
void BindGroundQueries(){
 if(!terrainHitLayout||!grounderClass.type_object||!grounderSolver.field_info||!groundLayers.field_info)return;
 bool include=true;auto type=grounderClass.type_object;void* args[]{type,&include};
 auto grounders=Call(GetComponents,actor.model,args);
 using FindMethodFn=void*(__cdecl*)(void*,const char*,int);
 auto find=reinterpret_cast<FindMethodFn>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),"il2cpp_class_get_method_from_name"));
 using ObjectClassFn=void*(__cdecl*)(void*);
 auto objectClass=reinterpret_cast<ObjectClassFn>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),"il2cpp_object_get_class"));
 for(int i=0;i<Count(grounders);++i){auto grounder=Item(grounders,i);
  auto solver=api->field_get_value_object(api->context,grounderSolver.field_info,grounder);
  auto layers=solver?api->field_get_value_object(api->context,groundLayers.field_info,solver):nullptr;
  auto raw=layers?api->object_unbox(api->context,layers):nullptr;int mask=0;if(raw)std::memcpy(&mask,raw,sizeof(mask));
  if(!mask)continue;actor.terrainMask=mask;
  // Prefer the game's ECS-aware queries; the standard Physics API is a fallback.
  const auto bind=[&](const char* field,int count,void*& receiver,const void*& method){
   auto delegate=ReadObjectField(solver,field);if(!delegate||!find||!objectClass)return;
   auto resolved=find(objectClass(delegate),"Invoke",count);
   const auto& expected=methods[count==6?TerrainRaycast:TerrainSphereCast].d;
   if(resolved&&GroundDelegateContract(resolved,expected)&&Pin(delegate)){receiver=delegate;method=resolved;}
  };
  bind("Raycast",6,actor.groundRaycast,actor.groundRaycastMethod);
  bind("SphereCast",7,actor.groundSphereCast,actor.groundSphereCastMethod);break;
 }
}
Practice practice;
PoseLease poseLease;
DelayedHits delayedHits;
CombatCollision delayedContact;
std::map<int,std::vector<CollisionWindow>> delayedContactWindows;
std::map<int,std::vector<DelayedHitRule>> delayedRules;
TargetLock targetLock;
TargetIdentities targetIdentities;
std::vector<CombatTarget> targetSnapshot;
bool lockRequest=false,focusRequest=false,cycleRequest=false,targetReleaseRequest=false;
bool previousLock=false,previousFocus=false,previousCycle=false;
bool ownedCamera=false;Quat cameraPrevious{},cameraLast{};
std::string targetingDetail="Q 选择目标，Ctrl 聚焦；Tab 为本模组切换目标快捷键";
int combatFollowupSlot=0;float combatFollowupSeconds=0;uint64_t combatFollowupMotion=0;
Command combatFollowupCommand=Command::Helm;
void ApplyCombatOutcome(const CombatEvent&,const CombatOutcome&);
uint64_t pumpedMotionSerial=0;
bool practiceAttackRequest=false,practiceAttackFromFront=true,practiceAttackGuardable=true;
uint64_t practiceAttackSerial=0;
void RestoreTargetCamera();
#ifdef MHE_MODULE_EXTENSION_HEADER
ModuleExtension moduleExtension;
#endif
Vec3 practiceBladeAxis{0,0,1};
Json practiceSummary=Json::array();
Json PracticeTargets();
PracticeHud practiceHud;
bool practiceResetRequest=false,practiceReady=false;
float enemyRefresh=0;
std::string practiceDetail="本地练习未开启";
BE_ResolvedFieldV1 worldContainer{};
constexpr const char* semantic[]{"hips","spine","chest","neck","head","left_shoulder","left_upper_arm","left_lower_arm","left_hand","right_shoulder","right_upper_arm","right_lower_arm","right_hand","left_upper_leg","left_lower_leg","left_foot","left_toes","right_upper_leg","right_lower_leg","right_foot","right_toes"};
constexpr const char* fallback[]{"Bip001_Pelvis","Bip001_Spine","Bip001_Spine1","Bip001_Neck","Bip001_Head","Bip001_L_Clavicle","Bip001_L_UpperArm","Bip001_L_Forearm","Bip001_L_Hand","Bip001_R_Clavicle","Bip001_R_UpperArm","Bip001_R_Forearm","Bip001_R_Hand","Bip001_L_Thigh","Bip001_L_Calf","Bip001_L_Foot","Bip001_L_Toe0","Bip001_R_Thigh","Bip001_R_Calf","Bip001_R_Foot","Bip001_R_Toe0"};
bool Pin(void* p){auto h=api->gchandle_new(api->context,p,1);if(!h)return false;actor.pins.push_back(h);return true;}
bool Close(Quat a,Quat b) {return std::abs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w)>.99999f;}
bool Close(Vec3 a,Vec3 b){return std::abs(a.x-b.x)+std::abs(a.y-b.y)+std::abs(a.z-b.z)<1e-5f;}
void ReleaseCameraBinding(){
 if(actor.cameraWritten&&IsAlive(actor.cameraTransform)){Vec3 p;
  if(Value(WorldPosition,actor.cameraTransform,p)&&Close(p,actor.cameraLast))SetValue(SetWorldPosition,actor.cameraTransform,actor.cameraPrevious);}
 actor.cameraWritten=false;
 if(api&&api->gchandle_free){if(actor.cameraPin)api->gchandle_free(api->context,actor.cameraPin);
  if(actor.cameraTransformPin)api->gchandle_free(api->context,actor.cameraTransformPin);}
 actor.cameraPin=actor.cameraTransformPin=0;actor.camera=actor.cameraTransform=nullptr;
}
void RestoreStageMotion() {
 auto restore=[](void* transform,bool& written,Vec3 previous,Vec3 last){
  if(written&&IsAlive(transform)){Vec3 current;if(Value(WorldPosition,transform,current)&&Close(current,last))SetValue(SetWorldPosition,transform,previous);}
  written=false;};
 restore(actor.root,actor.rootWritten,actor.rootPrevious,actor.rootLast);
 restore(actor.cameraTransform,actor.cameraWritten,actor.cameraPrevious,actor.cameraLast);
}
void RestoreOwned() {
 const auto restore=[](Binding& b){
  if(!IsAlive(b.transform)){b.written=b.positionWritten=false;return;}
  Quat q;Vec3 p;
  if(b.written&&Value(LocalRotation,b.transform,q)&&Close(q,b.lastRotation))SetValue(SetLocalRotation,b.transform,b.previousRotation);
  if(b.positionWritten&&Value(LocalPosition,b.transform,p)&&Close(p,b.lastPosition))SetValue(SetLocalPosition,b.transform,b.previousPosition);
  b.written=b.positionWritten=false;
 };
 restore(actor.weaponModel);for(auto& socket:actor.sockets)restore(socket.binding);
 for(auto& root:actor.secondaryRoots)restore(root.binding);
 for(auto& b:actor.bones)restore(b);
 for(auto& finger:actor.fingers)restore(finger.binding);
}
void RestoreEquipmentVisibility(){
 for(auto& visibility:actor.hiddenRenderers){
  bool current=false;
  if(visibility.written&&IsAlive(visibility.renderer)&&Value(RendererEnabled,visibility.renderer,current)&&!current)
   SetValue(RendererSetEnabled,visibility.renderer,visibility.previous);
  if(visibility.pin&&api&&api->gchandle_free)api->gchandle_free(api->context,visibility.pin);
 }
 actor.hiddenRenderers.clear();actor.hiddenEquipment=nullptr;
}
void UpdateEquipmentVisibility(){
 const bool sourceReady=sourceWeapon.Ready()&&(!actor.offhandRequested||offhandWeapon.Ready());
 const bool requested=config.show_prop&&config.visual_weapon!="equipped"&&(visualWeapon.Ready()||sourceReady);
 const auto hide=[&](){visualWeapon.SetVisible(false);sourceWeapon.SetVisible(false);offhandWeapon.SetVisible(false);};
 if(!requested){hide();RestoreEquipmentVisibility();return;}
 if(controller.normalSheathed()){hide();return;}
 if(!IsAlive(actor.equippedGo)){
  RestoreEquipmentVisibility();
  const bool shown=visualWeapon.Ready()?visualWeapon.SetVisible(true):sourceWeapon.SetVisible(true);
  if(actor.offhandRequested&&offhandWeapon.Ready())offhandWeapon.SetVisible(shown);
  actor.visualReason=shown?"独立展示武器已显示；原装备当前未生成":"独立展示武器未能显示";return;
 }
 if(!baseRendererClass.type_object||!methods[RendererEnabled].r.method_info||!methods[RendererSetEnabled].r.method_info){
  hide();RestoreEquipmentVisibility();
  actor.visualReason="原装备显隐接口不可用，保留原装备";return;
 }
 if(actor.hiddenEquipment!=actor.equippedGo){
  hide();RestoreEquipmentVisibility();actor.hiddenEquipment=actor.equippedGo;
  bool include=true;void* args[]{baseRendererClass.type_object,&include};auto renderers=Call(GetComponents,actor.equippedGo,args);
  if(Count(renderers)==0||Count(renderers)>128){actor.hiddenEquipment=nullptr;actor.visualReason="原装备显示对象未确认";return;}
  for(int i=0;i<Count(renderers);++i){auto renderer=Item(renderers,i);bool enabled=false;
   if(!IsAlive(renderer)||!Value(RendererEnabled,renderer,enabled)){RestoreEquipmentVisibility();actor.visualReason="原装备显示状态不可读取";return;}
   auto pin=api->gchandle_new(api->context,renderer,1);
   if(!pin){RestoreEquipmentVisibility();actor.visualReason="原装备对象无法保留";return;}
   // Discover disabled LOD renderers too. If the game later enables one,
   // record its actual pre-write state before hiding it.
   actor.hiddenRenderers.push_back({renderer,pin,enabled,false});}
 }
 for(auto& visibility:actor.hiddenRenderers){bool enabled=false;
  if(!IsAlive(visibility.renderer)||!Value(RendererEnabled,visibility.renderer,enabled)){
   hide();RestoreEquipmentVisibility();actor.visualReason="原装备对象已变化";return;}
  if(enabled){
   if(!visibility.written)visibility.previous=enabled;
   if(!SetValue(RendererSetEnabled,visibility.renderer,false)){
    hide();RestoreEquipmentVisibility();actor.visualReason="原装备未能隐藏，保留原装备";return;}
   visibility.written=true;
  }
 }
 bool shown=false;if(visualWeapon.Ready())shown=visualWeapon.SetVisible(true);
 else if(sourceReady){shown=sourceWeapon.SetVisible(true);if(actor.offhandRequested)shown=offhandWeapon.SetVisible(true)&&shown;}
 if(!shown){hide();RestoreEquipmentVisibility();actor.visualReason="独立展示武器未能显示，已恢复装备";return;}
 actor.visualReason=visualWeapon.Ready()?"独立原生武器已显示；动态表现未实机验收":"源武器参考模型已显示；原安装/材质认证状态见资源证据";
}

void ReleaseWeapon() {
 if(!actor.weaponHeld||!actor.weapon)return;
 // Per-priority slots compare the handle: this releases only our state lease.
 bool valid=false;if(actor.entity&&Value(Valid,actor.entity,valid)&&valid&&Call(WeaponComponent,actor.entity)==actor.weapon){
  bool delay=false,force=true;void* args[]{&weaponPerform,&actor.weaponHandle,&delay,&force};
  Set(WeaponDisablePriority,actor.weapon,args);Call(WeaponForceUpdate,actor.weapon);
 }
 actor.weaponHeld=false;actor.weapon=nullptr;actor.weaponHandle=-1;
}
bool HoldEquippedWeapon() {
 if(!config.show_prop||weaponInFight<0||weaponPerform<0)return false;
 for(int i=WeaponComponent;i<=WeaponForceUpdate;++i)if(!methods[i].r.method_info)return false;
 auto weapon=Call(WeaponComponent,actor.entity);if(!weapon)return false;
 bool active=false;void* priorityArgs[]{&weaponPerform};
 if(!Value(WeaponPriorityActive,weapon,active,priorityArgs)||active){Log("Weapon Perform slot is already owned; keeping its state");return false;}
 bool force=true;int handle=-1;void* args[]{&weaponInFight,&weaponPerform,&force};
 if(!Value(WeaponSetState,weapon,handle,args)||handle<0)return false;
 actor.weapon=weapon;actor.weaponHandle=handle;actor.weaponHeld=true;
 if(!Pin(weapon)){ReleaseWeapon();return false;}
 Call(WeaponForceUpdate,weapon);
 Log("Equipped weapon held by a scoped Perform/InFight state handle");return true;
}
void BindWeaponSockets(const std::unordered_map<std::string,void*>& named) {
 const auto weapon=profile.value("weapon",Json::object());
 if(!weapon.contains("sockets"))return;
 for(const auto& row:weapon["sockets"]){auto found=named.find(row.at("name").get<std::string>());if(found==named.end())continue;
  Socket socket;socket.binding.transform=found->second;socket.binding.parent=Call(Parent,found->second);
  const auto socketName=row.at("name").get<std::string>();const auto side=row.value("hand",socketName=="wep_L"?std::string("left"):(socketName=="wep_M"?std::string("parent"):std::string("right")));
  socket.handIndex=side=="left"?8:(side=="parent"?-1:12);
  const auto p=row.at("hand_position").get<std::vector<float>>(),q=row.at("hand_rotation_xyzw").get<std::vector<float>>();
  if(p.size()!=3||q.size()!=4)throw std::runtime_error("Invalid socket calibration");
  socket.handPosition={p[0],p[1],p[2]};socket.handRotation=Normalize({q[0],q[1],q[2],q[3]});
   if(!Finite(socket.handPosition)||!Finite(socket.handRotation)||Retarget::Len(socket.handPosition)>2.f)throw std::runtime_error("Invalid socket pose");
  if(socket.binding.parent&&Pin(socket.binding.transform)&&Pin(socket.binding.parent))actor.sockets.push_back(socket);
 }
 actor.weaponPoseDetail=actor.sockets.empty()?"未找到原生握刀节点":"原生握刀节点已绑定，等待剑身识别";
}
void CaptureWeaponModel() {
 using namespace Retarget;
 if(actor.sockets.empty()||!methods[WeaponViewPart].r.method_info||!methods[WeaponModel].r.method_info){actor.equippedGo=nullptr;return;}
 auto weapon=actor.weaponHeld?actor.weapon:Call(WeaponComponent,actor.entity);if(!weapon){actor.equippedGo=nullptr;return;}
 int index=0;void* part=nullptr;bool found=false;void* args[]{&index,&part};
 if(!Value(WeaponViewPart,weapon,found,args)||!found||!part){actor.equippedGo=nullptr;actor.bladeCalibrated=false;actor.weaponModel={};return;}
 auto go=Call(WeaponModel,part);if(!IsAlive(go)||go==actor.model){actor.equippedGo=nullptr;actor.bladeCalibrated=false;actor.weaponModel={};actor.mountCaptured=false;return;}
 auto root=RootTransform(go);if(!IsAlive(root)||root==actor.root){actor.equippedGo=nullptr;actor.bladeCalibrated=false;actor.weaponModel={};actor.mountCaptured=false;return;}
 actor.equippedGo=go;
 auto parent=Call(Parent,root);
 if(actor.weaponModel.transform!=root||actor.weaponModel.parent!=parent||actor.mountSocket!=actor.sockets.front().binding.transform){
  actor.weaponModel={};actor.mountCaptured=false;actor.bladeCalibrated=false;actor.bladeLength=0;actor.weaponRetry=0;
  actor.mountSocket=actor.sockets.front().binding.transform;if(!parent||!Pin(go)||!Pin(root)||!Pin(parent))return;
  actor.weaponModel.transform=root;actor.weaponModel.parent=parent;
 }
 Mat modelWorld,socketWorld;
 if(!Value(WorldMatrix,root,modelWorld)||!Value(WorldMatrix,actor.sockets.front().binding.transform,socketWorld))return;
 // Sample the first near-hand runtime relation after restoring our writes.
 // Keep it until the model, parent or socket changes; native animation must
 // not be folded into a new installation transform every frame.
 if(!actor.mountCaptured){
  const auto relative=Multiply(socketWorld.inverse(),modelWorld);
  if(Len(relative.point())>.4f){actor.bladeCalibrated=false;actor.weaponPoseDetail="武器尚未到右手挂点，保留原生位置";return;}
  actor.modelInSocketPosition=relative.point();actor.modelInSocketRotation=relative.rotation();actor.mountCaptured=true;
 }
 if(!actor.bladeCalibrated&&actor.entrySeconds>=actor.weaponRetry){
  actor.weaponRetry=actor.entrySeconds+.5f;WeaponPose::AxisEvidence best;Vec3 bestEdge{};
  const auto inverse=modelWorld.inverse();Vec3 grip=inverse.point(socketWorld.point());
  for(const auto [type,meshMethod]:std::array<std::pair<void*,Index>,2>{{{meshFilterClass.type_object,FilterMesh},{rendererClass.type_object,Mesh}}}){
   if(!type||!methods[meshMethod].r.method_info||!methods[MeshBounds].r.method_info)continue;
   bool include=true;void* listArgs[]{type,&include};auto list=Call(GetComponents,go,listArgs);
   for(int i=0;i<Count(list);++i){auto component=Item(list,i);auto mesh=Call(meshMethod,component);WeaponPose::Bounds bounds;Mat world;
    if(!mesh||!Value(MeshBounds,mesh,bounds)||!Value(WorldMatrix,Call(Transform,component),world))continue;
    if(!Finite(bounds.center)||!Finite(bounds.extents)||bounds.extents.x<0||bounds.extents.y<0||bounds.extents.z<0)continue;
    auto meshToRoot=Multiply(inverse,world);
    if(meshMethod==Mesh){
     // A skinned vertex is transformed by its bone and bind pose, not by the
     // renderer's authored transform. Only a single-bone rigid blade admits
     // this bounds-based calibration; deforming skins require vertex data.
     auto skinBones=Call(Bones,component),binds=Call(Bindposes,mesh);
     if(Count(skinBones)!=1||Count(binds)!=1)continue;
     auto pose=Item(binds,0);auto raw=pose?api->object_unbox(api->context,pose):nullptr;
     Mat boneWorld,bind;if(!raw||!Value(WorldMatrix,Item(skinBones,0),boneWorld))continue;
     std::memcpy(&bind,raw,sizeof(bind));meshToRoot=Multiply(inverse,Multiply(boneWorld,bind));
    }
    const std::array<float,3> e{bounds.extents.x,bounds.extents.y,bounds.extents.z};
    const int axis=int(std::max_element(e.begin(),e.end())-e.begin());Vec3 half{};if(axis==0)half.x=e[0];else if(axis==1)half.y=e[1];else half.z=e[2];
    float width=0;for(int c=0;c<3;++c)if(c!=axis){Vec3 cross{};if(c==0)cross.x=e[c];else if(c==1)cross.y=e[c];else cross.z=e[c];
     width=std::max(width,2*Len(Sub(meshToRoot.point(cross),meshToRoot.point({}))));}
    auto evidence=WeaponPose::InferAxis(meshToRoot.point(Sub(bounds.center,half)),meshToRoot.point(Add(bounds.center,half)),grip,width);
    if(evidence.length>best.length){best=evidence;
     int edge=-1;for(int c=0;c<3;++c)if(c!=axis&&(edge<0||e[c]>e[edge]))edge=c;
     Vec3 v{};if(edge==0)v.x=1;else if(edge==1)v.y=1;else v.z=1;
     bestEdge=Normalize(Sub(meshToRoot.point(v),meshToRoot.point({})));}
   }
  }
  if(best.length>0){actor.bladeRootAxis=best.axis;actor.bladeLength=best.length*Len(Sub(modelWorld.point(best.axis),modelWorld.point({})));actor.bladeCalibrated=true;
   actor.edgeRootAxis=bestEdge;
   actor.weaponPoseDetail=std::string("剑身已识别；保留原生安装关系；")+
    (actor.palmAligned[0]&&actor.palmAligned[1]?"双手掌映射已启用":"缺少手掌绑定数据，使用基础重定向")+"；没有插地角度覆盖";
   Log(actor.weaponPoseDetail+" length="+std::to_string(best.length));}
  else actor.weaponPoseDetail="挂点已同步；装备网格未能确认剑身轴，姿势仍按手掌映射播放";
 }
 if(actor.bladeCalibrated){const auto mount=Mul(actor.sockets.front().handRotation,actor.modelInSocketRotation);
  actor.bladeHandAxis=Normalize(Rotate(mount,actor.bladeRootAxis));}
}
bool WriteWeaponBinding(Binding& b,Vec3 position,Quat rotation) {
 if(!IsAlive(b.transform)||!IsAlive(b.parent))return false;
 Mat parent;Quat original;if(!Value(WorldMatrix,b.parent,parent)||!Value(LocalRotation,b.transform,original)||!Value(LocalPosition,b.transform,b.previousPosition))return false;
 b.previousRotation=original;b.lastRotation=Mul(Inverse(parent.rotation()),rotation);b.lastPosition=parent.inverse().point(position);
 b.written=SetValue(SetLocalRotation,b.transform,b.lastRotation);
 b.positionWritten=SetValue(SetLocalPosition,b.transform,b.lastPosition);return b.written&&b.positionWritten;
}
bool UpdateSourceGrip(Quat modelRotation) {
 using namespace Retarget;
 actor.sourceWeaponActive=false;
 auto track=sourceWeaponTracks.find(controller.slot);if(track==sourceWeaponTracks.end())return true;
 actor.sourceWeaponFrame=actor.weaponBlend.Sample(controller.motionSerial,controller.motionAge(),controller.transitionSeconds,track->second.Sample(controller.seconds));
 // Wrist separation and source weapon attachment are distinct data. Keep
 // the original wrist rotations; target grip constraints never invent axes.
 actor.gripFrameWorld=Mul(Mul(modelRotation,actor.sourceBasis),actor.sourceWeaponFrame.rotation);
 actor.sourceWeaponActive=true;return true;
}
void ApplyWeaponSockets() {
 if(actor.sockets.empty())return;
 for(auto& socket:actor.sockets){Mat hand;
  if(!Value(WorldMatrix,socket.handIndex<0?socket.binding.parent:actor.bones[socket.handIndex].transform,hand))continue;
  const auto p=hand.point(socket.handPosition);const auto q=Mul(hand.rotation(),socket.handRotation);
  WriteWeaponBinding(socket.binding,p,q);}
 if(actor.weaponHeld&&actor.mountCaptured&&IsAlive(actor.weaponModel.transform)){
  const auto& socket=actor.sockets.front();Mat hand;if(!Value(WorldMatrix,socket.handIndex<0?socket.binding.parent:actor.bones[socket.handIndex].transform,hand))return;auto q=Mul(hand.rotation(),socket.handRotation);
  const auto p=Retarget::Add(hand.point(socket.handPosition),Rotate(q,actor.modelInSocketPosition));
  WriteWeaponBinding(actor.weaponModel,p,Mul(q,actor.modelInSocketRotation));
 }
}
void Cleanup() {
 localMode.combatEnabled=false;
 presentationPreview=false;
 pumpedMotionSerial=0;
 delayedHits.Clear();delayedContact.Suspend();
 RestoreTargetCamera();targetLock.Release();targetIdentities.Clear();targetSnapshot.clear();
 lockRequest=focusRequest=cycleRequest=targetReleaseRequest=practiceAttackRequest=false;combatFollowupSlot=0;combatFollowupMotion=0;
#ifdef MHE_MODULE_EXTENSION_HEADER
 moduleExtension.Deactivate();
#endif
 paused=false;hudEditing=false;resumeSoundLoops=false;
 demoReturnStarted=false;
 demoHeld=false;demoChargeLevel=0;
 demoElapsed=0;demoFinalTail=false;
 localProjectiles.Clear();projectileMotion=std::numeric_limits<uint64_t>::max();
 for(auto& visual:projectileVisuals){if(visual.mesh)visual.mesh->Reset();visual.mesh.reset();visual.id=0;}
 soundPlayer.Stop();soundTimeline.Reset();themeTimeline.Reset();activeTheme.clear();themeClock=0;themeBowMotion=0;themeBowCharging=false;
 if(!practice.targets.empty())practiceSummary=PracticeTargets();
 practice.Suspend();practice.targets.clear();enemyRefresh=0;practiceDetail=config.local_practice?"演示结束，已保留本轮统计":"本地练习未开启";
 practiceHud.Update(false,"");
 suppressInput=false;autoDemo=false;demoStep=0;controller.stop();RestoreStageMotion();RestoreOwned();RestoreEquipmentVisibility();visualWeapon.Reset();sourceWeapon.Reset();offhandWeapon.Reset();practiceDummy.Reset();ReleaseWeapon();
 ReleaseCameraBinding();
 if(IsAlive(actor.prop)){void* args[]{actor.prop};Call(ObjectDestroy,nullptr,args);}
 if(IsAlive(actor.propMaterial)){void* args[]{actor.propMaterial};Call(ObjectDestroy,nullptr,args);}
 for(auto h:actor.pins)if(api&&api->gchandle_free)api->gchandle_free(api->context,h);
 actor=Actor{};cleanupPending=false;poseLease.Release();phase=initialized?"ready":"stopped";
 if(initialized)detail="演示已结束，"+(config.toggle_key>=VK_F1&&config.toggle_key<=VK_F24?"F"+std::to_string(config.toggle_key-VK_F1+1):"快捷键 "+std::to_string(config.toggle_key))+" 手动 / F9 自动开始";
}
void Collect(void* t,std::unordered_map<std::string,void*>& map,int depth=0) {
 if(!t||depth>64||map.size()>4096)return;
 const auto name=ObjectName(t);map.emplace(name,t);int n=0;
 if(Value(ChildCount,t,n)&&n>=0&&n<1024)for(int i=0;i<n;++i){void* args[]{&i};Collect(Call(GetChild,t,args),map,depth+1);}
}
struct MainSkeleton {
 void* transform=nullptr;std::string path;
 std::unordered_map<std::string,void*> bones;
};
MainSkeleton FindMainSkeleton(void* root,void* renderers){
 std::unordered_set<void*> palette;
 for(int r=0;r<Count(renderers);++r){auto transforms=Call(Bones,Item(renderers,r));
  for(int i=0;i<Count(transforms);++i)if(auto bone=Item(transforms,i))palette.insert(bone);}
 std::vector<MainSkeleton> found;std::vector<CharacterBinding::SkeletonCandidate> candidates;
 std::size_t visited=0;
 const auto visit=[&](auto&& self,void* transform,const std::string& path,unsigned depth)->void {
  if(!transform||depth>64||++visited>4096)return;
  if(ObjectName(transform)=="Bip001"){
   MainSkeleton skeleton{transform,path,{}};Collect(transform,skeleton.bones);
   unsigned coverage=0,directCoverage=0;bool skinned=true;
   const auto ancestorNames=profile.value("ancestor_palette_main_bones",std::vector<std::string>{});
   for(int i=0;i<21;++i){std::string name=fallback[i];
    if(profile.contains("bones")&&profile["bones"].contains(semantic[i]))name=profile["bones"][semantic[i]].get<std::string>();
    auto bone=skeleton.bones.find(name);
    if(bone!=skeleton.bones.end()){++coverage;
     if(palette.contains(bone->second))++directCoverage;
     else if(std::find(ancestorNames.begin(),ancestorNames.end(),name)!=ancestorNames.end()){
      bool descendant=false;for(auto p:palette)if(UnderSkeleton(p,bone->second)){descendant=true;break;}skinned=skinned&&descendant;
     }else skinned=false;
    }else skinned=false;}
   skinned=skinned&&directCoverage>=18;
   candidates.push_back({path,depth,coverage,skinned});found.push_back(std::move(skeleton));
  }
  int count=0;if(!Value(ChildCount,transform,count)||count<0||count>1024)return;
  for(int i=0;i<count;++i){void* args[]{&i};auto child=Call(GetChild,transform,args);
   const auto childName=ObjectName(child);self(self,child,path.empty()?childName:path+"/"+childName,depth+1);}
 };
 visit(visit,root,"",0);
 const auto selected=CharacterBinding::SelectPrimarySkeleton(candidates,profile.value("skeleton_root_path",std::string{}));
 return std::move(found.at(selected));
}
bool UnderSkeleton(void* bone,void* skeleton) {
 for(int depth=0;bone&&depth<64;++depth){if(bone==skeleton)return true;bone=Call(Parent,bone);}return false;
}

void CreateProp() {
 if(!config.show_prop||!gameObjectClass.class_info||!lineClass.type_object||!materialClass.class_info)return;
 for(int i=ObjectDestroy;i<FrameCount;++i)if(!methods[i].r.method_info)return;
 void* go=api->object_new(api->context,gameObjectClass.class_info);if(!go)return;
 // Track allocations immediately: failed construction/configuration must still be cleaned.
 actor.prop=go;Pin(go);auto label=api->string_new(api->context,"Longsword showcase blade");void* ctor[]{label};
 if(!Set(GoCtor,go,ctor))return;
 void* typeArgs[]{lineClass.type_object};void* line=Call(AddComponent,go,typeArgs);if(!line)return;
 void* transform=RootTransform(go);void* hand=actor.bones[12].transform;bool world=false;void* parentArgs[]{hand,&world};
 if(!transform||!Set(SetParent,transform,parentArgs))return;
 SetValue(SetLocalPosition,transform,Vec3{0,0,0});SetValue(SetLocalRotation,transform,Quat{});
 SetValue(LineWorld,line,false);SetValue(LineCount,line,2);SetValue(LineWidth,line,.035f);
 // A thin, collider-free line follows the hand. Grip orientation is user-adjustable.
 auto offset=profile.value("blade_direction",std::vector<float>{0,0,1});
 const auto weapon=profile.value("weapon",Json::object());
 const float length=weapon.value("length_m",1.25f);
 auto grip=weapon.value("grip_position",std::vector<float>{0,0,0});
 auto gripRotation=weapon.value("grip_rotation_xyzw",std::vector<float>{0,0,0,1});
 if(grip.size()==3)SetValue(SetLocalPosition,transform,Vec3{grip[0],grip[1],grip[2]});
 if(gripRotation.size()==4)SetValue(SetLocalRotation,transform,Normalize(Quat{gripRotation[0],gripRotation[1],gripRotation[2],gripRotation[3]}));
 Vec3 a{0,0,0},b{offset.at(0)*length,offset.at(1)*length,offset.at(2)*length};int zero=0,one=1;void* point0[]{&zero,&a};void* point1[]{&one,&b};
 Set(LinePoint,line,point0);Set(LinePoint,line,point1);
 if(offset.size()==3&&gripRotation.size()==4){actor.bladeHandAxis=Retarget::Normalize(Rotate(Normalize({gripRotation[0],gripRotation[1],gripRotation[2],gripRotation[3]}),{offset[0],offset[1],offset[2]}));
  actor.bladeLength=length;actor.bladeCalibrated=true;actor.weaponPoseDetail="备用演示刀跟随手掌映射；没有剑向覆盖";}
 void* shader=nullptr;for(const char* name:{"Sprites/Default","Unlit/Color","Universal Render Pipeline/Unlit","UI/Default"}){
  void* shaderName=api->string_new(api->context,name);void* shaderArgs[]{shaderName};shader=Call(ShaderFind,nullptr,shaderArgs);if(IsAlive(shader))break;shader=nullptr;}
 if(!shader){Log("演示刀材质未匹配；骨骼播放仍可使用");return;}
 void* material=api->object_new(api->context,materialClass.class_info);if(!material)return;
 actor.propMaterial=material;Pin(material);void* materialArgs[]{shader};if(!Set(MaterialCtor,material,materialArgs))return;
 void* setMaterial[]{material};Set(RendererMaterial,line,setMaterial);
 struct Color{float r,g,b,a;} color{.86f,.93f,1,1};SetValue(MaterialColor,material,color);
}

bool Bind() try {
 Cleanup();practiceSummary=Json::array();practice.Clear();auto entity=Call(MainCharacter);bool valid=false,alive=false;
 if(!entity||!Value(Valid,entity,valid)||!valid||!Value(AliveEntity,entity,alive)||!alive){detail="当前角色尚未就绪";return false;}
 auto data=Call(Template,entity);auto id=data?api->field_get_value_object(api->context,templateId.field_info,data):nullptr;
 auto character=Text(id);auto selectedProfile=characterProfiles.find(character);
 if(selectedProfile==characterProfiles.end()){detail="当前角色资料尚未加载，请检查版本与角色资源";return false;}
 profile=selectedProfile->second;
 const auto practiceAxis=profile.value("practice_blade_axis",std::vector<float>{0,0,1});
 practiceBladeAxis=Retarget::Normalize({practiceAxis[0],practiceAxis[1],practiceAxis[2]});
 auto com=Call(ModelComponent,entity);auto model=com?Call(ModelGo,com):nullptr;
 if(!IsAlive(model)){detail="角色模型未就绪";return false;}
 actor.entity=entity;actor.model=model;actor.root=RootTransform(model);actor.character=character;
 if(!actor.root||!Pin(entity)||!Pin(model)||!Pin(actor.root)){Cleanup();detail="无法保留角色对象";return false;}
 auto type=rendererClass.type_object;bool include=true;void* args[]{type,&include};auto renderers=Call(GetComponents,model,args);
 auto skeleton=FindMainSkeleton(actor.root,renderers);actor.skeletonPath=skeleton.path;
 std::unordered_map<std::string,void*> named;
 auto rigRoot=Call(Parent,skeleton.transform);
 Collect(rigRoot?rigRoot:skeleton.transform,named);
 // Anatomical bones and finger roots always come from the selected skinned
 // skeleton identity. Sibling IK sockets retain the primary rig's paths.
 for(const auto& [name,bone]:skeleton.bones)if(name.starts_with("Bip001"))named[name]=bone;
 std::unordered_map<void*,Mat> rest;
 Mat modelWorld;if(!Value(WorldMatrix,actor.root,modelWorld))throw std::runtime_error("Model matrix unavailable");auto modelInverse=modelWorld.inverse();
 unsigned registeredRenderers=0,unchangedRenderers=0;
 for(int r=0;r<Count(renderers);++r){auto renderer=Item(renderers,r);auto mesh=Call(Mesh,renderer);auto transforms=Call(Bones,renderer);auto poses=mesh?Call(Bindposes,mesh):nullptr;
  int count=Count(transforms);if(count<1||count!=Count(poses))continue;
  Mat renderWorld;if(!Value(WorldMatrix,Call(Transform,renderer),renderWorld))continue;
  auto rendererInModel=Multiply(modelInverse,renderWorld);
  std::vector<std::pair<void*,Mat>> rows;for(int j=0;j<count;++j){auto trans=Item(transforms,j);auto pose=Item(poses,j);auto raw=pose?api->object_unbox(api->context,pose):nullptr;if(!trans||!raw||!UnderSkeleton(trans,skeleton.transform))continue;
   Mat bind;std::memcpy(&bind,raw,sizeof(bind));rows.emplace_back(trans,Multiply(rendererInModel,bind.inverse()));}
  bool useRegistration=false;Mat registration;
  if(profile.contains("bind_registration_by_renderer")&&profile.contains("rest_model_matrices")){
   const auto path=ModelObjectPath(Call(Transform,renderer));const Json* rule=nullptr;
   const auto suffix=[](const std::string& p){const auto slash=p.find('/');return slash==std::string::npos?p:p.substr(slash+1);};
   for(auto it=profile["bind_registration_by_renderer"].begin();it!=profile["bind_registration_by_renderer"].end();++it)
    if(it.key()==path||suffix(it.key())==suffix(path)){if(rule)throw std::runtime_error("Ambiguous bind registration path");rule=&it.value();}
   if(rule){const auto matrix=rule->at("matrix").get<std::vector<float>>();if(matrix.size()!=16)throw std::runtime_error("Invalid bind registration");
    for(size_t i=0;i<16;++i){if(!std::isfinite(matrix[i]))throw std::runtime_error("Nonfinite bind registration");registration.v[i]=matrix[i];}
    double plainError=0,registeredError=0;size_t measured=0;
    for(const auto&[transform,rawRest]:rows){const auto name=ObjectName(transform);if(!profile["rest_model_matrices"].contains(name))continue;
     const auto expected=profile["rest_model_matrices"][name].get<std::vector<float>>();if(expected.size()!=16)throw std::runtime_error("Invalid expected model bind frame");
     const Vec3 point{expected[12],expected[13],expected[14]};if(!Finite(point))throw std::runtime_error("Invalid expected bind position");
     plainError+=Retarget::Len(Retarget::Sub(rawRest.point(),point));registeredError+=Retarget::Len(Retarget::Sub(Multiply(registration,rawRest).point(),point));++measured;
    }
    // This chooses between two resource-defined frames, rather than inventing
    // an angle. If the API already reports registered matrices, leave them.
    useRegistration=measured>0&&registeredError+1e-5<plainError*.25;
   }
  }
  if(useRegistration)++registeredRenderers;else ++unchangedRenderers;
  for(const auto&[transform,matrix]:rows)rest.emplace(transform,useRegistration?Multiply(registration,matrix):matrix);
 }
 actor.restRegistration="资源坐标注册 "+std::to_string(registeredRenderers)+" 个渲染器，保持 API 坐标 "+std::to_string(unchangedRenderers)+" 个";
 if(profile.contains("secondary_skin_roots")){
  const auto pathTail=[](const std::string& path){const auto slash=path.find('/');return slash==std::string::npos?path:path.substr(slash+1);};
  for(const auto& row:profile["secondary_skin_roots"]){const auto rootPath=pathTail(row.at("root_path").get<std::string>()),anchorPath=pathTail(row.at("anchor_path").get<std::string>());void* root=nullptr;void* anchor=nullptr;
   // Resolve only actual palette nodes and their ancestors, with exact resource
   // paths. A soldier or a weapon with duplicate bone names cannot be selected.
   for(const auto& [bone,bind]:rest){auto node=bone;for(int depth=0;node&&depth<64;++depth){const auto path=ModelObjectPath(node);
    if(path==rootPath||path.ends_with("/"+rootPath))root=node;if(path==anchorPath||path.ends_with("/"+anchorPath))anchor=node;if(node==actor.root)break;node=Call(Parent,node);
   }}
   if(!root||!anchor||!UnderSkeleton(anchor,skeleton.transform)||UnderSkeleton(root,skeleton.transform)||root==actor.root)continue;
   SecondarySkinRoot driven;driven.binding.transform=root;driven.binding.parent=Call(Parent,root);driven.anchor=anchor;
   const auto values=row.at("anchor_relative_matrix").get<std::vector<float>>();if(values.size()!=16)throw std::runtime_error("Invalid secondary skin rest relation");
   for(size_t i=0;i<16;++i){if(!std::isfinite(values[i]))throw std::runtime_error("Invalid secondary skin matrix");driven.anchorRelative.v[i]=values[i];}driven.anchorRelative.inverse();
   if(!driven.binding.parent||!Pin(root)||!Pin(driven.binding.parent)||!Pin(anchor))throw std::runtime_error("Secondary skin root pin failed");actor.secondaryRoots.push_back(driven);
  }
 }
 actor.bones.resize(21);
 // Some authored main bones are ancestors of the skin palette (e.g. Seraph
 // pelvis). Recover their bind frame from a real skinned child and the asset's
 // serialized local TRS, never the currently animated local transform.
 if(profile.contains("rest_local_transforms"))for(unsigned pass=0;pass<21;++pass){bool changed=false;
  for(const auto&[name,transform]:named){if(rest.contains(transform))continue;auto parent=Call(Parent,transform);
   (void)parent;
   for(const auto&[childName,childTransform]:named){if(!rest.contains(childTransform)||Call(Parent,childTransform)!=transform)continue;
    if(!profile["rest_local_transforms"].contains(childName))continue;const auto& row=profile["rest_local_transforms"][childName];
    const auto p=row.at("position").get<std::vector<float>>(),q=row.at("rotation_xyzw").get<std::vector<float>>(),s=row.at("scale").get<std::vector<float>>();
    if(p.size()!=3||q.size()!=4||s.size()!=3||!Finite(Vec3{p[0],p[1],p[2]})||!Finite(Vec3{s[0],s[1],s[2]}))throw std::runtime_error("Invalid authored rest transform");
    const auto rotation=Normalize({q[0],q[1],q[2],q[3]});Mat local;local.at(3,3)=1;
    for(int column=0;column<3;++column){Vec3 axis{};if(column==0)axis.x=s[column];else if(column==1)axis.y=s[column];else axis.z=s[column];const auto v=Rotate(rotation,axis);
     local.at(0,column)=v.x;local.at(1,column)=v.y;local.at(2,column)=v.z;}
    local.at(0,3)=p[0];local.at(1,3)=p[1];local.at(2,3)=p[2];rest.emplace(transform,Multiply(rest.at(childTransform),local.inverse()));changed=true;break;
   }
  }if(!changed)break;
 }
 for(int i=0;i<21;++i){std::string name=fallback[i];if(profile.contains("bones")&&profile["bones"].contains(semantic[i]))name=profile["bones"][semantic[i]].get<std::string>();
  auto found=named.find(name);if(found==named.end()||!rest.contains(found->second))throw std::runtime_error("Missing skinned bind bone: "+name);
  auto& binding=actor.bones[i];binding.transform=found->second;binding.parent=Call(Parent,binding.transform);binding.restWorld=rest.at(binding.transform).rotation();
  if(!binding.parent||!Pin(binding.transform)||!Pin(binding.parent))throw std::runtime_error("Bone pin failed");
  if(rest.contains(binding.parent))binding.restLocal=Multiply(rest.at(binding.parent).inverse(),rest.at(binding.transform)).point();
  else if(!Value(LocalPosition,binding.transform,binding.restLocal))throw std::runtime_error("Bone local unavailable");
  auto parent=binding.parent;while(parent&&binding.depth<64){++binding.depth;parent=Call(Parent,parent);}actor.order.push_back(i);
 }
 std::sort(actor.order.begin(),actor.order.end(),[](int a,int b){return actor.bones[a].depth<actor.bones[b].depth;});
 if(sourceRestReady){
  using namespace Retarget;
  const auto pos=[&](int i){return rest.at(actor.bones[i].transform).point();};
  actor.sourceBasis=SourceToTargetBodyBasis(Sub(sourceRestPositions[10],sourceRestPositions[6]),
   Sub(sourceRestPositions[2],sourceRestPositions[0]),Sub(pos(10),pos(6)),Sub(pos(2),pos(0)));
  const auto sourceRight=Sub(sourceRestPositions[10],sourceRestPositions[6]),targetRight=Sub(pos(10),pos(6));
  actor.sourceStageBasis=SourceToTargetBodyBasis({sourceRight.x,0,sourceRight.z},{0,1,0},{targetRight.x,0,targetRight.z},{0,1,0});
  // Source T-pose and target A-pose must agree before applying the world deltas.
  // Keep the target bone roll, and carry forearm correction to each hand/socket.
  const auto original=actor.bones;
  actor.nativeHandRest={original[8].restWorld,original[12].restWorld};
  for(const auto [bone,child]:std::array<std::pair<int,int>,6>{{{5,6},{6,7},{7,8},{9,10},{10,11},{11,12}}})
   actor.bones[bone].restWorld=AlignedBoneRest(original[bone].restWorld,Sub(pos(child),pos(bone)),
    Sub(sourceRestPositions[child],sourceRestPositions[bone]),actor.sourceBasis);
  for(const auto [hand,forearm]:std::array<std::pair<int,int>,2>{{{8,7},{12,11}}})
   actor.bones[hand].restWorld=CarryRestAlignment(actor.bones[forearm].restWorld,original[forearm].restWorld,original[hand].restWorld);
  if(sourcePalmsReady){
   const std::array<const char*,2> sides{"L","R"};const std::array<int,2> hands{8,12};
   for(int side=0;side<2;++side){
    const auto finger=[&](int number,Vec3& point){
     auto it=named.find(std::string("Bip001_")+sides[side]+"_Finger"+std::to_string(number));
     if(it==named.end()||!rest.contains(it->second))return false;
     point=rest.at(it->second).point();return true;};
    Vec3 index,middle,pinky;const int hand=hands[side];
    if(!finger(1,index)||!finger(2,middle)||!finger(4,pinky))continue;
    const auto forward=Sub(middle,pos(hand)),across=Sub(index,pinky);
    if(Len(forward)<.01f||Len(Cross(forward,across))<1e-5f)continue;
    actor.bones[hand].restWorld=AlignedPalmRest(original[hand].restWorld,sourcePalmFrames[side],PalmBasis(forward,across),actor.sourceBasis);
    actor.palmAligned[side]=true;
   }
  }
 }
 float hip=rest.at(actor.bones[0].transform).point().y;
 float foot=std::min({rest.at(actor.bones[15].transform).point().y,rest.at(actor.bones[16].transform).point().y,
  rest.at(actor.bones[19].transform).point().y,rest.at(actor.bones[20].transform).point().y});
 actor.soleOffset=std::clamp(foot,-.10f,.15f);
 if(profile.contains("ground_foot_probes"))for(const auto& probe:profile["ground_foot_probes"]){Vec3 p{};float weights=0;bool valid=true;
  for(const auto& influence:probe.at("influences")){const auto it=named.find(influence.at("bone_name").get<std::string>());const auto point=influence.at("local_point").get<std::vector<float>>();const float w=influence.at("weight").get<float>();
   if(it==named.end()||!rest.contains(it->second)||point.size()!=3||!std::isfinite(w)||w<0){valid=false;break;}
   p=Retarget::Add(p,Retarget::Scale(rest.at(it->second).point({point[0],point[1],point[2]}),w));weights+=w;
  }if(valid&&std::abs(weights-1)<.01f&&Finite(p))foot=std::min(foot,p.y);
 }
 float sourceHeight=1.0461f;
 if(sourceRestReady){
  const float sourceHip=Rotate(actor.sourceStageBasis,sourceRestPositions[0]).y;
  float sourceFoot=std::numeric_limits<float>::max();
  for(int index:{15,16,19,20})sourceFoot=std::min(sourceFoot,Rotate(actor.sourceStageBasis,sourceRestPositions[index]).y);
  const float measured=sourceHip-sourceFoot;
  if(std::isfinite(measured)&&measured>.2f&&measured<3.f)sourceHeight=measured;
  if(sourceSoleHeight>.2f&&sourceSoleHeight<3.f)sourceHeight=sourceSoleHeight;
 }
 const float measuredScale=(hip-foot)/sourceHeight;if(!std::isfinite(measuredScale)||measuredScale<.25f||measuredScale>4)throw std::runtime_error("Unsupported measured character scale");actor.scale=measuredScale;
 if(profile.contains("ground_foot_probes")){
  if(!profile["ground_foot_probes"].is_array()||profile["ground_foot_probes"].size()>32)throw std::runtime_error("Invalid source mesh foot probes");
  for(const auto& point:profile["ground_foot_probes"]){FootProbe probe;float totalWeight=0;
   for(const auto& influence:point.at("influences")){const auto name=influence.at("bone_name").get<std::string>();auto found=named.find(name);if(found==named.end())continue;
    const auto p=influence.at("local_point").get<std::vector<float>>();const float weight=influence.at("weight").get<float>();
    if(p.size()!=3||!Finite(Vec3{p[0],p[1],p[2]})||!std::isfinite(weight)||weight<0||weight>1)throw std::runtime_error("Invalid mesh foot influence");
    if(!Pin(found->second))throw std::runtime_error("Foot probe bone pin failed");probe.push_back({found->second,{p[0],p[1],p[2]},weight});totalWeight+=weight;
   }if(!probe.empty()&&std::abs(totalWeight-1)<.01f){for(auto& influence:probe)influence.weight/=totalWeight;actor.footProbes.push_back(std::move(probe));}
  }
 }
 for(size_t index=0;index<sourceFingerBones.size();++index){const auto name=TargetFingerName(sourceFingerBones[index].at("name").get<std::string>());if(name.empty())continue;
  auto found=named.find(name);if(found==named.end()||!rest.contains(found->second))continue;
  const bool left=sourceFingerBones[index].at("hand")=="left";const auto hand=left?8:12;
  FingerBinding finger;finger.source=index;auto& b=finger.binding;b.transform=found->second;b.parent=Call(Parent,b.transform);
  if(!b.parent||!Pin(b.transform)||!Pin(b.parent))throw std::runtime_error("Finger pin failed");
  b.restWorld=Retarget::CarryRestAlignment(actor.bones[hand].restWorld,actor.nativeHandRest[left?0:1],rest.at(b.transform).rotation());
  if(!Value(LocalRotation,b.transform,b.lastRotation))throw std::runtime_error("Finger local rotation unavailable");finger.from=b.lastRotation;actor.fingers.push_back(finger);
 }
 if(profile.contains("ground_hand_probes"))for(int side=0;side<2;++side){
  const char* key=side==0?"left":"right";const auto& bank=profile["ground_hand_probes"];
  if(!bank.is_object()||!bank.contains(key))continue;const auto& points=bank[key];
  if(!points.is_array()||points.size()>32)throw std::runtime_error("Invalid source mesh hand probes");
  for(const auto& point:points){FootProbe probe;float totalWeight=0;
   for(const auto& influence:point.at("influences")){auto found=named.find(influence.at("bone_name").get<std::string>());if(found==named.end())continue;
    const auto p=influence.at("local_point").get<std::vector<float>>();const float weight=influence.at("weight").get<float>();
    if(p.size()!=3||!Finite(Vec3{p[0],p[1],p[2]})||!std::isfinite(weight)||weight<0||weight>1)throw std::runtime_error("Invalid mesh hand influence");
    if(!Pin(found->second))throw std::runtime_error("Hand probe bone pin failed");probe.push_back({found->second,{p[0],p[1],p[2]},weight});totalWeight+=weight;
   }if(!probe.empty()&&std::abs(totalWeight-1)<.01f){for(auto& influence:probe)influence.weight/=totalWeight;actor.handProbes[side].push_back(std::move(probe));}
  }
 }
 if(named.contains("Bip001_R_Finger22"))actor.drawFinger=named.at("Bip001_R_Finger22");
 BindGroundQueries();lastSound.clear();soundCueCount=0;
 if(!poseLease.Acquire()){Cleanup();detail="另一个模块正在控制角色，请先结束其演示";return false;}
 BindWeaponSockets(named);
 const bool holding=HoldEquippedWeapon();
 if(config.show_prop&&config.visual_weapon!="equipped"){
  const Json* visual=nullptr;
  const auto chosen=config.visual_weapon=="laevatain_blade"?std::string("laevatain_full"):config.visual_weapon=="typhoea_bow"?std::string("typhoea_bow"):std::string{};
  if(!chosen.empty()&&visualWeaponRegistry.contains(chosen))visual=&visualWeaponRegistry.at(chosen);
  else if(config.visual_weapon!="source"&&profile.contains("visual_weapons")&&profile["visual_weapons"].contains(loadedMode))visual=&profile["visual_weapons"][loadedMode];
  else if(loadedMode=="longsword"&&config.laevatain_blade&&profile.contains("visual_weapon"))visual=&profile["visual_weapon"];
  if(visual){actor.visualMountName=visual->value("mount_socket_name",visual->value("mount_side",loadedMode=="bow"?std::string("left"):std::string("right"))=="left"?std::string("wep_L"):std::string("wep_R"));actor.visualSourceAdapter=visual->value("mount_driver",std::string{})=="mhw_source_attachment";visualWeapon.Bind(api,*visual);}
  else if(!modeProps.empty()){
   sourceWeapon.Bind(api,modeProps.front(),activeAssetRoot);actor.sourcePresentation=true;actor.visualMountName=loadedMode=="bow"?"wep_L":"wep_R";
  }
  if(modeProps.size()>1&&(loadedMode=="sword_shield"||loadedMode=="lance")){offhandWeapon.Bind(api,modeProps[1],activeAssetRoot);actor.offhandRequested=true;}
 }
 if(!holding&&!visualWeapon.Available()&&!sourceWeapon.Available())CreateProp();
 if(!controller.start(configuredIdleSlot))throw std::runtime_error("Idle clip unavailable");
 localMode.Reset(loadedMode);
#ifdef MHE_MODULE_EXTENSION_HEADER
 moduleExtension.Activate(actor.entity);
#endif
 suppressInput=true;phase="active";detail="动作演示已接管输入";
 Log("Bound "+character+" from main skinned bindposes; skeleton="+actor.skeletonPath+" bones=21");return true;
}catch(...){Cleanup();throw;}

bool SkipGrip(){
 if(actor.gripAvailable)Log("Supporting-hand IK unavailable; continuing with the retargeted pose");
 actor.gripAvailable=false;return true;
}
bool ApplyGrip(float blend) {
 using namespace Retarget;
 auto track=gripTracks.find(controller.slot);if(!actor.gripAvailable||track==gripTracks.end()||config.grip_strength<=0)return true;
 auto frame=track->second.Sample(controller.seconds);
 if(actor.sourceWeaponActive)frame.weight=actor.sourceWeaponFrame.supportWeight;
 if(loadedMode=="longsword"){
  const std::string model=actor.visualSourceAdapter&&visualWeapon.Available()?"wpn_misc_0016":
   actor.sourcePresentation&&!modeProps.empty()?"source:"+modeProps.front().value("id",std::string{}):"";
  const auto byModel=geometryContactMasks.find(model);
  if(byModel!=geometryContactMasks.end()){
   const auto byCharacter=byModel->second.find(actor.character);
   if(byCharacter!=byModel->second.end()){
    const auto mask=byCharacter->second.find(controller.slot);
    if(mask!=byCharacter->second.end())frame.weight=mask->second.Sample(controller.seconds);
   }
  }
 }
 const float strength=frame.weight*config.grip_strength*blend*
  std::min(1.f,controller.motionAge()/std::max(.01f,config.blend_seconds));if(strength<.001f)return true;
 const int upperIndex=loadedMode=="bow"?10:6,lowerIndex=upperIndex+1,supportIndex=upperIndex+2,dominantIndex=loadedMode=="bow"?8:12;
 Vec3 shoulder,elbow,left,right;Quat rootRotation,wristRotation;
 if(!Value(WorldPosition,actor.bones[upperIndex].transform,shoulder)||!Value(WorldPosition,actor.bones[lowerIndex].transform,elbow)||
  !Value(WorldPosition,actor.bones[supportIndex].transform,left)||!Value(WorldPosition,actor.bones[dominantIndex].transform,right)||
  !Value(Rotation,actor.root,rootRotation)||!Value(Rotation,actor.bones[supportIndex].transform,wristRotation))return SkipGrip();
 if(!Finite(shoulder)||!Finite(elbow)||!Finite(left)||!Finite(right)||Len(Sub(elbow,shoulder))<1e-5f||Len(Sub(left,elbow))<1e-5f)return SkipGrip();
 const auto offset=actor.sourceWeaponActive?Rotate(actor.gripFrameWorld,Scale(actor.sourceWeaponFrame.supportOffset,actor.scale)):
  Rotate(Mul(rootRotation,actor.sourceBasis),Scale(frame.offset,actor.scale));
 auto goal=Lerp(left,Add(right,offset),strength);
 // Most source contact intervals only move the supporting arm. A target
 // beyond its fixed arm reach uses the shared-grip projection below.
 auto solved=SolveArm(shoulder,goal,elbow,Len(Sub(elbow,shoulder)),Len(Sub(left,elbow)),Rotate(rootRotation,{-1,0,-1}));
 const int dominantUpper=dominantIndex-2,dominantLower=dominantIndex-1;
 const std::array<int,6> chain{upperIndex,lowerIndex,supportIndex,dominantUpper,dominantLower,dominantIndex};
 std::array<Quat,6> baseline{};for(size_t k=0;k<chain.size();++k)baseline[k]=actor.bones[chain[k]].lastRotation;
 const auto rollbackGrip=[&](){for(size_t k=0;k<chain.size();++k){auto& b=actor.bones[chain[k]];if(Close(b.lastRotation,baseline[k]))continue;
   Quat current;if(!Value(LocalRotation,b.transform,current))return false;
   if(Close(current,b.lastRotation)){if(!SetValue(SetLocalRotation,b.transform,baseline[k]))return false;b.lastRotation=baseline[k];}
   else b.written=false;}
  return SkipGrip();};
 const auto writeWorld=[&](int i,Quat world){auto& b=actor.bones[i];Quat parent;
  if(!Value(Rotation,b.parent,parent))return false;auto local=Mul(Inverse(parent),world);
  if(!SetValue(SetLocalRotation,b.transform,local))return false;b.lastRotation=local;return true;};
 const auto writeArm=[&](int upperIndex,int lowerIndex,int handIndex,Vec3 shoulder,Vec3 elbow,const ArmSolution& solution,Quat wrist){
  Quat upper;if(!Value(Rotation,actor.bones[upperIndex].transform,upper)||
   !writeWorld(upperIndex,Mul(FromTo(Sub(elbow,shoulder),Sub(solution.elbow,shoulder)),upper)))return false;
  Vec3 movedElbow,movedHand;Quat lower;
  if(!Value(WorldPosition,actor.bones[lowerIndex].transform,movedElbow)||!Value(WorldPosition,actor.bones[handIndex].transform,movedHand)||
   !Value(Rotation,actor.bones[lowerIndex].transform,lower))return false;
  return writeWorld(lowerIndex,Mul(FromTo(Sub(movedHand,movedElbow),Sub(solution.hand,movedElbow)),lower))&&writeWorld(handIndex,wrist);
 };
 // A source-derived contact can exceed a short character's arm reach. Move the
 // shared grip by the least distance into both fixed-length reach volumes,
 // retaining the source wrist offset and both original wrist rotations.
 if(solved.clamped){Vec3 dominantShoulder,dominantElbow;Quat dominantWrist;
  if(!Value(WorldPosition,actor.bones[dominantUpper].transform,dominantShoulder)||
   !Value(WorldPosition,actor.bones[dominantLower].transform,dominantElbow)||
   !Value(Rotation,actor.bones[dominantIndex].transform,dominantWrist))return rollbackGrip();
  if(!Finite(dominantShoulder)||!Finite(dominantElbow)||Len(Sub(dominantElbow,dominantShoulder))<1e-5f||
   Len(Sub(right,dominantElbow))<1e-5f)return rollbackGrip();
  const auto coupled=SolveTwoHandGrip(dominantShoulder,dominantElbow,right,shoulder,elbow,left,offset,strength,
   Rotate(rootRotation,{1,0,-1}),Rotate(rootRotation,{-1,0,-1}));
  if(coupled.reachable){
   if(!writeArm(dominantUpper,dominantLower,dominantIndex,dominantShoulder,dominantElbow,coupled.dominant,dominantWrist)||
    !writeArm(upperIndex,lowerIndex,supportIndex,shoulder,elbow,coupled.support,wristRotation))return rollbackGrip();
   return true;
  }
 }
 Quat upper;if(!Value(Rotation,actor.bones[upperIndex].transform,upper)||
  !writeWorld(upperIndex,Mul(FromTo(Sub(elbow,shoulder),Sub(solved.elbow,shoulder)),upper)))return rollbackGrip();
 Vec3 movedElbow,movedHand;Quat lower;
 if(!Value(WorldPosition,actor.bones[lowerIndex].transform,movedElbow)||!Value(WorldPosition,actor.bones[supportIndex].transform,movedHand)||
  !Value(Rotation,actor.bones[lowerIndex].transform,lower))return rollbackGrip();
 if(!writeWorld(lowerIndex,Mul(FromTo(Sub(movedHand,movedElbow),Sub(solved.hand,movedElbow)),lower)))return rollbackGrip();
 if(!writeWorld(supportIndex,wristRotation))return rollbackGrip();
 return true;
}

bool LiftGroundedHand(int side,float amount){
 using namespace Retarget;const int upper=side==0?6:10,lower=upper+1,hand=upper+2;
 Vec3 shoulder,elbow,wrist;Quat orientation,root;
 if(!Value(WorldPosition,actor.bones[upper].transform,shoulder)||!Value(WorldPosition,actor.bones[lower].transform,elbow)||
  !Value(WorldPosition,actor.bones[hand].transform,wrist)||!Value(Rotation,actor.bones[hand].transform,orientation)||!Value(Rotation,actor.root,root))return false;
 const auto goal=Add(wrist,{0,amount,0});const auto solved=SolveArm(shoulder,goal,elbow,Len(Sub(elbow,shoulder)),Len(Sub(wrist,elbow)),Rotate(root,{side==0?-1.f:1.f,0,-1}));
 if(Len(Sub(solved.hand,goal))>.001f)return false;
 const auto write=[&](int index,Quat world){auto& b=actor.bones[index];Quat parent;if(!Value(Rotation,b.parent,parent))return false;
  const auto local=Mul(Inverse(parent),world);if(!SetValue(SetLocalRotation,b.transform,local))return false;b.lastRotation=local;return true;};
 Quat rotation;if(!Value(Rotation,actor.bones[upper].transform,rotation)||!write(upper,Mul(FromTo(Sub(elbow,shoulder),Sub(solved.elbow,shoulder)),rotation)))return false;
 Vec3 movedElbow,movedWrist;if(!Value(WorldPosition,actor.bones[lower].transform,movedElbow)||!Value(WorldPosition,actor.bones[hand].transform,movedWrist)||
  !Value(Rotation,actor.bones[lower].transform,rotation))return false;
 return write(lower,Mul(FromTo(Sub(movedWrist,movedElbow),Sub(solved.hand,movedElbow)),rotation))&&write(hand,orientation);
}
bool TerrainQuery(Vec3 origin,Vec3 direction,float distance,float radius,TerrainHit& hit){
 if(!terrainHitLayout||!actor.terrainMask||!Finite(origin)||!Finite(direction)||!std::isfinite(distance)||distance<=0)return false;
 const bool sphere=radius>0;void* receiver=sphere?actor.groundSphereCast:actor.groundRaycast;
 const auto method=sphere?actor.groundSphereCastMethod:actor.groundRaycastMethod;
 alignas(16) unsigned char payload[RaycastHitLayout::Capacity]{};int ignoreTriggers=1,mask=actor.terrainMask;
 void* rayArgs[]{&origin,&direction,payload,&distance,&mask,&ignoreTriggers};
 void* sphereArgs[]{&origin,&radius,&direction,payload,&distance,&mask,&ignoreTriggers};
 bool found=false;
 if(method){void* ex=nullptr;auto box=api->runtime_invoke(api->context,method,receiver,sphere?sphereArgs:rayArgs,&ex);
  auto raw=!ex&&box?api->object_unbox(api->context,box):nullptr;if(raw)std::memcpy(&found,raw,sizeof(found));}
 else Value(sphere?TerrainSphereCast:TerrainRaycast,nullptr,found,sphere?sphereArgs:rayArgs);
 if(!found)return false;
 std::memcpy(&hit.point,payload+terrainHitLayout.point,sizeof(hit.point));std::memcpy(&hit.normal,payload+terrainHitLayout.normal,sizeof(hit.normal));std::memcpy(&hit.distance,payload+terrainHitLayout.distance,sizeof(hit.distance));
 return Finite(hit.point)&&Finite(hit.normal)&&std::isfinite(hit.distance)&&hit.distance>=0&&hit.distance<=distance+.01f;
}
bool GroundAt(Vec3 position,float expectedY,float& floor){
 TerrainHit hit;position.y=expectedY+.65f;
 if(!TerrainQuery(position,{0,-1,0},1.85f,0,hit)||!Walkable(hit,expectedY))return false;
 floor=hit.point.y;return true;
}
Vec3 GroundDisplay(Vec3 baseline,Vec3 offset,float blend){
 using namespace Retarget;
 std::array<Vec3,36> feet{};constexpr int indices[]{15,16,19,20};
 for(size_t i=0;i<4;++i)if(!Value(WorldPosition,actor.bones[indices[i]].transform,feet[i]))return offset;
 bool meshSole=!actor.footProbes.empty();size_t count=4;std::unordered_map<void*,Mat> frames;
 if(meshSole)for(const auto& probe:actor.footProbes){Vec3 point{};bool valid=true;
  for(const auto& influence:probe){auto frame=frames.find(influence.transform);if(frame==frames.end()){Mat matrix;if(!Value(WorldMatrix,influence.transform,matrix)){valid=false;break;}frame=frames.emplace(influence.transform,matrix).first;}
   point=Add(point,Scale(frame->second.point(influence.local),influence.weight));}
  if(!valid||!Finite(point)){meshSole=false;break;}if(count<feet.size())feet[count++]=point;
 }
 if(!meshSole)count=4;for(size_t i=count;i<feet.size();++i)feet[i]=feet[0];
 auto guide=groundTracks.find(controller.slot);
 if(guide==groundTracks.end()){actor.groundDetail="当前动作缺少脚底参考，保留动作位移";return offset;}
 if(actor.groundMotion!=controller.motionSerial){actor.clearanceFrom=actor.clearance;actor.groundMotion=controller.motionSerial;actor.groundBlend=0;}
 const float source=guide->second.Sample(controller.seconds)*actor.scale;
 actor.groundBlend=std::max(actor.groundBlend,std::clamp(controller.seconds/std::max(.01f,controller.transitionSeconds),0.f,1.f));
 const float transition=actor.groundBlend;
 actor.clearance=actor.clearanceFrom+(source-actor.clearanceFrom)*transition;
 float baselineFloor=baseline.y;bool baselineHit=GroundAt(baseline,baseline.y,baselineFloor);
 if(baselineHit&&actor.clearance>.01f){TerrainHit ceiling;
  if(TerrainQuery(Retarget::Add(baseline,Vec3{0,1.35f,0}),{0,1,0},actor.clearance,.22f,ceiling)&&ceiling.normal.y<-.5f)
   actor.clearance=std::min(actor.clearance,std::max(0.f,ceiling.distance-.03f));}
 float pathFloor=baselineFloor;const float travel=Len(offset);
 if(baselineHit&&travel>.001f){
  TerrainHit wall;auto origin=Add(baseline,Vec3{0,.65f+actor.clearance,0});
  if(TerrainQuery(origin,Scale(offset,1/travel),travel,.22f,wall)&&wall.normal.y<.5f)
   offset=Scale(offset,std::clamp((wall.distance-.03f)/travel,0.f,1.f));
  float upperTravel=Len(offset);
  if(upperTravel>.001f&&TerrainQuery(Add(baseline,Vec3{0,1.35f+actor.clearance,0}),Scale(offset,1/upperTravel),upperTravel,.22f,wall)&&wall.normal.y<.5f)
   offset=Scale(offset,std::clamp((wall.distance-.03f)/upperTravel,0.f,1.f));
  // A cosmetic root has no nav controller. Stop before a ledge instead of
  // displaying a grounded move over empty space. Check the full travel path.
  const float length=Len(offset);const int steps=std::min(128,std::max(1,int(std::ceil(length/.2f))));
  float& floor=pathFloor;
  for(int i=1;i<=steps;++i){float candidate=0;auto p=Add(baseline,Scale(offset,float(i)/steps));
   if(!GroundAt(p,floor,candidate)){offset=Scale(offset,float(i-1)/steps);break;}floor=candidate;}
 }
 float rootFloor=pathFloor;bool displacedHit=baselineHit&&GroundAt(Add(baseline,offset),pathFloor,rootFloor);
 std::array<float,36> floors{};
 for(size_t i=0;i<count;++i){floors[i]=rootFloor;
  if(displacedHit)GroundAt(Add(feet[i],offset),rootFloor,floors[i]);}
 for(size_t i=count;i<floors.size();++i)floors[i]=floors[0];
 actor.groundCorrection=FloorCorrection(feet,floors,meshSole?0:actor.soleOffset,actor.clearance);
 // Only a source-verified hand support interval can replace a shoe anchor.
 // During a forward roll the source shoes rise while a glove supports the
 // body. Preserve that pose and lift only the display root as needed.
 const float soleCorrection=actor.groundCorrection;const auto support=supportGroundTracks.find(controller.slot);
 if(support!=supportGroundTracks.end())for(int side=0;side<2;++side){
  const auto& guide=support->second[side];if(guide.Kind(controller.seconds)!=1||guide.active.Sample(controller.seconds)<.5f||actor.handProbes[side].empty())continue;
  const float clearance=guide.clearance.Sample(controller.seconds)*actor.scale;
  for(const auto& probe:actor.handProbes[side]){Vec3 point{};bool valid=true;
   for(const auto& influence:probe){auto frame=frames.find(influence.transform);if(frame==frames.end()){Mat matrix;if(!Value(WorldMatrix,influence.transform,matrix)){valid=false;break;}frame=frames.emplace(influence.transform,matrix).first;}
    point=Add(point,Scale(frame->second.point(influence.local),influence.weight));}
   if(!valid||!Finite(point))continue;float floor=rootFloor;
   if(displacedHit)GroundAt(Add(point,offset),rootFloor,floor);
   actor.groundCorrection=std::max(actor.groundCorrection,floor+clearance-point.y);
  }
 }
 if(support!=supportGroundTracks.end())for(int side=0;side<2;++side){
  const auto& guide=support->second[side];const int kind=guide.Kind(controller.seconds);
  if((kind!=2&&kind!=3)||(kind==2&&guide.active.Sample(controller.seconds)<.5f)||actor.handProbes[side].empty())continue;
  const float clearance=kind==3?0:guide.clearance.Sample(controller.seconds)*actor.scale;
  float correction=0;
  for(const auto& probe:actor.handProbes[side]){Vec3 point{};bool valid=true;
   for(const auto& influence:probe){auto frame=frames.find(influence.transform);if(frame==frames.end()){Mat matrix;if(!Value(WorldMatrix,influence.transform,matrix)){valid=false;break;}frame=frames.emplace(influence.transform,matrix).first;}
    point=Add(point,Scale(frame->second.point(influence.local),influence.weight));}
   if(!valid||!Finite(point))continue;float floor=rootFloor;if(displacedHit)GroundAt(Add(point,offset),rootFloor,floor);
   correction=std::max(correction,floor+clearance-point.y-actor.groundCorrection);
  }
  if(correction>.00001f){
   if(!LiftGroundedHand(side,correction))throw std::runtime_error("手部接地超出固定臂长可达范围，已停止演示并恢复控制");
   frames.clear();
  }
 }
 if(baselineHit&&actor.groundCorrection>soleCorrection+.01f){
  Vec3 highest=feet[0];for(const auto& bone:actor.bones){Vec3 point;if(Value(WorldPosition,bone.transform,point)&&Finite(point)&&point.y>highest.y)highest=point;}
  const Vec3 origin{highest.x+offset.x,rootFloor+.23f,highest.z+offset.z};TerrainHit ceiling;
  const float required=std::max(0.f,highest.y+actor.groundCorrection+.20f*actor.scale-origin.y);
  if(required>0&&TerrainQuery(origin,{0,1,0},required,.18f,ceiling)&&ceiling.normal.y<-.5f)
   throw std::runtime_error("手部支撑所需高度与场景顶面冲突，已停止演示并恢复控制");
 }
 actor.groundFloor=rootFloor;
 offset.y=actor.groundCorrection*blend;
 actor.groundDetail=displacedHit?(meshSole?"真实鞋底网格与场景地面已校准":"脚骨参考与场景地面已校准"):(meshSole?"真实鞋底已校准；场景查询不可用":"脚骨校准；场景查询不可用");
 return offset;
}

bool ApplyPose() {
 if(controller.output.rotations.size()!=actor.bones.size())return false;
 RestoreOwned();Quat modelRotation;if(!Value(Rotation,actor.root,modelRotation))return false;
 CaptureWeaponModel();
 Vec3 stageBaseline{};
 const bool stage=methods[WorldPosition].r.method_info&&methods[SetWorldPosition].r.method_info&&Value(WorldPosition,actor.root,stageBaseline);
 actor.groundFloor=stageBaseline.y;
 // From the body's skin bind matrices, map model-space source deltas to target rest axes.
 const float blend=std::min(1.f,actor.entrySeconds/std::max(.01f,config.blend_seconds));
 for(int index:actor.order){auto& b=actor.bones[index];if(!IsAlive(b.transform))return false;
  Quat parentRotation,original;if(!Value(Rotation,b.parent,parentRotation)||!Value(LocalRotation,b.transform,original))return false;
  auto desiredWorld=Mul(modelRotation,Retarget::ApplySourceDelta(controller.output.rotations[index],b.restWorld,actor.sourceBasis));
  auto desiredLocal=Mul(Inverse(parentRotation),desiredWorld);
  b.previousRotation=original;b.lastRotation=Slerp(original,desiredLocal,blend);
  if(!SetValue(SetLocalRotation,b.transform,b.lastRotation))return false;b.written=true;
  if(index==0){Vec3 p;if(!Value(LocalPosition,b.transform,p))return false;auto hip=Rotate(actor.sourceStageBasis,controller.output.hip);
   // Move the display model root when possible so renderer bounds follow it.
   if(!config.root_motion||stage){hip.x=0;hip.z=0;}hip.x*=actor.scale;hip.y*=actor.scale;hip.z*=actor.scale;
   auto vector=Rotate(modelRotation,hip);Vec3 local;void* args[]{&vector};if(!Value(InverseVector,b.parent,local,args))return false;
   b.previousPosition=p;b.lastPosition=Lerp(p,Vec3{b.restLocal.x+local.x,b.restLocal.y+local.y,b.restLocal.z+local.z},blend);
   if(!SetValue(SetLocalPosition,b.transform,b.lastPosition))return false;b.positionWritten=true;
  }
 }
 if(!UpdateSourceGrip(modelRotation)||!ApplyGrip(blend))return false;
 auto fingers=fingerTracks.find(controller.slot);if(fingers!=fingerTracks.end()){
  const auto frame=fingers->second.Sample(controller.seconds);const bool changed=actor.fingerMotion!=controller.motionSerial;
  for(auto& finger:actor.fingers){auto& b=finger.binding;Quat original,parent;if(finger.source>=frame.size()||!Value(LocalRotation,b.transform,original)||!Value(Rotation,b.parent,parent))return false;
   if(changed)finger.from=b.lastRotation;
   const auto world=Mul(modelRotation,Retarget::ApplySourceDelta(frame[finger.source],b.restWorld,actor.sourceBasis));
   const auto local=Mul(Inverse(parent),world);const auto weight=std::clamp(controller.motionAge()/std::max(.01f,controller.transitionSeconds),0.f,1.f);
   b.previousRotation=original;b.lastRotation=Slerp(finger.from,local,weight*blend);if(!SetValue(SetLocalRotation,b.transform,b.lastRotation))return false;b.written=true;
  }actor.fingerMotion=controller.motionSerial;
 }
 if(stage){auto offset=Rotate(actor.sourceStageBasis,controller.output.hip);
  offset.x=config.root_motion?offset.x*actor.scale:0;offset.y=0;offset.z=config.root_motion?offset.z*actor.scale:0;
#ifdef MHE_MODULE_EXTENSION_HEADER
  moduleExtension.FilterDisplayMotion(offset);
#endif
  auto worldOffset=Rotate(modelRotation,offset);worldOffset.x*=blend;worldOffset.z*=blend;
  worldOffset=GroundDisplay(stageBaseline,worldOffset,blend);Vec3 baseline;
  {
   actor.rootPrevious=stageBaseline;actor.rootLast=Retarget::Add(stageBaseline,worldOffset);
   actor.rootWritten=SetValue(SetWorldPosition,actor.root,actor.rootLast);
  }
  auto camera=Call(MainCamera);if(camera!=actor.camera){ReleaseCameraBinding();
   auto transform=IsAlive(camera)?Call(Transform,camera):nullptr;
   if(transform&&IsAlive(transform)){
    actor.camera=camera;actor.cameraTransform=transform;
    actor.cameraPin=api->gchandle_new(api->context,camera,1);
    actor.cameraTransformPin=api->gchandle_new(api->context,transform,1);
    if(!actor.cameraPin||!actor.cameraTransformPin)ReleaseCameraBinding();
   }}
  if(actor.cameraTransform&&IsAlive(actor.cameraTransform)&&Value(WorldPosition,actor.cameraTransform,baseline)){
   actor.cameraPrevious=baseline;actor.cameraLast={baseline.x+worldOffset.x,
    baseline.y+(worldOffset.y+std::max(0.f,Rotate(actor.sourceStageBasis,controller.output.hip).y)*actor.scale)*.5f,baseline.z+worldOffset.z};
   actor.cameraWritten=SetValue(SetWorldPosition,actor.cameraTransform,actor.cameraLast);
  }
 }
 for(auto& root:actor.secondaryRoots){Mat anchor;if(!IsAlive(root.anchor)||!Value(WorldMatrix,root.anchor,anchor))return false;const auto desired=Multiply(anchor,root.anchorRelative);
  if(!WriteWeaponBinding(root.binding,desired.point(),desired.rotation()))return false;
 }
 ApplyWeaponSockets();
 if(visualWeapon.Available()){
  void* socket=nullptr;
  const auto& name=actor.visualMountName;
  for(const auto& entry:actor.sockets)if(ObjectName(entry.binding.transform)==name){socket=entry.binding.transform;break;}
  if(!socket){actor.visualReason="所需的原始武器安装节点未找到";return true;}
  visualWeapon.Tick(socket,actor.visualFrameSeconds);
  if(actor.visualSourceAdapter&&visualWeapon.Ready()&&actor.sourceWeaponFrame.main.valid){Mat hand;
   if(Value(WorldMatrix,actor.bones[12].transform,hand)){
    const auto axes=Mul(Mul(Inverse(actor.bones[12].restWorld),actor.sourceBasis),sourceRestRotations[12]);const auto q=Mul(hand.rotation(),axes);
    if(!visualWeapon.SetWorldPose(Retarget::Add(hand.point(),Rotate(q,Retarget::Scale(actor.sourceWeaponFrame.main.position,actor.scale))),Mul(q,actor.sourceWeaponFrame.main.rotation)))return false;
   }
  }
  VisualWeapon::ManagedVisualWeapon::PoseState state;state.charge=std::max(std::clamp(controller.heldSeconds()/1.85f,0.f,1.f),loadedMode=="bow"&&controller.slot!=configuredIdleSlot?.01f:0.f);state.elapsed=actor.entrySeconds;state.fullEffects=config.effects_quality=="full";
  state.released=projectileMotion==controller.motionSerial&&projectileEventCursor>0;
  Mat draw;if(loadedMode=="bow"&&Value(WorldMatrix,actor.drawFinger?actor.drawFinger:actor.bones[12].transform,draw)){state.drawPointWorld=draw.point();state.drawPointValid=true;}
  visualWeapon.UpdatePose(state);
 }
 if(sourceWeapon.Available()||offhandWeapon.Available()){
  void* main=nullptr;void* other=nullptr;for(const auto& entry:actor.sockets){const auto name=ObjectName(entry.binding.transform);if(name==actor.visualMountName)main=entry.binding.transform;if(name=="wep_L")other=entry.binding.transform;}
  if(main)sourceWeapon.Tick(main,actor.visualFrameSeconds);if(other&&actor.offhandRequested)offhandWeapon.Tick(other,actor.visualFrameSeconds);
  const auto transport=[&](VisualWeapon::ManagedSourceWeapon& weapon,const WeaponPose::Attachment& mount,int hand){
   if(!weapon.Ready()||!mount.valid)return;Mat target;if(!Value(WorldMatrix,actor.bones[hand].transform,target))return;
   // Source hand axes are mapped through authored rests. Geometry was already
   // normalized by its own root bind during export: do not invert it twice.
   const auto axes=Mul(Mul(Inverse(actor.bones[hand].restWorld),actor.sourceBasis),sourceRestRotations[hand]);
   const auto q=Mul(target.rotation(),axes);
   weapon.SetWorldPose(Retarget::Add(target.point(),Rotate(q,Retarget::Scale(mount.position,actor.scale))),Mul(q,mount.rotation));
  };
  transport(sourceWeapon,actor.sourceWeaponFrame.main,loadedMode=="bow"?8:12);
  if(actor.offhandRequested)transport(offhandWeapon,actor.sourceWeaponFrame.offhand,8);
  const auto own=skinMotions.find(controller.slot);
  if(sourceWeapon.Ready()&&own!=skinMotions.end()&&skinGeometry.contains(own->second.prop)){
   const auto& geometry=skinGeometry.at(own->second.prop);
   if(!sourceWeapon.UpdateSkinPositions(SourceSkin::Deform(geometry.vertices,geometry.indices,geometry.weights,own->second.track.Sample(controller.seconds))))return false;
  }else if(sourceWeapon.Ready()&&actor.sourceSkinMotion!=controller.motionSerial&&!modeProps.empty()){
   const auto id=modeProps.front().value("id",std::string{});if(skinGeometry.contains(id)&&!sourceWeapon.UpdateSkinPositions(skinGeometry.at(id).vertices))return false;
  }actor.sourceSkinMotion=controller.motionSerial;
 }
 UpdateEquipmentVisibility();return true;
}

using ObjectClassFn=void*(__cdecl*)(void*);
using FindFieldFn=void*(__cdecl*)(const void*,const char*);
ObjectClassFn practiceObjectClass=nullptr;FindFieldFn practiceFindField=nullptr;
void* ReadObjectField(void* object,const char* name){
 if(!object||!practiceObjectClass||!practiceFindField)return nullptr;
 auto type=practiceObjectClass(object);auto field=type?practiceFindField(type,name):nullptr;
 return field?api->field_get_value_object(api->context,field,object):nullptr;
}
bool QueryPracticeTargets(){
 if(!practiceReady)return false;
 auto container=api->field_get_value_object(api->context,worldContainer.field_info,nullptr);
 if(!container)return false;auto unordered=Call(AllEnemies,container);
 auto list=ReadObjectField(unordered,"m_items"),items=ReadObjectField(list,"_items"),size=ReadObjectField(list,"_size");
 auto raw=size?api->object_unbox(api->context,size):nullptr;if(!raw||!items)return false;
 int count=0;std::memcpy(&count,raw,sizeof(count));if(count<0||count>8192||count>Count(items))return false;
 Vec3 origin;if(!Value(WorldPosition,actor.root,origin))return false;
 std::vector<PracticeTarget> found;found.reserve(32);targetIdentities.Begin();
 for(int i=0;i<count;++i){auto entity=Item(items,i);bool valid=false,alive=false;uint32_t uid=0;Vec3 position;
  if(!entity||entity==actor.entity||!Value(Valid,entity,valid)||!valid||!Value(AliveEntity,entity,alive)||!alive||
   !Value(EntityUid,entity,uid)||!uid||!Value(EntityPosition,entity,position)||!Finite(position)||Retarget::Len(Retarget::Sub(position,origin))>config.target_range+10)continue;
  auto name=Text(Call(EntityName,entity));if(name.empty())name="附近怪物";
  const float distance=Retarget::Len(Retarget::Sub(position,origin));
  uint64_t serverId=0,battleId=0;uint32_t serverGeneration=0;
  Value(EntityServerId,entity,serverId);Value(EntityServerBattleId,entity,battleId);Value(EntityServerGeneration,entity,serverGeneration);
  PracticeTarget target{uid,std::move(name),position,config.practice_radius,config.practice_hp,distance};
  target.generation=targetIdentities.Observe(uid,reinterpret_cast<uintptr_t>(entity),serverId,serverGeneration);
  target.serverId=serverId;target.serverBattleInstId=battleId;target.serverGeneration=serverGeneration;
  auto modelCom=Call(ModelComponent,entity);auto model=modelCom?Call(ModelGo,modelCom):nullptr;
  if(model&&rendererClass.type_object&&methods[RendererBounds].r.method_info){
   bool include=false;void* args[]{rendererClass.type_object,&include};auto renderers=Call(GetComponents,model,args);
   for(int r=0;r<std::min(Count(renderers),16);++r){struct Bounds{Vec3 center,extents;} bounds{};bool visible=false;auto renderer=Item(renderers,r);
    if(!renderer||!Value(RendererEnabled,renderer,visible)||!visible||!Value(RendererBounds,renderer,bounds))continue;
    auto capsule=BoundsHurtCapsule(bounds.center,bounds.extents,uint32_t(r+1));
    if(!capsule||Retarget::Len(Retarget::Sub(bounds.center,position))>30)continue;target.hurt.push_back(*capsule);
   }
  }
  if(target.hurt.empty()){target.position.y+=config.practice_height;target.hurt.push_back({target.position,target.position,target.radius,0,0});}
  else target.position=Retarget::Scale(Retarget::Add(target.hurt.front().a,target.hurt.front().b),.5f);
  found.push_back(std::move(target));
 }
 std::sort(found.begin(),found.end(),[](const auto& a,const auto& b){return a.distance==b.distance?a.id<b.id:a.distance<b.distance;});
 if(found.size()>128)found.resize(128);
 targetIdentities.End();practice.Refresh(found,config.practice_hp);targetSnapshot=practice.Geometry();
#ifdef MHE_MODULE_EXTENSION_HEADER
 moduleExtension.RefreshTargets();
#endif
 return true;
}
bool SceneBlocked(Vec3 from,Vec3 to,float& fraction){
 const auto delta=Retarget::Sub(to,from);const float length=Retarget::Len(delta);
 if(length<1e-6f)return false;TerrainHit hit;
 if(!TerrainQuery(from,Retarget::Scale(delta,1/length),length,0,hit))return false;
 fraction=hit.distance/length;return true;
}
void RestoreTargetCamera(){
 if(!ownedCamera)return;Quat current;
 if(actor.cameraTransform&&Value(LocalRotation,actor.cameraTransform,current)&&Close(current,cameraLast))SetValue(SetLocalRotation,actor.cameraTransform,cameraPrevious);
 ownedCamera=false;
}
void TickTargetCamera(float dt){
 if(targetReleaseRequest){targetReleaseRequest=false;targetLock.Release();RestoreTargetCamera();lockRequest=focusRequest=cycleRequest=false;}
 if(!config.target_lock){targetLock.Release();RestoreTargetCamera();lockRequest=focusRequest=cycleRequest=false;return;}
 Mat root,camera;if(!actor.cameraTransform||!Value(WorldMatrix,actor.root,root)||!Value(WorldMatrix,actor.cameraTransform,camera)){targetLock.Release();RestoreTargetCamera();return;}
 TargetingView view{root.point(),camera.point(),Retarget::Sub(camera.point({0,0,1}),camera.point()),{0,1,0}};
 targetLock.settings.mode=config.target_camera=="target"?TargetCameraMode::TargetCamera:TargetCameraMode::FocusCamera;
 targetLock.settings.acquireRange=config.target_range;targetLock.settings.releaseRange=config.target_range+10;
 targetLock.Update(std::clamp(dt,0.f,.1f),targetSnapshot,view,SceneBlocked);
 if(lockRequest){if(targetLock.Selected())targetLock.Cycle(1,targetSnapshot,view,SceneBlocked);else targetLock.Acquire(targetSnapshot,view,SceneBlocked);}
 if(cycleRequest)targetLock.Cycle(1,targetSnapshot,view,SceneBlocked);
 const auto focus=targetLock.Camera(focusRequest||lockRequest);lockRequest=cycleRequest=focusRequest=false;
 if(!focus){if(!targetLock.Selected())RestoreTargetCamera();return;}
 const auto delta=Retarget::Sub(focus->point,camera.point());if(Retarget::Len(delta)<1e-5f)return;
 const auto forward=Retarget::Normalize(delta);auto right=Retarget::Cross({0,1,0},forward);
 if(Retarget::Len(right)<1e-5f)return;
 const auto desired=Retarget::BasisRotation(right,Retarget::Cross(forward,right));
 Quat parentRotation{};auto parent=Call(Parent,actor.cameraTransform);if(parent&&!Value(Rotation,parent,parentRotation))return;
 Quat current;if(!Value(LocalRotation,actor.cameraTransform,current))return;
 if(!ownedCamera||!Close(current,cameraLast))cameraPrevious=current;
 cameraLast=Slerp(current,Mul(Inverse(parentRotation),desired),focus->persistent?std::clamp(dt*12.f,0.f,1.f):1.f);
 if(SetValue(SetLocalRotation,actor.cameraTransform,cameraLast))ownedCamera=true;
 targetingDetail="镜头已选择目标；近战方向仍由角色朝向和源动作决定";
}
void TickPractice(float dt){
 if(practiceResetRequest){practiceResetRequest=false;practiceDummy.Reset();practice.Clear();delayedHits.Clear();delayedContact.Suspend();localMode.Reset(loadedMode);practiceSummary=Json::array();enemyRefresh=0;pumpedMotionSerial=0;}
 if(!config.local_practice){practiceDummy.Reset();practice.Suspend();
  enemyRefresh-=std::clamp(dt,0.f,.1f);if(config.target_lock&&enemyRefresh<=0){enemyRefresh=.15f;if(!QueryPracticeTargets())targetSnapshot.clear();}
  if(!config.target_lock)targetSnapshot.clear();practice.targets.clear();practiceDetail="本地练习未开启";return;}
 if(config.practice_target=="dummy"){
  if(practice.targets.empty()){Mat root;if(!Value(WorldMatrix,actor.root,root)){practice.Suspend();return;}
   const Vec3 point=root.point({0,config.practice_height,3});PracticeTarget dummy{0xfffffffe,"本地练习目标（整球模拟弱点）",point,config.practice_radius,config.practice_hp,3};
   dummy.approximateGeometry=false;dummy.hurt.push_back({point,point,config.practice_radius,0,0,true,true});practice.Refresh({dummy},config.practice_hp);}
  targetSnapshot=practice.Geometry();
  if(!practiceDummy.Available()){
   Json prop{{"id","local_practice_dummy"},{"vertices",Json::array()},{"indices",Json::array()}};
   constexpr int rings=12,sides=20;constexpr float pi=3.14159265359f;
   for(int ring=0;ring<=rings;++ring){const float a=pi*ring/rings;for(int side=0;side<sides;++side){const float b=2*pi*side/sides;prop["vertices"].push_back({config.practice_radius*std::sin(a)*std::cos(b),config.practice_radius*std::cos(a),config.practice_radius*std::sin(a)*std::sin(b)});}}
   for(int ring=0;ring<rings;++ring)for(int side=0;side<sides;++side){const int a=ring*sides+side,b=ring*sides+(side+1)%sides,c=a+sides,d=b+sides;for(auto index:{a,c,b,b,c,d})prop["indices"].push_back(index);}
   practiceDummy.Bind(api,prop);
  }
  if(practiceDummy.Tick(actor.root,dt)&&!practice.targets.empty()){practiceDummy.SetWorldPose(practice.targets.begin()->second.position,Quat{});practiceDummy.SetVisible(true);}
 }else{
 practiceDummy.Reset();
 if(!practiceReady){practice.Suspend();practice.targets.clear();targetSnapshot.clear();practiceDetail="怪物读取接口未匹配；动作可继续播放";return;}
 if(!QueryPracticeTargets()){practice.Suspend();practice.targets.clear();targetSnapshot.clear();practiceDetail="等待场景怪物数据";return;}}
 if(presentationPreview||autoDemo){practice.Suspend();practiceDetail="自动演示或动作预览；练习血量保持原状态";return;}
 if(loadedMode=="bow"||loadedMode=="light_bowgun"){
  practiceDetail="本地箭弹练习；不修改真实怪物";return;
 }
 Vec3 grip;Quat rotation;
 if(!Value(WorldPosition,actor.bones[12].transform,grip)||!Value(Rotation,actor.bones[12].transform,rotation)){practice.Suspend();return;}
 const auto bladeAxis=actor.bladeCalibrated?actor.bladeHandAxis:practiceBladeAxis;
 if(actor.bladeCalibrated&&!actor.sockets.empty()){
  Mat hand;if(Value(WorldMatrix,actor.bones[12].transform,hand))grip=hand.point(actor.sockets.front().handPosition);}
 auto tip=Retarget::Add(grip,Rotate(rotation,Retarget::Scale(bladeAxis,config.practice_blade_length*actor.scale)));
 if(visualWeapon.Visible()){
  Mat weaponWorld;if(!Value(WorldMatrix,visualWeapon.RootTransform(),weaponWorld)){practice.Suspend();return;}
  grip=weaponWorld.point();tip=weaponWorld.point(Retarget::Scale(visualWeapon.BladeAxis(),visualWeapon.Length()));
 }else if(sourceWeapon.Visible()&&!modeProps.empty()&&modeProps.front().contains("source_attack_tip")){
  Mat source;if(Value(WorldMatrix,sourceWeapon.RootTransform(),source)){
   const auto p=modeProps.front().at("source_attack_tip").get<std::vector<float>>();if(p.size()==3){grip=source.point();tip=source.point({p[0],p[1],p[2]});}
  }
 }
 std::vector<WeaponCapsule> weapons{{grip,tip,.04f*actor.scale,0}};
 if(offhandWeapon.Visible()){Mat shield;if(Value(WorldMatrix,offhandWeapon.RootTransform(),shield))weapons.push_back({shield.point(),shield.point({0,0,.15f}),.25f*actor.scale,1});}
 Vec3 hip,chest;if(Value(WorldPosition,actor.bones[0].transform,hip)&&Value(WorldPosition,actor.bones[2].transform,chest))weapons.push_back({hip,chest,.2f*actor.scale,2});
 Vec3 origin;if(!Value(WorldPosition,actor.bones[0].transform,origin)){practice.Suspend();return;}
 const float teachingDamage=config.practice_damage;
 const auto sourceSerial=controller.motionSerial;const int sourceSlot=controller.slot;const float sourceSeconds=controller.seconds;
 const float sourceMultiplier=localMode.DamageMultiplier(controller);const int sourceLevel=localMode.HelmOriginalLevel();
 practice.UpdateCombat(sourceSerial,sourceSlot,sourceSeconds,weapons,origin,8.f*actor.scale,teachingDamage,SceneBlocked);
 if(controller.motionSerial!=sourceSerial){delayedContact.Suspend();return;}
 if(auto windows=delayedContactWindows.find(sourceSlot);windows!=delayedContactWindows.end()){
  const auto geometry=practice.Geometry();const auto contacts=delayedContact.Update(sourceSerial,sourceSlot,sourceSeconds,weapons,windows->second,geometry,origin,8.f*actor.scale,SceneBlocked);
  for(const auto& contact:contacts){const auto found=practice.targets.find(uint32_t(contact.key.id));if(found==practice.targets.end())continue;const auto& t=found->second;
   const uint32_t id=uint32_t(contact.strike-1);auto rules=delayedRules.find(sourceSlot);if(rules==delayedRules.end())continue;
   auto rule=std::find_if(rules->second.begin(),rules->second.end(),[&](const auto& r){return r.triggerWindow==id;});if(rule==rules->second.end())continue;
   auto target=std::find_if(geometry.begin(),geometry.end(),[&](const auto& g){return g.key==contact.key;});if(target==geometry.end())continue;
   PracticeHit seed{t.id,t.generation,sourceSerial,sourceSlot,contact.seconds,id,contact.part,teachingDamage*sourceMultiplier,contact.point,t.serverId,t.serverBattleInstId,t.serverGeneration};
   delayedHits.Schedule(seed,*rule,*target,sourceLevel,std::clamp(sourceSeconds-contact.seconds,0.f,.25f),0);
  }
 }else delayedContact.Suspend();
 practiceDetail=practice.targets.empty()?"20 米内暂无可用怪物":config.practice_target=="dummy"?"本地练习目标；独立模拟血量":"本地模拟中；不修改怪物真实血量";
}
void TickProjectiles(float dt){
 if(presentationPreview)return;
 if(loadedMode=="bow"||loadedMode=="light_bowgun"){
  if(projectileMotion!=controller.motionSerial){projectileMotion=controller.motionSerial;projectileEventCursor=0;activeProjectileRelease={};}
  auto events=projectileEvents.find(controller.slot);
  if(events!=projectileEvents.end())while(projectileEventCursor<events->second.size()&&events->second[projectileEventCursor]<=controller.seconds+1e-6f){
   const float emittedAt=events->second[projectileEventCursor];const auto window=uint32_t(projectileEventCursor);++projectileEventCursor;
   Mat hand;if(!Value(WorldMatrix,actor.bones[loadedMode=="bow"?8:12].transform,hand))continue;
   Vec3 origin=hand.point(),forward;
   Mat camera;if(actor.cameraTransform&&Value(WorldMatrix,actor.cameraTransform,camera))forward=Retarget::Sub(camera.point({0,0,50}),origin);
   else{Mat root;if(!Value(WorldMatrix,actor.root,root))continue;forward=Retarget::Sub(root.point({0,0,1}),root.point());}
   ProjectileTier parameters{1,0,100,config.projectile_speed,config.projectile_speed*3,.02f};
   if(localMode.combatEnabled){
    if(!activeProjectileRelease.accepted){
     const auto peek=localMode.PeekProjectile(controller);if(!peek.accepted)continue;
     if(!localProjectiles.CanLaunch(size_t(peek.parameters.arrows)*events->second.size()))continue;
     activeProjectileRelease=localMode.ReleaseProjectile(controller);if(!activeProjectileRelease.accepted)continue;
    }
    if(!localProjectiles.CanLaunch(size_t(activeProjectileRelease.parameters.arrows)))continue;
    parameters=activeProjectileRelease.parameters;
    // Light bowgun shell travel is still a teaching parameter when no source
    // projectile tier exists; source bow tiers always supply physical values.
    if(parameters.speed<=0)parameters={1,0,100,config.projectile_speed,config.projectile_speed*3,.02f};
   }else if(!localMode.ReleaseProjectile())continue;
   for(int arrow=0;arrow<parameters.arrows;++arrow){
   if(localProjectiles.Launch(origin,forward,parameters.speed,config.local_practice&&!autoDemo?config.practice_damage*parameters.motionValue/100.f:0,loadedMode=="bow",controller.motionSerial,controller.slot,window,emittedAt,std::clamp(controller.seconds-emittedAt,0.f,dt),parameters.radius,parameters.range)&&projectileProp.is_object()&&!projectileProp.empty()){
    auto free=std::find_if(projectileVisuals.begin(),projectileVisuals.end(),[](const auto& v){return v.id==0;});
    if(free!=projectileVisuals.end()){
     if(!free->mesh){free->mesh=std::make_unique<VisualWeapon::ManagedSourceWeapon>();free->mesh->Bind(api,projectileProp,activeAssetRoot);}
     if(free->mesh->Tick(actor.root,0)){free->id=localProjectiles.active.back().id;free->mesh->SetVisible(true);}
    }
   }
   }
  }
  localProjectiles.Update(std::clamp(dt,0.f,.1f),practice,[](Vec3 from,Vec3 to,float& fraction){
   const auto delta=Retarget::Sub(to,from);const auto length=Retarget::Len(delta);if(length<1e-6f)return false;TerrainHit hit;
   if(!TerrainQuery(from,Retarget::Scale(delta,1/length),length,.02f,hit))return false;fraction=hit.distance/length;return true;
  },[](const LocalProjectile&,uint32_t){});
  for(auto& visual:projectileVisuals)if(visual.id&&visual.mesh){auto current=std::find_if(localProjectiles.active.begin(),localProjectiles.active.end(),[&](const auto& p){return p.id==visual.id;});
   if(current==localProjectiles.active.end()){visual.mesh->SetVisible(false);visual.id=0;}
   else if(projectileProp.contains("source_forward_axis")){
    const auto a=projectileProp.at("source_forward_axis").get<std::vector<float>>();
    if(a.size()==3&&Retarget::Len({a[0],a[1],a[2]})>.001f)visual.mesh->SetWorldPose(current->position,Retarget::FromTo({a[0],a[1],a[2]},Retarget::Normalize(current->velocity)));
   }
  }
 }
}
std::string RoundedAmount(double value){char buffer[64]{};std::snprintf(buffer,sizeof(buffer),"%.0f",value);return buffer;}

uint32_t previousBits=0;bool previousToggle=false,previousEscape=false,previousDemo=false,resyncFocusKeys=false;int lastFrame=-1;
void ResetKeys() {previousBits=0;previousToggle=false;previousEscape=false;previousDemo=false;resyncFocusKeys=false;}
bool ActorIdentityIsCurrent(void* current) {
 bool alive=false;
 if(current!=actor.entity||!IsAlive(actor.model)||!Value(AliveEntity,current,alive)||!alive)return false;
 const auto component=Call(ModelComponent,current);
 return component&&Call(ModelGo,component)==actor.model;
}
void EmitSound(const AudioCue& cue){
 if(cue.kind=="stop"){soundPlayer.StopGroup(cue.group,cue.fadeOut>=0?cue.fadeOut:cue.fade);return;}
 const auto clip=soundClips.find(cue.clip);if(clip==soundClips.end())return;
 const auto category=cue.group.starts_with("theme.laevat")?"fire":cue.group.starts_with("theme.typh.charge")?"charge":(cue.condition.empty()||cue.condition=="always")?(cue.loop?"charge":"weapon"):"impact";
 const float layer=config.volumes.contains(cue.group)?config.volumes.at(cue.group):config.volumes.at(category);
 float pan=0,attenuation=1;Mat camera;Vec3 emitter;
 if(actor.cameraTransform&&Value(WorldMatrix,actor.cameraTransform,camera)&&Value(WorldPosition,actor.root,emitter)){
  const auto relative=camera.inverse().point(emitter);const float distance=Retarget::Len(relative);
  pan=std::clamp(relative.x/std::max(1.f,distance),-.8f,.8f);attenuation=1/(1+std::max(0.f,distance-8)*.1f);
 }
 if(soundPlayer.Play(clip->second,config.sound_volume*cue.gain*layer*attenuation,cue.group,cue.loop,cue.priority,cue.fadeIn>=0?cue.fadeIn:cue.fade,pan)){
  lastSound=cue.clip;++soundCueCount;soundDetail="动作音效就绪";
 }else{if(soundDetail!="音效播放失败；请检查音频设备")Log("Audio output unavailable; animation remains active");soundDetail="音效播放失败；请检查音频设备";}
}
void UpdateHud(){
 HudSnapshot snapshot;snapshot.active=controller.active;snapshot.autoDemo=autoDemo;snapshot.paused=paused||hudEditing;
 snapshot.modeLabel=modeDefinitions.contains(loadedMode)?modeDefinitions.at(loadedMode).value("label",loadedMode):loadedMode;
 snapshot.motionLabel=motionLabels.contains(controller.slot)?motionLabels.at(controller.slot):std::string("待机");
 snapshot.detail=detail;snapshot.feedback=practice.lastTarget.empty()?std::string{}:"模拟命中 "+practice.lastTarget;
 snapshot.chapter=autoDemo?std::to_string(demoStep+1)+" / "+std::to_string(demoSequence.size()):std::string{};
 snapshot.resourceValid=true;snapshot.resourceLabel=localMode.Label();snapshot.resource=localMode.Value();snapshot.resourceMax=localMode.Maximum();
 if(!localMode.feedback.empty())snapshot.feedback=localMode.feedback;
 snapshot.aiming=(loadedMode=="bow"||loadedMode=="light_bowgun")&&((GetAsyncKeyState(config.keys.at("aim"))&0x8000)!=0||controller.holding(Command::Charge));
 snapshot.charge=controller.heldSeconds();snapshot.chargeMax=3;snapshot.hits=practice.hits;snapshot.totalDamage=practice.total;snapshot.lastDamage=practice.lastDamage;
 const PracticeTarget* nearest=nullptr;const auto selected=targetLock.Selected();
 for(const auto&[id,target]:practice.targets){if(selected&&selected->id==id&&selected->generation==target.generation){nearest=&target;break;}if(!nearest||target.distance<nearest->distance)nearest=&target;}
 if(nearest){snapshot.targetValid=true;snapshot.targetName=nearest->name;snapshot.targetHp=nearest->hp;snapshot.targetMaxHp=nearest->maximumHP;}
 for(const auto&[bit,command]:controller.inputBindings){auto move=controller.moves.find(command);if(move==controller.moves.end()||move->second.empty())continue;
  const auto slot=move->second.front();const char* key=bit==1?"attack":bit==2?"secondary":bit==4?"spirit":bit==8?"dodge":bit==32?"aim":bit==64?"reload":"sheathe";
  const int vk=config.keys.at(key);std::string label=vk==1?"左键":vk==2?"右键":vk==6?"侧键":vk==32?"空格":vk>=48&&vk<=90?std::string(1,char(vk)):std::to_string(vk);
  snapshot.derivations.push_back({motionLabels.contains(slot)?motionLabels.at(slot):CommandName(command),label,controller.available(command),controller.buffered(command)});
 }
 HudSettings settings;settings.enabled=config.hud_enabled;settings.showDerivations=config.hud_show_derivations;settings.showTarget=config.hud_show_target;
 settings.showCrosshair=config.hud_show_crosshair;settings.editing=hudEditing;settings.scale=config.hud_scale;settings.anchorX=config.hud_anchor_x;settings.anchorY=config.hud_anchor_y;
 settings.margin=int(config.hud_margin);settings.layout=config.hud_layout;settings.theme=config.theme=="auto"?(loadedMode=="bow"?"tide":actor.character=="chr_0016_laevat"?"ember":"forest"):config.theme;
 practiceHud.Update(snapshot,settings);float x=0,y=0;if(practiceHud.TakeLayoutEdit(x,y)){config.hud_anchor_x=x;config.hud_anchor_y=y;hudLayoutChanged=true;++configRevision;appliedConfigRevision=configRevision;}
}
const std::vector<Command> defaultDemoSequence{Command::Slash,Command::Slash,Command::Slash,Command::Thrust,Command::Slash,
 Command::Spirit,Command::Spirit,Command::Spirit,Command::Spirit,Command::Spirit,
 Command::Fade,Command::Slash,Command::Foresight,
 Command::Spirit,Command::Spirit,Command::Spirit,Command::Spirit,Command::Spirit,
 Command::Sheathe,Command::Iai,Command::Slash,Command::Sheathe,Command::IaiSpirit,
 Command::Helm,Command::Sheathe,Command::NormalSheathe};
std::vector<Command> demoSequence=defaultDemoSequence;
std::vector<Command> fullDemoSequence=defaultDemoSequence;
void SelectDemoSequence(){
 demoSequence.clear();const auto append=[&](Command command,int count=1){if(controller.moves.contains(command))for(int i=0;i<count;++i)demoSequence.push_back(command);};
 if(config.demo_sequence=="full"){demoSequence=fullDemoSequence;return;}
 if(config.demo_sequence=="basic"){
  if(loadedMode=="bow"||loadedMode=="light_bowgun"){append(Command::Aim);append(Command::Fire,3);append(Command::Reload);}
  else{append(Command::Slash,int(controller.moves[Command::Slash].size()));append(Command::Thrust,int(controller.moves[Command::Thrust].size()));}
  append(Command::Dodge,int(controller.moves[Command::Dodge].size()));append(Command::NormalSheathe);
 }else{
  if(loadedMode=="longsword"){append(Command::Spirit,4);append(Command::Sheathe);append(Command::IaiSpirit);append(Command::Helm);}
  else{if(controller.moves.contains(Command::Charge)){append(Command::Charge);append(Command::Release);}append(Command::Guard);append(Command::Counter);append(Command::Special,int(controller.moves[Command::Special].size()));}
  append(Command::NormalSheathe);
 }if(demoSequence.empty())demoSequence=fullDemoSequence;
}
AudioCue ThemeCue(const Json& row){
 AudioCue cue{row.value("time",0.f),row.value("clip",std::string{}),row.value("gain",1.f)};
 cue.kind=row.value("kind",std::string("oneshot"));cue.group=row.value("group",std::string("weapon"));cue.loop=row.value("loop",false);cue.priority=row.value("priority",35);
 cue.fadeIn=row.value("fade_in",0.f);cue.fadeOut=row.value("fade_out",.04f);cue.variants=row.value("variants",std::vector<std::string>{});
 if(!std::isfinite(cue.time)||cue.time<0||cue.time>30||!std::isfinite(cue.gain)||cue.gain<0||cue.gain>2||cue.priority<0||cue.priority>100||cue.group.size()>128||cue.variants.size()>64)throw std::runtime_error("Invalid theme cue");
 if(cue.kind!="oneshot"&&cue.kind!="loop"&&cue.kind!="stop")throw std::runtime_error("Invalid theme lifecycle");
 for(float fade:{cue.fadeIn,cue.fadeOut})if(!std::isfinite(fade)||fade<0||fade>2)throw std::runtime_error("Invalid theme release");
 if(cue.kind!="stop"&&!soundClips.contains(cue.clip))throw std::runtime_error("Missing theme PCM");for(const auto& id:cue.variants)if(!soundClips.contains(id))throw std::runtime_error("Missing theme alternative");return cue;
}
void ThemePhase(const std::string& theme,const char* phase,int slot){
 if(!themeDefinitions.contains(theme)||!themeDefinitions[theme].contains(phase))return;
 themeTimeline.cues[slot].clear();for(const auto& row:themeDefinitions[theme][phase])themeTimeline.cues[slot].push_back(ThemeCue(row));
 themeClock=0;themeTimeline.Update(++themeSerial,slot,0,config.sound_enabled&&config.sound_volume>0&&soundReady,EmitSound);
}
void TickThemeSounds(float dt){
 const std::string selected=visualWeapon.Visible()?(actor.visualSourceAdapter?"laevatain_blade":loadedMode=="bow"?"typhoea_bow":""):"";
 if(selected!=activeTheme){
  if(!activeTheme.empty())ThemePhase(activeTheme,activeTheme=="laevatain_blade"?"stop":"cancel",1);
  activeTheme=selected;themeBowCharging=false;themeBowMotion=0;
  if(activeTheme=="laevatain_blade")ThemePhase(activeTheme,"start",0);
 }
 if(activeTheme=="typhoea_bow"){
  const bool charging=localMode.In(controller,Command::Charge)||controller.holding(Command::Charge);
  if(charging&&!themeBowCharging)ThemePhase(activeTheme,"charge",2);
  if(projectileMotion==controller.motionSerial&&projectileEventCursor>0&&themeBowMotion!=controller.motionSerial){themeBowMotion=controller.motionSerial;ThemePhase(activeTheme,"release",3);}
  else if(themeBowCharging&&!charging&&!localMode.In(controller,Command::Release)&&!localMode.In(controller,Command::Fire))ThemePhase(activeTheme,"cancel",4);
  themeBowCharging=charging;
 }
 themeClock+=dt;
 // No body-slot key is used here: a fire pad survives every ordinary slash.
}
float DemoDelay(Command command,const Clip& clip,size_t nextStep) {
 if(command==Command::Sheathe)return std::min(clip.duration(),float(controller.sheatheHoldFrame)/clip.fps)+.8f;
 if(command==Command::Helm||nextStep==0)return clip.duration()+.7f;
 // Short clips must chain before the controller returns to idle. Otherwise
 // repeated Spirit inputs keep restarting the first segment and its sound.
 return std::min(clip.duration()*.90f+.18f,std::max(.01f,clip.duration()-.04f));
}
void TickDemo(float dt) {
 if(!autoDemo||demoSequence.empty())return;demoClock+=dt;demoElapsed+=dt;if(demoClock<demoDelay)return;
 if(demoStep==0&&demoClock>demoDelay&&controller.resting()&&!demoReturnStarted){
  controller.returnToOrigin(.5f);demoReturnStarted=true;demoDelay+=.55f;return;}
 if(demoFinalTail){if(!config.demo_loop){autoDemo=false;demoFinalTail=false;detail="单次演示完成；F9 可再次播放";return;}demoElapsed=0;demoFinalTail=false;}
 demoReturnStarted=false;
 const auto command=demoSequence[demoStep];bool released=false;
 if(demoHeld){released=command==Command::Release;controller.held(demoHeldCommand,false);demoHeld=false;
  // A source counter release is a real clip. Let it reach its transition
  // window before scheduling the next showcase input, or its input buffer
  // can expire before the held charge action actually starts.
  if(!released&&!controller.resting()){demoClock=0;demoDelay=DemoDelay(Command::Release,controller.clips.at(controller.slot),demoStep);return;}
 }
 if(controller.holdPolicies.contains(command)){controller.held(command,true);demoHeld=true;demoHeldCommand=command;}
 else if(!released)controller.input(command);
 demoStep=(demoStep+1)%std::size(demoSequence);demoClock=0;
 if(demoStep==0)demoFinalTail=true;
 const auto& clip=controller.clips.at(controller.slot);
 demoDelay=DemoDelay(command,clip,demoStep);
 if(demoHeld){const auto& rule=controller.HoldRule(command);demoDelay=std::max(.6f,float(std::max(0,rule.frame))/clip.fps+.2f);
  if(command==Command::Charge&&!rule.releases.empty()){
   const size_t stages=controller.holdVariants.contains(command)?controller.holdVariants.at(command).size()+1:1;
   const auto level=(demoChargeLevel++/stages)%rule.releases.size();
   // A hold loop is already a real held pose. Releasing a low charge level
   // must not be delayed until its whole breathing loop has finished.
   demoDelay=std::max(.25f,rule.releases[level].seconds+.12f);
  }
 }
}
bool ActiveInputAction(const std::string& name){
 const bool ranged=loadedMode=="bow"||loadedMode=="light_bowgun";
 return name=="attack"||name=="secondary"||name=="dodge"||name=="sheathe"||
  (ranged?(name=="aim"||name=="reload"):name=="spirit");
}
bool InputAliasAvailable(int key,const std::string& owner){
 for(const auto&[name,value]:config.keys)if(name!=owner&&ActiveInputAction(name)&&value==key)return false;return true;
}
bool KeyClaimsCameraControl(int key){
 for(const auto&[name,value]:config.keys)if(ActiveInputAction(name)&&value==key)return true;
 if(key==VK_SHIFT&&InputAliasAvailable(key,"sheathe"))return true;
 return loadedMode=="longsword"&&key==config.spirit_alt_key&&InputAliasAvailable(key,"spirit");
}
template<class IsDown>bool CameraControlDown(int key,IsDown down){return !KeyClaimsCameraControl(key)&&down(key);}
template<class IsDown>uint32_t KeyBitsFrom(IsDown down) {
 uint32_t bits=0;
 const auto aliasAvailable=[](int key,const char* action){return InputAliasAvailable(key,action);};
 const bool ranged=loadedMode=="bow"||loadedMode=="light_bowgun";
 if(down(config.keys.at("attack")))bits|=1;if(down(config.keys.at("secondary"))&&(!ranged||config.keys.at("secondary")!=config.keys.at("aim")))bits|=2;
 if(!ranged&&(down(config.keys.at("spirit"))||(loadedMode=="longsword"&&aliasAvailable(config.spirit_alt_key,"spirit")&&down(config.spirit_alt_key))))bits|=4;
 if(down(config.keys.at("dodge")))bits|=8;
 if(down(config.keys.at("sheathe"))&&!(ranged&&config.keys.at("sheathe")==config.keys.at("reload"))||(aliasAvailable(VK_SHIFT,"sheathe")&&down(VK_SHIFT)))bits|=16;
 if(ranged&&controller.moves.contains(Command::Aim)&&down(config.keys.at("aim")))bits|=AimBit;
 if(ranged&&controller.moves.contains(Command::Reload)&&down(config.keys.at("reload")))bits|=ReloadBit;
 return bits;
}
uint32_t KeyBits(){return KeyBitsFrom([](int k){return k>0&&(GetAsyncKeyState(k)&0x8000)!=0;});}
void ApplyCombatOutcome(const CombatEvent& event,const CombatOutcome& outcome){
 if(!outcome.accepted)return;
 const bool current=controller.active&&event.motionSerial==controller.motionSerial&&event.slot==controller.slot;
 const bool audible=current&&config.sound_enabled&&config.sound_volume>0&&soundReady;
 if(outcome.hit)soundTimeline.Condition("hit",audible,EmitSound);
 if(!outcome.cue.empty())soundTimeline.Condition(outcome.cue,audible,EmitSound);
 if(!current||!localMode.combatEnabled||!outcome.followupSlot)return;
 const auto command=outcome.transition==CombatTransition::Counter?Command::Counter:
  outcome.transition==CombatTransition::PerfectRush?Command::Special:
  outcome.transition==CombatTransition::WeakFinish?Command::Charge:
  outcome.transition==CombatTransition::Helm?Command::Helm:Command::Release;
 if(controller.seconds+1e-6f>=outcome.startSeconds){
  if(controller.transitionToSlot(outcome.followupSlot,outcome.startSeconds,command))controller.tick(0);
 }else{combatFollowupSlot=outcome.followupSlot;combatFollowupSeconds=outcome.startSeconds;
  combatFollowupMotion=controller.motionSerial;combatFollowupCommand=command;}
}
CombatEvent EventFromContact(const PracticeHit& hit){
 CombatEvent event;event.motionSerial=hit.motionSerial;event.slot=hit.slot;event.seconds=hit.seconds;event.damage=hit.damage;
 event.targetId=hit.targetId;event.generation=hit.generation;event.windowId=hit.windowId;event.partId=hit.partId;
 event.weakPointKnown=hit.weakPointKnown;event.weakPoint=hit.weakPoint;event.contactSerial=hit.contactSerial;return event;
}
void InstallPracticeCallbacks(){
 practice.contactDamage=[](const PracticeHit& hit){
  if(!localMode.combatEnabled)return hit.damage;
  if(hit.motionSerial!=controller.motionSerial||hit.slot!=controller.slot)return 0.f;
  const auto* rule=localMode.rules.Find(hit.slot);if(!rule)return 0.f;
  return hit.damage*rule->MotionValue(hit.windowId,localMode.DamageLevel(controller))/100.f*localMode.ContactMultiplier(controller,EventFromContact(hit));
 };
 practice.acceptedHit=[](const PracticeHit& hit){
  if(localMode.combatEnabled)ApplyCombatOutcome(EventFromContact(hit),localMode.Consume(EventFromContact(hit),controller));
  else{localMode.Hit(controller);soundTimeline.Condition("hit",config.sound_enabled&&config.sound_volume>0&&soundReady,EmitSound);}
#ifdef MHE_MODULE_EXTENSION_HEADER
  moduleExtension.OnContact(hit);
#endif
 };
}
#ifdef MHE_MODULE_EXTENSION_IMPLEMENTATION
#include MHE_MODULE_EXTENSION_IMPLEMENTATION
#endif
void ResetTrainingTransient(){
 practice.Suspend();delayedHits.Clear();delayedContact.Suspend();localProjectiles.Clear();
 for(auto& visual:projectileVisuals){if(visual.mesh)visual.mesh->SetVisible(false);visual.id=0;}
 projectileMotion=std::numeric_limits<uint64_t>::max();projectileEventCursor=0;
 combatFollowupSlot=0;combatFollowupMotion=0;combatFollowupSeconds=0;pumpedMotionSerial=0;practiceAttackRequest=false;
 localMode.SuspendCombatTracking();
}
bool BeginPresentationPreview(int slot){
 if(!controller.clips.contains(slot)||controller.clips.at(slot).frames.empty())return false;
 RestoreStageMotion();RestoreOwned();ResetTrainingTransient();autoDemo=false;demoHeld=false;
 presentationPreview=true;localMode.combatEnabled=false;controller.specialSheatheSeconds=0;
 detail="动作预览中；攻击键将从待机开始手动练习";
 return controller.start(slot);
}
bool TakeManualControl(){
 if(presentationPreview||autoDemo){const bool wasDemo=autoDemo;RestoreStageMotion();RestoreOwned();ResetTrainingTransient();presentationPreview=false;
  if(wasDemo)localMode.Reset(loadedMode);
  if(!controller.start(configuredIdleSlot))return false;}
 autoDemo=false;localMode.combatEnabled=ManualCombatActive();return true;
}
void PumpTrainingStep(float dt) {
 practice.BeginFrame();
 const auto previousMisses=practice.misses;
 if(!presentationPreview)localMode.Tick(std::clamp(dt,0.f,.1f),controller);
 if(localMode.ChargeExhausted(controller)){controller.start(configuredIdleSlot);localMode.SuspendCombatTracking();}
 soundTimeline.Update(controller.motionSerial,controller.slot,controller.seconds,
  config.sound_enabled&&config.sound_volume>0&&soundReady,EmitSound);
 try{TickPractice(dt);}catch(const std::exception& e){practice.Suspend();practice.targets.clear();practiceDetail=std::string("怪物读取暂不可用：")+e.what();}
 TickTargetCamera(dt);
 TickProjectiles(dt);
 if(localMode.combatEnabled){
  const auto pulses=delayedHits.Update(std::clamp(dt,0.f,.25f),practice.Geometry(),SceneBlocked);
  for(const auto& pulse:pulses){CombatContact contact;contact.key={pulse.hit.targetId,pulse.hit.generation};contact.part=pulse.hit.partId;contact.strike=uint64_t(pulse.hit.windowId)+1;
   contact.seconds=pulse.hit.seconds;contact.point=pulse.hit.point;
   if(practice.ApplyContact(contact,pulse.hit.motionSerial,pulse.hit.slot,pulse.hit.damage))localMode.CreditDelayedHelmPulse(pulse.hit.slot,pulse.hit.windowId,pulse.originalLevel);
  }
 }else delayedHits.Clear();
 const bool audible=config.sound_enabled&&config.sound_volume>0&&soundReady;
 bool dummyNearby=false;for(const auto&[id,target]:practice.targets)if(target.hp>0&&target.distance<=6)dummyNearby=true;
 if(!presentationPreview&&config.local_practice&&config.practice_target=="dummy"){
  CombatEvent event;const auto outcome=localMode.DummyAttackOutcome(std::clamp(dt,0.f,.1f),controller,dummyNearby,event);ApplyCombatOutcome(event,outcome);
 }
 if(practiceAttackRequest){practiceAttackRequest=false;
  if(localMode.combatEnabled&&config.practice_target=="dummy"&&dummyNearby){
   CombatEvent event;event.kind=CombatEventKind::IncomingAttack;event.motionSerial=controller.motionSerial;event.slot=controller.slot;event.seconds=controller.seconds;
   event.targetId=0xfffffffe;event.attackSerial=++practiceAttackSerial;event.fromFront=practiceAttackFromFront;event.guardable=practiceAttackGuardable;
   const auto outcome=localMode.Consume(event,controller);ApplyCombatOutcome(event,outcome);
  }
 }
 if(autoDemo){soundTimeline.Condition("counter",audible,EmitSound);soundTimeline.Condition("guard",audible,EmitSound);}
 if(localMode.combatEnabled&&practice.misses>previousMisses&&practice.events.empty())localMode.feedback="本次挥击未命中";
 TickThemeSounds(std::clamp(dt,0.f,.1f));
}
void ApplyPendingCombatFollowup(){
 if(!combatFollowupSlot)return;
 if(!localMode.combatEnabled||controller.motionSerial!=combatFollowupMotion)combatFollowupSlot=0;
 else if(controller.seconds+1e-6f>=combatFollowupSeconds){controller.transitionToSlot(combatFollowupSlot,combatFollowupSeconds,combatFollowupCommand);controller.tick(0);combatFollowupSlot=0;}
}
bool AdvanceShowcaseFrame(float dt){
 const float step=std::clamp(dt,0.f,.1f)*config.speed;
 if(localMode.combatEnabled){
  float remaining=step;
  do {
   if(pumpedMotionSerial!=controller.motionSerial){
    controller.tick(0);actor.visualFrameSeconds=0;RestoreStageMotion();
#ifdef MHE_MODULE_EXTENSION_HEADER
    moduleExtension.Frame(0);
#endif
    if(!ApplyPose())return false;const auto source=controller.motionSerial;PumpTrainingStep(0);pumpedMotionSerial=source;
   }
   if(remaining<=1e-7f)break;
   const float sub=std::min(remaining,1.f/60);controller.tick(sub);ApplyPendingCombatFollowup();
   actor.entrySeconds+=sub;actor.visualFrameSeconds=sub/std::max(.25f,config.speed);RestoreStageMotion();
#ifdef MHE_MODULE_EXTENSION_HEADER
   moduleExtension.Frame(sub/std::max(.25f,config.speed));
#endif
   if(!controller.active||!ApplyPose())return false;
   const auto source=controller.motionSerial;PumpTrainingStep(sub);pumpedMotionSerial=source;
   remaining-=sub;
  } while(remaining>1e-7f);
 }else{
  float remaining=step;while(remaining>0){const float sub=std::min(remaining,1.f/120);TickDemo(sub);controller.tick(sub);remaining-=sub;}
  actor.entrySeconds+=step;actor.visualFrameSeconds=std::clamp(dt,0.f,.25f);
  if(!controller.active||!ApplyPose())return false;PumpTrainingStep(std::clamp(dt,0.f,.1f));pumpedMotionSerial=controller.motionSerial;
 }
 return true;
}
void Frame(float dt) {
 if(unityThread&&unityThread!=GetCurrentThreadId())return;unityThread=GetCurrentThreadId();
 int frame=-1;if(Value(FrameCount,nullptr,frame)&&frame==lastFrame)return;lastFrame=frame;
 soundPlayer.Poll();
 if(cleanupPending){Cleanup();ResetKeys();}
 if(!initialized||!ready)return;
#ifdef MHE_MODULE_EXTENSION_HEADER
 moduleExtension.ObservePolicy();
 {std::string extensionReason;
  if(moduleExtension.RebindRequested(extensionReason)){Cleanup();detail=std::move(extensionReason);return;}}
#endif
 if(!pendingMode.empty()){
  const auto next=pendingMode;pendingMode.clear();if(actor.model)Cleanup();
  try{ReadMode(next);detail="武器模式已切换，请开始演示";appliedConfigRevision=configRevision;}
  catch(const std::exception& e){config.mode=loadedMode;detail=std::string("模式载入失败：")+e.what();}
 }
 if(!Focused()||!config.enabled){if(actor.model)Cleanup();ResetKeys();commands.clear();previewRequest=-1;
  resyncFocusKeys=true;
  if(!config.enabled)toggleRequest=demoRequest=false;return;}
 bool f8=(GetAsyncKeyState(config.toggle_key)&0x8000)!=0;
 bool escape=(GetAsyncKeyState(VK_ESCAPE)&0x8000)!=0;
 bool f9=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
 if(resyncFocusKeys){previousToggle=f8;previousEscape=escape;previousDemo=f9;previousBits=KeyBits();
  previousLock=(GetAsyncKeyState(config.lock_key)&0x8000)!=0;previousFocus=(GetAsyncKeyState(config.focus_key)&0x8000)!=0;previousCycle=(GetAsyncKeyState(config.cycle_key)&0x8000)!=0;resyncFocusKeys=false;}
 if(f8&&!previousToggle)toggleRequest=true;if(escape&&!previousEscape)stopRequest=true;
 if(f9&&!previousDemo)demoRequest=true;
 previousToggle=f8;previousEscape=escape;previousDemo=f9;
 if(stopRequest){stopRequest=false;toggleRequest=demoRequest=false;commands.clear();previewRequest=-1;Cleanup();return;}
 if(toggleRequest){toggleRequest=false;if(actor.model){Cleanup();return;}if(!Bind())return;previousBits=KeyBits();}
 if(demoRequest){demoRequest=false;
  const bool startDemo=!autoDemo;
  if((presentationPreview||autoDemo)&&!TakeManualControl()){Cleanup();return;}
  if(!actor.model){if(!Bind())return;previousBits=KeyBits();}
  if(startDemo){ResetTrainingTransient();localMode.combatEnabled=false;}
  autoDemo=startDemo;demoStep=0;demoClock=0;demoDelay=.6f;demoChargeLevel=0;demoElapsed=0;demoFinalTail=false;
  if(demoHeld){controller.held(demoHeldCommand,false);demoHeld=false;}
  if(autoDemo&&controller.idleSlot!=configuredIdleSlot)controller.start(configuredIdleSlot);
  detail=autoDemo?"自动演示中；攻击键切回手动，"+(config.toggle_key>=VK_F1&&config.toggle_key<=VK_F24?"F"+std::to_string(config.toggle_key-VK_F1+1):"快捷键 "+std::to_string(config.toggle_key))+"/Esc 结束":"已切回手动演示";
 }
 if(!actor.model)return;
 auto current=Call(MainCharacter);
 if(!ActorIdentityIsCurrent(current)){Cleanup();detail="角色切换或模型替换，演示已退出";return;}
 if(targetReleaseRequest||!config.target_lock){targetReleaseRequest=false;targetLock.Release();RestoreTargetCamera();lockRequest=focusRequest=cycleRequest=false;}
 if(paused||hudEditing){previousBits=KeyBits();commands.clear();previewRequest=-1;actor.visualFrameSeconds=0;
  if(!ApplyPose()){Cleanup();detail="暂停期间姿势失效，已退出";}UpdateHud();return;}
 if(previewRequest>=0){const auto slot=previewRequest;previewRequest=-1;if(!BeginPresentationPreview(slot)){Cleanup();return;}}
 const auto controlDown=[](int key){return CameraControlDown(key,[](int value){return (GetAsyncKeyState(value)&0x8000)!=0;});};
 const bool lockDown=controlDown(config.lock_key),focusDown=controlDown(config.focus_key),cycleDown=controlDown(config.cycle_key);
 if(lockDown&&!previousLock)lockRequest=true;if(focusDown&&!previousFocus)focusRequest=true;if(cycleDown&&!previousCycle)cycleRequest=true;
 previousLock=lockDown;previousFocus=focusDown;previousCycle=cycleDown;
 auto bits=KeyBits();const auto edges=bits&~previousBits;
 if(edges||!commands.empty()){if(!TakeManualControl()){Cleanup();return;}if(demoHeld){controller.held(demoHeldCommand,false);demoHeld=false;}detail="手动演示中，F9 可切换自动演示";}
 localMode.combatEnabled=ManualCombatActive();
 controller.specialSheatheSeconds=loadedMode=="longsword"&&!autoDemo&&!presentationPreview&&config.combat_rules?localMode.rules.specialSheatheSeconds:0;
 if(!autoDemo)controller.hardwareInput(edges,bits);previousBits=bits;
 for(auto c:commands)controller.input(c);commands.clear();
#ifdef MHE_MODULE_EXTENSION_HEADER
 moduleExtension.ObservePolicy();
#endif
 if(!AdvanceShowcaseFrame(dt)){Cleanup();detail="姿势绑定失效，已恢复原生控制";return;}
#ifdef MHE_MODULE_EXTENSION_HEADER
 {std::string extensionReason;
  if(moduleExtension.RebindRequested(extensionReason)){Cleanup();detail=std::move(extensionReason);return;}}
#endif
 UpdateHud();
 if(resumeSoundLoops){resumeSoundLoops=false;soundTimeline.ResumeLoops(config.sound_enabled&&config.sound_volume>0&&soundReady,EmitSound);themeTimeline.ResumeLoops(config.sound_enabled&&config.sound_volume>0&&soundReady,EmitSound);}
}
void __cdecl TailDetour(void* self,float dt,void* method) {
 {std::lock_guard lock(gate);int frame=-1;
  if(unityThread==GetCurrentThreadId()&&(!Value(FrameCount,nullptr,frame)||frame!=lastFrame))RestoreStageMotion();}
 if(nextTail)nextTail(self,dt,method);
 std::lock_guard lock(gate);
 try{Frame(dt);}catch(const std::exception& e){if(unityThread==GetCurrentThreadId())Cleanup();detail=e.what();Log(std::string("Frame failed: ")+e.what());}
}
void __cdecl InputDetour(void* self,void* method) {
 // Suppression is confined to the explicitly activated showcase session.
 if(suppressInput.load()&&Focused())return;if(nextInput)nextInput(self,method);
}

// Shared-chain input reads, derived from BE's media-screen input interception.
// Hardware polling for the showcase itself continues to read GetAsyncKeyState.
struct Shield {
 BE_MethodDescriptorV1 descriptor;
 BE_ResolvedMethodV1 resolved{};
 bool installed=false;
};
Shield shields[]{
 {{"Rewired_Core.dll","Rewired","Player","GetButton","System.String","System.Boolean",1}},
 {{"Rewired_Core.dll","Rewired","Player","GetButtonDown","System.String","System.Boolean",1}},
 {{"Rewired_Core.dll","Rewired","Player","GetButtonUp","System.String","System.Boolean",1}},
 {{"Rewired_Core.dll","Rewired","Player","GetButton","System.Int32","System.Boolean",1}},
 {{"Rewired_Core.dll","Rewired","Player","GetButtonDown","System.Int32","System.Boolean",1}},
 {{"Rewired_Core.dll","Rewired","Player","GetButtonUp","System.Int32","System.Boolean",1}},
 {{"Rewired_Core.dll","Rewired","Keyboard","GetKey","UnityEngine.KeyCode","System.Boolean",1}},
 {{"Rewired_Core.dll","Rewired","Keyboard","GetKeyDown","UnityEngine.KeyCode","System.Boolean",1}},
 {{"Rewired_Core.dll","Rewired","Keyboard","GetKeyUp","UnityEngine.KeyCode","System.Boolean",1}},
 {{"Rewired_Core.dll","Rewired","Controller","GetButton","System.Int32","System.Boolean",1}},
 {{"Rewired_Core.dll","Rewired","Controller","GetButtonDown","System.Int32","System.Boolean",1}},
 {{"Rewired_Core.dll","Rewired","Controller","GetButtonUp","System.Int32","System.Boolean",1}}
};
template<size_t N> struct ShieldRead {
 using Fn=bool(__cdecl*)(void*,uintptr_t,void*);
 static inline Fn next=nullptr;
 static bool __cdecl Detour(void* self,uintptr_t key,void* method) {
  return suppressInput.load()&&Focused()?false:(next?next(self,key,method):false);
 }
 static bool Install() {
  auto& s=shields[N];if(s.installed)return true;
  if(api->resolve_method(api->context,&s.descriptor,&s.resolved)!=BE_Result_Ok||!NativeHookContract(s.resolved))return N>=6;
  s.installed=api->create_hook(api->context,ID,s.resolved.method_pointer,
   reinterpret_cast<void*>(Detour),reinterpret_cast<void**>(&next))==BE_Result_Ok;
  return s.installed;
 }
};
template<size_t... N> bool InstallShields(std::index_sequence<N...>) {return (ShieldRead<N>::Install()&&...);}

void ResolveWeaponConstants() {
 using FindFieldFn=void*(__cdecl*)(const void*,const char*);
 const auto find=reinterpret_cast<FindFieldFn>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),"il2cpp_class_get_field_from_name"));
 if(!find)return;
 auto value=[&](const char* type,const char* name,int& out){BE_ResolvedClassV1 c{};
  if(api->resolve_class(api->context,Game,"Beyond.Gameplay.View",type,&c)!=BE_Result_Ok||!c.class_info)return;
  auto field=find(c.class_info,name);if(!field)return;auto boxed=api->field_get_value_object(api->context,field,nullptr);
  auto raw=boxed?api->object_unbox(api->context,boxed):nullptr;if(raw)std::memcpy(&out,raw,sizeof(out));};
 value("WeaponComponent.WeaponState","InFight",weaponInFight);
 value("WeaponComponent.WeaponStatePriority","Perform",weaponPerform);
}
bool ResolveTerrainLayout(){
 terrainHitLayout={};const auto module=GetModuleHandleW(L"GameAssembly.dll");
 using SizeFn=int32_t(__cdecl*)(const void*,uint32_t*);using ValueTypeFn=bool(__cdecl*)(const void*);using FieldFlagsFn=uint32_t(__cdecl*)(const void*);
 auto size=reinterpret_cast<SizeFn>(GetProcAddress(module,"il2cpp_class_value_size"));
 auto isValue=reinterpret_cast<ValueTypeFn>(GetProcAddress(module,"il2cpp_class_is_valuetype"));
 auto flags=reinterpret_cast<FieldFlagsFn>(GetProcAddress(module,"il2cpp_field_get_flags"));
 if(!size||!isValue||!flags)return false;
 BE_ResolvedClassV1 hit{},vector{};
 constexpr auto physics="UnityEngine.PhysicsModule.dll";
 if(api->resolve_class(api->context,physics,"UnityEngine","RaycastHit",&hit)!=BE_Result_Ok||!hit.class_info||!isValue(hit.class_info)||
    api->resolve_class(api->context,Core,"UnityEngine","Vector3",&vector)!=BE_Result_Ok||!vector.class_info||!isValue(vector.class_info))return false;
 auto boxed=api->object_new(api->context,hit.class_info);if(!boxed)return false;
 const auto pin=api->gchandle_new(api->context,boxed,1);if(!pin)return false;
 auto raw=api->object_unbox(api->context,boxed);const auto base=reinterpret_cast<uintptr_t>(boxed),unboxed=reinterpret_cast<uintptr_t>(raw);
 const size_t header=unboxed>base?unboxed-base:0;api->gchandle_free(api->context,pin);
 uint32_t alignment=0;const auto bytes=size(hit.class_info,&alignment);
 if(alignment>16||size(vector.class_info,&alignment)!=sizeof(Vec3)||alignment>16)return false;
 const auto field=[&](const char* assembly,const char* type,const char* name,const char* value,int32_t& offset){
  BE_FieldDescriptorV1 d{assembly,"UnityEngine",type,name,value};BE_ResolvedFieldV1 f{};
  if(api->resolve_field(api->context,&d,&f)!=BE_Result_Ok||!f.field_info||(flags(f.field_info)&0x10u))return false;offset=f.offset;return true;
 };
 int32_t x=0,y=0,z=0,point=0,normal=0,distance=0;
 if(!field(Core,"Vector3","x","System.Single",x)||!field(Core,"Vector3","y","System.Single",y)||!field(Core,"Vector3","z","System.Single",z)||
    x!=header||y!=header+4||z!=header+8)return false;
 return field(physics,"RaycastHit","m_Point","UnityEngine.Vector3",point)&&field(physics,"RaycastHit","m_Normal","UnityEngine.Vector3",normal)&&
  field(physics,"RaycastHit","m_Distance","System.Single",distance)&&terrainHitLayout.Configure(bytes,header,point,normal,distance);
}
bool Resolve() {
 if(!api||!api->resolve_method||!api->resolve_class||!api->runtime_invoke||!api->object_unbox||
 !api->resolve_field||!api->field_get_value_object||!api->copy_managed_string||!api->gchandle_new||
 !api->gchandle_free||!api->object_new||!api->string_new||!api->create_hook)return false;
 bool ok=true;for(int i=0;i<MethodCount;++i){methods[i].required=i<ObjectDestroy||i==FrameCount;methods[i].r={};
  auto result=api->resolve_method(api->context,&methods[i].d,&methods[i].r);
  if(result!=BE_Result_Ok||!methods[i].r.method_info||!methods[i].r.method_pointer){if(methods[i].required)ok=false;}}
 // Native detours require the exact signature and instance/static ABI. A
 // same-name, same-arity wildcard could silently accept a changed argument.
 methodFlags=reinterpret_cast<MethodFlagsFn>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),"il2cpp_method_get_flags"));
 if(!NativeHookContract(methods[Tail].r)||!NativeHookContract(methods[CheckInput].r))ok=false;
 BE_FieldDescriptorV1 field{Game,"Beyond.Gameplay","GameDataWithId","id","System.String"};
 if(api->resolve_field(api->context,&field,&templateId)!=BE_Result_Ok)ok=false;
 if(api->resolve_class(api->context,Core,"UnityEngine","SkinnedMeshRenderer",&rendererClass)!=BE_Result_Ok||!rendererClass.type_object)ok=false;
 api->resolve_class(api->context,Core,"UnityEngine","GameObject",&gameObjectClass);
 api->resolve_class(api->context,Core,"UnityEngine","LineRenderer",&lineClass);
 api->resolve_class(api->context,Core,"UnityEngine","Material",&materialClass);
 api->resolve_class(api->context,Core,"UnityEngine","MeshFilter",&meshFilterClass);
 api->resolve_class(api->context,Core,"UnityEngine","Renderer",&baseRendererClass);
 api->resolve_class(api->context,"RootMotion.dll","RootMotion.FinalIK","Grounder",&grounderClass);
 BE_FieldDescriptorV1 solverField{"RootMotion.dll","RootMotion.FinalIK","Grounder","solver","RootMotion.FinalIK.Grounding"};
 BE_FieldDescriptorV1 layerField{"RootMotion.dll","RootMotion.FinalIK","Grounding","layers","UnityEngine.LayerMask"};
 api->resolve_field(api->context,&solverField,&grounderSolver);api->resolve_field(api->context,&layerField,&groundLayers);
 ResolveTerrainLayout();
 ResolveWeaponConstants();
 BE_FieldDescriptorV1 containerField{Game,"Beyond.Gameplay.Core","GameWorld","objectContainer","Beyond.Gameplay.Core.ObjectContainer"};
 api->resolve_field(api->context,&containerField,&worldContainer);
 const auto gameModule=GetModuleHandleW(L"GameAssembly.dll");
 practiceObjectClass=reinterpret_cast<ObjectClassFn>(GetProcAddress(gameModule,"il2cpp_object_get_class"));
 practiceFindField=reinterpret_cast<FindFieldFn>(GetProcAddress(gameModule,"il2cpp_class_get_field_from_name"));
 practiceReady=worldContainer.field_info&&practiceObjectClass&&practiceFindField&&methods[AllEnemies].r.method_info&&
  methods[EntityPosition].r.method_info&&methods[EntityUid].r.method_info;
 if(!ok){detail="游戏接口未全部匹配，查看诊断信息";return false;}
 if(!frameHooked){frameHooked=api->create_hook(api->context,FrameOwner,methods[Tail].r.method_pointer,reinterpret_cast<void*>(TailDetour),reinterpret_cast<void**>(&nextTail))==BE_Result_Ok;}
 if(!inputHooked){inputHooked=api->create_hook(api->context,ID,methods[CheckInput].r.method_pointer,reinterpret_cast<void*>(InputDetour),reinterpret_cast<void**>(&nextInput))==BE_Result_Ok;}
#ifdef MHE_MODULE_EXTENSION_HEADER
 moduleExtension.Install();
#endif
 return frameHooked&&inputHooked&&InstallShields(std::make_index_sequence<std::size(shields)>{});
}
float DemoDuration() {
 // Single tours also finish the display-origin return before holding the
 // ordinary sheath. TickDemo adds that final 0.55-second return interval.
 float result=config.demo_loop?1.15f:1.70f;int slot=configuredIdleSlot;size_t step=0,chargeLevel=0,releaseLevel=0;const HoldPolicy* previousHold=nullptr;
 for(auto command:demoSequence){auto move=controller.moves.find(command);if(move==controller.moves.end()||move->second.empty())continue;
  const int previousSlot=slot;const auto& sequence=move->second;auto current=std::find(sequence.begin(),sequence.end(),slot);
  slot=current!=sequence.end()&&std::next(current)!=sequence.end()?*std::next(current):sequence.front();
  if(command==Command::Release&&previousHold&&!previousHold->releases.empty())slot=previousHold->releases[releaseLevel].slot;
  const HoldPolicy* rule=controller.holdPolicies.contains(command)?&controller.holdPolicies.at(command):nullptr;
  if(auto variants=controller.holdVariants.find(command);variants!=controller.holdVariants.end())for(const auto& variant:variants->second)
   if(variant.fromSlots.contains(previousSlot)){slot=variant.slot;rule=&variant.policy;break;}
  auto clip=controller.clips.find(slot);if(clip!=controller.clips.end()){
   float duration=DemoDelay(command,clip->second,(++step)%std::size(demoSequence));previousHold=nullptr;
   if(rule){duration=std::max(.6f,float(std::max(0,rule->frame))/clip->second.fps+.2f);
    if(command==Command::Charge&&!rule->releases.empty()){
     const size_t stages=controller.holdVariants.contains(command)?controller.holdVariants.at(command).size()+1:1;
     releaseLevel=(chargeLevel++/stages)%rule->releases.size();duration=std::max(.25f,rule->releases[releaseLevel].seconds+.12f);previousHold=rule;
    }
   }result+=duration;
  }
 }
 return result/std::max(.25f,config.speed);
}
Json PracticeTargets(){
 std::vector<const PracticeTarget*> sorted;for(const auto& [id,t]:practice.targets)sorted.push_back(&t);
 std::sort(sorted.begin(),sorted.end(),[](const auto* a,const auto* b){return a->distance==b->distance?a->id<b->id:a->distance<b->distance;});
 Json targets=Json::array();for(const auto* t:sorted){if(targets.size()>=12)break;
  targets.push_back({{"id",t->id},{"generation",t->generation},{"name",t->name},{"hp",t->hp},{"max_hp",t->maximumHP},{"approximate_geometry",t->approximateGeometry}});}
 return targets;
}
Json Status() {
 Json targets=PracticeTargets();if(!controller.active&&targets.empty())targets=practiceSummary;
 Json supported=Json::array();for(const auto&[id,p]:characterProfiles)supported.push_back(id);
 Json modes=Json::array();for(const auto&[id,p]:modeDefinitions)if(std::filesystem::exists(packageRoot/std::filesystem::path(p.value("data_root",std::string("assets")))/L"catalog.json"))modes.push_back(id);
 Json actions=Json::array(),derivations=Json::array();for(const auto&[command,slots]:controller.moves){
  const auto name=CommandName(command);actions.push_back(name);const auto slot=slots.empty()?0:slots.front();
  derivations.push_back({{"name",name},{"label",motionLabels.contains(slot)?motionLabels.at(slot):name},{"available",controller.available(command)},{"buffered",controller.buffered(command)}});}
 const bool aimed=controller.isHeld(Command::Aim)||controller.holding(Command::Charge);
 const auto modeLabel=modeDefinitions.contains(loadedMode)?modeDefinitions.at(loadedMode).value("label",loadedMode):loadedMode;
 const auto selected=targetLock.Selected();bool approximate=false;for(const auto& target:targetSnapshot)if(selected&&target.key==*selected)approximate=target.approximateGeometry;
 const bool controlConflict=KeyClaimsCameraControl(config.lock_key)||KeyClaimsCameraControl(config.focus_key)||KeyClaimsCameraControl(config.cycle_key);
 auto state=Json{{"runtime_ready",ready},{"active",controller.active},{"slot",controller.slot},{"character",actor.character},{"detail",detail},{"phase",phase},{"auto_demo",autoDemo},{"demo_step",demoStep},{"demo_seconds",DemoDuration()},{"weapon_mode",visualWeapon.Visible()?config.visual_weapon:(actor.weaponHeld?"equipped":(actor.prop?"fallback":"none"))},{"sound_ready",soundReady},{"sound_enabled",config.sound_enabled},{"sound_detail",!config.sound_enabled?"音效已关闭":(config.sound_volume==0?"音量为零":soundDetail)},
  {"last_sound",lastSound},{"last_sound_label",soundLabels.contains(lastSound)?soundLabels.at(lastSound):""},{"sound_cues",soundCueCount},{"ground_detail",actor.groundDetail},{"ground_correction",actor.groundCorrection},{"terrain_layout_verified",bool(terrainHitLayout)},
  {"practice",{{"enabled",config.local_practice},{"ready",practiceReady},{"detail",practiceDetail},{"targets",targets},
   {"hits",practice.hits},{"attempts",practice.attempts},{"misses",practice.misses},{"total",practice.total},{"last_damage",practice.lastDamage},{"last_target",practice.lastTarget},{"summary",!controller.active}}},
  {"grip_ready",actor.gripAvailable},{"grip_detail",actor.gripAvailable?"辅助握刀可用":"辅助握刀不可用，已保留基础重定向"},
  {"blade_calibrated",visualWeapon.Visible()||actor.bladeCalibrated},{"weapon_pose_detail",actor.weaponPoseDetail},
  {"visual_weapon_ready",visualWeapon.Ready()},{"visual_weapon_visible",visualWeapon.Visible()},{"visual_weapon_detail",actor.visualReason.empty()?visualWeapon.Status():actor.visualReason},{"laevatain_blade",config.laevatain_blade},
  {"skeleton_path",actor.skeletonPath},{"supported_characters",supported},{"rest_registration",actor.restRegistration},
  {"secondary_skin_roots",actor.secondaryRoots.size()},
  {"mode",loadedMode},{"mode_label",modeLabel},{"motion_label",motionLabels.contains(controller.slot)?motionLabels.at(controller.slot):std::string("待机")},
  {"paused",paused},{"hud_editing",hudEditing},{"config_revision",configRevision},{"applied_config_revision",appliedConfigRevision},
  {"demo_sequence",config.demo_sequence},{"demo_loop",config.demo_loop},{"keyboard_preset",config.keyboard_preset},{"demo_elapsed",demoElapsed/std::max(.25f,config.speed)},{"demo_progress",std::clamp(demoElapsed/std::max(.001f,DemoDuration()*config.speed),0.f,1.f)},{"demo_sequences",{"full","basic","signature"}},
  {"hud_layout_changed",hudLayoutChanged},{"hud",{{"anchor_x",config.hud_anchor_x},{"anchor_y",config.hud_anchor_y},{"scale",config.hud_scale},{"layout",config.hud_layout},{"theme",config.theme}}},
  {"capabilities",{{"modes",modes},{"commands",actions},{"pause",true},{"hud_edit",true},{"local_practice",true},{"targeting",true}}},
  {"targeting",{{"enabled",config.target_lock},{"mode",config.target_camera},{"selected_id",selected?selected->id:0},{"selected_generation",selected?selected->generation:0},{"detail",controlConflict?"镜头快捷键与动作键冲突；动作优先，仍可使用网页按钮":targetingDetail},{"approximate_geometry",approximate},{"scene_query_available",actor.terrainMask!=0}}},
  {"combat",{{"enabled",localMode.combatEnabled},{"numeric_scope",localMode.rules.numericScope},{"followup_pending",combatFollowupSlot!=0},{"delayed_pending",delayedHits.Active()},{"delayed_pulses",delayedHits.pulses},{"delayed_anchor_scope","local_target_contact_anchor"}}},
  {"resource",{{"label",localMode.Label()},{"value",localMode.Value()},{"max",localMode.Maximum()},{"level",localMode.level},{"local_only",true},{"blocks",localMode.blocks},{"feedback",localMode.feedback}}},
  {"aim",{{"active",aimed},{"charge",controller.heldSeconds()},{"max_charge",3}}},
  {"derivations",derivations},{"catalog",productCatalog},
  {"projectiles",localProjectiles.active.size()},{"projectile_markers",projectileEvents.size()},
  {"palm_aligned_left",actor.palmAligned[0]},{"palm_aligned_right",actor.palmAligned[1]}};
#ifdef MHE_MODULE_EXTENSION_HEADER
 moduleExtension.WriteStatus(state);
#endif
 return state;
}
void Configure(const char* raw) {
 if(!raw||std::strlen(raw)>65536)throw std::runtime_error("Invalid configuration size");
 auto j=Json::parse(raw);if(!j.is_object())throw std::runtime_error("Configuration must be object");Configuration c;
 if(j.contains("schema_version")&&(!j["schema_version"].is_number_integer()||(j["schema_version"]!=1&&j["schema_version"]!=2)))throw std::runtime_error("Unsupported configuration version");
 const auto schema=j.value("schema_version",1);
 c.mode=j.value("mode",std::string("longsword"));c.visual_weapon=j.value("visual_weapon",std::string("auto"));c.theme=j.value("theme",std::string("auto"));c.hud_layout=j.value("hud_layout",std::string("teaching"));c.effects_quality=j.value("effects_quality",std::string("full"));
 if(schema==1&&!j.contains("visual_weapon")&&!j.value("laevatain_blade",true))c.visual_weapon="equipped";
 c.demo_sequence=j.value("demo_sequence",std::string("full"));c.demo_loop=j.value("demo_loop",true);
 c.keyboard_preset=j.value("keyboard_preset",std::string("mouse5"));
 if(!std::set<std::string>{"mouse5","mouse3","custom"}.contains(c.keyboard_preset))throw std::runtime_error("Unknown keyboard preset");
 if(!std::set<std::string>{"full","basic","signature"}.contains(c.demo_sequence))throw std::runtime_error("Unknown demo sequence");
 if(!std::set<std::string>{"longsword","sword_shield","greatsword","lance","light_bowgun","bow"}.contains(c.mode))throw std::runtime_error("Unknown weapon mode");
 if(!std::set<std::string>{"auto","equipped","laevatain_blade","typhoea_bow","source"}.contains(c.visual_weapon))throw std::runtime_error("Unknown visual weapon");
 if(c.visual_weapon=="laevatain_blade"&&c.mode!="longsword")throw std::runtime_error("Flame blade adapter currently requires longsword source geometry");
 if(c.visual_weapon=="typhoea_bow"&&c.mode!="bow")throw std::runtime_error("Bow model requires bow mode");
 if(!std::set<std::string>{"auto","forest","ember","tide","high_contrast"}.contains(c.theme)||!std::set<std::string>{"compact","teaching","photo"}.contains(c.hud_layout))throw std::runtime_error("Unknown presentation theme/layout");
 if(c.effects_quality!="full"&&c.effects_quality!="reduced")throw std::runtime_error("Unknown effects quality");
 c.practice_target=j.value("practice_target",std::string("dummy"));if(c.practice_target!="dummy"&&c.practice_target!="monsters")throw std::runtime_error("Unknown practice target source");
 c.hud_enabled=j.value("hud_enabled",true);c.hud_show_derivations=j.value("hud_show_derivations",true);c.hud_show_target=j.value("hud_show_target",true);c.hud_show_crosshair=j.value("hud_show_crosshair",true);
 c.enabled=j.value("enabled",true);c.root_motion=j.value("root_motion",true);c.show_prop=j.value("show_prop",true);
 c.sound_enabled=j.value("sound_enabled",true);
 c.local_practice=j.value("local_practice",false);c.laevatain_blade=j.value("laevatain_blade",true);
 c.target_lock=j.value("target_lock",true);c.combat_rules=j.value("combat_rules",true);
 c.target_camera=j.value("target_camera",std::string("focus"));if(c.target_camera!="focus"&&c.target_camera!="target")throw std::runtime_error("Unknown target camera mode");
 auto key=[&](const char* name,int defaultKey){if(!j.contains(name))return defaultKey;const auto& value=j[name];
  if(!value.is_number_integer()||value<1||value>254)throw std::runtime_error(name);return value.get<int>();};
 const int unique=c.keyboard_preset=="mouse3"?VK_MBUTTON:VK_XBUTTON2;
 c.toggle_key=key("toggle_key",ToggleDefault);c.spirit_key=key("spirit_key",unique);c.spirit_alt_key=key("spirit_alt_key",c.spirit_key);
 if(c.toggle_key==VK_ESCAPE||c.toggle_key==VK_F9)throw std::runtime_error("Toggle key conflicts with exit or automatic demonstration");
 c.keys["reload"]=c.keyboard_preset=="mouse3"?'R':VK_XBUTTON1;
 if(c.mode=="bow"||c.mode=="light_bowgun")c.keys["secondary"]=unique;
 c.keys["spirit"]=c.spirit_key;if(j.contains("keys")){if(!j["keys"].is_object())throw std::runtime_error("Invalid key bindings");
  for(auto it=j["keys"].begin();it!=j["keys"].end();++it){if(!c.keys.contains(it.key())||!it.value().is_number_integer())throw std::runtime_error("Invalid input binding");
   const auto value=it.value().get<int>();if(value<1||value>255||value==VK_ESCAPE||value==c.toggle_key||value==VK_F9)throw std::runtime_error("Reserved or invalid input key");c.keys[it.key()]=value;}}
 if(j.contains("mode_keys")){if(!j["mode_keys"].is_object()||j["mode_keys"].size()>6)throw std::runtime_error("Invalid mode binding registry");
  for(auto it=j["mode_keys"].begin();it!=j["mode_keys"].end();++it){if(!std::set<std::string>{"longsword","sword_shield","greatsword","lance","light_bowgun","bow"}.contains(it.key())||!it.value().is_object())throw std::runtime_error("Unknown key mode");
   auto keys=c.keys;for(auto keyIt=it.value().begin();keyIt!=it.value().end();++keyIt){if(!keys.contains(keyIt.key()))throw std::runtime_error("Unknown input key");const int value=keyIt.value().get<int>();
    if(value<1||value>255||value==VK_ESCAPE||value==c.toggle_key||value==VK_F9)throw std::runtime_error("Reserved mode key");keys[keyIt.key()]=value;}c.mode_keys[it.key()]=std::move(keys);}
  if(!j.contains("keys")&&c.mode_keys.contains(c.mode))c.keys=c.mode_keys.at(c.mode);
 }c.mode_keys[c.mode]=c.keys;
 const auto reservedInput=[&](int value){return value<1||value>254||value==VK_ESCAPE||value==VK_F9||value==c.toggle_key;};
 for(const auto&[name,value]:c.keys)if(reservedInput(value))throw std::runtime_error("Reserved input binding");
 if(reservedInput(c.spirit_key)||reservedInput(c.spirit_alt_key))throw std::runtime_error("Reserved alternate spirit binding");
 c.lock_key=key("lock_key",'Q');c.focus_key=key("focus_key",VK_CONTROL);c.cycle_key=key("cycle_key",VK_TAB);
 std::set<int> cameraKeys;for(int control:{c.lock_key,c.focus_key,c.cycle_key})
  if(control==VK_ESCAPE||control==VK_F8||control==VK_F9||control==c.toggle_key||!cameraKeys.insert(control).second)throw std::runtime_error("Reserved or duplicate camera key");
 auto number=[&](const char* name,float defaultValue,float min,float max){float n=j.value(name,defaultValue);if(!std::isfinite(n)||n<min||n>max)throw std::runtime_error(name);return n;};
 c.speed=number("speed",1,.25f,2);c.blend_seconds=number("blend_seconds",.12f,.02f,.5f);c.buffer_seconds=number("buffer_seconds",.3f,.08f,1);
 c.sound_volume=number("sound_volume",.65f,0,1);
 c.grip_strength=number("grip_strength",1,0,1);
 c.practice_hp=number("practice_hp",1000,1,1000000);c.practice_damage=number("practice_damage",100,1,100000);
 c.practice_radius=number("practice_radius",.65f,.05f,3);c.practice_height=number("practice_height",.9f,0,5);
 c.practice_blade_length=number("practice_blade_length",1.25f,.1f,3);
 c.projectile_speed=number("projectile_speed",30,1,120);
 c.target_range=number("target_range",30,5,80);
 c.hud_scale=number("hud_scale",1,.75f,2);c.hud_anchor_x=number("hud_anchor_x",.02f,0,1);c.hud_anchor_y=number("hud_anchor_y",.05f,0,1);c.hud_margin=number("hud_margin",16,0,128);
 if(j.contains("volumes")){if(!j["volumes"].is_object())throw std::runtime_error("Invalid sound groups");for(auto it=j["volumes"].begin();it!=j["volumes"].end();++it){
  if(!c.volumes.contains(it.key()))throw std::runtime_error("Unknown sound group");const float value=it.value().get<float>();if(!std::isfinite(value)||value<0||value>1)throw std::runtime_error("Invalid sound group volume");c.volumes[it.key()]=value;}}
 if(c.local_practice!=config.local_practice||c.practice_hp!=config.practice_hp||c.practice_target!=config.practice_target||c.practice_radius!=config.practice_radius||c.practice_height!=config.practice_height)practiceResetRequest=true;
 if(practiceResetRequest&&!controller.active){practice.Clear();practiceSummary=Json::array();practiceResetRequest=false;}
 if(c.practice_radius!=config.practice_radius||c.practice_height!=config.practice_height||c.practice_blade_length!=config.practice_blade_length){practice.Suspend();enemyRefresh=0;}
 if(!c.sound_enabled||c.sound_volume==0){soundPlayer.Stop();soundTimeline.Mute();themeTimeline.Mute();}
 if(c.sound_enabled&&c.sound_volume>0&&(!config.sound_enabled||config.sound_volume==0))resumeSoundLoops=true;
 if(c.sound_enabled&&c.sound_volume>0&&(c.sound_volume!=config.sound_volume||c.volumes!=config.volumes)){soundPlayer.Stop();resumeSoundLoops=true;}
 if(!c.enabled||c.show_prop!=config.show_prop||c.laevatain_blade!=config.laevatain_blade||c.visual_weapon!=config.visual_weapon)cleanupPending=actor.model!=nullptr;
 if(!c.enabled){suppressInput=false;soundPlayer.Stop();practiceHud.Update(false,"");}
 const bool sequenceChanged=c.demo_sequence!=config.demo_sequence;
 config=c;if(j.contains("hud_anchor_x")&&j.contains("hud_anchor_y"))hudLayoutChanged=false;controller.transitionSeconds=c.blend_seconds;controller.bufferSeconds=c.buffer_seconds;
 if(initialized&&c.mode==loadedMode&&sequenceChanged){SelectDemoSequence();if(autoDemo){demoStep=0;demoClock=0;demoDelay=.6f;demoElapsed=0;demoFinalTail=false;
   if(demoHeld){controller.held(demoHeldCommand,false);demoHeld=false;}
  }}
 ++configRevision;if(initialized&&c.mode!=loadedMode){pendingMode=c.mode;if(!actor.model){ReadMode(c.mode);pendingMode.clear();appliedConfigRevision=configRevision;}}
 else appliedConfigRevision=configRevision;
}
Command ParseCommand(const std::string& n) {
 return CommandFromName(n);
}
void ReadAudio() {
 soundPlayer.Stop();soundTimeline.Reset();soundTimeline.cues.clear();soundClips.clear();soundLabels.clear();soundReady=false;
 themeTimeline.Reset();themeTimeline.cues.clear();themeDefinitions=Json::object();activeTheme.clear();themeClock=0;themeBowCharging=false;themeBowMotion=0;
 try {
  const auto root=activeAudioRoot.empty()?(activeAssetRoot.empty()?packageRoot/L"assets":activeAssetRoot)/L"audio":activeAudioRoot;std::ifstream file(root/L"cues.json");
  if(!file)throw std::runtime_error("Audio cue manifest missing");Json manifest;file>>manifest;
  if((manifest.at("schema_version")!=1&&manifest.at("schema_version")!=2)||!manifest.at("clips").is_array()||manifest["clips"].size()>1024||!manifest.at("cues").is_object())
   throw std::runtime_error("Invalid audio manifest");
  for(const auto& entry:manifest["clips"]){const auto id=entry.at("id").get<std::string>();
   const auto path=std::filesystem::path(entry.at("file").get<std::string>());
   if(id.empty()||path.has_parent_path()||path.is_absolute()||path.extension()!=L".wav")throw std::runtime_error("Invalid audio path");
  auto pcm=ReadPcmWav(root/path);BalanceShowcaseSound(pcm);
  if(!soundClips.emplace(id,std::move(pcm)).second)throw std::runtime_error("Duplicate audio clip");
  soundLabels[id]=entry.value("label",id);
  }
  size_t count=0;
  for(auto it=manifest["cues"].begin();it!=manifest["cues"].end();++it){size_t consumed=0;const int slot=std::stoi(it.key(),&consumed);
   if(consumed!=it.key().size()||!controller.clips.contains(slot)||!it.value().is_array()||it.value().size()>256)throw std::runtime_error("Invalid audio cue slot");
   auto& cues=soundTimeline.cues[slot];float previous=-1;
   for(const auto& item:it.value()){AudioCue cue{item.at("time").get<float>(),item.at("clip").get<std::string>(),item.value("gain",1.f)};
    cue.kind=item.value("kind",std::string("oneshot"));cue.group=item.value("group",std::string("weapon"));
    cue.condition=item.value("condition",std::string{});cue.loop=item.value("loop",false);cue.priority=item.value("priority",0);cue.fade=item.value("fade",0.f);
    cue.fadeIn=item.value("fade_in",-1.f);cue.fadeOut=item.value("fade_out",-1.f);
    for(float fade:{cue.fadeIn,cue.fadeOut})if(!std::isfinite(fade)||fade< -1||fade>2)throw std::runtime_error("Invalid split audio fade");
    if(item.contains("variants"))cue.variants=item.at("variants").get<std::vector<std::string>>();
    if((cue.kind!="oneshot"&&cue.kind!="start"&&cue.kind!="loop"&&cue.kind!="stop")||cue.group.size()>128||cue.condition.size()>64||
      cue.priority<0||cue.priority>100||!std::isfinite(cue.fade)||cue.fade<0||cue.fade>2||cue.variants.size()>64)throw std::runtime_error("Invalid audio lifecycle cue");
    for(const auto& variant:cue.variants)if(!soundClips.contains(variant))throw std::runtime_error("Unknown audio variant");
    if(!std::isfinite(cue.time)||cue.time<0||cue.time>controller.clips.at(slot).duration()||cue.time<previous||
     !std::isfinite(cue.gain)||cue.gain<0||cue.gain>2||(cue.kind!="stop"&&!soundClips.contains(cue.clip)))throw std::runtime_error("Invalid audio cue");
    previous=cue.time;cues.push_back(std::move(cue));++count;
   }
  }
  if(soundClips.empty()||count==0)throw std::runtime_error("Empty audio manifest");
  const auto themeRoot=packageRoot/L"assets/audio";std::ifstream themed(themeRoot/L"theme_cues.json");
  if(themed){Json manifest;themed>>manifest;if(manifest.at("schema_version")!=1||!manifest.at("clips").is_array()||manifest["clips"].size()>64||!manifest.at("themes").is_object())throw std::runtime_error("Invalid independent theme manifest");
   for(const auto& entry:manifest["clips"]){const auto id=entry.at("id").get<std::string>();auto path=std::filesystem::path(entry.at("file").get<std::string>());
    if(id.empty()||path.has_parent_path()||path.is_absolute()||path.extension()!=L".wav")throw std::runtime_error("Invalid theme PCM path");auto pcm=ReadPcmWav(themeRoot/path);BalanceShowcaseSound(pcm);
    if(!soundClips.emplace(id,std::move(pcm)).second)throw std::runtime_error("Duplicate theme PCM");soundLabels[id]=entry.value("label",id);
   }themeDefinitions=manifest.at("themes");for(auto theme=themeDefinitions.begin();theme!=themeDefinitions.end();++theme)for(const auto& soundPhase:theme.value())for(const auto& row:soundPhase)ThemeCue(row);
  }
  soundReady=true;soundDetail="动作及独立武器音效就绪";
 }catch(const std::exception& e){soundClips.clear();soundTimeline.cues.clear();soundDetail=std::string("音效资源未就绪：")+e.what();}
}
void ReadMotionAssets(const std::filesystem::path& assets) {
 auto read=[](const std::filesystem::path& p){std::ifstream f(p);if(!f)throw std::runtime_error("Missing "+p.string());Json j;f>>j;return j;};
 auto catalog=read(assets/L"catalog.json");if(!catalog.is_array()||catalog.size()>4096)throw std::runtime_error("Invalid catalog");
 controller.clips.clear();for(auto& entry:catalog){auto file=entry.at("file").get<std::string>();auto relative=std::filesystem::path(file);
  if(relative.has_parent_path()||relative.is_absolute()||relative.extension()!=L".lsm")throw std::runtime_error("Invalid clip path");
  auto clip=ReadClip(assets/relative,21);clip.slot=entry.at("slot").get<int>();controller.clips.emplace(clip.slot,std::move(clip));}
 profile=characterProfiles.at("chr_0003_endminf");
 const auto axis=profile.value("practice_blade_axis",std::vector<float>{0,0,1});
 practiceBladeAxis=Retarget::Normalize({axis[0],axis[1],axis[2]});
 auto source=read(assets/L"source_rig.json");sourceRestReady=false;
 sourceSoleHeight=source.contains("sole_geometry")?source["sole_geometry"].value("hip_height",1.0568615f):1.0568615f;
 if(!std::isfinite(sourceSoleHeight)||sourceSoleHeight<.2f||sourceSoleHeight>3)throw std::runtime_error("Invalid source sole stature");
 for(int i=0;i<21;++i){const auto& p=source.at("bones").at(semantic[i]).at("position");
  sourceRestPositions[i]={p.at(0).get<float>(),p.at(1).get<float>(),p.at(2).get<float>()};
  if(!Finite(sourceRestPositions[i]))throw std::runtime_error("Invalid source rest position");
  const auto q=source.at("bones").at(semantic[i]).at("rotation").get<std::vector<float>>();if(q.size()!=4)throw std::runtime_error("Invalid source rest rotation");
  sourceRestRotations[i]=Normalize({q[0],q[1],q[2],q[3]});}
 sourceRestReady=true;sourcePalmsReady=false;
 if(source.contains("hand_anatomy")){
  const std::array<const char*,2> sides{"left","right"};
  for(int side=0;side<2;++side){const auto& row=source.at("hand_anatomy").at(sides[side]);
   const auto f=row.at("forward").get<std::vector<float>>(),a=row.at("across").get<std::vector<float>>();
   if(f.size()!=3||a.size()!=3)throw std::runtime_error("Invalid source palm dimensions");
   const Vec3 forward{f[0],f[1],f[2]},across{a[0],a[1],a[2]};
   if(!Finite(forward)||!Finite(across)||Retarget::Len(forward)<.01f||Retarget::Len(Retarget::Cross(forward,across))<1e-5f)
    throw std::runtime_error("Invalid source palm geometry");
   sourcePalmFrames[side]=Retarget::PalmBasis(forward,across);
  }
  sourcePalmsReady=true;
 }
 fingerTracks.clear();sourceFingerBones=Json::array();auto fingerRoot=assets;
 if(modeDefinitions.contains(loadedMode)&&modeDefinitions.at(loadedMode).contains("finger_root")){
  const auto relative=std::filesystem::path(modeDefinitions.at(loadedMode).at("finger_root").get<std::string>());
  if(relative.is_absolute()||relative.has_root_name())throw std::runtime_error("Invalid finger root");for(const auto& part:relative)if(part==L"..")throw std::runtime_error("Unsafe finger root");fingerRoot=packageRoot/relative;}
 if(std::filesystem::exists(fingerRoot/L"finger_tracks.json")){
  const auto manifest=read(fingerRoot/L"finger_tracks.json");if(manifest.at("schema_version")!=1||!manifest.at("bones").is_array()||manifest["bones"].size()>96||!manifest.at("tracks").is_object())throw std::runtime_error("Invalid finger manifest");
  sourceFingerBones=manifest.at("bones");std::set<std::string> names;for(const auto& bone:sourceFingerBones)if(!names.insert(bone.at("name").get<std::string>()).second)throw std::runtime_error("Duplicate source finger semantic");
  for(auto it=manifest["tracks"].begin();it!=manifest["tracks"].end();++it){const int slot=std::stoi(it.key());const auto path=std::filesystem::path(it.value().at("file").get<std::string>());
   if(path.has_parent_path()||path.is_absolute()||path.extension()!=L".lsf"||!controller.clips.contains(slot))throw std::runtime_error("Invalid finger track path/slot");
   auto track=ReadFingers(fingerRoot/path,controller.clips.at(slot).frames.size(),sourceFingerBones.size());if(track.fps!=controller.clips.at(slot).fps)throw std::runtime_error("Finger motion clock mismatch");fingerTracks.emplace(slot,std::move(track));}
 }
 sourceWeaponTracks.clear();auto weapons=read(assets/L"source_weapon_tracks.json");
 const auto weaponSchema=weapons.at("schema_version").get<int>();
 if((weaponSchema!=1&&weaponSchema!=2&&weaponSchema!=3)||!weapons.at("tracks").is_object()||weapons["tracks"].size()>4096)
  throw std::runtime_error("Invalid source weapon manifest");
 if(weaponSchema==3)sourceWeaponTracks=PackedTracks::ReadSourceWeapon(assets,weapons,controller.clips);
 else for(auto it=weapons["tracks"].begin();it!=weapons["tracks"].end();++it){size_t consumed=0;int slot=std::stoi(it.key(),&consumed);
  if(consumed!=it.key().size()||!controller.clips.contains(slot))throw std::runtime_error("Invalid source weapon slot");
  WeaponPose::SourceTrack track;track.fps=it.value().at("fps").get<float>();
  if(track.fps!=controller.clips.at(slot).fps||it.value().at("frames").size()!=controller.clips.at(slot).frames.size())
   throw std::runtime_error("Source weapon dimensions mismatch");
  for(const auto& row:it.value().at("frames")){const auto q=row.at("rotation_xyzw").get<std::vector<float>>(),p=row.at("support_offset").get<std::vector<float>>();
   if(q.size()!=4||p.size()!=3)throw std::runtime_error("Invalid source weapon frame");
   WeaponPose::SourceFrame frame{{q[0],q[1],q[2],q[3]},{p[0],p[1],p[2]},row.at("support_weight").get<float>()};
   if(!Finite(frame.rotation)||std::abs(Dot(frame.rotation,frame.rotation)-1)>.001f||!Finite(frame.supportOffset)||Retarget::Len(frame.supportOffset)>3||
     !std::isfinite(frame.supportWeight)||frame.supportWeight<0||frame.supportWeight>1)throw std::runtime_error("Invalid source weapon sample");
   const auto attachment=[&](const char* side,const char* node){WeaponPose::Attachment result;
    if(!row.contains(side)||!row.contains("attachments")||!row["attachments"].contains(node))return result;
    const auto& hand=row.at(side);const auto& socket=row["attachments"].at(node);
    const auto h=hand.at("position").get<std::vector<float>>(),hp=hand.at("rotation_xyzw").get<std::vector<float>>();
    const auto p=socket.at("position").get<std::vector<float>>(),q=socket.at("rotation_xyzw").get<std::vector<float>>();
    if(h.size()!=3||p.size()!=3||hp.size()!=4||q.size()!=4)throw std::runtime_error("Invalid authored source attachment");
    const Quat handQ=Normalize({hp[0],hp[1],hp[2],hp[3]}),socketQ=Normalize({q[0],q[1],q[2],q[3]});
    result.position=Rotate(Inverse(handQ),Retarget::Sub({p[0],p[1],p[2]},{h[0],h[1],h[2]}));result.rotation=Mul(Inverse(handQ),socketQ);result.valid=true;
    if(!Finite(result.position)||Retarget::Len(result.position)>3)throw std::runtime_error("Invalid source attachment distance");return result;};
   frame.main=attachment(loadedMode=="bow"?"left":"right",loadedMode=="bow"?"30":"47");frame.offhand=attachment("left","30");
   track.frames.push_back(frame);}
  sourceWeaponTracks.emplace(slot,std::move(track));}
 geometryContactMasks.clear();const auto contactFile=assets/L"contact_masks.json";
 if(std::filesystem::exists(contactFile)){
  const auto contacts=read(contactFile);
  if(contacts.at("schema_version")!=2||!contacts.at("models").is_object()||contacts["models"].size()>2||
   contacts.value("authored_contact_flag",true))throw std::runtime_error("Invalid geometric contact bank");
  for(auto model=contacts["models"].begin();model!=contacts["models"].end();++model){
   if(model.key()!="wpn_misc_0016"&&model.key()!="source:longsword_bs_swo001")throw std::runtime_error("Unknown geometric contact model");
   for(auto character=model.value().at("characters").begin();character!=model.value().at("characters").end();++character){
    if(!characterProfiles.contains(character.key()))throw std::runtime_error("Unknown geometric contact character");
    auto& target=geometryContactMasks[model.key()][character.key()];const auto& tracks=character.value().at("tracks");
    if(!tracks.is_object()||tracks.size()>4096)throw std::runtime_error("Invalid geometric contact tracks");
    for(auto it=tracks.begin();it!=tracks.end();++it){size_t consumed=0;const int slot=std::stoi(it.key(),&consumed);
     if(consumed!=it.key().size()||!controller.clips.contains(slot))throw std::runtime_error("Unknown geometric contact slot");
     const auto& row=it.value();GeometryContactMask mask;mask.fps=row.at("fps").get<float>();mask.weights=row.at("weights").get<std::vector<float>>();
     const auto& passed=row.at("pass_mask");
     if(mask.fps!=controller.clips.at(slot).fps||row.at("frames").get<size_t>()!=mask.weights.size()||
      mask.weights.size()!=controller.clips.at(slot).frames.size()||!passed.is_array()||passed.size()!=mask.weights.size())throw std::runtime_error("Geometric contact dimensions mismatch");
     for(size_t f=0;f<mask.weights.size();++f){bool verified=false;
      if(passed[f].is_boolean())verified=passed[f].get<bool>();
      else if(passed[f].is_number_integer()&&(passed[f]==0||passed[f]==1))verified=passed[f]==1;
      else throw std::runtime_error("Invalid geometric contact pass flag");
      if(!std::isfinite(mask.weights[f])||mask.weights[f]<0||mask.weights[f]>1||(!verified&&mask.weights[f]!=0))throw std::runtime_error("Unverified geometric contact weight");
     }
     target.emplace(slot,std::move(mask));
    }
   }
  }
 }
 groundTracks.clear();auto ground=read(assets/L"ground_tracks.json");
 const auto groundSchema=ground.at("schema_version").get<int>();
 if((groundSchema!=1&&groundSchema!=2)||!ground.at("tracks").is_object()||ground["tracks"].size()>4096)throw std::runtime_error("Invalid ground manifest");
 for(auto it=ground["tracks"].begin();it!=ground["tracks"].end();++it){size_t consumed=0;int slot=std::stoi(it.key(),&consumed);
  if(consumed!=it.key().size()||!controller.clips.contains(slot))throw std::runtime_error("Invalid ground slot");
  GroundTrack track;track.fps=it.value().at("fps").get<float>();track.clearance=it.value().at("clearance").get<std::vector<float>>();
  const auto& clip=controller.clips.at(slot);
  if(track.fps!=clip.fps||track.clearance.size()!=clip.frames.size())throw std::runtime_error("Ground track dimensions mismatch");
  for(float value:track.clearance)if(!std::isfinite(value)||value<0||value>8)throw std::runtime_error("Invalid foot clearance");
  groundTracks.emplace(slot,std::move(track));}
 supportGroundTracks.clear();const auto supportFile=assets/L"support_ground_tracks.json";
 if(std::filesystem::exists(supportFile)){
  const auto support=read(supportFile);if(support.at("schema_version")!=1||!support.at("tracks").is_object()||support["tracks"].size()>4096)throw std::runtime_error("Invalid supporting ground bank");
  for(auto it=support["tracks"].begin();it!=support["tracks"].end();++it){size_t used=0;const int slot=std::stoi(it.key(),&used);
   if(used!=it.key().size()||!controller.clips.contains(slot))throw std::runtime_error("Unknown supporting ground slot");
   const auto& clip=controller.clips.at(slot);const auto& row=it.value();const float fps=row.at("fps").get<float>();
   if(fps!=clip.fps||row.at("frames").get<size_t>()!=clip.frames.size())throw std::runtime_error("Supporting ground dimensions mismatch");
   std::array<SupportGroundSide,2> sides;
   for(int side=0;side<2;++side){const auto& values=row.at(side==0?"left":"right");auto& target=sides[side];target.active.fps=target.clearance.fps=fps;
    target.active.clearance=values.at("active").get<std::vector<float>>();target.clearance.clearance=values.at("clearance").get<std::vector<float>>();
    target.kind=values.value("kind",std::vector<int>(clip.frames.size(),1));
    if(target.active.clearance.size()!=clip.frames.size()||target.clearance.clearance.size()!=clip.frames.size()||target.kind.size()!=clip.frames.size())throw std::runtime_error("Incomplete supporting ground frames");
    for(int value:target.kind)if(value<0||value>3)throw std::runtime_error("Invalid ground support kind");
    for(float value:target.active.clearance)if(!std::isfinite(value)||value<0||value>1)throw std::runtime_error("Invalid ground support flag");
    for(float value:target.clearance.clearance)if(!std::isfinite(value)||value<0||value>8)throw std::runtime_error("Invalid supporting hand clearance");
   }supportGroundTracks.emplace(slot,std::move(sides));
  }
 }
 gripTracks.clear();auto grip=read(assets/L"grip_tracks.json");
 const auto gripSchema=grip.at("schema_version").get<int>();
 if((gripSchema!=1&&gripSchema!=2)||!grip.at("tracks").is_object()||grip.at("tracks").size()>4096)throw std::runtime_error("Invalid grip manifest");
 if(gripSchema==2)gripTracks=PackedTracks::ReadGripBank(assets,grip,controller.clips);
 else for(auto it=grip["tracks"].begin();it!=grip["tracks"].end();++it){const int slot=std::stoi(it.key());auto path=std::filesystem::path(it.value().get<std::string>());
  if(path.has_parent_path()||path.is_absolute()||path.extension()!=L".lsg"||!controller.clips.contains(slot))throw std::runtime_error("Invalid grip path");
  gripTracks.emplace(slot,ReadGrip(assets/path,controller.clips.at(slot).frames.size()));}
 practice.Clear();practiceSummary=Json::array();practice.windows.clear();auto hit=read(assets/L"hit_windows.json");
 if(hit.at("schema_version")!=1||!hit.at("windows").is_object()||hit["windows"].size()>4096)throw std::runtime_error("Invalid practice manifest");
 for(auto it=hit["windows"].begin();it!=hit["windows"].end();++it){int slot=std::stoi(it.key());
  if(!controller.clips.contains(slot)||!it.value().is_array()||it.value().size()>16)throw std::runtime_error("Invalid hit slot");
  float lastEnd=-1;for(const auto& w:it.value()){HitWindow window{w.at(0).get<float>(),w.at(1).get<float>(),w.at(2).get<float>()};
   if(!std::isfinite(window.begin)||!std::isfinite(window.end)||!std::isfinite(window.multiplier)||window.begin<0||window.end<=window.begin||
    window.begin<lastEnd||window.end>controller.clips.at(slot).duration()||window.multiplier<=0||window.multiplier>10)throw std::runtime_error("Invalid hit window");
   practice.windows[slot].push_back(window);lastEnd=window.end;}}
 auto moves=read(assets/L"moves.json");configuredIdleSlot=controller.idleSlot=moves.value("idle",1);controller.moves.clear();
 for(auto it=moves.at("commands").begin();it!=moves.at("commands").end();++it){auto sequence=it.value().get<std::vector<int>>();
  if(sequence.empty())throw std::runtime_error("Empty move sequence");for(auto slot:sequence)if(!controller.clips.contains(slot))throw std::runtime_error("Configured move slot unavailable");
  controller.setMove(ParseCommand(it.key()),std::move(sequence));}
 if(moves.contains("sheathe_hold_frame"))controller.sheatheHoldFrame=moves["sheathe_hold_frame"].get<int>();
 if(!controller.clips.contains(controller.idleSlot))throw std::runtime_error("Required idle motion missing");
 motionLabels.clear();if(moves.contains("labels"))for(auto it=moves["labels"].begin();it!=moves["labels"].end();++it)motionLabels[std::stoi(it.key())]=it.value().get<std::string>();
 controller.holdPolicies.clear();if(moves.contains("hold_commands"))for(auto it=moves["hold_commands"].begin();it!=moves["hold_commands"].end();++it){
  const auto command=ParseCommand(it.key());HoldPolicy policy;policy.frame=it.value().value("frame",-1);policy.loop=it.value().value("loop",false);policy.loopFrame=it.value().value("loop_frame",0);
  if(!controller.moves.contains(command)||policy.frame< -1)throw std::runtime_error("Invalid hold command");
  for(int slot:controller.moves.at(command))if(policy.loopFrame<0||policy.loopFrame>=int(controller.clips.at(slot).frames.size())-1||policy.frame>=int(controller.clips.at(slot).frames.size()))throw std::runtime_error("Invalid hold loop frame");
  float previous=-1;for(const auto& r:it.value().value("releases",Json::array())){HoldRelease release{r.at("seconds").get<float>(),r.at("slot").get<int>()};
   if(!std::isfinite(release.seconds)||release.seconds<0||release.seconds<previous||!controller.clips.contains(release.slot))throw std::runtime_error("Invalid charge release");
   policy.releases.push_back(release);previous=release.seconds;}controller.holdPolicies[command]=std::move(policy);}
 if(moves.contains("inputs")){controller.inputBindings.clear();const std::map<std::string,uint32_t> names{{"slash",1},{"attack",1},{"thrust",2},{"secondary",2},{"spirit",4},{"dodge",8},{"sheathe",16},{"aim",32},{"reload",64}};
  for(auto it=moves["inputs"].begin();it!=moves["inputs"].end();++it){auto n=names.find(it.key());if(n==names.end())throw std::runtime_error("Invalid input mapping");controller.inputBindings[n->second]=ParseCommand(it.value().get<std::string>());}}
 if(moves.contains("chords"))for(auto it=moves["chords"].begin();it!=moves["chords"].end();++it){const auto bits=std::stoul(it.key());const auto command=ParseCommand(it.value().get<std::string>());
  if(bits<1||bits>127||!controller.moves.contains(command))throw std::runtime_error("Invalid chord");controller.chordBindings[uint32_t(bits)]=command;}
 if((loadedMode=="bow"||loadedMode=="light_bowgun")&&controller.moves.contains(Command::Aim))controller.inputBindings[AimBit]=Command::Aim;
 if((loadedMode=="light_bowgun"||loadedMode=="bow")&&controller.moves.contains(Command::Reload))controller.inputBindings[ReloadBit]=Command::Reload;
 controller.hardwareRoutes.clear();if(moves.contains("hardware_routes"))for(auto slot=moves["hardware_routes"].begin();slot!=moves["hardware_routes"].end();++slot){const int id=std::stoi(slot.key());
  if(!controller.clips.contains(id)||!slot.value().is_object())throw std::runtime_error("Invalid hardware follow-up slot");
  for(auto it=slot.value().begin();it!=slot.value().end();++it){const auto bits=std::stoul(it.key());const auto command=ParseCommand(it.value().get<std::string>());
   if(bits<1||bits>127||!controller.moves.contains(command))throw std::runtime_error("Invalid hardware follow-up command");controller.hardwareRoutes[id][uint32_t(bits)]=command;}}
 controller.tapHoldBindings.clear();if(moves.contains("input_behavior")){const std::map<std::string,uint32_t> names{{"attack",1},{"secondary",2},{"spirit",4},{"dodge",8},{"sheathe",16},{"aim",32},{"reload",64}};
  for(auto it=moves["input_behavior"].begin();it!=moves["input_behavior"].end();++it){const auto key=names.find(it.key());const auto command=ParseCommand(it.value().at("hold").get<std::string>());const float seconds=it.value().value("hold_seconds",.35f);
   if(key==names.end()||!std::isfinite(seconds)||seconds<.05f||seconds>2||!controller.holdPolicies.contains(command))throw std::runtime_error("Invalid tap/hold input rule");
   if(!controller.inputBindings.contains(key->second)||controller.inputBindings.at(key->second)!=ParseCommand(it.value().at("tap").get<std::string>()))throw std::runtime_error("Tap action differs from input binding");
   controller.tapHoldBindings[key->second]={command,seconds};}}
 controller.holdVariants.clear();if(moves.contains("hold_variants"))for(auto it=moves["hold_variants"].begin();it!=moves["hold_variants"].end();++it){const auto command=ParseCommand(it.key());
  if(!controller.holdPolicies.contains(command)||!it.value().is_array()||it.value().size()>16)throw std::runtime_error("Invalid contextual charge variants");
  for(const auto& row:it.value()){HoldVariant variant;variant.slot=row.at("slot").get<int>();variant.fromSlots=row.at("from_slots").get<std::set<int>>();
   if(!controller.clips.contains(variant.slot)||variant.fromSlots.empty())throw std::runtime_error("Invalid contextual hold slot");
   for(int from:variant.fromSlots)if(!controller.clips.contains(from))throw std::runtime_error("Unknown charge prerequisite");
   variant.policy.frame=row.value("frame",-1);variant.policy.loop=row.value("loop",false);variant.policy.loopFrame=row.value("loop_frame",0);
   if(variant.policy.loopFrame<0||variant.policy.loopFrame>=int(controller.clips.at(variant.slot).frames.size())-1)throw std::runtime_error("Invalid contextual hold loop frame");
   if(variant.policy.frame< -1||variant.policy.frame>=int(controller.clips.at(variant.slot).frames.size()))throw std::runtime_error("Invalid contextual hold frame");
   float previous=-1;for(const auto& release:row.at("releases")){HoldRelease value{release.at("seconds").get<float>(),release.at("slot").get<int>()};
    if(!std::isfinite(value.seconds)||value.seconds<0||value.seconds<previous||!controller.clips.contains(value.slot))throw std::runtime_error("Invalid contextual charge release");
    variant.policy.releases.push_back(value);previous=value.seconds;}
   controller.holdVariants[command].push_back(std::move(variant));}}
 if((loadedMode=="bow"||loadedMode=="light_bowgun")&&controller.moves.contains(Command::Aim))controller.inputBindings[AimBit]=Command::Aim;
 if((loadedMode=="light_bowgun"||loadedMode=="bow")&&controller.moves.contains(Command::Reload))controller.inputBindings[ReloadBit]=Command::Reload;
 modeProps.clear();projectileProp=Json{};const auto propsRoot=assets/L"props/index.json";
 if(std::filesystem::exists(propsRoot)){const auto registry=read(propsRoot);const auto& rows=registry.contains("props")?registry.at("props"):registry.at("items");
  if(!rows.is_array()||rows.size()>16)throw std::runtime_error("Invalid source props registry");
  for(const auto& row:rows){const auto relative=std::filesystem::path(row.at("file").get<std::string>());if(relative.is_absolute()||relative.extension()!=L".json")throw std::runtime_error("Invalid prop file");
   for(const auto& part:relative)if(part==L"..")throw std::runtime_error("Unsafe prop path");auto prop=read(assets/relative);
   const auto id=prop.value("id",row.value("id",std::string{}));
   if(id.find("ya001")!=std::string::npos||row.value("role",std::string{})=="projectile")projectileProp=std::move(prop);else modeProps.push_back(std::move(prop));}}
 if(loadedMode=="light_bowgun"&&projectileProp.empty()){
  // A locally authored tracer, independent of the native bullet system.
  projectileProp={{"id","local_bullet_tracer"},{"source_forward_axis",{0,0,1}},{"vertices",{{-.008f,-.008f,0},{.008f,-.008f,0},{.008f,.008f,0},{-.008f,.008f,0},{-.008f,-.008f,.2f},{.008f,-.008f,.2f},{.008f,.008f,.2f},{-.008f,.008f,.2f}}},{"indices",{0,1,2,0,2,3,4,6,5,4,7,6,0,4,5,0,5,1,1,5,6,1,6,2,2,6,7,2,7,3,3,7,4,3,4,0}}};
 }
 projectileEvents.clear();const auto markersPath=assets/L"projectile_events.json";
 if(std::filesystem::exists(markersPath)){const auto markers=read(markersPath);if(markers.at("schema_version")!=1||!markers.at("tracks").is_object())throw std::runtime_error("Invalid projectile event manifest");
  for(auto it=markers["tracks"].begin();it!=markers["tracks"].end();++it){const int slot=std::stoi(it.key());if(!controller.clips.contains(slot)||!it.value().is_array()||it.value().size()>128)throw std::runtime_error("Invalid projectile event slot");
   float previous=-1;for(const auto& marker:it.value()){const float time=marker.at("time").get<float>();if(!std::isfinite(time)||time<0||time<previous||time>controller.clips.at(slot).duration())throw std::runtime_error("Invalid projectile marker");projectileEvents[slot].push_back(time);previous=time;}}
 }
 skinGeometry.clear();skinMotions.clear();const auto skinPath=assets/L"source_skin_tracks.json";
 if(std::filesystem::exists(skinPath)){const auto manifest=read(skinPath);if(manifest.at("schema_version")!=1||!manifest.at("props").is_object()||!manifest.at("tracks").is_object())throw std::runtime_error("Invalid source own-palette manifest");
  for(auto it=manifest["props"].begin();it!=manifest["props"].end();++it){const auto path=std::filesystem::path(it.value().at("file").get<std::string>());
   if(path.is_absolute()||path.extension()!=L".json")throw std::runtime_error("Invalid own-palette prop path");for(const auto& part:path)if(part==L"..")throw std::runtime_error("Unsafe skin prop");
   const auto prop=read(assets/path);SkinGeometry geometry;geometry.bones=it.value().at("bone_names").size();
   if(!geometry.bones||geometry.bones>128||prop.at("vertices").size()>65535)throw std::runtime_error("Invalid source skin palette size");
   for(const auto& p:prop.at("vertices")){if(p.size()!=3)throw std::runtime_error("Invalid skin vertex");geometry.vertices.push_back({p[0].get<float>(),p[1].get<float>(),p[2].get<float>()});}
   geometry.indices=it.value().at("skin_bone_indices").get<std::vector<SourceSkin::Indices>>();geometry.weights=it.value().at("skin_weights").get<std::vector<SourceSkin::Weights>>();
   SourceSkin::Deform(geometry.vertices,geometry.indices,geometry.weights,std::vector<SourceSkin::Matrix>(geometry.bones,SourceSkin::Identity()));skinGeometry.emplace(it.key(),std::move(geometry));
  }
  for(auto it=manifest["tracks"].begin();it!=manifest["tracks"].end();++it){const int slot=std::stoi(it.key());const auto id=it.value().at("prop_id").get<std::string>();auto path=std::filesystem::path(it.value().at("file").get<std::string>());
   if(!controller.clips.contains(slot)||!skinGeometry.contains(id)||path.is_absolute()||path.extension()!=L".lsk")throw std::runtime_error("Invalid source skin track");for(const auto& part:path)if(part==L"..")throw std::runtime_error("Unsafe skin track");
   auto track=SourceSkin::Read(assets/path,controller.clips.at(slot).frames.size(),skinGeometry.at(id).bones);if(track.fps!=controller.clips.at(slot).fps)throw std::runtime_error("Source skin clock mismatch");skinMotions.emplace(slot,SkinMotion{id,std::move(track)});
  }
 }
 ReadAudio();
}
void ReadMode(const std::string& id){
 auto found=modeDefinitions.find(id);if(found==modeDefinitions.end())throw std::runtime_error("Unknown weapon mode: "+id);
 const auto relative=std::filesystem::path(found->second.value("data_root",std::string("assets")));
 if(relative.is_absolute()||relative.has_root_name())throw std::runtime_error("Invalid mode root");
 for(const auto& part:relative)if(part==L"..")throw std::runtime_error("Unsafe mode root");
 // A failed switch restores the entire previous loaded set, including source
 // anatomy and audio; it never mixes a new pose with an old weapon track.
 auto oldController=std::move(controller);auto oldGrip=std::move(gripTracks);auto oldGround=std::move(groundTracks);auto oldGeometryContacts=std::move(geometryContactMasks);auto oldSupportGround=std::move(supportGroundTracks);
 auto oldWeapons=std::move(sourceWeaponTracks);auto oldWindows=std::move(practice.windows);auto oldAuthoredWindows=std::move(practice.authoredWindows);auto oldFingers=std::move(fingerTracks);auto oldFingerBones=std::move(sourceFingerBones);
 auto oldDelayedWindows=std::move(delayedContactWindows);auto oldDelayedRules=std::move(delayedRules);
 auto oldSounds=std::move(soundClips);auto oldSoundLabels=std::move(soundLabels);auto oldTimeline=std::move(soundTimeline);
 auto oldThemeTimeline=std::move(themeTimeline);const auto oldThemes=themeDefinitions;
 const auto oldRest=sourceRestPositions;const auto oldRestRotations=sourceRestRotations;const auto oldSoleHeight=sourceSoleHeight;const auto oldPalms=sourcePalmFrames;const bool oldRestReady=sourceRestReady,oldPalmsReady=sourcePalmsReady,oldSoundReady=soundReady;
 const auto oldRoot=activeAssetRoot,oldAudioRoot=activeAudioRoot;const auto oldMode=loadedMode;const int oldIdle=configuredIdleSlot;
 const auto oldKeys=config.keys;const auto oldModeKeys=config.mode_keys;const auto oldLocalMode=localMode;
 auto oldDemo=demoSequence;auto oldFullDemo=fullDemoSequence;auto oldLabels=std::move(motionLabels);auto oldProjectiles=std::move(projectileEvents);auto oldProps=std::move(modeProps);const auto oldProjectileProp=projectileProp;const auto oldSoundDetail=soundDetail;
 auto oldSkinGeometry=std::move(skinGeometry);auto oldSkinMotions=std::move(skinMotions);
 try{
  controller=Controller{};controller.longswordRules=id=="longsword";soundTimeline=AudioTimeline{};
  activeAssetRoot=packageRoot/relative;const auto audioRelative=std::filesystem::path(found->second.value("audio_root",(relative/L"audio").generic_string()));
  if(audioRelative.is_absolute()||audioRelative.has_root_name())throw std::runtime_error("Invalid audio root");for(const auto& part:audioRelative)if(part==L"..")throw std::runtime_error("Unsafe audio root");
  activeAudioRoot=packageRoot/audioRelative;loadedMode=id;ReadMotionAssets(activeAssetRoot);
  controller.transitionSeconds=config.blend_seconds;controller.bufferSeconds=config.buffer_seconds;
  demoSequence.clear();std::ifstream moveInput(activeAssetRoot/L"moves.json");Json modeMoves;moveInput>>modeMoves;
  const auto& modeDemo=modeMoves.contains("demo")?modeMoves.at("demo"):found->second.value("demo",Json::array());
  for(const auto& name:modeDemo)demoSequence.push_back(ParseCommand(name.get<std::string>()));
  if(demoSequence.empty()&&id=="longsword")demoSequence=defaultDemoSequence;
  if(demoSequence.empty())for(const auto&[command,slots]:controller.moves)if(!slots.empty())demoSequence.push_back(command);
  for(auto command:demoSequence)if(!controller.moves.contains(command))throw std::runtime_error("Demo references unavailable command");
  if(demoSequence.empty())throw std::runtime_error("Mode lacks complete demo sequence");
  fullDemoSequence=demoSequence;SelectDemoSequence();
  if(config.mode_keys.contains(id))config.keys=config.mode_keys.at(id);
  else if(found->second.contains("default_bindings")&&found->second["default_bindings"].contains("keys")){
   const auto& bindings=found->second["default_bindings"];
   const auto& preset=bindings.contains("presets")&&bindings["presets"].contains(config.keyboard_preset)?bindings["presets"][config.keyboard_preset]:bindings;
   for(auto it=preset["keys"].begin();it!=preset["keys"].end();++it){const int key=it.value().get<int>();
    if(!config.keys.contains(it.key())||key<1||key>255||key==VK_ESCAPE||key==config.toggle_key||key==VK_F9)throw std::runtime_error("Invalid default binding");config.keys[it.key()]=key;}
   config.mode_keys[id]=config.keys;
  }
  std::ifstream combatInput(activeAssetRoot/L"combat_rules.json");Json combatDocument;if(!combatInput)throw std::runtime_error("Source combat manifest missing");combatInput>>combatDocument;
  auto combat=ReadCombatAssets(combatDocument,id,controller.clips);practice.authoredWindows=std::move(combat.windows);delayedContactWindows=std::move(combat.delayedWindows);delayedRules=std::move(combat.delayedRules);delayedHits.Clear();delayedContact.Suspend();localMode.Configure(std::move(combat.rules));
  controller.specialSheatheSeconds=id=="longsword"?localMode.rules.specialSheatheSeconds:0;controller.specialSheatheReadySeconds=localMode.rules.specialSheatheReadySeconds;
  practice.collision.settings.maximumFrameGap=.25f;practice.collision.settings.teleportDistance=5.f;
  controller.combatAllowed=[](Command command,const Controller& c){return !localMode.combatEnabled||localMode.Allows(command,c);};
  controller.combatSelectSlot=[](Command command,int proposed,const Controller& c){return localMode.SelectSlot(command,proposed,c);};
  controller.combatCaptureInput=[](Command command,const Controller& c){localMode.CaptureInput(command,c);};
  controller.combatSelectRelease=[](Command command,int proposed,float seconds,const Controller& c){return localMode.SelectRelease(command,proposed,seconds,c);};
  controller.combatCanTransition=[](Command command,const Controller& c)->std::optional<bool>{
   const auto* rule=localMode.rules.Find(c.slot);if(!localMode.combatEnabled||!rule)return std::nullopt;
   for(const auto& branch:rule->timedInputs)if(branch.command==CommandName(command))return localMode.CanTransition(command,c);
   return std::nullopt;
  };
  controller.combatSelectCompletion=[](const Controller& c){const auto* rule=localMode.rules.Find(c.slot);return localMode.combatEnabled&&rule?rule->completionFollowupSlot:0;};
  InstallPracticeCallbacks();
  localResource=0;localAmmo=6;localMode.Reset(id);localMode.combatEnabled=false;lastSound.clear();soundCueCount=0;config.mode=id;
 }catch(...){
  delayedContactWindows=std::move(oldDelayedWindows);delayedRules=std::move(oldDelayedRules);delayedHits.Clear();delayedContact.Suspend();
  controller=std::move(oldController);gripTracks=std::move(oldGrip);groundTracks=std::move(oldGround);sourceWeaponTracks=std::move(oldWeapons);geometryContactMasks=std::move(oldGeometryContacts);supportGroundTracks=std::move(oldSupportGround);
  practice.windows=std::move(oldWindows);practice.authoredWindows=std::move(oldAuthoredWindows);fingerTracks=std::move(oldFingers);sourceFingerBones=std::move(oldFingerBones);soundClips=std::move(oldSounds);soundLabels=std::move(oldSoundLabels);soundTimeline=std::move(oldTimeline);themeTimeline=std::move(oldThemeTimeline);themeDefinitions=oldThemes;
  sourceRestPositions=oldRest;sourceRestRotations=oldRestRotations;sourceSoleHeight=oldSoleHeight;sourcePalmFrames=oldPalms;sourceRestReady=oldRestReady;sourcePalmsReady=oldPalmsReady;soundReady=oldSoundReady;
  activeAssetRoot=oldRoot;activeAudioRoot=oldAudioRoot;loadedMode=oldMode;configuredIdleSlot=oldIdle;config.keys=oldKeys;config.mode_keys=oldModeKeys;localMode=oldLocalMode;skinGeometry=std::move(oldSkinGeometry);skinMotions=std::move(oldSkinMotions);demoSequence=std::move(oldDemo);fullDemoSequence=std::move(oldFullDemo);motionLabels=std::move(oldLabels);projectileEvents=std::move(oldProjectiles);modeProps=std::move(oldProps);projectileProp=oldProjectileProp;soundDetail=oldSoundDetail;throw;
 }
}
void ReadAssets(){
 characterProfiles.clear();const auto profilesRoot=packageRoot/L"profiles";
 for(const auto& file:std::filesystem::recursive_directory_iterator(profilesRoot)){
  if(!file.is_regular_file()||file.path().extension()!=L".json")continue;
  std::ifstream input(file.path());Json selected;input>>selected;if(!selected.is_object()||!selected.contains("character_id")||!selected.contains("bones"))continue;
  const auto id=selected.at("character_id").get<std::string>();if(!id.starts_with("chr_")||id.size()>64)throw std::runtime_error("Invalid character profile ID");
  const auto axis=selected.value("practice_blade_axis",std::vector<float>{0,0,1});
  if(axis.size()!=3||!Finite(Vec3{axis[0],axis[1],axis[2]})||Retarget::Len({axis[0],axis[1],axis[2]})<1e-5f)throw std::runtime_error("Invalid practice blade axis");
  if(!characterProfiles.emplace(id,std::move(selected)).second)throw std::runtime_error("Duplicate character profile");
 }
 if(!characterProfiles.contains("chr_0003_endminf")||!characterProfiles.contains("chr_0016_laevat"))throw std::runtime_error("Reference character profiles missing");
 visualWeaponRegistry.clear();for(const auto& file:std::filesystem::recursive_directory_iterator(profilesRoot/L"weapons")){
  if(!file.is_regular_file()||file.path().extension()!=L".json")continue;std::ifstream input(file.path());Json value;input>>value;
  if(value.is_object()&&value.contains("asset_path_hash")&&value.contains("renderer_path"))visualWeaponRegistry[file.path().stem().string()]=std::move(value);
 }
 modeDefinitions.clear();const auto modesPath=packageRoot/L"assets/modes/index.json";
 if(std::filesystem::exists(modesPath)){std::ifstream input(modesPath);Json index;input>>index;
  if(!index.is_object()||!index.contains("modes")||!index["modes"].is_array()||index["modes"].size()>32)throw std::runtime_error("Invalid mode registry");
  for(const auto& item:index["modes"]){const auto id=item.at("id").get<std::string>();if(!modeDefinitions.emplace(id,item).second)throw std::runtime_error("Duplicate weapon mode");}}
 if(!modeDefinitions.contains("longsword"))modeDefinitions["longsword"]={{"id","longsword"},{"label","太刀"},{"data_root","assets"}};
 ReadMode(config.mode);
 const auto productPath=packageRoot/L"assets/product_catalog.json";
 if(std::filesystem::exists(productPath)){std::ifstream input(productPath);input>>productCatalog;}
 else productCatalog={{"schema",1},{"modes",Json::array()},{"characters",Json::array()},{"weapons",Json::array()}};
}
BE_Result BE_CALL Initialize(const BE_ThirdPartyHostV1* host,const char* raw) {
 if(!host||host->version!=1||host->struct_size<sizeof(BE_ThirdPartyHostV1)||!host->package_root||!host->reply)return BE_Result_InvalidArgument;
 std::lock_guard lock(gate);if(initialized||cleanupPending)return BE_Result_Conflict;
 try {bridge=*host;packageRoot=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(host->package_root)));Configure(raw);ReadAssets();}
 catch(const std::exception& e){if(host->log)host->log(host->context,e.what());bridge={};return BE_Result_InvalidArgument;}
 initialized=true;exitBootstrap=false;ready=false;phase="waiting";detail="等待游戏运行时";ResetKeys();resyncFocusKeys=true;
#ifdef MHE_MODULE_EXTENSION_HEADER
 moduleExtension.Configure();
#endif
 practiceHud.Start();
 bootstrap=std::thread([]{unsigned attempts=0;while(!exitBootstrap){
  {std::lock_guard lock(gate);if(!initialized)return;
   if(!api)api=bridge.runtime?bridge.runtime:(bridge.get_runtime?bridge.get_runtime(bridge.context):nullptr);
   if(api){ready=Resolve();if(ready){phase="ready";
#ifdef MHE_MODULE_EXTENSION_HEADER
    detail=ModuleExtension::ReadyPrompt();
#else
    detail="女管理员动作演示就绪，按 F8 开始";
#endif
    Log(detail);return;}
    if(++attempts==1){for(int i=0;i<MethodCount;++i)if(methods[i].required&&!methods[i].r.method_info)Log(std::string("Missing contract: ")+methods[i].d.class_name+"."+methods[i].d.method_name);}}}
  for(int n=0;n<10&&!exitBootstrap;++n)std::this_thread::sleep_for(std::chrono::milliseconds(100));
 }});return BE_Result_Ok;
}
BE_Result BE_CALL ConfigurationChanged(const char* raw) {std::lock_guard lock(gate);const auto previous=config;const auto revision=configRevision;const auto applied=appliedConfigRevision;const auto pending=pendingMode;
 try{Configure(raw);return BE_Result_Ok;}catch(...){config=previous;configRevision=revision;appliedConfigRevision=applied;pendingMode=pending;return BE_Result_InvalidArgument;}}
BE_Result BE_CALL Message(const char* request,const char* raw) {
 std::lock_guard lock(gate);if(!initialized)return BE_Result_NotReady;
 try {if(!raw||std::strlen(raw)>65536)throw std::runtime_error("Message too long");auto j=Json::parse(raw);auto action=j.at("action").get<std::string>();
  BE_Result result=BE_Result_Ok;
  if(action=="status"){}
  else if(action=="practice_reset"){practiceResetRequest=true;if(!controller.active){practice.Clear();practiceSummary=Json::array();practiceResetRequest=false;}}
  else if(action=="stop"){stopRequest=true;suppressInput=false;soundPlayer.Stop();practiceHud.Update(false,"");}
  else if(action=="catalog"){}
  else if(action=="set_mode"){const auto next=j.at("mode").get<std::string>();if(!modeDefinitions.contains(next))result=BE_Result_NotFound;
   else{const auto previous=config;const auto revision=configRevision,applied=appliedConfigRevision;const auto pending=pendingMode;
    try{config.mode=next;if((config.visual_weapon=="laevatain_blade"&&next!="longsword")||(config.visual_weapon=="typhoea_bow"&&next!="bow"))config.visual_weapon="auto";
     ++configRevision;if(actor.model)pendingMode=next;else{ReadMode(next);appliedConfigRevision=configRevision;}}
    catch(...){config=previous;configRevision=revision;appliedConfigRevision=applied;pendingMode=pending;throw;}}}
  else if(!ready)result=BE_Result_NotReady;
  else if(action=="target_select")lockRequest=true;
  else if(action=="target_focus")focusRequest=true;
  else if(action=="target_cycle")cycleRequest=true;
  else if(action=="target_release")targetReleaseRequest=true;
  else if(action=="practice_attack"){
   if(!controller.active||!config.local_practice||config.practice_target!="dummy"||autoDemo)result=BE_Result_NotReady;
   else{practiceAttackRequest=true;practiceAttackFromFront=j.value("from_front",true);practiceAttackGuardable=j.value("guardable",true);}
  }
  else if(action=="pause"){if(!controller.active)result=BE_Result_NotReady;else{paused=true;soundPlayer.Stop();commands.clear();detail="演示已暂停";}}
  else if(action=="resume"){if(!controller.active)result=BE_Result_NotReady;else{paused=false;hudEditing=false;resumeSoundLoops=true;previousBits=KeyBits();detail="演示继续";}}
  else if(action=="hud_edit"){hudEditing=j.value("enabled",!hudEditing);if(hudEditing){paused=true;soundPlayer.Stop();commands.clear();}else{paused=false;resumeSoundLoops=true;previousBits=KeyBits();}}
  else if(action=="toggle")toggleRequest=true;
  else if(action=="demo")demoRequest=true;
  else if(action=="command"){if(!controller.active)result=BE_Result_NotReady;else if(commands.size()<16)commands.push_back(ParseCommand(j.at("name").get<std::string>()));else result=BE_Result_Conflict;}
  else if(action=="preview"){auto slot=j.at("slot").get<int>();if(!controller.active)result=BE_Result_NotReady;else if(!controller.clips.contains(slot))result=BE_Result_NotFound;else previewRequest=slot;}
  else result=BE_Result_InvalidArgument;
  auto response=Status();response["request_id"]=j.value("request_id",std::string{});response["action"]=action;
  response["accepted"]=result==BE_Result_Ok;auto body=response.dump();return bridge.reply(bridge.context,request,result,body.c_str());
 }catch(const std::exception& e){auto body=Json{{"error",e.what()}}.dump();return bridge.reply(bridge.context,request,BE_Result_InvalidArgument,body.c_str());}
}
void BE_CALL Shutdown() {
#ifdef MHE_MODULE_EXTENSION_HEADER
 moduleExtension.Deactivate();
#endif
 exitBootstrap=true;
 {std::lock_guard lock(gate);suppressInput=false;initialized=false;ready=false;toggleRequest=stopRequest=demoRequest=false;autoDemo=false;
  soundPlayer.Stop();soundTimeline.Reset();practiceHud.Update(false,"");
  commands.clear();previewRequest=-1;controller.stop();cleanupPending=actor.model!=nullptr;
  // A retired module retains its lease until its resident Unity frame has
  // restored owned transforms. Releasing earlier could overlap another DLL.
  if(!cleanupPending)poseLease.Release();phase="stopped";}
 if(bootstrap.joinable())bootstrap.join();practiceHud.Stop();
 std::lock_guard lock(gate);
 // The resident frame node restores transforms on the next Unity frame, even when
 // the host has retired the module's input hook. No Unity access on this worker.
 if(inputHooked&&api&&api->release_module_hooks)api->release_module_hooks(api->context,ID);
 inputHooked=false;for(auto& s:shields)s.installed=false;bridge={};
}
const BE_ThirdPartyModuleV1 module{sizeof(BE_ThirdPartyModuleV1),1,ID,Initialize,ConfigurationChanged,Message,Shutdown};
}
extern "C" __declspec(dllexport) const BE_ThirdPartyModuleV1* BE_CALL BetterEndfield_GetThirdPartyModuleV1(){return &module;}
