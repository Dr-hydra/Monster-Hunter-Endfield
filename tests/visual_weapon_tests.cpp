#include "../native/visual_weapon.h"
#include <iostream>
#include <memory>
#include <fstream>
#include <cstring>
using namespace LS;
using namespace LS::VisualWeapon;
namespace {
void Check(bool value){if(!value)throw std::runtime_error("mock contract violation");}
struct Object {
 std::string kind,name;bool alive=true,active=true,created=false;int layer=8;
 Object*transform=nullptr;Object*go=nullptr;Object*parent=nullptr;std::vector<Object*>children,items;
 Vec3 p{},s{1,1,1};Quat q{};Object*renderer=nullptr;Object*mesh=nullptr;Object*materials=nullptr;Object*bones=nullptr;Object*rootBone=nullptr;
 Object*propertyBlock=nullptr;std::map<std::string,float>properties;
 Bounds bounds{{0,.695f,0},{.108f,1.374f,.407f}};
 alignas(16)std::array<unsigned char,64> bytes{};
};
struct Descriptor {std::string cls,name;};
struct Mock {
 BE_HostApiV1 host{};std::vector<std::unique_ptr<Object>>objects;std::vector<std::unique_ptr<Descriptor>>methods;
 std::map<uint32_t,Object*>pins;uint32_t pinId=0;Object*prefab=nullptr;Object*socket=nullptr;
 int loads=0,disposes=0,rendererAdds=0;bool done=false,error=false,resourcesReady=true;std::string missing,failMethod;
 std::vector<std::string>events;std::string classGo="GameObject",classTr="Transform",classRenderer="SkinnedMeshRenderer";
 std::map<std::string,std::string>extraClasses;
 Mock(){
  host.context=this;host.resolve_method=&Resolve;host.resolve_class=&Class;host.runtime_invoke=&Invoke;host.object_new=&New;host.string_new=&String;
  host.object_unbox=&Unbox;host.gchandle_new=&Pin;host.gchandle_free=&Free;host.copy_managed_string=&Copy;
  prefab=Go("wpn_misc_0016",nullptr);auto root=Go("Root",prefab->transform);auto ring=Go("wep_quan_jnt",root->transform);auto blade=Go("wep_jian_jnt",ring->transform);blade->transform->q={0,.7071068f,0,.7071068f};
  auto meshBranch=Go("Mesh_Sword",prefab->transform);auto lod=Go("lod0",meshBranch->transform);auto meshGo=Go("S_wpn_misc_00016_02_lod0",lod->transform);meshGo->transform->p={2.717f,-.132f,0};
  auto renderer=Add("SkinnedMeshRenderer");renderer->go=meshGo;meshGo->renderer=renderer;renderer->mesh=Add("Mesh");renderer->materials=Add("Array");renderer->materials->items={Add("Material")};renderer->bones=Add("Array");renderer->bones->items={blade->transform};renderer->rootBone=blade->transform;
  socket=Go("wep_R",nullptr)->transform;
 }
 Object*Add(std::string kind){auto o=std::make_unique<Object>();o->kind=std::move(kind);auto p=o.get();objects.push_back(std::move(o));return p;}
 Object*Go(std::string name,Object*parent,bool created=false){auto go=Add("GameObject");go->name=std::move(name);go->created=created;auto t=Add("Transform");t->go=go;t->name=go->name;t->parent=parent;go->transform=t;if(parent)parent->children.push_back(t);return go;}
 template<class T>Object*Box(T value){auto o=Add("Box");std::memcpy(o->bytes.data(),&value,sizeof(value));return o;}
 static Mock&Self(void*c){return *static_cast<Mock*>(c);}
 static BE_Result BE_CALL Resolve(void*c,const BE_MethodDescriptorV1*d,BE_ResolvedMethodV1*r){auto&m=Self(c);if(m.missing==d->method_name)return BE_Result_NotFound;auto method=std::make_unique<Descriptor>();method->cls=d->class_name;method->name=d->method_name;r->method_info=method.get();m.methods.push_back(std::move(method));return BE_Result_Ok;}
 static BE_Result BE_CALL Class(void*c,const char*,const char*,const char*n,BE_ResolvedClassV1*r){auto&m=Self(c);std::string*p=nullptr;if(n==m.classGo)p=&m.classGo;else if(n==m.classTr)p=&m.classTr;else if(n==m.classRenderer)p=&m.classRenderer;else{m.extraClasses[n]=n;p=&m.extraClasses[n];}r->class_info=p;r->type_object=p;return BE_Result_Ok;}
 static void* BE_CALL New(void*c,const void*cls){auto&m=Self(c);auto kind=*static_cast<const std::string*>(cls);auto go=m.Add(kind=="MaterialPropertyBlock"?"PropertyBlock":"GameObject");go->created=kind!="MaterialPropertyBlock";return go;}
 static void* BE_CALL String(void*c,const char*s){auto o=Self(c).Add("String");o->name=s;return o;}
 static void* BE_CALL Unbox(void*,void*b){return static_cast<Object*>(b)->bytes.data();}
 static uint32_t BE_CALL Pin(void*c,void*o,int pinned){Check(pinned==1);auto&m=Self(c);auto id=++m.pinId;m.pins[id]=static_cast<Object*>(o);return id;}
 static void BE_CALL Free(void*c,uint32_t id){auto&m=Self(c);if(m.pins.erase(id)!=1)throw std::runtime_error("freeing unknown pin");m.events.push_back("free");}
 static int BE_CALL Copy(void*,const void*o,char*out,size_t count){auto&s=static_cast<const Object*>(o)->name;auto n=std::min(s.size(),count-1);std::memcpy(out,s.data(),n);out[n]=0;return int(n);}
 static void* BE_CALL Invoke(void*c,const void*info,void*self,void**a,void**exception){auto&m=Self(c);auto d=static_cast<const Descriptor*>(info);auto o=static_cast<Object*>(self);auto n=d->name;
  if(n==m.failMethod){*exception=reinterpret_cast<void*>(1);return nullptr;}
  if(n=="LoadAsync"){++m.loads;m.events.push_back("load");Check(*static_cast<uint8_t*>(a[2])==0);return m.Box<uint64_t>(123);}
  if(d->cls=="FAssetProxyHandle"){
   if(n=="Dispose"){++m.disposes;m.events.push_back("dispose");return nullptr;}
   if(n=="IsValid")return m.Box(true);if(n=="get_isDone")return m.Box(m.done);if(n=="get_hasError")return m.Box(m.error);if(n=="Get")return m.prefab;
  }
  if(n=="IsInitialized")return m.Box(m.resourcesReady);if(n=="IsAsyncIniting")return m.Box(false);
  if(n=="op_Implicit")return m.Box(a[0]&&static_cast<Object*>(a[0])->alive);
  if(n=="Destroy"){auto obj=static_cast<Object*>(a[0]);Check(obj->created);obj->alive=false;m.events.push_back("destroy");return nullptr;}
  if(n==".ctor"){if(d->cls=="MaterialPropertyBlock")return nullptr;auto text=static_cast<Object*>(a[0]);o->name=text->name;o->transform=m.Add("Transform");o->transform->name=o->name;o->transform->go=o;return nullptr;}
  if(n=="SetFloat"){o->properties[static_cast<Object*>(a[0])->name]=*static_cast<float*>(a[1]);return nullptr;}
  if(n=="SetPropertyBlock"){o->propertyBlock=static_cast<Object*>(a[0]);return nullptr;}
  if(n=="get_transform")return d->cls=="GameObject"?o->transform:o->go->transform;
  if(n=="get_gameObject")return o->go;
  if(n=="get_name"){auto text=m.Add("String");text->name=o->name;return text;}
  if(n=="SetActive"){o->active=*static_cast<bool*>(a[0]);return nullptr;}
  if(n=="get_layer")return m.Box(o->layer);if(n=="set_layer"){o->layer=*static_cast<int*>(a[0]);return nullptr;}
  if(n=="get_childCount")return m.Box(int(o->children.size()));if(n=="GetChild")return o->children.at(*static_cast<int*>(a[0]));
  if(n=="get_parent")return o->parent;
  if(n=="SetParent"){o->parent=static_cast<Object*>(a[0]);Check(o->parent!=nullptr);o->parent->children.push_back(o);return nullptr;}
  if(n=="get_position"||n=="get_localPosition")return m.Box(o->p);
  if(n=="get_rotation"||n=="get_localRotation")return m.Box(o->q);
  if(n=="get_localScale")return m.Box(o->s);
  if(n=="set_localPosition"){o->p=*static_cast<Vec3*>(a[0]);return nullptr;}if(n=="set_localRotation"){o->q=*static_cast<Quat*>(a[0]);return nullptr;}if(n=="set_localScale"){o->s=*static_cast<Vec3*>(a[0]);return nullptr;}
  if(n=="set_position"){o->p=*static_cast<Vec3*>(a[0]);return nullptr;}if(n=="set_rotation"){o->q=*static_cast<Quat*>(a[0]);return nullptr;}
  if(n=="InverseTransformPoint")return m.Box(*static_cast<Vec3*>(a[0]));
  if(n=="GetComponent")return o->renderer;
  if(n=="AddComponent"){Check(*static_cast<std::string*>(a[0])=="SkinnedMeshRenderer");++m.rendererAdds;auto r=m.Add("SkinnedMeshRenderer");r->go=o;o->renderer=r;return r;}
  if(n=="get_sharedMesh")return o->mesh;if(n=="get_sharedMaterials")return o->materials;if(n=="get_bones")return o->bones;if(n=="get_rootBone")return o->rootBone;
  if(n=="set_sharedMesh"){o->mesh=static_cast<Object*>(a[0]);return nullptr;}if(n=="set_sharedMaterials"){o->materials=static_cast<Object*>(a[0]);return nullptr;}if(n=="set_bones"){o->bones=static_cast<Object*>(a[0]);Check(o->bones->items[0]->go->created);return nullptr;}if(n=="set_rootBone"){o->rootBone=static_cast<Object*>(a[0]);Check(o->rootBone->go->created);return nullptr;}
  if(n=="get_localBounds")return m.Box(o->bounds);if(n=="set_localBounds"){o->bounds=*static_cast<Bounds*>(a[0]);return nullptr;}
  if(n=="set_updateWhenOffscreen"||n=="set_enabled"||n=="set_renderingLayerMask")return nullptr;if(n=="get_renderingLayerMask")return m.Box(uint32_t(2));
  if(n=="CreateInstance"){auto array=m.Add("Array");array->items.resize(*static_cast<int*>(a[1]));return array;}
  if(n=="SetValue"){o->items.at(*static_cast<int*>(a[1]))=static_cast<Object*>(a[0]);return nullptr;}
  if(n=="GetLength")return m.Box(int(o->items.size()));if(n=="GetValue")return o->items.at(*static_cast<int*>(a[0]));
  throw std::runtime_error("unhandled mock method: "+d->cls+"."+n);
 }
 bool AllVisualDestroyed()const {for(const auto&o:objects)if(o->created&&o->alive)return false;return true;}
};
void Require(bool condition,const char*message){if(!condition)throw std::runtime_error(message);}
Object* AddPart(Mock&m,const std::string&branch,const std::string&name,const std::vector<Object*>&bones,Object*rootBone){
 auto group=m.Go(branch,m.prefab->transform);auto lod=m.Go("lod0",group->transform);auto go=m.Go(name,lod->transform);auto renderer=m.Add("SkinnedMeshRenderer");renderer->go=go;go->renderer=renderer;renderer->mesh=m.Add("Mesh");renderer->materials=m.Add("Array");renderer->materials->items={m.Add("Material")};renderer->bones=m.Add("Array");renderer->bones->items=bones;renderer->rootBone=rootBone;return renderer;
}
// Independent numeric skin oracle using the native eRC fixture. Renderer
// translation deliberately differs by 2.717 m from its bone, so an accidental
// renderer-matrix multiplication cannot pass this world-position regression.
using Matrix=std::array<std::array<double,4>,4>;
Matrix Multiply(Matrix a,Matrix b){Matrix result{};for(int r=0;r<4;++r)for(int c=0;c<4;++c)for(int k=0;k<4;++k)result[r][c]+=a[r][k]*b[k][c];return result;}
Matrix Trs(const Object*o){double x=o->q.x,y=o->q.y,z=o->q.z,w=o->q.w;auto n=std::sqrt(x*x+y*y+z*z+w*w);x/=n;y/=n;z/=n;w/=n;
 Matrix m{{{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),o->p.x},{2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),o->p.y},{2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y),o->p.z},{0,0,0,1}}};
 for(int r=0;r<3;++r){m[r][0]*=o->s.x;m[r][1]*=o->s.y;m[r][2]*=o->s.z;}return m;}
Matrix World(const Object*o){return o->parent?Multiply(World(o->parent),Trs(o)):Trs(o);}
std::array<double,3>Point(Matrix m,std::array<double,3>p){std::array<double,3>out{};for(int r=0;r<3;++r)out[r]=m[r][0]*p[0]+m[r][1]*p[1]+m[r][2]*p[2]+m[r][3];return out;}
double Distance(std::array<double,3>a,std::array<double,3>b){double sum=0;for(int i=0;i<3;++i)sum+=(a[i]-b[i])*(a[i]-b[i]);return std::sqrt(sum);}
void SkinEquivalence(Mock&m){
 m.socket->p={3,2,-1};m.socket->q={0,.7071068f,0,.7071068f};
 Object*sourceRenderer=nullptr;Object*cloneRenderer=nullptr;
 for(auto&o:m.objects)if(o->kind=="SkinnedMeshRenderer"){if(o->go->created)cloneRenderer=o.get();else sourceRenderer=o.get();}
 Require(sourceRenderer&&cloneRenderer,"skin fixture renderer availability");
 Matrix bind{{{2.5610544e-7,1,-8.742278e-8,1.065814e-16},{-8.742277e-8,8.742278e-8,1,-1.29930614e-20},{1,-2.561054e-7,8.7422784e-8,-1.7978433e-23},{0,0,0,1}}};
 std::array<double,3>vertex{.08199812,-.007199967,1.6476015};
 auto expected=Point(Multiply(World(m.socket),Multiply(World(sourceRenderer->bones->items[0]),bind)),vertex);
 auto actual=Point(Multiply(World(cloneRenderer->bones->items[0]),bind),vertex);
 Require(Distance(actual,expected)<1e-6,"native source/clone skin world position differs");
 auto wrong=Point(Multiply(World(cloneRenderer->go->transform),bind),vertex);
 Require(Distance(actual,wrong)>1,"renderer offset fixture must discriminate wrong skin matrix");
 Require(std::abs(cloneRenderer->go->transform->p.x-2.717f)<1e-6f,"renderer authored TRS retained");
 Require(cloneRenderer->mesh==sourceRenderer->mesh&&cloneRenderer->materials==sourceRenderer->materials,"native resources shared readonly");
}
}
int main(int argc,char**argv){try{
 Json profile;std::ifstream input(std::filesystem::path(argc>1?argv[1]:".")/"profiles/weapons/laevatain_longsword.json");input>>profile;
 {
  Mock m;ManagedVisualWeapon weapon;Require(weapon.Bind(&m.host,profile),"bind");m.resourcesReady=false;Require(!weapon.Tick(m.socket,.1f)&&m.loads==0,"load before resources");m.resourcesReady=true;
  Require(!weapon.Tick(m.socket,.1f),"async waits");for(int i=0;i<10;++i)weapon.Tick(m.socket,.1f);Require(m.loads==1&&m.rendererAdds==0,"one request before done");
  m.done=true;Require(weapon.Tick(m.socket,.1f)&&weapon.Ready()&&m.rendererAdds==1,"pure renderer ready");Require(weapon.Length()>2&&weapon.BladeAxis().y>.99f,"profile axis");
  auto root=static_cast<Object*>(weapon.RootTransform());Require(root->parent==m.socket&&std::abs(root->q.w-1)<1e-6f,"authored mount has no invented rotation");
  Require(!root->go->active&&!weapon.Visible(),"clone must wait for original equipment visibility ownership");SkinEquivalence(m);
  Require(weapon.SetVisible(true)&&weapon.Visible()&&root->go->active,"atomic visible enable");Require(weapon.SetVisible(false)&&!weapon.Visible()&&!root->go->active,"explicit hide");
  for(int i=0;i<5;++i)Require(weapon.Tick(m.socket,.1f),"stable");Require(m.loads==1&&m.rendererAdds==1,"no duplication");
  const Vec3 sourcePoint{1,2,3};const Quat sourceRotation{.5f,.5f,.5f,.5f};
  Require(weapon.SetWorldPose(sourcePoint,sourceRotation),"resource-backed source mount adapter");
  Require(std::abs(root->p.x-1)<1e-6f&&std::abs(root->q.y-.5f)<1e-6f,"adapter drives owned visual root");
  Require(std::abs(m.prefab->transform->p.x)<1e-6f&&std::abs(m.prefab->transform->q.w-1)<1e-6f,"adapter keeps original prefab immutable");
  Require(!weapon.SetWorldPose({std::numeric_limits<float>::quiet_NaN(),0,0},sourceRotation),"reject nonfinite adapted mount");
  weapon.Reset();Require(m.disposes==1&&m.pins.empty()&&m.AllVisualDestroyed()&&m.prefab->alive,"cleanup source retained");
  auto dispose=std::find(m.events.begin(),m.events.end(),"dispose"),destroy=std::find(m.events.begin(),m.events.end(),"destroy");Require(destroy<dispose,"destroy before asset dispose");weapon.Reset();Require(m.disposes==1,"double reset idempotent");
  Require(!weapon.Available()&&!weapon.Ready()&&!weapon.Visible(),"reset must clear availability across character switch");Require(!weapon.Tick(m.socket,.1f)&&m.loads==1,"switch without explicit visual profile must not reload old character blade");
 }
 {
  Mock m;ManagedVisualWeapon weapon;Require(weapon.Bind(&m.host,profile),"bind pending");weapon.Tick(m.socket,.1f);weapon.Reset();Require(m.loads==1&&m.disposes==1&&m.pins.empty()&&m.rendererAdds==0,"cancel pending request");
 }
 {
  Mock m;ManagedVisualWeapon weapon;Require(weapon.Bind(&m.host,profile),"bind failure");m.done=true;m.failMethod="set_sharedMesh";Require(!weapon.Tick(m.socket,.1f)&&!weapon.Available(),"setter failure");Require(m.disposes==1&&m.pins.empty()&&m.AllVisualDestroyed()&&m.prefab->alive,"partial clone rollback");
 }
 {
  Mock m;ManagedVisualWeapon weapon;Require(weapon.Bind(&m.host,profile),"bind timeout");for(int i=0;i<40;++i)weapon.Tick(m.socket,.5f);Require(m.loads==1&&m.disposes==1&&m.pins.empty()&&!weapon.Available(),"timeout releases once");
 }
 {
  Mock m;m.missing="Dispose";ManagedVisualWeapon weapon;Require(!weapon.Bind(&m.host,profile)&&m.loads==0,"missing dispose prevents request");
 }
 {
  Mock m;ManagedVisualWeapon weapon;Require(weapon.Bind(&m.host,profile),"bind active visibility failure");m.done=true;Require(weapon.Tick(m.socket,.1f)&&weapon.SetVisible(true),"show before visibility failure");
  m.failMethod="SetActive";Require(!weapon.SetVisible(false)&&!weapon.Ready()&&!weapon.Visible()&&!weapon.Available(),"visibility failure must discard clone");Require(m.disposes==1&&m.pins.empty()&&m.AllVisualDestroyed(),"visibility setter failure releases all objects");
 }
 {
  Mock m;auto changed=profile;changed["mount_rotation_xyzw"]={0,-.7071068f,0,.7071068f};ManagedVisualWeapon weapon;Require(weapon.Bind(&m.host,changed),"bind changed profile");m.done=true;Require(!weapon.Tick(m.socket,.1f)&&m.rendererAdds==0&&m.disposes==1,"mount mismatch prevents fabricated rotation");
 }
 {
  Mock m;auto ring=m.prefab->transform->children[0]->children[0];std::vector<Object*>chain;
  for(int i=0;i<38;++i)chain.push_back(m.Go("chain_"+std::to_string(i),ring)->transform);
  auto ringRenderer=AddPart(m,"Mesh_Ring","S_wpn_misc_00016_01_lod0",{ring},ring);auto vfx=AddPart(m,"Mesh_VFX","S_wpn_misc_00016_03_lod0",chain,chain[0]);
  auto full=profile;full["renderer_paths"]={profile["renderer_path"],"Mesh_Ring/lod0/S_wpn_misc_00016_01_lod0","Mesh_VFX/lod0/S_wpn_misc_00016_03_lod0"};full["controls"]={{"material_effects",Json::array({{{"renderer_path",profile["renderer_path"]},{"property","_VFXColorIntensity"},{"rest_value",80},{"start_duration",.6f}}})}};
  ManagedVisualWeapon weapon;Require(weapon.Bind(&m.host,full),"multi bind");m.done=true;Require(weapon.Tick(m.socket,.1f)&&m.rendererAdds==3,"three native parts must be cloned");
  ManagedVisualWeapon::PoseState pose;pose.elapsed=.3f;Require(weapon.UpdatePose(pose),"owned material timeline");bool palette=false,block=false;
  for(const auto&o:m.objects)if(o->kind=="SkinnedMeshRenderer"&&o->go->created){if(o->bones->items.size()==38){palette=true;for(auto bone:o->bones->items)Require(bone->go->created,"all 38 chain bones must be owned");}if(o->propertyBlock){block=true;Require(std::abs(o->propertyBlock->properties["_VFXColorIntensity"]-40)<1e-4f,"source duration native color envelope");}}
  Require(palette&&block,"complete palette and private block");Require(!ringRenderer->propertyBlock&&!vfx->propertyBlock,"source renderers remain immutable");weapon.Reset();Require(m.pins.empty()&&m.AllVisualDestroyed()&&m.disposes==1,"full multi part cleanup");
 }
 {
  Mock m;auto root=m.prefab->transform->children[0];auto bow=m.Go("wpn_bow_01_jnt",root)->transform;std::vector<Object*>arm;
  for(int i=0;i<9;++i)arm.push_back(m.Go("arm_"+std::to_string(i),bow)->transform);
  auto up=m.Go("slot_up_string_01_vfx",bow)->transform;auto down=m.Go("slot_dn_string_01_vfx",bow)->transform;auto mid=m.Go("slot_M_string_01_vfx",bow)->transform;mid->p={.25f,-.12f,-.007f};
  AddPart(m,"Mesh_all","bow_body",arm,bow); // both part nodes are under the same Mesh_all branch in the source
  auto all=m.prefab->transform->children.back();auto lod=all->children[0];auto go=m.Go("bow_string",lod);auto stringRenderer=m.Add("SkinnedMeshRenderer");stringRenderer->go=go;go->renderer=stringRenderer;stringRenderer->mesh=m.Add("Mesh");stringRenderer->materials=m.Add("Array");stringRenderer->materials->items={m.Add("Material")};stringRenderer->bones=m.Add("Array");stringRenderer->bones->items={up,down,mid};stringRenderer->rootBone=bow;
  auto config=profile;config["renderer_path"]="Mesh_all/lod0/bow_body";config["renderer_paths"]={"Mesh_all/lod0/bow_body","Mesh_all/lod0/bow_string"};config["mount_bone_path"]="Root/wpn_bow_01_jnt";config["controls"]={{"string_center_path","Root/wpn_bow_01_jnt/slot_M_string_01_vfx"}};
  ManagedVisualWeapon weapon;Require(weapon.Bind(&m.host,config),"bow bind");m.done=true;Require(weapon.Tick(m.socket,.1f)&&m.rendererAdds==2,"bow body nine bone and string three bone clone");
  ManagedVisualWeapon::PoseState pose;pose.drawPointValid=true;pose.drawPointWorld={.7f,.1f,.2f};pose.charge=.8f;Require(weapon.UpdatePose(pose),"string contact update");Object*clonedMiddle=nullptr;for(const auto&o:m.objects)if(o->kind=="Transform"&&o->go&&o->go->created&&o->name==mid->name)clonedMiddle=o.get();
  Require(clonedMiddle&&std::abs(clonedMiddle->p.x-.7f)<1e-6f&&std::abs(mid->p.x-.25f)<1e-6f,"clone string follows contact without mutating source");pose.released=true;Require(weapon.UpdatePose(pose)&&std::abs(clonedMiddle->p.x-.25f)<1e-6f,"release restores authored string position");weapon.Reset();Require(m.pins.empty()&&m.AllVisualDestroyed(),"bow cleanup");
 }
 std::cout<<"visual weapon: async/pure renderer/own bones/source immutability/rollback/dispose/mount evidence passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
