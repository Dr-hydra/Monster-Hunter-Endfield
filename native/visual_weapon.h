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

// This helper only creates empty GameObjects, Transforms and one renderer.
// It never instantiates a gameplay/skill prefab or writes the source asset.
// All methods, including Reset, must execute on the Unity thread.
class ManagedVisualWeapon {
 const BE_HostApiV1* host_=nullptr;
 enum Method { LoadAsync,HandleGet,HandleDone,HandleError,HandleValid,HandleDispose,
  ResourceReady,ResourceBusy,GoCtor,GoTransform,GoComponent,GoAdd,GoActive,GoLayer,GoSetLayer,
  ComponentTransform,ComponentGo,ObjectName,ObjectAlive,ObjectDestroy,TransformChildCount,
  TransformGetChild,TransformParent,TransformSetParent,TransformPosition,TransformRotation,
  TransformLocalPosition,TransformSetLocalPosition,TransformLocalRotation,TransformSetLocalRotation,
  TransformLocalScale,TransformSetLocalScale,TransformInversePoint,RendererMesh,RendererSetMesh,
  RendererMaterials,RendererSetMaterials,RendererBones,RendererSetBones,RendererRoot,
  RendererSetRoot,RendererBounds,RendererSetBounds,RendererSetOffscreen,RendererSetEnabled,
  RendererLayers,RendererSetLayers,ArrayCreate,ArraySet,ArrayLength,ArrayGet,MethodCount };
 std::array<BE_ResolvedMethodV1,MethodCount> methods_{};
 BE_ResolvedClassV1 goClass_{},transformClass_{},rendererClass_{};
 struct Node {void* source=nullptr;void* clone=nullptr;std::string parent;};
 std::map<std::string,Node> nodes_;
 std::vector<uint32_t> roots_;
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
 int Count(void* array)const {int dim=0,n=0;void* a[]{&dim};return Value(ArrayLength,array,n,a)&&n>=0&&n<=4096?n:0;}
 void* Item(void* array,int i)const {void* a[]{&i};return Call(ArrayGet,array,a);}
 void* HandleSelf()const {return handleBox_?host_->object_unbox(host_->context,handleBox_):nullptr;}
 void DestroyVisual() {
  if(Alive(rootGo_))SetValue(GoActive,rootGo_,false);
  // Reverse creation order also collects a node that failed before parenting.
  for(auto it=createdObjects_.rbegin();it!=createdObjects_.rend();++it)if(Alive(*it)){SetValue(GoActive,*it,false);void* d[]{*it};Set(ObjectDestroy,nullptr,d);}
  createdObjects_.clear();
  rootGo_=rootTransform_=parent_=nullptr;ready_=visible_=false;nodes_.clear();
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
  auto sourceRoot=Call(GoTransform,prefab);if(!sourceRoot||!Walk(sourceRoot,"","",0))return Fail("独立刀资源层级无效");
  if(!nodes_.contains(rendererPath_)||!nodes_.contains(mountPath_)||!nodes_.contains(mountReferencePath_))return Fail("独立刀部件或原生握柄不存在");
  auto sourceRendererGo=Call(ComponentGo,nodes_.at(rendererPath_).source);void* componentArgs[]{rendererClass_.type_object};
  auto sourceRenderer=Call(GoComponent,sourceRendererGo,componentArgs);
  auto mesh=Call(RendererMesh,sourceRenderer);auto materials=Call(RendererMaterials,sourceRenderer);auto bones=Call(RendererBones,sourceRenderer);auto sourceRootBone=Call(RendererRoot,sourceRenderer);
  if(!mesh||Count(materials)!=1||Count(bones)!=1||!sourceRootBone)return Fail("独立刀网格/单骨/原生材质契约变化");
  std::string bonePath,rootBonePath;auto sourceBone=Item(bones,0);
  for(const auto& [path,node]:nodes_){if(node.source==sourceBone)bonePath=path;if(node.source==sourceRootBone)rootBonePath=path;}
  if(bonePath.empty()||rootBonePath.empty()||bonePath!=mountPath_||rootBonePath!=mountPath_)return Fail("独立刀自身刀骨契约变化，保留原装备");
  // The prefab root is the default installation frame. WeaponMountPoint 100
  // is a VFX custom point, so it must NOT imply a fabricated grip rotation.
  // A different installation reference requires explicit source evidence.
  Vec3 mountWorld,rootWorld,mountRelative;Quat mountWorldQ,rootWorldQ;
  auto sourceMount=nodes_.at(mountReferencePath_).source;
  if(!Value(TransformPosition,sourceMount,mountWorld)||!Value(TransformPosition,sourceRoot,rootWorld)||!Value(TransformRotation,sourceMount,mountWorldQ)||!Value(TransformRotation,sourceRoot,rootWorldQ))return Fail("独立刀握柄变换读取失败");
  void* inverseArgs[]{&mountWorld};if(!Value(TransformInversePoint,sourceRoot,mountRelative,inverseArgs))return Fail("独立刀握柄坐标转换失败");
  const auto actualQ=Inverse(Mul(Inverse(rootWorldQ),mountWorldQ));
  const auto actualP=Rotate(actualQ,{-mountRelative.x,-mountRelative.y,-mountRelative.z});
  const auto distance=[](Vec3 a,Vec3 b){return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));};
  if(distance(actualP,mountPosition_)>.002f||std::abs(Dot(actualQ,mountRotation_))<.9999f)return Fail("独立刀安装数据与当前客户端不符，保留原装备");
  std::set<std::string> needed{""};
  for(auto path:{rendererPath_,bonePath,rootBonePath})for(;;){needed.insert(path);if(path.empty())break;path=nodes_.at(path).parent;}
  rootGo_=CreateGo("MHE_Visual_LaevatainLongsword",sourceRoot);if(!rootGo_)return Fail("独立刀纯视觉对象创建失败");
  rootTransform_=Call(GoTransform,rootGo_);if(!rootTransform_)return Fail("独立刀根节点创建失败");nodes_.at("").clone=rootTransform_;
  bool keepWorld=false;void* rootParentArgs[]{socket,&keepWorld};
  if(!Set(TransformSetParent,rootTransform_,rootParentArgs)||!SetValue(TransformSetLocalPosition,rootTransform_,actualP)||!SetValue(TransformSetLocalRotation,rootTransform_,actualQ)||!SetValue(TransformSetLocalScale,rootTransform_,Vec3{1,1,1}))return Fail("独立刀挂点安装失败");
  for(const auto&path:needed){if(path.empty())continue;auto&node=nodes_.at(path);auto go=CreateGo(Name(node.source),node.source);if(!go)return Fail("独立刀子节点创建失败");
   node.clone=Call(GoTransform,go);auto parent=nodes_.at(node.parent).clone;void* a[]{parent,&keepWorld};if(!node.clone||!parent||!Set(TransformSetParent,node.clone,a)||!CopyTrs(node.source,node.clone))return Fail("独立刀自身骨架复制失败");
   // Objects remain under an inactive root while the palette is assembled.
   if(!SetValue(GoActive,go,true))return Fail("独立刀子节点激活失败");}
  auto rendererGo=Call(ComponentGo,nodes_.at(rendererPath_).clone);void* addArgs[]{rendererClass_.type_object};auto renderer=Call(GoAdd,rendererGo,addArgs);
  int one=1;void* arrayArgs[]{transformClass_.type_object,&one};auto palette=Call(ArrayCreate,nullptr,arrayArgs);if(!renderer||!palette||!Pin(palette))return Fail("独立刀渲染器或自身骨数组创建失败");
  int zero=0;void* setArray[]{nodes_.at(bonePath).clone,&zero};if(!Set(ArraySet,palette,setArray))return Fail("独立刀自身骨数组写入失败");
  void* meshArgs[]{mesh};void* materialArgs[]{materials};void* bonesArgs[]{palette};void* rootArgs[]{nodes_.at(rootBonePath).clone};
  Bounds bounds;if(!Value(RendererBounds,sourceRenderer,bounds)||!Finite(bounds.center)||!Finite(bounds.extents))return Fail("独立刀蒙皮包围盒无效");
  if(!Set(RendererSetMesh,renderer,meshArgs)||!Set(RendererSetMaterials,renderer,materialArgs)||!Set(RendererSetBones,renderer,bonesArgs)||!Set(RendererSetRoot,renderer,rootArgs)||!SetValue(RendererSetBounds,renderer,bounds)||!SetValue(RendererSetOffscreen,renderer,true)||!SetValue(RendererSetEnabled,renderer,true))return Fail("独立刀渲染参数设置失败");
  uint32_t layerMask=0;if(Value(RendererLayers,sourceRenderer,layerMask))SetValue(RendererSetLayers,renderer,layerMask);
  // The caller takes visibility ownership of original equipment first, then
  // enables this root. Never show both weapons on a failed visibility write.
  parent_=socket;ready_=true;visible_=false;status_="独立长刀已准备，等待原装备显隐接管";return true;
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
  Reset();host_=host;bound_=false;failed_=false;status_="等待独立刀运行时";
  if(!host_||!host_->resolve_method||!host_->resolve_class||!host_->runtime_invoke||!host_->object_new||!host_->string_new||!host_->object_unbox||!host_->gchandle_new||!host_->gchandle_free||!host_->copy_managed_string){status_="独立刀缺少宿主接口，保留原装备";return false;}
  try{
   const auto&hash=profile.at("asset_path_hash");assetHash_=hash.is_string()?std::stoull(hash.get<std::string>()):hash.get<uint64_t>();
   rendererPath_=profile.at("renderer_path").get<std::string>();mountPath_=profile.at("mount_bone_path").get<std::string>();mountReferencePath_=profile.value("mount_reference_path",std::string{});
   axis_=ReadVec(profile,"blade_root_axis",{0,1,0});edge_=ReadVec(profile,"edge_root_axis",{0,0,1});length_=profile.at("blade_length_m").get<float>();
   mountPosition_=ReadVec(profile,"mount_position",{});mountRotation_=ReadQuat(profile,"mount_rotation_xyzw");
   if(!assetHash_||rendererPath_.empty()||mountPath_.empty()||rendererPath_.find("..")!=std::string::npos||mountPath_.find("..")!=std::string::npos||!std::isfinite(length_)||length_<=0||length_>10)throw std::runtime_error("invalid visual weapon profile");
  }catch(const std::exception&){status_="独立刀配置无效，保留原装备";return false;}
  bound_=ResolveRuntime();if(!bound_)status_="独立刀所需方法未解析，保留原装备";return bound_;
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
  visible_=visible;status_=visible?"实验安装：莱万汀原生刀身/材质；完整火焰未接入":"独立长刀已准备，等待原装备显隐接管";return true;
 }
 bool Visible()const{return ready_&&visible_;}
 bool Ready()const{return ready_;}bool Available()const{return bound_&&!failed_;}
 void* RootTransform()const{return rootTransform_;}Vec3 BladeAxis()const{return axis_;}Vec3 EdgeAxis()const{return edge_;}
 float Length()const{return length_;}const std::string&Status()const{return status_;}
};
} // namespace LS::VisualWeapon
