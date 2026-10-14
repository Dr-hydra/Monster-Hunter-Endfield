#pragma once
#include <BetterEndfield/ModuleApi.h>
#include <nlohmann/json.hpp>
#include "core.h"
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

namespace LS::VisualWeapon {
using Json=nlohmann::json;
struct Bounds {Vec3 center{},extents{};};

// This helper only creates empty GameObjects, Transforms and owned renderers.
// It never instantiates a gameplay/skill prefab or writes the source asset.
// All methods, including Reset, must execute on the Unity thread.
class ManagedVisualWeapon {
 const BE_HostApiV1* host_=nullptr;
 enum Method { LoadAsync,HandleGet,HandleDone,HandleError,HandleValid,HandleDispose,
  ResourceReady,ResourceBusy,GoCtor,GoTransform,GoComponent,GoAdd,GoActive,GoLayer,GoSetLayer,
  ComponentTransform,ComponentGo,ObjectName,ObjectAlive,ObjectDestroy,TransformChildCount,
  TransformGetChild,TransformParent,TransformSetParent,TransformPosition,TransformRotation,TransformSetPosition,TransformSetRotation,
  TransformLocalPosition,TransformSetLocalPosition,TransformLocalRotation,TransformSetLocalRotation,
  TransformLocalScale,TransformSetLocalScale,TransformInversePoint,RendererMesh,RendererSetMesh,
  RendererMaterials,RendererSetMaterials,RendererBones,RendererSetBones,RendererRoot,
  RendererSetRoot,RendererBounds,RendererSetBounds,RendererSetOffscreen,RendererSetEnabled,
  RendererLayers,RendererSetLayers,ArrayCreate,ArraySet,ArrayLength,ArrayGet,FilterSharedMesh,FilterSetSharedMesh,
  PropertyBlockCtor,PropertyFloat,RendererPropertyBlock,MethodCount };
 std::array<BE_ResolvedMethodV1,MethodCount> methods_{};
 BE_ResolvedClassV1 goClass_{},transformClass_{},rendererClass_{};
 BE_ResolvedClassV1 staticRendererClass_{},filterClass_{};
 BE_ResolvedClassV1 propertyBlockClass_{};
 struct Node {void* source=nullptr;void* clone=nullptr;std::string parent;Vec3 restPosition{};Quat restRotation{};};
 struct RenderPart {std::string path;void* source=nullptr;void* clone=nullptr;void* mesh=nullptr;void* propertyBlock=nullptr;std::vector<std::string> bonePaths;std::string rootPath;bool effect=false;bool skinned=true;};
 std::map<std::string,Node> nodes_;
 std::vector<uint32_t> roots_;
 std::vector<std::string> rendererPaths_;std::vector<RenderPart>parts_;std::string stringCenterPath_;
 Json controls_;
 Json rendererKinds_;
 void* handleBox_=nullptr;void* rootGo_=nullptr;void* rootTransform_=nullptr;void* parent_=nullptr;
 bool bound_=false,requested_=false,failed_=false,ready_=false,visible_=false;
 float waiting_=0;uint64_t assetHash_=0;
 std::string rendererPath_,mountPath_,mountReferencePath_,status_="独立刀未启用";
 std::vector<void*> createdObjects_;
 Vec3 axis_{0,1,0},edge_{0,0,1},mountPosition_{};Quat mountRotation_{};float length_=0;
 static constexpr const char* Core="UnityEngine.CoreModule.dll";
 bool Resolve(Method id,const char* assembly,const char* ns,const char* cls,const char* name,const char* parameters,const char* returns,uint32_t count) {
  BE_MethodDescriptorV1 d{assembly,ns,cls,name,parameters,returns,count};
  return host_->resolve_method(host_->context,&d,&methods_[id])==BE_Result_Ok&&methods_[id].method_info;
 }
 void* Call(Method id,void* self=nullptr,void** args=nullptr,bool* success=nullptr) const {
  if(success)*success=false;if(!host_||!methods_[id].method_info)return nullptr;
  void* ex=nullptr;auto value=host_->runtime_invoke(host_->context,methods_[id].method_info,self,args,&ex);
  if(success)*success=!ex;return ex?nullptr:value;
 }
 template<class T> bool Value(Method id,void* self,T& out,void** args=nullptr)const {
  bool ok=false;auto boxed=Call(id,self,args,&ok);if(!ok||!boxed)return false;
  auto raw=host_->object_unbox(host_->context,boxed);if(!raw)return false;std::memcpy(&out,raw,sizeof(T));return true;
 }
 bool Set(Method id,void* self,void** args=nullptr)const {bool ok=false;Call(id,self,args,&ok);return ok;}
 template<class T>bool SetValue(Method id,void* self,T value)const {void* a[]{&value};return Set(id,self,a);}
 bool Alive(void* object)const {bool ok=false;void* a[]{object};return object&&Value(ObjectAlive,nullptr,ok,a)&&ok;}
 bool Pin(void* object) {if(!object)return false;auto handle=host_->gchandle_new(host_->context,object,1);if(!handle)return false;roots_.push_back(handle);return true;}
 std::string Name(void* object)const {char text[512]{};auto s=Call(ObjectName,object);if(s)host_->copy_managed_string(host_->context,s,text,sizeof(text));return text;}
 int Count(void* array)const {if(!array)return 0;int dim=0,n=0;void* a[]{&dim};return Value(ArrayLength,array,n,a)&&n>=0&&n<=4096?n:0;}
 void* Item(void* array,int i)const {void* a[]{&i};return Call(ArrayGet,array,a);}
 void* HandleSelf()const {return handleBox_?host_->object_unbox(host_->context,handleBox_):nullptr;}
 void DestroyVisual() {
  if(Alive(rootGo_))SetValue(GoActive,rootGo_,false);
  // Reverse creation order also collects a node that failed before parenting.
  for(auto it=createdObjects_.rbegin();it!=createdObjects_.rend();++it)if(Alive(*it)){SetValue(GoActive,*it,false);void* d[]{*it};Set(ObjectDestroy,nullptr,d);}
  createdObjects_.clear();
  rootGo_=rootTransform_=parent_=nullptr;ready_=visible_=false;nodes_.clear();parts_.clear();
 }
 void ReleaseReferences() {
  // Destroy/deactivate renderer first: mesh/material dependency lease follows.
  if(auto self=HandleSelf())Set(HandleDispose,self);
  handleBox_=nullptr;for(auto r:roots_)host_->gchandle_free(host_->context,r);roots_.clear();requested_=false;
 }
 bool Fail(const char* reason) {DestroyVisual();ReleaseReferences();failed_=true;status_=reason;return false;}
 static Vec3 ReadVec(const Json& j,const char* key,Vec3 fallback) {
  if(!j.contains(key))return fallback;auto v=j.at(key).get<std::vector<float>>();if(v.size()!=3)throw std::runtime_error("visual weapon vector count");
  Vec3 result{v[0],v[1],v[2]};if(!Finite(result))throw std::runtime_error("visual weapon nonfinite vector");return result;
 }
 static Quat ReadQuat(const Json&j,const char*key) {
  auto v=j.at(key).get<std::vector<float>>();if(v.size()!=4)throw std::runtime_error("visual weapon quaternion count");return Normalize({v[0],v[1],v[2],v[3]});
 }
 bool Walk(void* source,const std::string& path,const std::string& parent,int depth) {
  if(!source||depth>64||nodes_.size()>512||nodes_.contains(path))return false;
  nodes_[path]={source,nullptr,parent};int count=0;if(!Value(TransformChildCount,source,count)||count<0||count>512)return false;
  for(int i=0;i<count;++i){void* a[]{&i};auto child=Call(TransformGetChild,source,a);auto name=Name(child);
   if(name.empty()||name.find('/')!=std::string::npos||!Walk(child,path.empty()?name:path+"/"+name,path,depth+1))return false;}
  return true;
 }
 void* CreateGo(const std::string&name,void* source=nullptr) {
  auto go=host_->object_new(host_->context,goClass_.class_info);if(!go)return nullptr;createdObjects_.push_back(go);if(!Pin(go))return nullptr;
  auto s=host_->string_new(host_->context,name.c_str());void* a[]{s};if(!s||!Set(GoCtor,go,a))return nullptr;
  bool active=false;if(!SetValue(GoActive,go,active))return nullptr;
  if(source){auto sourceGo=Call(ComponentGo,source);int layer=0;if(!Value(GoLayer,sourceGo,layer)||!SetValue(GoSetLayer,go,layer))return nullptr;}
  return go;
 }
 bool CopyTrs(void*source,void*target) {
  Vec3 p,s;Quat q;
  return Value(TransformLocalPosition,source,p)&&Value(TransformLocalRotation,source,q)&&Value(TransformLocalScale,source,s)&&Finite(p)&&Finite(q)&&Finite(s)&&
   SetValue(TransformSetLocalPosition,target,p)&&SetValue(TransformSetLocalRotation,target,q)&&SetValue(TransformSetLocalScale,target,s);
 }
 bool Build(void*prefab,void*socket) {
  auto sourceRoot=Call(GoTransform,prefab);if(!sourceRoot||!Walk(sourceRoot,"","",0))return Fail("独立武器资源层级无效");
  if(!nodes_.contains(mountPath_)||!nodes_.contains(mountReferencePath_))return Fail("独立武器原生安装参考不存在");
  std::set<std::string>needed{""};
  auto need=[&](std::string path){for(;;){needed.insert(path);if(path.empty())break;path=nodes_.at(path).parent;}};
  for(const auto&path:rendererPaths_){
   if(!nodes_.contains(path))return Fail("独立武器渲染部件不存在");
   bool skinned=rendererKinds_.value(path,std::string{"skinned"})!="static";
   auto sourceGo=Call(ComponentGo,nodes_.at(path).source);void*args[]{skinned?rendererClass_.type_object:staticRendererClass_.type_object};auto renderer=Call(GoComponent,sourceGo,args);
   void*mesh=nullptr;if(skinned)mesh=Call(RendererMesh,renderer);else{void*a[]{filterClass_.type_object};auto filter=Call(GoComponent,sourceGo,a);mesh=Call(FilterSharedMesh,filter);}
   auto materials=Call(RendererMaterials,renderer);auto bones=skinned?Call(RendererBones,renderer):nullptr;auto root=skinned?Call(RendererRoot,renderer):nullptr;
   int n=Count(bones);if(!mesh||Count(materials)<1||(skinned&&(n<1||n>256||!root)))return Fail("独立武器网格/骨架/材质契约变化");
   RenderPart part;part.path=path;part.source=renderer;part.mesh=mesh;part.skinned=skinned;part.effect=path.find("VFX")!=std::string::npos;
   for(int i=0;i<n;++i){auto bone=Item(bones,i);std::string bonePath;
    for(const auto&[p,node]:nodes_)if(node.source==bone){bonePath=p;break;}
    if(bonePath.empty())return Fail("独立武器骨数组含外部骨骼，拒绝借用角色骨架");part.bonePaths.push_back(bonePath);need(bonePath);}
   if(skinned){for(const auto&[p,node]:nodes_)if(node.source==root){part.rootPath=p;break;}
    if(part.rootPath.empty())return Fail("独立武器rootBone不在自身骨架");need(part.rootPath);}need(path);parts_.push_back(std::move(part));
  }
  if(parts_.empty())return Fail("独立武器没有渲染部件");
  if(!stringCenterPath_.empty()){if(!nodes_.contains(stringCenterPath_))return Fail("弓弦中心骨不存在");need(stringCenterPath_);}
  Vec3 mountWorld,rootWorld,mountRelative;Quat mountWorldQ,rootWorldQ;
  auto sourceMount=nodes_.at(mountReferencePath_).source;
  if(!Value(TransformPosition,sourceMount,mountWorld)||!Value(TransformPosition,sourceRoot,rootWorld)||!Value(TransformRotation,sourceMount,mountWorldQ)||!Value(TransformRotation,sourceRoot,rootWorldQ))return Fail("独立武器安装参考读取失败");
  void* inverseArgs[]{&mountWorld};if(!Value(TransformInversePoint,sourceRoot,mountRelative,inverseArgs))return Fail("独立武器安装坐标转换失败");
  const auto actualQ=Inverse(Mul(Inverse(rootWorldQ),mountWorldQ));
  const auto actualP=Rotate(actualQ,{-mountRelative.x,-mountRelative.y,-mountRelative.z});
  const auto distance=[](Vec3 a,Vec3 b){return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));};
  if(distance(actualP,mountPosition_)>.002f||std::abs(Dot(actualQ,mountRotation_))<.9999f)return Fail("独立武器安装数据与当前客户端不符");
  rootGo_=CreateGo("MHE_Visual_Weapon",sourceRoot);if(!rootGo_)return Fail("独立武器纯视觉对象创建失败");
  rootTransform_=Call(GoTransform,rootGo_);if(!rootTransform_)return Fail("独立武器根节点创建失败");nodes_.at("").clone=rootTransform_;
  bool keepWorld=false;void*rootParentArgs[]{socket,&keepWorld};
  if(!Set(TransformSetParent,rootTransform_,rootParentArgs)||!SetValue(TransformSetLocalPosition,rootTransform_,actualP)||!SetValue(TransformSetLocalRotation,rootTransform_,actualQ)||!SetValue(TransformSetLocalScale,rootTransform_,Vec3{1,1,1}))return Fail("独立武器挂点安装失败");
  for(const auto&path:needed){if(path.empty())continue;auto&node=nodes_.at(path);auto go=CreateGo(Name(node.source),node.source);if(!go)return Fail("独立武器子节点创建失败");
   node.clone=Call(GoTransform,go);auto parent=nodes_.at(node.parent).clone;void*a[]{parent,&keepWorld};
   if(!node.clone||!parent||!Set(TransformSetParent,node.clone,a)||!CopyTrs(node.source,node.clone)||!Value(TransformLocalPosition,node.source,node.restPosition)||!Value(TransformLocalRotation,node.source,node.restRotation))return Fail("独立武器自身骨架复制失败");
   if(!SetValue(GoActive,go,true))return Fail("独立武器子节点激活失败");}
  for(auto&part:parts_){
   auto rendererGo=Call(ComponentGo,nodes_.at(part.path).clone);void*addArgs[]{part.skinned?rendererClass_.type_object:staticRendererClass_.type_object};part.clone=Call(GoAdd,rendererGo,addArgs);
   auto materials=Call(RendererMaterials,part.source);void*materialArgs[]{materials};
   if(!part.skinned){void*a[]{filterClass_.type_object};auto filter=Call(GoAdd,rendererGo,a);void*meshArgs[]{part.mesh};
    if(!part.clone||!filter||!Set(FilterSetSharedMesh,filter,meshArgs)||!Set(RendererSetMaterials,part.clone,materialArgs)||!SetValue(RendererSetEnabled,part.clone,true))return Fail("独立武器静态渲染部件创建失败");continue;}
   int n=int(part.bonePaths.size());void*arrayArgs[]{transformClass_.type_object,&n};auto palette=Call(ArrayCreate,nullptr,arrayArgs);
   if(!part.clone||!palette||!Pin(palette))return Fail("独立武器渲染器或骨数组创建失败");
   for(int i=0;i<n;++i){void*a[]{nodes_.at(part.bonePaths[i]).clone,&i};if(!Set(ArraySet,palette,a))return Fail("独立武器骨数组写入失败");}
   void*meshArgs[]{part.mesh};void*bonesArgs[]{palette};void*rootArgs[]{nodes_.at(part.rootPath).clone};
   Bounds bounds;if(!Value(RendererBounds,part.source,bounds)||!Finite(bounds.center)||!Finite(bounds.extents))return Fail("独立武器蒙皮包围盒无效");
   if(!Set(RendererSetMesh,part.clone,meshArgs)||!Set(RendererSetMaterials,part.clone,materialArgs)||!Set(RendererSetBones,part.clone,bonesArgs)||!Set(RendererSetRoot,part.clone,rootArgs)||!SetValue(RendererSetBounds,part.clone,bounds)||!SetValue(RendererSetOffscreen,part.clone,true)||!SetValue(RendererSetEnabled,part.clone,true))return Fail("独立武器渲染参数设置失败");
   uint32_t layer=0;if(Value(RendererLayers,part.source,layer))SetValue(RendererSetLayers,part.clone,layer);
  }
  parent_=socket;ready_=true;visible_=false;status_="独立武器全部部件已准备，等待原装备显隐接管";return true;
 }
 bool ResolveRuntime() {
  bool ok=true;auto m=[&](Method id,const char*cls,const char*name,const char*p,const char*r,int n){ok=Resolve(id,Core,"UnityEngine",cls,name,p,r,n)&&ok;};
  auto r=[&](Method id,const char*cls,const char*name,const char*p,const char*ret,int n){ok=Resolve(id,"Common.Beyond.dll","Beyond.Resource",cls,name,p,ret,n)&&ok;};
  r(LoadAsync,"I18NAssetLoader","LoadAsync","Beyond.Resource.StringPathHash|System.Type|Beyond.Resource.RootCategory","Beyond.Resource.FAssetProxyHandle",3);
  r(HandleGet,"FAssetProxyHandle","Get",nullptr,"UnityEngine.Object",0);r(HandleDone,"FAssetProxyHandle","get_isDone",nullptr,"System.Boolean",0);
  r(HandleError,"FAssetProxyHandle","get_hasError",nullptr,"System.Boolean",0);r(HandleValid,"FAssetProxyHandle","IsValid",nullptr,"System.Boolean",0);r(HandleDispose,"FAssetProxyHandle","Dispose",nullptr,"System.Void",0);
  r(ResourceReady,"ResourceManager","IsInitialized",nullptr,"System.Boolean",0);r(ResourceBusy,"ResourceManager","IsAsyncIniting",nullptr,"System.Boolean",0);
  m(GoCtor,"GameObject",".ctor","System.String","System.Void",1);m(GoTransform,"GameObject","get_transform",nullptr,"UnityEngine.Transform",0);
  m(GoComponent,"GameObject","GetComponent","System.Type","UnityEngine.Component",1);m(GoAdd,"GameObject","AddComponent","System.Type","UnityEngine.Component",1);
  m(GoActive,"GameObject","SetActive","System.Boolean","System.Void",1);m(GoLayer,"GameObject","get_layer",nullptr,"System.Int32",0);m(GoSetLayer,"GameObject","set_layer","System.Int32","System.Void",1);
  m(ComponentTransform,"Component","get_transform",nullptr,"UnityEngine.Transform",0);m(ComponentGo,"Component","get_gameObject",nullptr,"UnityEngine.GameObject",0);
  m(ObjectName,"Object","get_name",nullptr,"System.String",0);m(ObjectAlive,"Object","op_Implicit","UnityEngine.Object","System.Boolean",1);m(ObjectDestroy,"Object","Destroy","UnityEngine.Object","System.Void",1);
  m(TransformChildCount,"Transform","get_childCount",nullptr,"System.Int32",0);m(TransformGetChild,"Transform","GetChild","System.Int32","UnityEngine.Transform",1);
  m(TransformParent,"Transform","get_parent",nullptr,"UnityEngine.Transform",0);m(TransformSetParent,"Transform","SetParent","UnityEngine.Transform|System.Boolean","System.Void",2);
  m(TransformPosition,"Transform","get_position",nullptr,"UnityEngine.Vector3",0);m(TransformRotation,"Transform","get_rotation",nullptr,"UnityEngine.Quaternion",0);
  m(TransformSetPosition,"Transform","set_position","UnityEngine.Vector3","System.Void",1);m(TransformSetRotation,"Transform","set_rotation","UnityEngine.Quaternion","System.Void",1);
  m(TransformLocalPosition,"Transform","get_localPosition",nullptr,"UnityEngine.Vector3",0);m(TransformSetLocalPosition,"Transform","set_localPosition","UnityEngine.Vector3","System.Void",1);
  m(TransformLocalRotation,"Transform","get_localRotation",nullptr,"UnityEngine.Quaternion",0);m(TransformSetLocalRotation,"Transform","set_localRotation","UnityEngine.Quaternion","System.Void",1);
  m(TransformLocalScale,"Transform","get_localScale",nullptr,"UnityEngine.Vector3",0);m(TransformSetLocalScale,"Transform","set_localScale","UnityEngine.Vector3","System.Void",1);
  m(TransformInversePoint,"Transform","InverseTransformPoint","UnityEngine.Vector3","UnityEngine.Vector3",1);
  m(RendererMesh,"SkinnedMeshRenderer","get_sharedMesh",nullptr,"UnityEngine.Mesh",0);m(RendererSetMesh,"SkinnedMeshRenderer","set_sharedMesh","UnityEngine.Mesh","System.Void",1);
  m(RendererMaterials,"Renderer","get_sharedMaterials",nullptr,"UnityEngine.Material[]",0);m(RendererSetMaterials,"Renderer","set_sharedMaterials","UnityEngine.Material[]","System.Void",1);
  m(RendererBones,"SkinnedMeshRenderer","get_bones",nullptr,"UnityEngine.Transform[]",0);m(RendererSetBones,"SkinnedMeshRenderer","set_bones","UnityEngine.Transform[]","System.Void",1);
  m(RendererRoot,"SkinnedMeshRenderer","get_rootBone",nullptr,"UnityEngine.Transform",0);m(RendererSetRoot,"SkinnedMeshRenderer","set_rootBone","UnityEngine.Transform","System.Void",1);
  m(RendererBounds,"SkinnedMeshRenderer","get_localBounds",nullptr,"UnityEngine.Bounds",0);m(RendererSetBounds,"SkinnedMeshRenderer","set_localBounds","UnityEngine.Bounds","System.Void",1);
  m(RendererSetOffscreen,"SkinnedMeshRenderer","set_updateWhenOffscreen","System.Boolean","System.Void",1);m(RendererSetEnabled,"Renderer","set_enabled","System.Boolean","System.Void",1);
  // Rendering-layer helpers are optional across Unity/client versions.
  Resolve(RendererLayers,Core,"UnityEngine","Renderer","get_renderingLayerMask",nullptr,"System.UInt32",0);Resolve(RendererSetLayers,Core,"UnityEngine","Renderer","set_renderingLayerMask","System.UInt32","System.Void",1);
  auto a=[&](Method id,const char*n,const char*p,const char*r,int c){ok=Resolve(id,"mscorlib.dll","System","Array",n,p,r,c)&&ok;};
  a(ArrayCreate,"CreateInstance","System.Type|System.Int32","System.Array",2);a(ArraySet,"SetValue","System.Object|System.Int32","System.Void",2);a(ArrayLength,"GetLength","System.Int32","System.Int32",1);a(ArrayGet,"GetValue","System.Int32","System.Object",1);
  for(auto [name,out]:std::array<std::pair<const char*,BE_ResolvedClassV1*>,3>{{{"GameObject",&goClass_},{"Transform",&transformClass_},{"SkinnedMeshRenderer",&rendererClass_}}})ok=host_->resolve_class(host_->context,Core,"UnityEngine",name,out)==BE_Result_Ok&&out->class_info&&out->type_object&&ok;
  return ok;
 }
public:
 ManagedVisualWeapon()=default;
 ManagedVisualWeapon(const ManagedVisualWeapon&)=delete;ManagedVisualWeapon&operator=(const ManagedVisualWeapon&)=delete;
 // An automatic destructor cannot safely invoke Unity from a worker thread.
 bool Bind(const BE_HostApiV1*host,const Json&profile) {
  Reset();host_=host;methods_={};bound_=false;failed_=false;status_="等待独立刀运行时";
  if(!host_||!host_->resolve_method||!host_->resolve_class||!host_->runtime_invoke||!host_->object_new||!host_->string_new||!host_->object_unbox||!host_->gchandle_new||!host_->gchandle_free||!host_->copy_managed_string){status_="独立刀缺少宿主接口，保留原装备";return false;}
  try{
   const auto&hash=profile.at("asset_path_hash");assetHash_=hash.is_string()?std::stoull(hash.get<std::string>()):hash.get<uint64_t>();
   rendererPath_=profile.at("renderer_path").get<std::string>();rendererPaths_=profile.value("renderer_paths",std::vector<std::string>{rendererPath_});rendererKinds_=profile.value("renderer_kinds",Json::object());controls_=profile.value("controls",Json::object());stringCenterPath_=controls_.value("string_center_path",std::string{});mountPath_=profile.at("mount_bone_path").get<std::string>();mountReferencePath_=profile.value("mount_reference_path",std::string{});
   axis_=ReadVec(profile,"blade_root_axis",{0,1,0});edge_=ReadVec(profile,"edge_root_axis",{0,0,1});length_=profile.at("blade_length_m").get<float>();
   mountPosition_=ReadVec(profile,"mount_position",{});mountRotation_=ReadQuat(profile,"mount_rotation_xyzw");
   if(rendererPaths_.empty()||rendererPaths_.size()>32)throw std::runtime_error("invalid renderer count");for(const auto&p:rendererPaths_)if(p.empty()||p.find("..")!=std::string::npos)throw std::runtime_error("invalid renderer path");
   if(!assetHash_||rendererPath_.empty()||mountPath_.empty()||rendererPath_.find("..")!=std::string::npos||mountPath_.find("..")!=std::string::npos||!std::isfinite(length_)||length_<=0||length_>10)throw std::runtime_error("invalid visual weapon profile");
  }catch(const std::exception&){status_="独立刀配置无效，保留原装备";return false;}
  bound_=ResolveRuntime();
  for(const auto&path:rendererPaths_)if(rendererKinds_.value(path,std::string{"skinned"})=="static"){
   bound_=Resolve(FilterSharedMesh,Core,"UnityEngine","MeshFilter","get_sharedMesh",nullptr,"UnityEngine.Mesh",0)&&bound_;
   bound_=Resolve(FilterSetSharedMesh,Core,"UnityEngine","MeshFilter","set_sharedMesh","UnityEngine.Mesh","System.Void",1)&&bound_;
   bound_=host_->resolve_class(host_->context,Core,"UnityEngine","MeshRenderer",&staticRendererClass_)==BE_Result_Ok&&staticRendererClass_.type_object&&bound_;
   bound_=host_->resolve_class(host_->context,Core,"UnityEngine","MeshFilter",&filterClass_)==BE_Result_Ok&&filterClass_.type_object&&bound_;break;
  }
  if(controls_.contains("material_effects")&&!controls_["material_effects"].empty()){
   bound_=Resolve(PropertyBlockCtor,Core,"UnityEngine","MaterialPropertyBlock",".ctor",nullptr,"System.Void",0)&&bound_;
   bound_=Resolve(PropertyFloat,Core,"UnityEngine","MaterialPropertyBlock","SetFloat","System.String|System.Single","System.Void",2)&&bound_;
   bound_=Resolve(RendererPropertyBlock,Core,"UnityEngine","Renderer","SetPropertyBlock","UnityEngine.MaterialPropertyBlock","System.Void",1)&&bound_;
   bound_=host_->resolve_class(host_->context,Core,"UnityEngine","MaterialPropertyBlock",&propertyBlockClass_)==BE_Result_Ok&&propertyBlockClass_.class_info&&bound_;
  }
  if(!bound_)status_="独立刀所需方法未解析，保留原装备";return bound_;
 }
 bool Tick(void*parentSocket,float dt) {
  if(!bound_||failed_)return false;
  if(!Alive(parentSocket)){if(requested_||ready_)Reset();status_="独立刀挂点已释放";return false;}
  if(ready_){if(!Alive(rootGo_)||!Alive(rootTransform_)||parent_!=parentSocket)return Fail("独立刀对象或挂点已变更");return true;}
  if(!requested_){bool initialized=false,busy=true;if(!Value(ResourceReady,nullptr,initialized)||!Value(ResourceBusy,nullptr,busy)||!initialized||busy){status_="等待客户端资源索引就绪";return false;}
   uint8_t category=0;void* a[]{&assetHash_,goClass_.type_object,&category};handleBox_=Call(LoadAsync,nullptr,a);if(!handleBox_||!Pin(handleBox_))return Fail("独立刀资源请求失败");
   requested_=true;waiting_=0;status_="正在异步加载莱万汀长刀";}
  bool valid=false,done=false,error=false;auto self=HandleSelf();
  if(!self||!Value(HandleValid,self,valid)||!valid||!Value(HandleError,self,error)||error||!Value(HandleDone,self,done))return Fail("独立刀资源句柄无效或加载错误");
  if(!done){if(std::isfinite(dt)&&dt>0)waiting_+=std::min(dt,.5f);if(waiting_>15)return Fail("独立刀加载超时，保留原装备");return false;}
  auto prefab=Call(HandleGet,self);if(!Alive(prefab)||!Pin(prefab))return Fail("独立刀资源完成但未取得prefab");
  return Build(prefab,parentSocket);
 }
 void Reset() {if(host_){DestroyVisual();ReleaseReferences();}waiting_=0;bound_=false;failed_=false;status_="独立刀已释放";}
 bool SetVisible(bool visible) {
  if(!ready_||!Alive(rootGo_)){visible_=false;return false;}
  if(!SetValue(GoActive,rootGo_,visible))return Fail("独立刀显隐失败，已释放视觉对象并保留原装备");
  visible_=visible;status_=visible?"原生独立武器部件已显示；运行外观待验证":"独立武器已准备，等待原装备显隐接管";return true;
 }
 // Retargeted contact points drive owned weapon bones. No wrist correction.
 struct PoseState {Vec3 drawPointWorld{};bool drawPointValid=false;float charge=0;bool released=false;bool fullEffects=true;float elapsed=0;};
 bool SetWorldPose(Vec3 point,Quat rotation){
  if(!ready_||!Finite(point)||!Finite(rotation))return false;
  // A resource-qualified mode adapter transports the owned graph root. The
  // original skill's dynamic GEN mount is neither modified nor inferred here.
  return SetValue(TransformSetPosition,rootTransform_,point)&&SetValue(TransformSetRotation,rootTransform_,Normalize(rotation));
 }
 bool UpdatePose(const PoseState&pose){
  if(!ready_)return false;
  for(auto&part:parts_)if(part.effect&&!SetValue(RendererSetEnabled,part.clone,pose.fullEffects))return Fail("独立武器效果显隐失败");
  if(controls_.contains("material_effects"))for(const auto&effect:controls_["material_effects"]){
   auto found=std::find_if(parts_.begin(),parts_.end(),[&](const auto&part){return part.path==effect.value("renderer_path",std::string{});});
   if(found==parts_.end())return Fail("材质效果部件未克隆");
   auto&part=*found;if(!part.propertyBlock){part.propertyBlock=host_->object_new(host_->context,propertyBlockClass_.class_info);if(!part.propertyBlock||!Pin(part.propertyBlock)||!Set(PropertyBlockCtor,part.propertyBlock))return Fail("私有材质属性块创建失败");}
   float duration=effect.value("start_duration",.6f),reference=effect.value("rest_value",1.f);float phase=duration>0?std::clamp(pose.elapsed/duration,0.f,1.f):1.f;
   float value=reference*phase*(pose.fullEffects?1.f:.35f);auto property=effect.value("property",std::string{});auto str=host_->string_new(host_->context,property.c_str());void*a[]{str,&value};void*block[]{part.propertyBlock};
   if(property.empty()||!str||!std::isfinite(value)||!Set(PropertyFloat,part.propertyBlock,a)||!Set(RendererPropertyBlock,part.clone,block))return Fail("私有材质视觉时间轴设置失败");
  }
  if(!stringCenterPath_.empty()){
   auto&n=nodes_.at(stringCenterPath_);Vec3 target=n.restPosition;
   if(pose.drawPointValid&&!pose.released&&pose.charge>0){
    if(!Finite(pose.drawPointWorld))return Fail("弓弦目标坐标无效");auto parent=nodes_.at(n.parent).clone;void*a[]{const_cast<Vec3*>(&pose.drawPointWorld)};
    if(!Value(TransformInversePoint,parent,target,a)||!Finite(target))return Fail("弓弦目标坐标转换失败");
   }
   if(!SetValue(TransformSetLocalPosition,n.clone,target))return Fail("弓弦自身骨驱动失败");
  }
  return true;
 }
 bool Visible()const{return ready_&&visible_;}
 bool Ready()const{return ready_;}bool Available()const{return bound_&&!failed_;}
 void* RootTransform()const{return rootTransform_;}Vec3 BladeAxis()const{return axis_;}Vec3 EdgeAxis()const{return edge_;}
 float Length()const{return length_;}const std::string&Status()const{return status_;}
};
// Original MHW prop geometry is built locally. This path creates no gameplay
// object and uses no resource-loader request. The input schema and mount proof
// are supplied by the offline model exporter rather than primitive substitutes.
class ManagedSourceWeapon {
 const BE_HostApiV1*host_=nullptr;
 enum Method {GoCtor,GoTr,GoAdd,GoActive,Parent,LocalP,LocalQ,LocalS,WorldP,WorldQ,AliveMethod,DestroyMethod,
  MeshCtor,MeshVerts,MeshNormals,MeshIndices,MeshBounds,FilterMesh,RenderMaterial,ShaderFind,MaterialCtor,MaterialColor,
  ArrayCreate,ArrayAddress,MeshUv,TextureCtor,TextureRaw,TextureApply,MaterialTexture,Count};
 std::array<BE_ResolvedMethodV1,Count>methods_{};
 BE_ResolvedClassV1 goClass_{},meshClass_{},filterClass_{},rendererClass_{},materialClass_{},vectorClass_{},intClass_{};
 BE_ResolvedClassV1 vector2Class_{},textureClass_{},byteClass_{};std::filesystem::path assetRoot_;
 std::vector<uint32_t>pins_;std::vector<void*>owned_;Json data_;
 void*root_=nullptr;void*transform_=nullptr;void*parent_=nullptr;
 void*dynamicMesh_=nullptr;void*dynamicVertices_=nullptr;uintptr_t dynamicAddress_=0;size_t vertexCount_=0;
 bool bound_=false,ready_=false,visible_=false;std::string status_="源武器未启用";
 static constexpr const char*Core="UnityEngine.CoreModule.dll";
 bool Resolve(Method id,const char*assembly,const char*ns,const char*cls,const char*name,const char*params,const char*result,uint32_t n){BE_MethodDescriptorV1 d{assembly,ns,cls,name,params,result,n};return host_->resolve_method(host_->context,&d,&methods_[id])==BE_Result_Ok&&methods_[id].method_info;}
 void*Call(Method id,void*self=nullptr,void**args=nullptr,bool*ok=nullptr)const{if(ok)*ok=false;if(!host_||!methods_[id].method_info)return nullptr;void*ex=nullptr;auto r=host_->runtime_invoke(host_->context,methods_[id].method_info,self,args,&ex);if(ok)*ok=!ex;return ex?nullptr:r;}
 bool Set(Method id,void*self,void**args=nullptr)const{bool ok=false;Call(id,self,args,&ok);return ok;}
 template<class T>bool SetValue(Method id,void*self,T value)const{void*a[]{&value};return Set(id,self,a);}
 template<class T>bool Value(Method id,void*self,T&value,void**args=nullptr)const{bool ok=false;auto b=Call(id,self,args,&ok);if(!b||!ok)return false;auto raw=host_->object_unbox(host_->context,b);if(!raw)return false;std::memcpy(&value,raw,sizeof(T));return true;}
 bool Alive(void*o)const{bool alive=false;void*a[]{o};return o&&Value(AliveMethod,nullptr,alive,a)&&alive;}
 bool Pin(void*o){if(!o)return false;auto h=host_->gchandle_new(host_->context,o,1);if(!h)return false;pins_.push_back(h);return true;}
 void*Own(const BE_ResolvedClassV1&cls){auto o=host_->object_new(host_->context,cls.class_info);if(o){owned_.push_back(o);if(!Pin(o))return nullptr;}return o;}
 bool Fail(const char*s){Reset();status_=s;return false;}
 template<class T>void*Array(const BE_ResolvedClassV1&cls,const std::vector<T>&values){
  int count=int(values.size());void*a[]{cls.type_object,&count};auto array=Call(ArrayCreate,nullptr,a);if(!array||!Pin(array))return nullptr;
  int index=0;void*addressArgs[]{array,&index};uintptr_t address=0;if(!Value(ArrayAddress,nullptr,address,addressArgs)||!address)return nullptr;
  std::memcpy(reinterpret_cast<void*>(address),values.data(),values.size()*sizeof(T));return array;
 }
 bool ResolveRuntime(){
  bool ok=true;auto m=[&](Method id,const char*c,const char*n,const char*p,const char*r,int count){ok=Resolve(id,Core,"UnityEngine",c,n,p,r,count)&&ok;};
  m(GoCtor,"GameObject",".ctor","System.String","System.Void",1);m(GoTr,"GameObject","get_transform",nullptr,"UnityEngine.Transform",0);m(GoAdd,"GameObject","AddComponent","System.Type","UnityEngine.Component",1);m(GoActive,"GameObject","SetActive","System.Boolean","System.Void",1);
  m(Parent,"Transform","SetParent","UnityEngine.Transform|System.Boolean","System.Void",2);m(LocalP,"Transform","set_localPosition","UnityEngine.Vector3","System.Void",1);m(LocalQ,"Transform","set_localRotation","UnityEngine.Quaternion","System.Void",1);m(LocalS,"Transform","set_localScale","UnityEngine.Vector3","System.Void",1);m(WorldP,"Transform","set_position","UnityEngine.Vector3","System.Void",1);m(WorldQ,"Transform","set_rotation","UnityEngine.Quaternion","System.Void",1);
  m(AliveMethod,"Object","op_Implicit","UnityEngine.Object","System.Boolean",1);m(DestroyMethod,"Object","Destroy","UnityEngine.Object","System.Void",1);
  m(MeshCtor,"Mesh",".ctor",nullptr,"System.Void",0);m(MeshVerts,"Mesh","set_vertices","UnityEngine.Vector3[]","System.Void",1);m(MeshNormals,"Mesh","set_normals","UnityEngine.Vector3[]","System.Void",1);m(MeshIndices,"Mesh","set_triangles","System.Int32[]","System.Void",1);m(MeshBounds,"Mesh","RecalculateBounds",nullptr,"System.Void",0);m(FilterMesh,"MeshFilter","set_sharedMesh","UnityEngine.Mesh","System.Void",1);m(RenderMaterial,"Renderer","set_sharedMaterial","UnityEngine.Material","System.Void",1);
  m(ShaderFind,"Shader","Find","System.String","UnityEngine.Shader",1);m(MaterialCtor,"Material",".ctor","UnityEngine.Shader","System.Void",1);
  m(MeshUv,"Mesh","set_uv","UnityEngine.Vector2[]","System.Void",1);
  m(TextureCtor,"Texture2D",".ctor","System.Int32|System.Int32|UnityEngine.TextureFormat|System.Boolean|System.Boolean","System.Void",5);
  m(TextureRaw,"Texture2D","LoadRawTextureData","System.Byte[]","System.Void",1);m(TextureApply,"Texture2D","Apply","System.Boolean|System.Boolean","System.Void",2);
  m(MaterialTexture,"Material","SetTexture","System.String|UnityEngine.Texture","System.Void",2);
  Resolve(MaterialColor,Core,"UnityEngine","Material","SetColor","System.String|UnityEngine.Color","System.Void",2);
  ok=Resolve(ArrayCreate,"mscorlib.dll","System","Array","CreateInstance","System.Type|System.Int32","System.Array",2)&&ok;
  ok=Resolve(ArrayAddress,"mscorlib.dll","System.Runtime.InteropServices","Marshal","UnsafeAddrOfPinnedArrayElement","System.Array|System.Int32","System.IntPtr",2)&&ok;
  for(auto[name,out]:std::array<std::pair<const char*,BE_ResolvedClassV1*>,6>{{{"GameObject",&goClass_},{"Mesh",&meshClass_},{"MeshFilter",&filterClass_},{"MeshRenderer",&rendererClass_},{"Material",&materialClass_},{"Vector3",&vectorClass_}}})ok=host_->resolve_class(host_->context,Core,"UnityEngine",name,out)==BE_Result_Ok&&out->class_info&&out->type_object&&ok;
  ok=host_->resolve_class(host_->context,"mscorlib.dll","System","Int32",&intClass_)==BE_Result_Ok&&intClass_.type_object&&ok;
  ok=host_->resolve_class(host_->context,"mscorlib.dll","System","Byte",&byteClass_)==BE_Result_Ok&&byteClass_.type_object&&ok;
  ok=host_->resolve_class(host_->context,Core,"UnityEngine","Vector2",&vector2Class_)==BE_Result_Ok&&vector2Class_.type_object&&ok;
  ok=host_->resolve_class(host_->context,Core,"UnityEngine","Texture2D",&textureClass_)==BE_Result_Ok&&textureClass_.class_info&&ok;return ok;
 }
 void*LoadTexture(const Json&description){
  if(assetRoot_.empty())return nullptr;std::filesystem::path relative=description.value("file",std::string{});
  if(relative.empty()||relative.is_absolute())return nullptr;for(const auto&part:relative)if(part=="..")return nullptr;
  int width=description.value("width",0),height=description.value("height",0);if(width<1||height<1||width>2048||height>2048)return nullptr;
  std::ifstream file(assetRoot_/relative,std::ios::binary);if(!file)return nullptr;std::vector<uint8_t>bytes((std::istreambuf_iterator<char>(file)),{});
  if(bytes.size()!=size_t(width)*size_t(height)*4)return nullptr;
  auto texture=Own(textureClass_);int format=4;bool mipmaps=false,linear=description.value("color_space",std::string{"sRGB"})!="sRGB";void*ctor[]{&width,&height,&format,&mipmaps,&linear};
  if(!texture||!Set(TextureCtor,texture,ctor))return nullptr;auto array=Array(byteClass_,bytes);void*data[]{array};bool makeNonReadable=true;void*apply[]{&mipmaps,&makeNonReadable};
  return array&&Set(TextureRaw,texture,data)&&Set(TextureApply,texture,apply)?texture:nullptr;
 }
 bool Build(void*socket){
  std::vector<Vec3>vertices,normals;std::vector<int>indices;
  try{for(const auto&v:data_.at("vertices")){if(!v.is_array()||v.size()!=3)return Fail("源网格顶点格式无效");Vec3 p{v[0].get<float>(),v[1].get<float>(),v[2].get<float>()};if(!Finite(p))return Fail("源网格顶点非有限");vertices.push_back(p);}for(const auto&i:data_.at("indices")){int index=i.get<int>();if(index<0||index>=int(vertices.size()))return Fail("源网格索引越界");indices.push_back(index);}for(const auto&v:data_.value("normals",Json::array())){if(v.size()!=3)return Fail("源法线格式无效");Vec3 p{v[0].get<float>(),v[1].get<float>(),v[2].get<float>()};if(!Finite(p))return Fail("源法线非有限");normals.push_back(p);}}
  catch(...){return Fail("源武器几何读取失败");}
  if(vertices.size()<3||vertices.size()>65535||indices.empty()||indices.size()%3||(!normals.empty()&&normals.size()!=vertices.size()))return Fail("源武器几何数量无效");
  root_=Own(goClass_);auto name=host_->string_new(host_->context,("MHE_Source_"+data_.value("id",std::string{"Weapon"})).c_str());void*goArgs[]{name};if(!root_||!name||!Set(GoCtor,root_,goArgs)||!SetValue(GoActive,root_,false))return Fail("源武器对象创建失败");
  transform_=Call(GoTr,root_);bool keep=false;void*parentArgs[]{socket,&keep};if(!transform_||!Set(Parent,transform_,parentArgs)||!SetValue(LocalP,transform_,Vec3{})||!SetValue(LocalQ,transform_,Quat{})||!SetValue(LocalS,transform_,Vec3{1,1,1}))return Fail("源武器挂接失败");
  auto mesh=Own(meshClass_);auto vArray=Array(vectorClass_,vertices);auto iArray=Array(intClass_,indices);void*va[]{vArray};void*ia[]{iArray};if(!mesh||!Set(MeshCtor,mesh)||!vArray||!iArray||!Set(MeshVerts,mesh,va)||!Set(MeshIndices,mesh,ia))return Fail("源武器网格缓冲创建失败");
  dynamicMesh_=mesh;dynamicVertices_=vArray;vertexCount_=vertices.size();int addressIndex=0;void*addressArgs[]{vArray,&addressIndex};
  if(!Value(ArrayAddress,nullptr,dynamicAddress_,addressArgs)||!dynamicAddress_)return Fail("源武器动态顶点地址不可用");
  if(!normals.empty()){auto array=Array(vectorClass_,normals);void*a[]{array};if(!array||!Set(MeshNormals,mesh,a))return Fail("源武器法线设置失败");}if(!Set(MeshBounds,mesh))return Fail("源武器包围盒设置失败");
  if(data_.contains("uv")){
   std::vector<std::array<float,2>>uv;for(const auto&v:data_["uv"]){if(v.size()!=2)return Fail("源UV格式无效");float u=v[0].get<float>(),w=v[1].get<float>();if(!std::isfinite(u)||!std::isfinite(w))return Fail("源UV非有限");uv.push_back({u,w});}if(uv.size()!=vertices.size())return Fail("源UV/顶点数不一致");auto array=Array(vector2Class_,uv);void*a[]{array};if(!array||!Set(MeshUv,mesh,a))return Fail("源UV设置失败");
  }
  void*shader=nullptr;for(const auto*n:{"Unlit/Texture","Universal Render Pipeline/Unlit","Sprites/Default","Unlit/Color","Hidden/Internal-Colored","Standard"}){auto text=host_->string_new(host_->context,n);void*a[]{text};shader=Call(ShaderFind,nullptr,a);if(Alive(shader))break;}if(!Alive(shader))return Fail("客户端缺少源武器展示shader");
  auto material=Own(materialClass_);void*materialArgs[]{shader};if(!material||!Set(MaterialCtor,material,materialArgs))return Fail("源武器私有材质创建失败");
  if(methods_[MaterialColor].method_info){std::array<float,4>color{.73f,.76f,.8f,1};for(const auto*n:{"_Color","_BaseColor"}){auto text=host_->string_new(host_->context,n);void*a[]{text,&color};Set(MaterialColor,material,a);}}
  bool textured=false;
  if(data_.contains("materials"))for(const auto&m:data_["materials"]){
   if(!m.contains("runtime_textures")||!m["runtime_textures"].contains("albedo"))continue;
   auto texture=LoadTexture(m["runtime_textures"]["albedo"]);if(!texture)return Fail("MRL3原贴图缺失或上传失败");
   for(const auto*n:{"_MainTex","_BaseMap"}){auto text=host_->string_new(host_->context,n);void*a[]{text,texture};if(!Set(MaterialTexture,material,a))return Fail("原贴图材质设置失败");}textured=true;break;
  }
  void*filterArgs[]{filterClass_.type_object};void*rendererArgs[]{rendererClass_.type_object};auto filter=Call(GoAdd,root_,filterArgs);auto renderer=Call(GoAdd,root_,rendererArgs);void*meshArgs[]{mesh};void*matArgs[]{material};if(!filter||!renderer||!Set(FilterMesh,filter,meshArgs)||!Set(RenderMaterial,renderer,matArgs))return Fail("源武器渲染器创建失败");
  parent_=socket;ready_=true;visible_=false;status_=textured?"原网格/原UV/MRL3贴图已准备；安装待核对":"源网格已准备；原安装/材质仍需核对";return true;
 }
public:
 // The owner must call Reset on the Unity thread before releasing the host.
 // A DLL/worker-thread automatic destructor must never invoke Unity objects.
 ManagedSourceWeapon()=default;ManagedSourceWeapon(const ManagedSourceWeapon&)=delete;ManagedSourceWeapon&operator=(const ManagedSourceWeapon&)=delete;
 bool Bind(const BE_HostApiV1*host,const Json&prop,const std::filesystem::path&assetRoot={}){Reset();host_=host;methods_={};data_=prop;assetRoot_=assetRoot;if(!host_||!host_->resolve_method||!host_->resolve_class||!host_->runtime_invoke||!host_->object_new||!host_->string_new||!host_->object_unbox||!host_->gchandle_new||!host_->gchandle_free)return false;bound_=ResolveRuntime();if(!bound_)status_="源网格SDK方法不可用，保留原装备";return bound_;}
 bool Tick(void*socket,float){if(!bound_)return false;if(!Alive(socket))return Fail("源武器挂点失效");if(ready_)return parent_==socket&&Alive(root_)?true:Fail("源武器身份变化");return Build(socket);}
 bool SetVisible(bool value){if(!ready_)return false;if(!SetValue(GoActive,root_,value))return Fail("源武器显隐失败，已释放");visible_=value;return true;}
 bool SetWorldPose(Vec3 point,Quat rotation){if(!ready_||!Finite(point)||!Finite(rotation))return false;return SetValue(WorldP,transform_,point)&&SetValue(WorldQ,transform_,Normalize(rotation));}
 bool UpdateSkinPositions(const std::vector<Vec3>& positions){
  if(!ready_||!dynamicMesh_||!dynamicVertices_||!dynamicAddress_||positions.size()!=vertexCount_)return false;
  for(auto p:positions)if(!Finite(p))return Fail("原武器自身动画含无效顶点");
  std::memcpy(reinterpret_cast<void*>(dynamicAddress_),positions.data(),positions.size()*sizeof(Vec3));void*a[]{dynamicVertices_};
  return Set(MeshVerts,dynamicMesh_,a)&&Set(MeshBounds,dynamicMesh_);
 }
 void Reset(){if(host_){if(Alive(root_))SetValue(GoActive,root_,false);for(auto i=owned_.rbegin();i!=owned_.rend();++i)if(Alive(*i)){void*a[]{*i};Set(DestroyMethod,nullptr,a);}for(auto h:pins_)host_->gchandle_free(host_->context,h);}owned_.clear();pins_.clear();root_=transform_=parent_=dynamicMesh_=dynamicVertices_=nullptr;dynamicAddress_=0;vertexCount_=0;ready_=visible_=bound_=false;data_=Json{};status_="源武器已释放";}
 bool Ready()const{return ready_;}bool Available()const{return bound_;}bool Visible()const{return ready_&&visible_;}void*RootTransform()const{return transform_;}const std::string&Status()const{return status_;}
};
} // namespace LS::VisualWeapon
