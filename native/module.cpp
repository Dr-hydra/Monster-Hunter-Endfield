#include <BetterEndfield/ThirdPartyModule.h>
#include <Windows.h>
#include <nlohmann/json.hpp>
#include "core.h"
#include "retarget.h"
#include "weapon_pose.h"
#include "grip.h"
#include "grounding.h"
#include "practice.h"
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

namespace {
using Json=nlohmann::json;
using namespace LS;
constexpr char ID[]="endfield.longsword.showcase";
constexpr char FrameOwner[]="endfield.longsword.showcase.frame";
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
std::map<std::string,PcmClip> soundClips;
bool soundReady=false;
std::string soundDetail="等待音效资源";
std::string lastSound;
std::map<std::string,std::string> soundLabels;
uint64_t soundCueCount=0;
int configuredIdleSlot=1;
struct Configuration {
 bool enabled=true, root_motion=true, show_prop=true, sound_enabled=true, local_practice=false;
 int toggle_key=VK_F8, spirit_key=VK_XBUTTON2, spirit_alt_key=VK_CONTROL;
 float speed=1,blend_seconds=.12f,buffer_seconds=.3f,sound_volume=.65f,practice_hp=1000,practice_damage=100,grip_strength=1;
 float practice_radius=.65f,practice_height=.9f,practice_blade_length=1.25f;
} config;
bool toggleRequest=false, stopRequest=false, demoRequest=false, autoDemo=false;
size_t demoStep=0;float demoClock=0,demoDelay=.6f;
std::vector<Command> commands;
int previewRequest=-1;
struct Method { BE_MethodDescriptorV1 d; BE_ResolvedMethodV1 r{}; bool required=true; };
#define M(a,n,c,m,p,r,k) {{a,n,c,m,p,r,k}}
enum Index {MainCharacter,ModelComponent,ModelGo,Template,Valid,AliveEntity,Transform,Name,Alive,
 ChildCount,GetChild,Parent,LocalPosition,SetLocalPosition,Rotation,SetLocalRotation,LocalRotation,
 WorldMatrix,InverseVector,GetComponents,ArrayLength,ArrayValue,Bones,Mesh,Bindposes,
 Tail,CheckInput,ObjectDestroy,GoCtor,AddComponent,SetParent,LineCount,LinePoint,LineWidth,
 LineWorld,ShaderFind,MaterialCtor,RendererMaterial,MaterialColor,FrameCount,
 WeaponComponent,WeaponSetState,WeaponDisablePriority,WeaponPriorityActive,WeaponForceUpdate,
 WorldPosition,SetWorldPosition,MainCamera,AllEnemies,EntityPosition,EntityUid,EntityName,
 TerrainRaycast,TerrainSphereCast,WeaponViewPart,WeaponModel,FilterMesh,MeshBounds,MethodCount};
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
 M(Core,"UnityEngine","Mesh","get_bounds",nullptr,"UnityEngine.Bounds",0)
};
#undef M
static_assert(std::size(methods)==MethodCount);
BE_ResolvedClassV1 rendererClass{},gameObjectClass{},lineClass{},materialClass{};
BE_ResolvedClassV1 meshFilterClass{};
BE_ResolvedClassV1 grounderClass{};
BE_ResolvedFieldV1 grounderSolver{},groundLayers{};
BE_ResolvedFieldV1 templateId{};
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
struct Socket {Binding binding{};Vec3 handPosition{};Quat handRotation{};};
struct Actor {
 void* entity=nullptr;void* model=nullptr;void* root=nullptr;
 std::string character;
 std::vector<uint32_t> pins;
 std::vector<Binding> bones;
 std::vector<int> order;
 void* prop=nullptr;void* propMaterial=nullptr;float scale=1,entrySeconds=0;
 Quat sourceBasis{};bool gripAvailable=true;
 void* weapon=nullptr;int weaponHandle=-1;bool weaponHeld=false;
 std::vector<Socket> sockets;Binding weaponModel{};
 Vec3 bladeRootAxis{},bladeHandAxis{},edgeRootAxis{},modelInSocketPosition{};Quat modelInSocketRotation{};
 WeaponPose::SourceBlend weaponBlend{};WeaponPose::SourceFrame sourceWeaponFrame{};
 bool sourceWeaponActive=false;Quat gripFrameWorld{};
 std::array<bool,2> palmAligned{};
 float bladeLength=0,weaponRetry=0;bool bladeCalibrated=false;
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
std::array<Vec3,21> sourceRestPositions{};
bool sourceRestReady=false;
std::array<Quat,2> sourcePalmFrames{};
bool sourcePalmsReady=false;
std::map<int,GripTrack> gripTracks;
std::map<int,WeaponPose::SourceTrack> sourceWeaponTracks;
std::map<int,GroundTrack> groundTracks;
void* ReadObjectField(void* object,const char* name);
bool Pin(void* p);
void BindGroundQueries(){
 if(!grounderClass.type_object||!grounderSolver.field_info||!groundLayers.field_info)return;
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
   if(resolved&&Pin(delegate)){receiver=delegate;method=resolved;}
  };
  bind("Raycast",6,actor.groundRaycast,actor.groundRaycastMethod);
  bind("SphereCast",7,actor.groundSphereCast,actor.groundSphereCastMethod);break;
 }
}
Practice practice;
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
 for(auto& b:actor.bones)if(b.written&&IsAlive(b.transform)){
  Quat q;if(Value(LocalRotation,b.transform,q)&&Close(q,b.lastRotation))SetValue(SetLocalRotation,b.transform,b.previousRotation);
  if(b.positionWritten){Vec3 p;if(Value(LocalPosition,b.transform,p)&&Close(p,b.lastPosition))SetValue(SetLocalPosition,b.transform,b.previousPosition);}
  b.written=b.positionWritten=false;
 }
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
  const auto p=row.at("hand_position").get<std::vector<float>>(),q=row.at("hand_rotation_xyzw").get<std::vector<float>>();
  if(p.size()!=3||q.size()!=4)throw std::runtime_error("Invalid socket calibration");
  socket.handPosition={p[0],p[1],p[2]};socket.handRotation=Normalize({q[0],q[1],q[2],q[3]});
  if(!Finite(socket.handPosition)||!Finite(socket.handRotation)||Retarget::Len(socket.handPosition)>.25f)throw std::runtime_error("Invalid socket pose");
  if(socket.binding.parent&&Pin(socket.binding.transform)&&Pin(socket.binding.parent))actor.sockets.push_back(socket);
 }
 actor.weaponPoseDetail=actor.sockets.empty()?"未找到原生握刀节点":"原生握刀节点已绑定，等待剑身识别";
}
void CaptureWeaponModel() {
 using namespace Retarget;
 if(!actor.weaponHeld||actor.sockets.empty()||!methods[WeaponViewPart].r.method_info||!methods[WeaponModel].r.method_info)return;
 int index=0;void* part=nullptr;bool found=false;void* args[]{&index,&part};
 if(!Value(WeaponViewPart,actor.weapon,found,args)||!found||!part){actor.bladeCalibrated=false;actor.weaponModel={};return;}
 auto go=Call(WeaponModel,part);if(!IsAlive(go)||go==actor.model){actor.bladeCalibrated=false;actor.weaponModel={};return;}
 auto root=RootTransform(go);if(!IsAlive(root)){actor.bladeCalibrated=false;actor.weaponModel={};return;}
 if(actor.weaponModel.transform!=root){
  actor.weaponModel={};actor.bladeCalibrated=false;actor.bladeLength=0;actor.weaponRetry=0;
  auto parent=Call(Parent,root);if(!parent||!Pin(go)||!Pin(root)||!Pin(parent))return;
  actor.weaponModel.transform=root;actor.weaponModel.parent=parent;
 }
 Mat modelWorld,socketWorld;
 if(!Value(WorldMatrix,root,modelWorld)||!Value(WorldMatrix,actor.sockets.front().binding.transform,socketWorld))return;
 // This relation is captured after restoring our writes, before the source
 // pose. It preserves the weapon prefab's authored mount transform.
 const auto relative=Multiply(socketWorld.inverse(),modelWorld);
 actor.modelInSocketPosition=relative.point();actor.modelInSocketRotation=relative.rotation();
 if(Len(actor.modelInSocketPosition)>.4f){actor.bladeCalibrated=false;actor.weaponPoseDetail="武器未在右手挂点，保留原生位置";return;}
 if(!actor.bladeCalibrated&&actor.entrySeconds>=actor.weaponRetry){
  actor.weaponRetry=actor.entrySeconds+.5f;WeaponPose::AxisEvidence best;Vec3 bestEdge{};
  const auto inverse=modelWorld.inverse();Vec3 grip=inverse.point(socketWorld.point());
  for(const auto [type,meshMethod]:std::array<std::pair<void*,Index>,2>{{{meshFilterClass.type_object,FilterMesh},{rendererClass.type_object,Mesh}}}){
   if(!type||!methods[meshMethod].r.method_info||!methods[MeshBounds].r.method_info)continue;
   bool include=true;void* listArgs[]{type,&include};auto list=Call(GetComponents,go,listArgs);
   for(int i=0;i<Count(list);++i){auto component=Item(list,i);auto mesh=Call(meshMethod,component);WeaponPose::Bounds bounds;Mat world;
    if(!mesh||!Value(MeshBounds,mesh,bounds)||!Value(WorldMatrix,Call(Transform,component),world))continue;
    if(!Finite(bounds.center)||!Finite(bounds.extents)||bounds.extents.x<0||bounds.extents.y<0||bounds.extents.z<0)continue;
    const auto meshToRoot=Multiply(inverse,world);const std::array<float,3> e{bounds.extents.x,bounds.extents.y,bounds.extents.z};
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
 actor.sourceWeaponFrame=actor.weaponBlend.Sample(controller.motionSerial,controller.seconds,controller.transitionSeconds,track->second.Sample(controller.seconds));
 // This frame transports source hand separation ONLY. A source socket does
 // not establish the absent MHW weapon mounting matrix or blade direction.
 // Keep the anatomical hand retarget and native equipment mount untouched.
 actor.gripFrameWorld=Mul(Mul(modelRotation,actor.sourceBasis),actor.sourceWeaponFrame.rotation);
 actor.sourceWeaponActive=true;return true;
}
void ApplyWeaponSockets() {
 if(!actor.weaponHeld||actor.sockets.empty())return;
 Mat hand;if(!Value(WorldMatrix,actor.bones[12].transform,hand))return;auto handRotation=hand.rotation();
 for(auto& socket:actor.sockets){const auto p=hand.point(socket.handPosition);const auto q=Mul(handRotation,socket.handRotation);
  WriteWeaponBinding(socket.binding,p,q);}
 if(actor.bladeCalibrated&&IsAlive(actor.weaponModel.transform)){
  const auto& socket=actor.sockets.front();auto q=Mul(handRotation,socket.handRotation);
  const auto p=Retarget::Add(hand.point(socket.handPosition),Rotate(q,actor.modelInSocketPosition));
  WriteWeaponBinding(actor.weaponModel,p,Mul(q,actor.modelInSocketRotation));
 }
}
void Cleanup() {
 soundPlayer.Stop();soundTimeline.Reset();
 if(!practice.targets.empty())practiceSummary=PracticeTargets();
 practice.Suspend();practice.targets.clear();enemyRefresh=0;practiceDetail=config.local_practice?"演示结束，已保留本轮统计":"本地练习未开启";
 practiceHud.Update(false,"");
 suppressInput=false;autoDemo=false;demoStep=0;controller.stop();RestoreStageMotion();RestoreOwned();ReleaseWeapon();
 ReleaseCameraBinding();
 if(IsAlive(actor.prop)){void* args[]{actor.prop};Call(ObjectDestroy,nullptr,args);}
 if(IsAlive(actor.propMaterial)){void* args[]{actor.propMaterial};Call(ObjectDestroy,nullptr,args);}
 for(auto h:actor.pins)if(api&&api->gchandle_free)api->gchandle_free(api->context,h);
 actor=Actor{};cleanupPending=false;phase=initialized?"ready":"stopped";
 if(initialized)detail="演示已结束，F8 手动 / F9 自动开始";
}
void Collect(void* t,std::unordered_map<std::string,void*>& map,int depth=0) {
 if(!t||depth>64||map.size()>4096)return;map.emplace(ObjectName(t),t);int n=0;
 if(Value(ChildCount,t,n)&&n>=0&&n<1024)for(int i=0;i<n;++i){void* args[]{&i};Collect(Call(GetChild,t,args),map,depth+1);}
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

bool Bind() {
 Cleanup();practiceSummary=Json::array();practice.Clear();auto entity=Call(MainCharacter);bool valid=false,alive=false;
 if(!entity||!Value(Valid,entity,valid)||!valid||!Value(AliveEntity,entity,alive)||!alive){detail="当前角色尚未就绪";return false;}
 auto data=Call(Template,entity);auto id=data?api->field_get_value_object(api->context,templateId.field_info,data):nullptr;
 auto character=Text(id);if(character!="chr_0003_endminf"){detail="第一版仅适配女管理员，请切换主控角色";return false;}
 auto com=Call(ModelComponent,entity);auto model=com?Call(ModelGo,com):nullptr;
 if(!IsAlive(model)){detail="角色模型未就绪";return false;}
 actor.entity=entity;actor.model=model;actor.root=RootTransform(model);actor.character=character;
 if(!actor.root||!Pin(entity)||!Pin(model)||!Pin(actor.root)){Cleanup();detail="无法保留角色对象";return false;}
 std::unordered_map<std::string,void*> named;Collect(actor.root,named);
 std::unordered_map<void*,Mat> rest;
 auto type=rendererClass.type_object;bool include=true;void* args[]{type,&include};auto renderers=Call(GetComponents,model,args);
 Mat modelWorld;if(!Value(WorldMatrix,actor.root,modelWorld))throw std::runtime_error("Model matrix unavailable");auto modelInverse=modelWorld.inverse();
 for(int r=0;r<Count(renderers);++r){auto renderer=Item(renderers,r);auto mesh=Call(Mesh,renderer);auto transforms=Call(Bones,renderer);auto poses=mesh?Call(Bindposes,mesh):nullptr;
  int count=Count(transforms);if(count<30||count!=Count(poses))continue;
  Mat renderWorld;if(!Value(WorldMatrix,Call(Transform,renderer),renderWorld))continue;
  auto rendererInModel=Multiply(modelInverse,renderWorld);
  for(int j=0;j<count;++j){auto trans=Item(transforms,j);auto pose=Item(poses,j);auto raw=pose?api->object_unbox(api->context,pose):nullptr;if(!trans||!raw)continue;
   Mat bind;std::memcpy(&bind,raw,sizeof(bind));rest.emplace(trans,Multiply(rendererInModel,bind.inverse()));}
 }
 actor.bones.resize(21);
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
  // Source T-pose and target A-pose must agree before applying the world deltas.
  // Keep the target bone roll, and carry forearm correction to each hand/socket.
  const auto original=actor.bones;
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
 actor.scale=std::clamp((hip-foot)/1.0461f,.5f,2.f);
 BindGroundQueries();lastSound.clear();soundCueCount=0;
 BindWeaponSockets(named);
 if(!HoldEquippedWeapon())CreateProp();
 if(!controller.start(configuredIdleSlot))throw std::runtime_error("Idle clip unavailable");
 suppressInput=true;phase="active";detail="动作演示已接管输入";
 Log("Bound female administrator from mesh bindposes; bones=21");return true;
}

bool SkipGrip(){
 if(actor.gripAvailable)Log("Supporting-hand IK unavailable; continuing with the retargeted pose");
 actor.gripAvailable=false;return true;
}
bool ApplyGrip(float blend) {
 using namespace Retarget;
 auto track=gripTracks.find(controller.slot);if(!actor.gripAvailable||track==gripTracks.end()||config.grip_strength<=0)return true;
 auto frame=track->second.Sample(controller.seconds);
 if(actor.sourceWeaponActive)frame.weight=actor.sourceWeaponFrame.supportWeight;
 const float strength=frame.weight*config.grip_strength*blend*
  std::min(1.f,controller.seconds/std::max(.01f,config.blend_seconds));if(strength<.001f)return true;
 Vec3 shoulder,elbow,left,right;Quat rootRotation,wristRotation;
 if(!Value(WorldPosition,actor.bones[6].transform,shoulder)||!Value(WorldPosition,actor.bones[7].transform,elbow)||
  !Value(WorldPosition,actor.bones[8].transform,left)||!Value(WorldPosition,actor.bones[12].transform,right)||
  !Value(Rotation,actor.root,rootRotation)||!Value(Rotation,actor.bones[8].transform,wristRotation))return SkipGrip();
 if(!Finite(shoulder)||!Finite(elbow)||!Finite(left)||!Finite(right)||Len(Sub(elbow,shoulder))<1e-5f||Len(Sub(left,elbow))<1e-5f)return SkipGrip();
 const auto offset=actor.sourceWeaponActive?Rotate(actor.gripFrameWorld,Scale(actor.sourceWeaponFrame.supportOffset,actor.scale)):
  Rotate(Mul(rootRotation,actor.sourceBasis),Scale(frame.offset,actor.scale));
 auto goal=Lerp(left,Add(right,offset),strength);
 // A source close-hand interval only nudges the supporting arm; unreachable
 // targets are clamped without stretching bones or changing the dominant hand.
 const auto solved=SolveArm(shoulder,goal,elbow,Len(Sub(elbow,shoulder)),Len(Sub(left,elbow)),Rotate(rootRotation,{-1,0,-1}));
 const std::array<Quat,3> baseline{actor.bones[6].lastRotation,actor.bones[7].lastRotation,actor.bones[8].lastRotation};
 const auto rollbackGrip=[&](){for(int k=0;k<3;++k){auto& b=actor.bones[6+k];if(Close(b.lastRotation,baseline[k]))continue;
   Quat current;if(!Value(LocalRotation,b.transform,current))return false;
   if(Close(current,b.lastRotation)){if(!SetValue(SetLocalRotation,b.transform,baseline[k]))return false;b.lastRotation=baseline[k];}
   else b.written=false;}
  return SkipGrip();};
 const auto writeWorld=[&](int i,Quat world){auto& b=actor.bones[i];Quat parent;
  if(!Value(Rotation,b.parent,parent))return false;auto local=Mul(Inverse(parent),world);
  if(!SetValue(SetLocalRotation,b.transform,local))return false;b.lastRotation=local;return true;};
 Quat upper;if(!Value(Rotation,actor.bones[6].transform,upper)||
  !writeWorld(6,Mul(FromTo(Sub(elbow,shoulder),Sub(solved.elbow,shoulder)),upper)))return rollbackGrip();
 Vec3 movedElbow,movedHand;Quat lower;
 if(!Value(WorldPosition,actor.bones[7].transform,movedElbow)||!Value(WorldPosition,actor.bones[8].transform,movedHand)||
  !Value(Rotation,actor.bones[7].transform,lower))return rollbackGrip();
 if(!writeWorld(7,Mul(FromTo(Sub(movedHand,movedElbow),Sub(solved.hand,movedElbow)),lower)))return rollbackGrip();
 if(!writeWorld(8,wristRotation))return rollbackGrip();
 return true;
}

bool TerrainQuery(Vec3 origin,Vec3 direction,float distance,float radius,TerrainHit& hit){
 if(!actor.terrainMask||!Finite(origin)||!Finite(direction)||!std::isfinite(distance)||distance<=0)return false;
 const bool sphere=radius>0;void* receiver=sphere?actor.groundSphereCast:actor.groundRaycast;
 const auto method=sphere?actor.groundSphereCastMethod:actor.groundRaycastMethod;
 // Current game's extended RaycastHit: payload fields are point 0x00, normal
 // 0x0c and distance 0x1c (managed dump offsets exclude the 0x10 box header).
 alignas(16) unsigned char payload[128]{};int ignoreTriggers=1,mask=actor.terrainMask;
 void* rayArgs[]{&origin,&direction,payload,&distance,&mask,&ignoreTriggers};
 void* sphereArgs[]{&origin,&radius,&direction,payload,&distance,&mask,&ignoreTriggers};
 bool found=false;
 if(method){void* ex=nullptr;auto box=api->runtime_invoke(api->context,method,receiver,sphere?sphereArgs:rayArgs,&ex);
  auto raw=!ex&&box?api->object_unbox(api->context,box):nullptr;if(raw)std::memcpy(&found,raw,sizeof(found));}
 else Value(sphere?TerrainSphereCast:TerrainRaycast,nullptr,found,sphere?sphereArgs:rayArgs);
 if(!found)return false;
 std::memcpy(&hit.point,payload,12);std::memcpy(&hit.normal,payload+12,12);std::memcpy(&hit.distance,payload+28,4);
 return Finite(hit.point)&&Finite(hit.normal)&&std::isfinite(hit.distance)&&hit.distance>=0&&hit.distance<=distance+.01f;
}
bool GroundAt(Vec3 position,float expectedY,float& floor){
 TerrainHit hit;position.y=expectedY+.65f;
 if(!TerrainQuery(position,{0,-1,0},1.85f,0,hit)||!Walkable(hit,expectedY))return false;
 floor=hit.point.y;return true;
}
Vec3 GroundDisplay(Vec3 baseline,Vec3 offset,float blend){
 using namespace Retarget;
 std::array<Vec3,4> feet{};constexpr int indices[]{15,16,19,20};
 for(size_t i=0;i<feet.size();++i)if(!Value(WorldPosition,actor.bones[indices[i]].transform,feet[i]))return offset;
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
 std::array<float,4> floors{};
 for(size_t i=0;i<feet.size();++i){floors[i]=rootFloor;
  if(displacedHit)GroundAt(Add(feet[i],offset),rootFloor,floors[i]);}
 actor.groundCorrection=FloorCorrection(feet,floors,actor.soleOffset,actor.clearance);
 actor.groundFloor=rootFloor;
 offset.y=actor.groundCorrection*blend;
 actor.groundDetail=displacedHit?"脚底高度与场景地面已校准":"脚底高度已校准；场景地面查询不可用";
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
  if(index==0){Vec3 p;if(!Value(LocalPosition,b.transform,p))return false;auto hip=Rotate(actor.sourceBasis,controller.output.hip);
   // Move the display model root when possible so renderer bounds follow it.
   if(!config.root_motion||stage){hip.x=0;hip.z=0;}hip.x*=actor.scale;hip.y*=actor.scale;hip.z*=actor.scale;
   auto vector=Rotate(modelRotation,hip);Vec3 local;void* args[]{&vector};if(!Value(InverseVector,b.parent,local,args))return false;
   b.previousPosition=p;b.lastPosition=Lerp(p,Vec3{b.restLocal.x+local.x,b.restLocal.y+local.y,b.restLocal.z+local.z},blend);
   if(!SetValue(SetLocalPosition,b.transform,b.lastPosition))return false;b.positionWritten=true;
  }
 }
 if(stage){auto offset=Rotate(actor.sourceBasis,controller.output.hip);
  offset.x=config.root_motion?offset.x*actor.scale:0;offset.y=0;offset.z=config.root_motion?offset.z*actor.scale:0;
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
    baseline.y+(worldOffset.y+std::max(0.f,Rotate(actor.sourceBasis,controller.output.hip).y)*actor.scale)*.5f,baseline.z+worldOffset.z};
   actor.cameraWritten=SetValue(SetWorldPosition,actor.cameraTransform,actor.cameraLast);
  }
 }
 if(!UpdateSourceGrip(modelRotation)||!ApplyGrip(blend))return false;
 ApplyWeaponSockets();return true;
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
 std::vector<PracticeTarget> found;found.reserve(32);
 for(int i=0;i<count;++i){auto entity=Item(items,i);bool valid=false,alive=false;uint32_t uid=0;Vec3 position;
  if(!entity||entity==actor.entity||!Value(Valid,entity,valid)||!valid||!Value(AliveEntity,entity,alive)||!alive||
   !Value(EntityUid,entity,uid)||!uid||!Value(EntityPosition,entity,position)||!Finite(position)||Retarget::Len(Retarget::Sub(position,origin))>20)continue;
  auto name=Text(Call(EntityName,entity));if(name.empty())name="附近怪物";
  const float distance=Retarget::Len(Retarget::Sub(position,origin));
  position.y+=config.practice_height;found.push_back({uid,std::move(name),position,config.practice_radius,config.practice_hp,distance});
 }
 std::sort(found.begin(),found.end(),[](const auto& a,const auto& b){return a.distance==b.distance?a.id<b.id:a.distance<b.distance;});
 if(found.size()>128)found.resize(128);
 practice.Refresh(found,config.practice_hp);return true;
}
void TickPractice(float dt){
 if(practiceResetRequest){practiceResetRequest=false;practice.Clear();practiceSummary=Json::array();enemyRefresh=0;}
 if(!config.local_practice){practice.Suspend();practice.targets.clear();practiceDetail="本地练习未开启";return;}
 if(!practiceReady){practice.Suspend();practice.targets.clear();practiceDetail="怪物读取接口未匹配；动作可继续播放";return;}
 enemyRefresh-=std::clamp(dt,0.f,.1f);
 if(enemyRefresh<=0){enemyRefresh=.15f;
  if(!QueryPracticeTargets()){practice.Suspend();practice.targets.clear();practiceDetail="等待场景怪物数据";return;}}
 Vec3 grip;Quat rotation;
 if(!Value(WorldPosition,actor.bones[12].transform,grip)||!Value(Rotation,actor.bones[12].transform,rotation)){practice.Suspend();return;}
 // Only local practice volumes use this measured visual direction. No native
 // hit boxes, damage events or network requests are emitted here.
 const auto bladeAxis=actor.bladeCalibrated?actor.bladeHandAxis:practiceBladeAxis;
 if(actor.bladeCalibrated&&!actor.sockets.empty()){
  Mat hand;if(Value(WorldMatrix,actor.bones[12].transform,hand))grip=hand.point(actor.sockets.front().handPosition);}
 auto tip=Retarget::Add(grip,Rotate(rotation,Retarget::Scale(bladeAxis,config.practice_blade_length*actor.scale)));
 practice.Update(controller.motionSerial,controller.slot,controller.seconds,grip,tip,config.practice_damage);
 practiceDetail=practice.targets.empty()?"20 米内暂无可用怪物":"本地模拟中；不修改怪物真实血量";
}
std::string RoundedAmount(double value){char buffer[64]{};std::snprintf(buffer,sizeof(buffer),"%.0f",value);return buffer;}

uint32_t previousBits=0;bool previousToggle=false,previousEscape=false,previousDemo=false,resyncFocusKeys=false;int lastFrame=-1;
void ResetKeys() {previousBits=0;previousToggle=false;previousEscape=false;previousDemo=false;resyncFocusKeys=false;}
constexpr Command demoSequence[]{Command::Slash,Command::Slash,Command::Thrust,Command::Slash,
 Command::Spirit,Command::Spirit,Command::Spirit,Command::Spirit,
 Command::Fade,Command::Slash,Command::Foresight,
 Command::Spirit,Command::Spirit,Command::Spirit,Command::Spirit,
 Command::Sheathe,Command::Iai,Command::Slash,Command::Sheathe,Command::IaiSpirit,
 Command::Helm,Command::Sheathe,Command::NormalSheathe};
float DemoDelay(Command command,const Clip& clip,size_t nextStep) {
 if(command==Command::Sheathe)return std::min(clip.duration(),float(controller.sheatheHoldFrame)/clip.fps)+.8f;
 if(command==Command::Helm||nextStep==0)return clip.duration()+.7f;
 // Short clips must chain before the controller returns to idle. Otherwise
 // repeated Spirit inputs keep restarting the first segment and its sound.
 return std::min(clip.duration()*.90f+.18f,std::max(.01f,clip.duration()-.04f));
}
void TickDemo(float dt) {
 if(!autoDemo)return;demoClock+=dt;if(demoClock<demoDelay)return;
 const auto command=demoSequence[demoStep];controller.input(command);
 demoStep=(demoStep+1)%std::size(demoSequence);demoClock=0;
 const auto& clip=controller.clips.at(controller.slot);
 demoDelay=DemoDelay(command,clip,demoStep);
}
uint32_t KeyBits() {
 auto down=[](int k){return k>0&&(GetAsyncKeyState(k)&0x8000)!=0;};uint32_t bits=0;
 if(down(VK_LBUTTON))bits|=1;if(down(VK_RBUTTON))bits|=2;
 if(down(config.spirit_key)||down(config.spirit_alt_key))bits|=4;
 if(down(VK_SPACE))bits|=8;if(down(VK_SHIFT)||down('R'))bits|=16;return bits;
}
void Frame(float dt) {
 if(unityThread&&unityThread!=GetCurrentThreadId())return;unityThread=GetCurrentThreadId();
 int frame=-1;if(Value(FrameCount,nullptr,frame)&&frame==lastFrame)return;lastFrame=frame;
 soundPlayer.Poll();
 if(cleanupPending){Cleanup();ResetKeys();}
 if(!initialized||!ready)return;
 if(!Focused()||!config.enabled){if(actor.model)Cleanup();ResetKeys();commands.clear();previewRequest=-1;
  resyncFocusKeys=true;
  if(!config.enabled)toggleRequest=demoRequest=false;return;}
 bool f8=(GetAsyncKeyState(config.toggle_key)&0x8000)!=0;
 bool escape=(GetAsyncKeyState(VK_ESCAPE)&0x8000)!=0;
 bool f9=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
 if(resyncFocusKeys){previousToggle=f8;previousEscape=escape;previousDemo=f9;previousBits=KeyBits();resyncFocusKeys=false;}
 if(f8&&!previousToggle)toggleRequest=true;if(escape&&!previousEscape)stopRequest=true;
 if(f9&&!previousDemo)demoRequest=true;
 previousToggle=f8;previousEscape=escape;previousDemo=f9;
 if(stopRequest){stopRequest=false;toggleRequest=demoRequest=false;commands.clear();previewRequest=-1;Cleanup();return;}
 if(toggleRequest){toggleRequest=false;if(actor.model){Cleanup();return;}if(!Bind())return;previousBits=KeyBits();}
 if(demoRequest){demoRequest=false;
  if(!actor.model){if(!Bind())return;previousBits=KeyBits();}
  autoDemo=!autoDemo;demoStep=0;demoClock=0;demoDelay=.6f;
  if(autoDemo&&controller.idleSlot!=configuredIdleSlot)controller.start(configuredIdleSlot);
  detail=autoDemo?"自动演示中；攻击键切回手动，F8/Esc 结束":"已切回手动演示";
 }
 if(!actor.model)return;
 auto current=Call(MainCharacter);bool alive=false;
 if(current!=actor.entity||!IsAlive(actor.model)||!Value(AliveEntity,current,alive)||!alive){Cleanup();detail="角色切换或模型释放，演示已退出";return;}
 if(previewRequest>=0){autoDemo=false;controller.start(previewRequest);previewRequest=-1;}
 auto bits=KeyBits();const auto edges=bits&~previousBits;
 if(edges||!commands.empty()){autoDemo=false;detail="手动演示中，F9 可切换自动演示";}
 controller.inputBits(edges,bits);previousBits=bits;
 for(auto c:commands)controller.input(c);commands.clear();
 auto step=std::clamp(dt,0.f,.1f)*config.speed;float remaining=step;
 while(remaining>0){float sub=std::min(remaining,1.f/120);TickDemo(sub);controller.tick(sub);remaining-=sub;}
 actor.entrySeconds+=step;
 if(!controller.active){Cleanup();return;}
 if(!ApplyPose()){Cleanup();detail="姿势绑定失效，已恢复原生控制";return;}
 try{TickPractice(dt);}catch(const std::exception& e){practice.Suspend();practice.targets.clear();practiceDetail=std::string("怪物读取暂不可用：")+e.what();}
 practiceHud.Update(config.local_practice,"本地太刀练习 · 模拟血量\n"+practiceDetail+"\n命中 "+std::to_string(practice.hits)+" 次 · 伤害 "+RoundedAmount(practice.total)+
  (practice.lastTarget.empty()?"":"\n"+practice.lastTarget+" −"+RoundedAmount(practice.lastDamage)));
 soundTimeline.Update(controller.motionSerial,controller.slot,controller.seconds,
  config.sound_enabled&&config.sound_volume>0&&soundReady,[](const AudioCue& cue){
   const auto clip=soundClips.find(cue.clip);if(clip==soundClips.end())return;
   if(soundPlayer.Play(clip->second,config.sound_volume*cue.gain)){lastSound=cue.clip;++soundCueCount;soundDetail="动作音效就绪";}
   else {if(soundDetail!="音效播放失败；请检查音频设备")Log("Audio output unavailable; animation remains active");
    soundDetail="音效播放失败；请检查音频设备";}
  });
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
  if(api->resolve_method(api->context,&s.descriptor,&s.resolved)!=BE_Result_Ok||!s.resolved.method_pointer)return N>=6;
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
bool Resolve() {
 if(!api||!api->resolve_method||!api->resolve_class||!api->runtime_invoke||!api->object_unbox||
 !api->resolve_field||!api->field_get_value_object||!api->copy_managed_string||!api->gchandle_new||
 !api->gchandle_free||!api->object_new||!api->string_new||!api->create_hook)return false;
 bool ok=true;for(int i=0;i<MethodCount;++i){methods[i].required=i<ObjectDestroy||i==FrameCount;
  auto result=api->resolve_method(api->context,&methods[i].d,&methods[i].r);
  if(result!=BE_Result_Ok||!methods[i].r.method_info||!methods[i].r.method_pointer){if(methods[i].required)ok=false;}}
 // TailLateTick descriptors in BE use a wildcard parameter type on some builds.
 if(!methods[Tail].r.method_pointer){auto d=methods[Tail].d;d.parameter_types=nullptr;
  if(api->resolve_method(api->context,&d,&methods[Tail].r)==BE_Result_Ok) {
   ok=true;for(int i=0;i<ObjectDestroy;++i)if(!methods[i].r.method_info||!methods[i].r.method_pointer)ok=false;
  }}
 BE_FieldDescriptorV1 field{Game,"Beyond.Gameplay","GameDataWithId","id","System.String"};
 if(api->resolve_field(api->context,&field,&templateId)!=BE_Result_Ok)ok=false;
 if(api->resolve_class(api->context,Core,"UnityEngine","SkinnedMeshRenderer",&rendererClass)!=BE_Result_Ok||!rendererClass.type_object)ok=false;
 api->resolve_class(api->context,Core,"UnityEngine","GameObject",&gameObjectClass);
 api->resolve_class(api->context,Core,"UnityEngine","LineRenderer",&lineClass);
 api->resolve_class(api->context,Core,"UnityEngine","Material",&materialClass);
 api->resolve_class(api->context,Core,"UnityEngine","MeshFilter",&meshFilterClass);
 api->resolve_class(api->context,"RootMotion.dll","RootMotion.FinalIK","Grounder",&grounderClass);
 BE_FieldDescriptorV1 solverField{"RootMotion.dll","RootMotion.FinalIK","Grounder","solver","RootMotion.FinalIK.Grounding"};
 BE_FieldDescriptorV1 layerField{"RootMotion.dll","RootMotion.FinalIK","Grounding","layers","UnityEngine.LayerMask"};
 api->resolve_field(api->context,&solverField,&grounderSolver);api->resolve_field(api->context,&layerField,&groundLayers);
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
 return frameHooked&&inputHooked&&InstallShields(std::make_index_sequence<std::size(shields)>{});
}
float DemoDuration() {
 float result=.6f;int slot=configuredIdleSlot;size_t step=0;
 for(auto command:demoSequence){auto move=controller.moves.find(command);if(move==controller.moves.end()||move->second.empty())continue;
  const auto& sequence=move->second;auto current=std::find(sequence.begin(),sequence.end(),slot);
  slot=current!=sequence.end()&&std::next(current)!=sequence.end()?*std::next(current):sequence.front();
  auto clip=controller.clips.find(slot);if(clip!=controller.clips.end())result+=DemoDelay(command,clip->second,(++step)%std::size(demoSequence));
 }
 return result/std::max(.25f,config.speed);
}
Json PracticeTargets(){
 std::vector<const PracticeTarget*> sorted;for(const auto& [id,t]:practice.targets)sorted.push_back(&t);
 std::sort(sorted.begin(),sorted.end(),[](const auto* a,const auto* b){return a->distance==b->distance?a->id<b->id:a->distance<b->distance;});
 Json targets=Json::array();for(const auto* t:sorted){if(targets.size()>=12)break;
  targets.push_back({{"id",t->id},{"name",t->name},{"hp",t->hp},{"max_hp",config.practice_hp}});}
 return targets;
}
Json Status() {
 Json targets=PracticeTargets();if(!controller.active&&targets.empty())targets=practiceSummary;
 return Json{{"runtime_ready",ready},{"active",controller.active},{"slot",controller.slot},{"character",actor.character},{"detail",detail},{"phase",phase},{"auto_demo",autoDemo},{"demo_step",demoStep},{"demo_seconds",DemoDuration()},{"weapon_mode",actor.weaponHeld?"equipped":(actor.prop?"fallback":"none")},{"sound_ready",soundReady},{"sound_enabled",config.sound_enabled},{"sound_detail",!config.sound_enabled?"音效已关闭":(config.sound_volume==0?"音量为零":soundDetail)},
  {"last_sound",lastSound},{"last_sound_label",soundLabels.contains(lastSound)?soundLabels.at(lastSound):""},{"sound_cues",soundCueCount},{"ground_detail",actor.groundDetail},{"ground_correction",actor.groundCorrection},
  {"practice",{{"enabled",config.local_practice},{"ready",practiceReady},{"detail",practiceDetail},{"targets",targets},
   {"hits",practice.hits},{"total",practice.total},{"last_damage",practice.lastDamage},{"last_target",practice.lastTarget},{"summary",!controller.active}}},
  {"grip_ready",actor.gripAvailable},{"grip_detail",actor.gripAvailable?"辅助握刀可用":"辅助握刀不可用，已保留基础重定向"},
  {"blade_calibrated",actor.bladeCalibrated},{"weapon_pose_detail",actor.weaponPoseDetail},
  {"palm_aligned_left",actor.palmAligned[0]},{"palm_aligned_right",actor.palmAligned[1]}};
}
void Configure(const char* raw) {
 if(!raw||std::strlen(raw)>65536)throw std::runtime_error("Invalid configuration size");
 auto j=Json::parse(raw);if(!j.is_object())throw std::runtime_error("Configuration must be object");Configuration c;
 c.enabled=j.value("enabled",true);c.root_motion=j.value("root_motion",true);c.show_prop=j.value("show_prop",true);
 c.sound_enabled=j.value("sound_enabled",true);
 c.local_practice=j.value("local_practice",false);
 auto key=[&](const char* name,int defaultKey){auto k=j.value(name,defaultKey);if(k<1||k>255)throw std::runtime_error(name);return k;};
 c.toggle_key=key("toggle_key",VK_F8);c.spirit_key=key("spirit_key",VK_XBUTTON2);c.spirit_alt_key=key("spirit_alt_key",VK_CONTROL);
 auto number=[&](const char* name,float defaultValue,float min,float max){float n=j.value(name,defaultValue);if(!std::isfinite(n)||n<min||n>max)throw std::runtime_error(name);return n;};
 c.speed=number("speed",1,.25f,2);c.blend_seconds=number("blend_seconds",.12f,.02f,.5f);c.buffer_seconds=number("buffer_seconds",.3f,.08f,1);
 c.sound_volume=number("sound_volume",.65f,0,1);
 c.grip_strength=number("grip_strength",1,0,1);
 c.practice_hp=number("practice_hp",1000,1,1000000);c.practice_damage=number("practice_damage",100,1,100000);
 c.practice_radius=number("practice_radius",.65f,.05f,3);c.practice_height=number("practice_height",.9f,0,5);
 c.practice_blade_length=number("practice_blade_length",1.25f,.1f,3);
 if(c.local_practice!=config.local_practice||c.practice_hp!=config.practice_hp)practiceResetRequest=true;
 if(practiceResetRequest&&!controller.active){practice.Clear();practiceSummary=Json::array();practiceResetRequest=false;}
 if(c.practice_radius!=config.practice_radius||c.practice_height!=config.practice_height||c.practice_blade_length!=config.practice_blade_length){practice.Suspend();enemyRefresh=0;}
 if(!c.sound_enabled||c.sound_volume==0){soundPlayer.Stop();soundTimeline.Mute();}
 if(!c.enabled||c.show_prop!=config.show_prop)cleanupPending=actor.model!=nullptr;
 if(!c.enabled){suppressInput=false;soundPlayer.Stop();practiceHud.Update(false,"");}
 config=c;controller.transitionSeconds=c.blend_seconds;controller.bufferSeconds=c.buffer_seconds;
}
Command ParseCommand(const std::string& n) {
 if(n=="slash")return Command::Slash;if(n=="thrust")return Command::Thrust;if(n=="spirit")return Command::Spirit;
 if(n=="fade")return Command::Fade;if(n=="foresight")return Command::Foresight;if(n=="sheathe")return Command::Sheathe;
 if(n=="iai")return Command::Iai;if(n=="iai_spirit")return Command::IaiSpirit;if(n=="helm")return Command::Helm;
 if(n=="dodge")return Command::Dodge;if(n=="normal_sheathe")return Command::NormalSheathe;throw std::runtime_error("Unknown command");
}
void ReadAudio() {
 soundPlayer.Stop();soundTimeline.Reset();soundTimeline.cues.clear();soundClips.clear();soundLabels.clear();soundReady=false;
 try {
  const auto root=packageRoot/L"assets/audio";std::ifstream file(root/L"cues.json");
  if(!file)throw std::runtime_error("Audio cue manifest missing");Json manifest;file>>manifest;
  if(manifest.at("schema_version")!=1||!manifest.at("clips").is_array()||manifest["clips"].size()>64||!manifest.at("cues").is_object())
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
   if(consumed!=it.key().size()||!controller.clips.contains(slot)||!it.value().is_array()||it.value().size()>32)throw std::runtime_error("Invalid audio cue slot");
   auto& cues=soundTimeline.cues[slot];float previous=-1;
   for(const auto& item:it.value()){AudioCue cue{item.at("time").get<float>(),item.at("clip").get<std::string>(),item.value("gain",1.f)};
    if(!std::isfinite(cue.time)||cue.time<0||cue.time>controller.clips.at(slot).duration()||cue.time<previous||
     !std::isfinite(cue.gain)||cue.gain<0||cue.gain>2||!soundClips.contains(cue.clip))throw std::runtime_error("Invalid audio cue");
    previous=cue.time;cues.push_back(std::move(cue));++count;
   }
  }
  if(soundClips.empty()||count==0)throw std::runtime_error("Empty audio manifest");
  soundReady=true;soundDetail="动作音效就绪";
 }catch(const std::exception& e){soundClips.clear();soundTimeline.cues.clear();soundDetail=std::string("音效资源未就绪：")+e.what();}
}
void ReadAssets() {
 auto assets=packageRoot/L"assets";auto read=[](const std::filesystem::path& p){std::ifstream f(p);if(!f)throw std::runtime_error("Missing "+p.string());Json j;f>>j;return j;};
 auto catalog=read(assets/L"catalog.json");if(!catalog.is_array()||catalog.size()>512)throw std::runtime_error("Invalid catalog");
 controller.clips.clear();for(auto& entry:catalog){auto file=entry.at("file").get<std::string>();auto relative=std::filesystem::path(file);
  if(relative.has_parent_path()||relative.is_absolute()||relative.extension()!=L".lsm")throw std::runtime_error("Invalid clip path");
  auto clip=ReadClip(assets/relative,21);clip.slot=entry.at("slot").get<int>();controller.clips.emplace(clip.slot,std::move(clip));}
 profile=read(packageRoot/L"profiles/female_admin.json");if(profile.at("character_id")!="chr_0003_endminf")throw std::runtime_error("Unexpected character profile");
 const auto axis=profile.value("practice_blade_axis",std::vector<float>{0,0,1});
 if(axis.size()!=3||!Finite(Vec3{axis[0],axis[1],axis[2]})||Retarget::Len({axis[0],axis[1],axis[2]})<1e-5f)throw std::runtime_error("Invalid practice blade axis");
 practiceBladeAxis=Retarget::Normalize({axis[0],axis[1],axis[2]});
 auto source=read(assets/L"source_rig.json");sourceRestReady=false;
 for(int i=0;i<21;++i){const auto& p=source.at("bones").at(semantic[i]).at("position");
  sourceRestPositions[i]={p.at(0).get<float>(),p.at(1).get<float>(),p.at(2).get<float>()};
  if(!Finite(sourceRestPositions[i]))throw std::runtime_error("Invalid source rest position");}
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
 sourceWeaponTracks.clear();auto weapons=read(assets/L"source_weapon_tracks.json");
 if(weapons.at("schema_version")!=1||!weapons.at("tracks").is_object()||weapons["tracks"].size()>64)
  throw std::runtime_error("Invalid source weapon manifest");
 for(auto it=weapons["tracks"].begin();it!=weapons["tracks"].end();++it){size_t consumed=0;int slot=std::stoi(it.key(),&consumed);
  if(consumed!=it.key().size()||!controller.clips.contains(slot))throw std::runtime_error("Invalid source weapon slot");
  WeaponPose::SourceTrack track;track.fps=it.value().at("fps").get<float>();
  if(track.fps!=controller.clips.at(slot).fps||it.value().at("frames").size()!=controller.clips.at(slot).frames.size())
   throw std::runtime_error("Source weapon dimensions mismatch");
  for(const auto& row:it.value().at("frames")){const auto q=row.at("rotation_xyzw").get<std::vector<float>>(),p=row.at("support_offset").get<std::vector<float>>();
   if(q.size()!=4||p.size()!=3)throw std::runtime_error("Invalid source weapon frame");
   WeaponPose::SourceFrame frame{{q[0],q[1],q[2],q[3]},{p[0],p[1],p[2]},row.at("support_weight").get<float>()};
   if(!Finite(frame.rotation)||std::abs(Dot(frame.rotation,frame.rotation)-1)>.001f||!Finite(frame.supportOffset)||Retarget::Len(frame.supportOffset)>3||
     !std::isfinite(frame.supportWeight)||frame.supportWeight<0||frame.supportWeight>1)throw std::runtime_error("Invalid source weapon sample");
   track.frames.push_back(frame);}
  sourceWeaponTracks.emplace(slot,std::move(track));}
 groundTracks.clear();auto ground=read(assets/L"ground_tracks.json");
 if(ground.at("schema_version")!=1||!ground.at("tracks").is_object()||ground["tracks"].size()>512)throw std::runtime_error("Invalid ground manifest");
 for(auto it=ground["tracks"].begin();it!=ground["tracks"].end();++it){size_t consumed=0;int slot=std::stoi(it.key(),&consumed);
  if(consumed!=it.key().size()||!controller.clips.contains(slot))throw std::runtime_error("Invalid ground slot");
  GroundTrack track;track.fps=it.value().at("fps").get<float>();track.clearance=it.value().at("clearance").get<std::vector<float>>();
  const auto& clip=controller.clips.at(slot);
  if(track.fps!=clip.fps||track.clearance.size()!=clip.frames.size())throw std::runtime_error("Ground track dimensions mismatch");
  for(float value:track.clearance)if(!std::isfinite(value)||value<0||value>4)throw std::runtime_error("Invalid foot clearance");
  groundTracks.emplace(slot,std::move(track));}
 gripTracks.clear();auto grip=read(assets/L"grip_tracks.json");
 if(grip.at("schema_version")!=1||!grip.at("tracks").is_object()||grip.at("tracks").size()>64)throw std::runtime_error("Invalid grip manifest");
 for(auto it=grip["tracks"].begin();it!=grip["tracks"].end();++it){const int slot=std::stoi(it.key());auto path=std::filesystem::path(it.value().get<std::string>());
  if(path.has_parent_path()||path.is_absolute()||path.extension()!=L".lsg"||!controller.clips.contains(slot))throw std::runtime_error("Invalid grip path");
  gripTracks.emplace(slot,ReadGrip(assets/path,controller.clips.at(slot).frames.size()));}
 practice.Clear();practiceSummary=Json::array();practice.windows.clear();auto hit=read(assets/L"hit_windows.json");
 if(hit.at("schema_version")!=1||!hit.at("windows").is_object()||hit["windows"].size()>64)throw std::runtime_error("Invalid practice manifest");
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
 for(auto id:{controller.idleSlot,306})if(!controller.clips.contains(id))throw std::runtime_error("Required motion missing");
 ReadAudio();
}
BE_Result BE_CALL Initialize(const BE_ThirdPartyHostV1* host,const char* raw) {
 if(!host||host->version!=1||host->struct_size<sizeof(BE_ThirdPartyHostV1)||!host->package_root||!host->reply)return BE_Result_InvalidArgument;
 std::lock_guard lock(gate);if(initialized||cleanupPending)return BE_Result_Conflict;
 try {bridge=*host;packageRoot=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(host->package_root)));Configure(raw);ReadAssets();}
 catch(const std::exception& e){if(host->log)host->log(host->context,e.what());bridge={};return BE_Result_InvalidArgument;}
 initialized=true;exitBootstrap=false;ready=false;phase="waiting";detail="等待游戏运行时";ResetKeys();resyncFocusKeys=true;
 practiceHud.Start();
 bootstrap=std::thread([]{unsigned attempts=0;while(!exitBootstrap){
  {std::lock_guard lock(gate);if(!initialized)return;
   if(!api)api=bridge.runtime?bridge.runtime:(bridge.get_runtime?bridge.get_runtime(bridge.context):nullptr);
   if(api){ready=Resolve();if(ready){phase="ready";detail="女管理员动作演示就绪，按 F8 开始";Log(detail);return;}
    if(++attempts==1){for(int i=0;i<MethodCount;++i)if(methods[i].required&&!methods[i].r.method_info)Log(std::string("Missing contract: ")+methods[i].d.class_name+"."+methods[i].d.method_name);}}}
  for(int n=0;n<10&&!exitBootstrap;++n)std::this_thread::sleep_for(std::chrono::milliseconds(100));
 }});return BE_Result_Ok;
}
BE_Result BE_CALL ConfigurationChanged(const char* raw) {std::lock_guard lock(gate);try{Configure(raw);return BE_Result_Ok;}catch(...){return BE_Result_InvalidArgument;}}
BE_Result BE_CALL Message(const char* request,const char* raw) {
 std::lock_guard lock(gate);if(!initialized)return BE_Result_NotReady;
 try {if(!raw||std::strlen(raw)>65536)throw std::runtime_error("Message too long");auto j=Json::parse(raw);auto action=j.at("action").get<std::string>();
  BE_Result result=BE_Result_Ok;
  if(action=="status"){}
  else if(action=="practice_reset"){practiceResetRequest=true;if(!controller.active){practice.Clear();practiceSummary=Json::array();practiceResetRequest=false;}}
  else if(action=="stop"){stopRequest=true;suppressInput=false;soundPlayer.Stop();practiceHud.Update(false,"");}
  else if(!ready)result=BE_Result_NotReady;
  else if(action=="toggle")toggleRequest=true;
  else if(action=="demo")demoRequest=true;
  else if(action=="command"){if(!controller.active)result=BE_Result_NotReady;else if(commands.size()<16)commands.push_back(ParseCommand(j.at("name").get<std::string>()));else result=BE_Result_Conflict;}
  else if(action=="preview"){auto slot=j.at("slot").get<int>();if(!controller.active)result=BE_Result_NotReady;else if(!controller.clips.contains(slot))result=BE_Result_NotFound;else previewRequest=slot;}
  else result=BE_Result_InvalidArgument;
  auto body=Status().dump();return bridge.reply(bridge.context,request,result,body.c_str());
 }catch(const std::exception& e){auto body=Json{{"error",e.what()}}.dump();return bridge.reply(bridge.context,request,BE_Result_InvalidArgument,body.c_str());}
}
void BE_CALL Shutdown() {
 exitBootstrap=true;
 {std::lock_guard lock(gate);suppressInput=false;initialized=false;ready=false;toggleRequest=stopRequest=demoRequest=false;autoDemo=false;
  soundPlayer.Stop();soundTimeline.Reset();practiceHud.Update(false,"");
  commands.clear();previewRequest=-1;controller.stop();cleanupPending=actor.model!=nullptr;phase="stopped";}
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
