#include "../native/visual_weapon.h"
#include <iostream>
#include <memory>
#include <array>
#include <cstring>
using namespace LS;
using namespace LS::VisualWeapon;
namespace {
void Require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct ClassInfo{std::string name;size_t stride=0;};
struct MethodInfo{std::string cls,name;};
struct Object {
 std::string kind,name;bool owned=false,alive=true,active=true;Object*transform=nullptr;Object*parent=nullptr;Object*mesh=nullptr;Object*material=nullptr;Object*texture=nullptr;
 Vec3 position{},scale{1,1,1};Quat rotation{};std::vector<Object*>children;std::vector<unsigned char>raw;std::array<unsigned char,32>box{};
 std::vector<unsigned char>vertices,normals,indices,uv,textureData;int width=0,height=0;bool linear=false,applied=false;
};
struct SourceMock {
 BE_HostApiV1 host{};std::map<std::string,std::unique_ptr<ClassInfo>>classes;std::vector<std::unique_ptr<MethodInfo>>methods;std::vector<std::unique_ptr<Object>>objects;
 std::map<uint32_t,Object*>pins;uint32_t nextPin=0;Object*socket=nullptr;Object*otherSocket=nullptr;Object*shader=nullptr;Object*originalEquipment=nullptr;
 std::string missing,fail,failClass;bool noShader=false;int originalWrites=0,destroyCalls=0,shaderQueries=0;
 SourceMock(){host.context=this;host.resolve_method=Resolve;host.resolve_class=ResolveClass;host.runtime_invoke=Invoke;host.object_new=New;host.string_new=String;host.object_unbox=Unbox;host.gchandle_new=Pin;host.gchandle_free=Free;
  socket=Add("Transform");socket->position={3,4,5};otherSocket=Add("Transform");shader=Add("Shader");originalEquipment=Add("GameObject");}
 Object*Add(std::string kind,bool owned=false){auto object=std::make_unique<Object>();object->kind=std::move(kind);object->owned=owned;auto result=object.get();objects.push_back(std::move(object));return result;}
 template<class T>Object*Box(T value){auto object=Add("Box");std::memcpy(object->box.data(),&value,sizeof(value));return object;}
 static SourceMock&Self(void*context){return*static_cast<SourceMock*>(context);}
 static BE_Result BE_CALL Resolve(void*context,const BE_MethodDescriptorV1*description,BE_ResolvedMethodV1*resolved){auto&m=Self(context);if(description->method_name==m.missing)return BE_Result_NotFound;auto info=std::make_unique<MethodInfo>();info->cls=description->class_name;info->name=description->method_name;resolved->method_info=info.get();m.methods.push_back(std::move(info));return BE_Result_Ok;}
 static BE_Result BE_CALL ResolveClass(void*context,const char*,const char*,const char*name,BE_ResolvedClassV1*resolved){auto&m=Self(context);if(name==m.failClass)return BE_Result_NotFound;if(!m.classes.contains(name)){auto info=std::make_unique<ClassInfo>();info->name=name;info->stride=std::string(name)=="Vector3"?12:std::string(name)=="Vector2"?8:std::string(name)=="Int32"?4:std::string(name)=="Byte"?1:0;m.classes[name]=std::move(info);}resolved->class_info=m.classes.at(name).get();resolved->type_object=m.classes.at(name).get();return BE_Result_Ok;}
 static void*BE_CALL New(void*context,const void*type){return Self(context).Add(static_cast<const ClassInfo*>(type)->name,true);}
 static void*BE_CALL String(void*context,const char*text){auto object=Self(context).Add("String");object->name=text;return object;}
 static void*BE_CALL Unbox(void*,void*object){return static_cast<Object*>(object)->box.data();}
 static uint32_t BE_CALL Pin(void*context,void*object,int pinned){auto&m=Self(context);Require(pinned==1,"source arrays and owners must be pinned");const auto id=++m.nextPin;m.pins[id]=static_cast<Object*>(object);return id;}
 static void BE_CALL Free(void*context,uint32_t id){Require(Self(context).pins.erase(id)==1,"pin freed exactly once");}
 bool Clean()const {for(const auto&object:objects)if(object->owned&&object->alive)return false;return pins.empty();}
 void Destroy(Object*object){Require(object->owned,"original Unity resources are never destroyed");Require(object->alive,"owned resource destroyed once");object->alive=false;++destroyCalls;if(object->transform){object->transform->alive=false;for(auto child:object->transform->children)child->alive=false;}}
 static void*BE_CALL Invoke(void*context,const void*method,void*self,void**args,void**exception){auto&m=Self(context);bool current=false;for(const auto&entry:m.methods)if(entry.get()==method)current=true;Require(current,"method resolution lease belongs to current SDK host");auto info=static_cast<const MethodInfo*>(method);auto object=static_cast<Object*>(self);const auto&name=info->name;if(name==m.fail||info->cls+"."+name==m.fail){*exception=reinterpret_cast<void*>(1);return nullptr;}
  if(name=="op_Implicit")return m.Box(args[0]&&static_cast<Object*>(args[0])->alive);
  if(name=="Destroy"){m.Destroy(static_cast<Object*>(args[0]));return nullptr;}
  if(name=="CreateInstance"){auto type=static_cast<ClassInfo*>(args[0]);const int count=*static_cast<int*>(args[1]);Require(count>=0&&type->stride>0,"array type has native stride");auto array=m.Add("Array");array->raw.resize(size_t(count)*type->stride);return array;}
  if(name=="UnsafeAddrOfPinnedArrayElement"){auto array=static_cast<Object*>(args[0]);Require(*static_cast<int*>(args[1])==0,"native upload starts at index zero");bool pinned=false;for(auto[p,owner]:m.pins)if(owner==array)pinned=true;Require(pinned,"array address only read while pinned");return m.Box(reinterpret_cast<uintptr_t>(array->raw.data()));}
  if(name=="Find"){++m.shaderQueries;return m.noShader?nullptr:m.shader;}
  if(name==".ctor"){
   Require(object&&object->owned,"constructor applies only to new owner");
   if(info->cls=="GameObject"){object->name=static_cast<Object*>(args[0])->name;object->transform=m.Add("Transform");object->transform->parent=nullptr;}
   if(info->cls=="Texture2D"){object->width=*static_cast<int*>(args[0]);object->height=*static_cast<int*>(args[1]);Require(*static_cast<int*>(args[2])==4,"RGBA32 texture format");Require(!*static_cast<bool*>(args[3]),"source texture no invented mipmap generation");object->linear=*static_cast<bool*>(args[4]);}
   if(info->cls=="Material")Require(args[0]==m.shader,"private material uses read-only shader");return nullptr;
  }
  if(name=="get_transform")return object->transform;
  if(name=="SetActive"){if(!object->owned)++m.originalWrites;object->active=*static_cast<bool*>(args[0]);return nullptr;}
  if(name=="SetParent"){Require(object!=m.socket&&object!=m.otherSocket,"original socket not reparented");object->parent=static_cast<Object*>(args[0]);Require(!*static_cast<bool*>(args[1]),"local authored mount preserved");return nullptr;}
  if(name=="set_localPosition"||name=="set_position"){if(object==m.socket||object==m.otherSocket)++m.originalWrites;object->position=*static_cast<Vec3*>(args[0]);return nullptr;}
  if(name=="set_localRotation"||name=="set_rotation"){if(object==m.socket||object==m.otherSocket)++m.originalWrites;object->rotation=*static_cast<Quat*>(args[0]);return nullptr;}
  if(name=="set_localScale"){object->scale=*static_cast<Vec3*>(args[0]);return nullptr;}
  if(name=="set_vertices"){object->vertices=static_cast<Object*>(args[0])->raw;return nullptr;}if(name=="set_normals"){object->normals=static_cast<Object*>(args[0])->raw;return nullptr;}if(name=="set_triangles"){object->indices=static_cast<Object*>(args[0])->raw;return nullptr;}if(name=="set_uv"){object->uv=static_cast<Object*>(args[0])->raw;return nullptr;}
  if(name=="RecalculateBounds"||name=="SetColor")return nullptr;
  if(name=="LoadRawTextureData"){object->textureData=static_cast<Object*>(args[0])->raw;return nullptr;}
  if(name=="Apply"){Require(!*static_cast<bool*>(args[0])&&*static_cast<bool*>(args[1]),"texture apply drops managed CPU backing");object->applied=true;return nullptr;}
  if(name=="SetTexture"){Require(static_cast<Object*>(args[1])->applied,"texture applied before material assignment");object->texture=static_cast<Object*>(args[1]);return nullptr;}
  if(name=="AddComponent"){auto type=static_cast<ClassInfo*>(args[0]);auto component=m.Add(type->name);object->transform->children.push_back(component);return component;}
  if(name=="set_sharedMesh"){object->mesh=static_cast<Object*>(args[0]);return nullptr;}if(name=="set_sharedMaterial"){object->material=static_cast<Object*>(args[0]);return nullptr;}
  throw std::runtime_error("Unhandled source mock method: "+info->cls+"."+name);
 }
};
Json Fixture(){return{{"id","source-fixture"},{"vertices",{{0,0,0},{0,1,0},{1,0,0}}},{"normals",{{0,0,1},{0,0,1},{0,0,1}}},{"indices",{0,1,2}},{"uv",{{.15,.25},{.75,.125},{.25,.8}}},{"materials",{{{"runtime_textures",{{"albedo",{{"file","source-fixture.rgba"},{"width",1},{"height",1},{"color_space","sRGB"}}}}}}}}};}
template<class T>std::vector<unsigned char>Bytes(std::initializer_list<T>values){std::vector<unsigned char>bytes(values.size()*sizeof(T));std::memcpy(bytes.data(),values.begin(),bytes.size());return bytes;}
void ResourcesAreOriginal(const SourceMock&mock){Require(mock.originalWrites==0&&mock.socket->alive&&mock.shader->alive&&mock.originalEquipment->alive&&mock.originalEquipment->active,"original equipment, socket and shader preserved read-only");}
}
int main(int argc,char**argv){try{
 const auto directory=argc>1?std::filesystem::path(argv[1]):std::filesystem::current_path()/"source-weapon-fixture";std::filesystem::create_directories(directory);constexpr unsigned char rgba[]{19,37,91,255};{std::ofstream file(directory/"source-fixture.rgba",std::ios::binary);file.write(reinterpret_cast<const char*>(rgba),4);}const auto fixture=Fixture();const auto original=fixture.dump();
 {
  SourceMock mock;ManagedSourceWeapon weapon;Require(weapon.Bind(&mock.host,fixture,directory)&&weapon.Tick(mock.socket,.1f)&&weapon.Ready(),"build native triangle source");Require(!weapon.Visible(),"source remains hidden until equipment ownership acquired");Require(weapon.SetVisible(true)&&weapon.Visible(),"explicit source visibility");Object*mesh=nullptr;Object*texture=nullptr;for(const auto&object:mock.objects){if(object->kind=="Mesh")mesh=object.get();if(object->kind=="Texture2D")texture=object.get();}
  Require(mesh&&mesh->vertices==Bytes<float>({0,0,0,0,1,0,1,0,0})&&mesh->indices==Bytes<int>({0,1,2}),"source vertices and winding uploaded exactly");Require(mesh->uv==Bytes<float>({.15f,.25f,.75f,.125f,.25f,.8f}),"source UV not regenerated or remapped");Require(texture&&texture->textureData==std::vector<unsigned char>(std::begin(rgba),std::end(rgba))&&texture->width==1&&texture->height==1&&!texture->linear&&texture->applied,"original RGBA and sRGB survive upload");Require(weapon.SetWorldPose({2,3,4},{0,0,0,1}),"local visual posing supported");const auto stablePins=mock.pins.size();
  for(int pass=0;pass<500;++pass){Require(weapon.UpdateSkinPositions({{0,0,0},{0,1,float(pass)/500},{1,0,0}}),"own animation uploads exact positions");Require(mock.pins.size()==stablePins,"per-frame skinning must reuse pinned vertex buffers");}
  Require(!weapon.UpdateSkinPositions({{0,0,0}}),"wrong-sized skin updates rejected without overwriting buffers");
  weapon.Reset();Require(mock.Clean(),"success Reset destroys all owners and unpins buffers");ResourcesAreOriginal(mock);
 }
 for(const auto method:{"UnsafeAddrOfPinnedArrayElement","CreateInstance","set_vertices","set_uv","LoadRawTextureData"}){SourceMock mock;mock.missing=method;ManagedSourceWeapon weapon;Require(!weapon.Bind(&mock.host,fixture,directory)&&!weapon.Tick(mock.socket,.1f)&&!weapon.Visible()&&mock.Clean(),"missing SDK method refuses source fallback before allocation");ResourcesAreOriginal(mock);}
 for(const auto failure:{"UnsafeAddrOfPinnedArrayElement","set_vertices","set_triangles","set_normals","set_uv","Texture2D..ctor","Material..ctor","LoadRawTextureData","Apply","SetTexture","set_sharedMesh","set_sharedMaterial"}){SourceMock mock;mock.fail=failure;ManagedSourceWeapon weapon;Require(weapon.Bind(&mock.host,fixture,directory),"failure fixture can resolve");Require(!weapon.Tick(mock.socket,.1f)&&!weapon.Ready()&&!weapon.Visible()&&mock.Clean(),"upload failure fully clears owners and pins");ResourcesAreOriginal(mock);}
 {SourceMock mock;mock.noShader=true;ManagedSourceWeapon weapon;Require(weapon.Bind(&mock.host,fixture,directory)&&!weapon.Tick(mock.socket,.1f)&&mock.shaderQueries==6&&mock.Clean(),"missing all fallback shaders fails hidden and clears geometry");ResourcesAreOriginal(mock);}
 {SourceMock mock;ManagedSourceWeapon weapon;for(int pass=0;pass<5;++pass){Require(weapon.Bind(&mock.host,fixture,directory)&&weapon.Tick(mock.socket,.1f),"repeat binding builds");Require(weapon.Bind(&mock.host,fixture,directory)&&mock.Clean(),"rebind clears preceding model before rebuilding");Require(weapon.Tick(mock.socket,.1f),"rebound source builds");weapon.Reset();Require(mock.Clean(),"repeat resets have zero pins/owners");}ResourcesAreOriginal(mock);}
 {SourceMock first,second;second.missing="SetColor";ManagedSourceWeapon weapon;Require(weapon.Bind(&first.host,fixture,directory)&&weapon.Tick(first.socket,.1f),"first SDK host builds source");weapon.Reset();Require(first.Clean(),"first SDK host fully released");Require(weapon.Bind(&second.host,fixture,directory)&&weapon.Tick(second.socket,.1f),"rebinding with missing optional method never invokes previous host lease");weapon.Reset();Require(second.Clean(),"second SDK host fully released");}
 {SourceMock mock;ManagedSourceWeapon blade,shield;Require(blade.Bind(&mock.host,fixture,directory)&&shield.Bind(&mock.host,fixture,directory)&&blade.Tick(mock.socket,.1f)&&shield.Tick(mock.otherSocket,.1f),"two source parts independently mount");auto shieldTransform=static_cast<Object*>(shield.RootTransform());blade.Reset();Require(shield.Ready()&&shieldTransform->alive&&shieldTransform->parent==mock.otherSocket&&!mock.pins.empty(),"reset blade does not destroy shield resources");shield.Reset();Require(mock.Clean(),"all source parts clean after reset");ResourcesAreOriginal(mock);}
 {SourceMock mock;ManagedSourceWeapon weapon;Require(weapon.Bind(&mock.host,fixture,directory)&&weapon.Tick(mock.socket,.1f),"visible failure fixture builds");mock.fail="SetActive";Require(!weapon.SetVisible(true)&&!weapon.Visible()&&mock.Clean(),"failed activation releases all source state");ResourcesAreOriginal(mock);}
 {SourceMock mock;{ManagedSourceWeapon weapon;Require(weapon.Bind(&mock.host,fixture,directory)&&weapon.Tick(mock.socket,.1f),"explicit lifecycle source builds");weapon.Reset();}Require(mock.Clean(),"main-thread explicit Reset clears state before helper destruction");}
 {SourceMock mock;auto invalid=fixture;invalid["materials"][0]["runtime_textures"]["albedo"]["file"]="../outside.rgba";ManagedSourceWeapon weapon;Require(weapon.Bind(&mock.host,invalid,directory)&&!weapon.Tick(mock.socket,.1f)&&mock.Clean(),"texture cannot escape source asset root");}
 Require(fixture.dump()==original,"input geometry and source metadata unchanged");std::cout<<"Source mesh UV/RGBA fidelity, SDK/shader/upload fallback, two-part ownership, repeat lifecycle and read-only resources passed\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
