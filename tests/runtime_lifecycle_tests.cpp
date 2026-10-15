#include <Windows.h>
#include <atomic>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "test_support.h"

namespace Fixture {
thread_local void* attached=nullptr;
std::atomic<bool> domainReady=false,attachReady=true,failResolve=false;
std::atomic<int> attaches=0,detaches=0,resolveCalls=0,allocations=0,freedPins=0;
const HMODULE Game=reinterpret_cast<HMODULE>(static_cast<uintptr_t>(0x1234));
HMODULE Module(LPCWSTR name){return name&&std::wcscmp(name,L"GameAssembly.dll")==0?Game:GetModuleHandleW(name);}
void* __cdecl Domain(){return domainReady?reinterpret_cast<void*>(1):nullptr;}
void* __cdecl Current(){return attached;}
void* __cdecl Attach(void* domain){CHECK(domain==reinterpret_cast<void*>(1));++attaches;if(!attachReady)return nullptr;attached=reinterpret_cast<void*>(static_cast<uintptr_t>(GetCurrentThreadId()));return attached;}
void __cdecl Detach(void* value){CHECK(value==attached);++detaches;attached=nullptr;}
uint32_t __cdecl MethodFlags(const void*,uint32_t* impl){CHECK(attached);*impl=0;return 0;}
uint32_t __cdecl FieldFlags(const void*){CHECK(attached);return 0;}
bool __cdecl IsValue(const void*){CHECK(attached);return true;}
int32_t __cdecl ValueSize(const void* type,uint32_t* alignment){CHECK(attached);*alignment=4;return type==reinterpret_cast<void*>(101)?12:64;}
FARPROC Proc(HMODULE module,LPCSTR name){
 if(module!=Game)return GetProcAddress(module,name);
 if(std::strcmp(name,"il2cpp_domain_get")==0)return reinterpret_cast<FARPROC>(Domain);
 if(std::strcmp(name,"il2cpp_thread_current")==0)return reinterpret_cast<FARPROC>(Current);
 if(std::strcmp(name,"il2cpp_thread_attach")==0)return reinterpret_cast<FARPROC>(Attach);
 if(std::strcmp(name,"il2cpp_thread_detach")==0)return reinterpret_cast<FARPROC>(Detach);
 if(std::strcmp(name,"il2cpp_method_get_flags")==0)return reinterpret_cast<FARPROC>(MethodFlags);
 if(std::strcmp(name,"il2cpp_field_get_flags")==0)return reinterpret_cast<FARPROC>(FieldFlags);
 if(std::strcmp(name,"il2cpp_class_is_valuetype")==0)return reinterpret_cast<FARPROC>(IsValue);
 if(std::strcmp(name,"il2cpp_class_value_size")==0)return reinterpret_cast<FARPROC>(ValueSize);
 return nullptr;
}
}
#define GetModuleHandleW Fixture::Module
#define GetProcAddress Fixture::Proc
#include "../native/module.cpp"
#undef GetProcAddress
#undef GetModuleHandleW

namespace Fixture {
struct Transform {bool alive=true;Vec3 position{1,2,3};Quat rotation{};};
struct Node {std::string owner;void* target=nullptr;void* detour=nullptr;bool active=true;};
std::vector<Node> nodes;std::mutex nodeGate;
std::atomic<int> frames=0,inputs=0,shieldsRead=0,created=0,released=0;
std::atomic<bool> holdOriginalTail=false,originalTailEntered=false;
std::array<std::atomic<int>,12> originalShieldReads{};
int frameNumber=1;alignas(16) unsigned char boxed[80]{};
void __cdecl OriginalTail(void*,float,void*){++frames;originalTailEntered=true;
 while(holdOriginalTail.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));}
void __cdecl OriginalInput(void*,void*){++inputs;}
template<size_t N>bool __cdecl OriginalShield(void*,uintptr_t,void*){++shieldsRead;++originalShieldReads[N];return N%2==0;}
template<size_t... N>auto ShieldPointers(std::index_sequence<N...>){return std::array<void*,sizeof...(N)>{reinterpret_cast<void*>(OriginalShield<N>)...};}
const auto shieldPointers=ShieldPointers(std::make_index_sequence<12>{});
BE_Result BE_CALL ResolveMethod(void*,const BE_MethodDescriptorV1* d,BE_ResolvedMethodV1* result){
 CHECK(attached);++resolveCalls;if(failResolve&&std::string(d->method_name)=="TailLateTick")return BE_Result_NotFound;
 for(size_t i=0;i<std::size(methods);++i)if(std::strcmp(d->class_name,methods[i].d.class_name)==0&&std::strcmp(d->method_name,methods[i].d.method_name)==0&&d->parameter_count==methods[i].d.parameter_count){
  result->method_info=reinterpret_cast<void*>(i+1);
  result->method_pointer=i==Tail?reinterpret_cast<void*>(OriginalTail):i==CheckInput?reinterpret_cast<void*>(OriginalInput):reinterpret_cast<void*>(0x10000+i*16);return BE_Result_Ok;}
 for(size_t i=0;i<std::size(shields);++i){const auto& expected=shields[i].descriptor;
  if(std::strcmp(d->class_name,expected.class_name)==0&&std::strcmp(d->method_name,expected.method_name)==0&&std::strcmp(d->parameter_types,expected.parameter_types)==0){result->method_info=reinterpret_cast<void*>(1000+i);result->method_pointer=shieldPointers[i];return BE_Result_Ok;}}
 return BE_Result_NotFound;
}
BE_Result BE_CALL ResolveClass(void*,const char*,const char*,const char* name,BE_ResolvedClassV1* result){
 CHECK(attached);const uintptr_t value=std::strcmp(name,"Vector3")==0?101:100;result->class_info=reinterpret_cast<void*>(value);result->type_object=reinterpret_cast<void*>(value);return BE_Result_Ok;}
BE_Result BE_CALL ResolveField(void*,const BE_FieldDescriptorV1* d,BE_ResolvedFieldV1* result){
 CHECK(attached);const std::string name=d->field_name;result->field_info=reinterpret_cast<void*>(1);
 result->offset=name=="x"||name=="m_Point"?16:name=="y"?20:name=="z"?24:name=="m_Normal"?28:name=="m_Distance"?44:16;return BE_Result_Ok;}
void* BE_CALL ObjectNew(void*,const void*){CHECK(attached);++allocations;return boxed;}
void* BE_CALL Unbox(void*,void* object){CHECK(attached);return object==boxed?boxed+16:object;}
uint32_t BE_CALL Pin(void*,void*,int){CHECK(attached);return 1;}
void BE_CALL Free(void*,uint32_t){CHECK(attached);++freedPins;}
void* BE_CALL String(void*,const char*){CHECK(attached);return nullptr;}
void* BE_CALL FieldValue(void*,const void*,void*){CHECK(attached);return nullptr;}
int BE_CALL CopyString(void*,const void*,char*,size_t){CHECK(attached);return 0;}
void* BE_CALL Invoke(void*,const void* token,void* self,void** args,void** exception){
 CHECK(attached);*exception=nullptr;auto* transform=static_cast<Fixture::Transform*>(self);
 switch(Index(reinterpret_cast<uintptr_t>(token)-1)){
 case FrameCount:return &frameNumber;
 case Alive:{static bool value;auto* object=static_cast<Fixture::Transform*>(args[0]);value=object&&object->alive;return &value;}
 case LocalRotation:return &transform->rotation;
 case LocalPosition:return &transform->position;
 case SetLocalRotation:transform->rotation=*static_cast<Quat*>(args[0]);return nullptr;
 case SetLocalPosition:transform->position=*static_cast<Vec3*>(args[0]);return nullptr;
 default:*exception=reinterpret_cast<void*>(1);return nullptr;
 }
}
BE_Result BE_CALL Create(void*,const char* owner,void* target,void* detour,void** next){
 CHECK(attached);std::lock_guard lock(nodeGate);
 for(auto& node:nodes)if(node.owner==owner&&node.target==target){CHECK(!node.active);CHECK(node.detour==detour);node.active=true;*next=target;++created;return BE_Result_Ok;}
 nodes.push_back({owner,target,detour,true});*next=target;++created;return BE_Result_Ok;
}
BE_Result BE_CALL Release(void*,const char* owner){std::lock_guard lock(nodeGate);++released;for(auto& node:nodes)if(node.owner==owner)node.active=false;return BE_Result_Ok;}
void* Entry(void* target){std::lock_guard lock(nodeGate);for(const auto& node:nodes)if(node.target==target)return node.active?node.detour:target;return target;}
size_t Active(const char* owner){std::lock_guard lock(nodeGate);return std::count_if(nodes.begin(),nodes.end(),[&](const auto& node){return node.active&&node.owner==owner;});}
void Tick(){attached=reinterpret_cast<void*>(static_cast<uintptr_t>(GetCurrentThreadId()));++frameNumber;
 reinterpret_cast<TailFn>(Entry(reinterpret_cast<void*>(OriginalTail)))(nullptr,.016f,nullptr);attached=nullptr;}
BE_Result BE_CALL Reply(void*,const char*,BE_Result result,const char*){return result;}
void BE_CALL Log(void*,const char*){}
template<class Predicate>void Wait(Predicate predicate){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
 while(std::chrono::steady_clock::now()<deadline){{std::lock_guard lock(gate);if(predicate())return;}std::this_thread::sleep_for(std::chrono::milliseconds(5));}throw std::runtime_error("lifecycle fixture timed out");}
bool IsReady(){return ready;}
}
int main(int argc,char** argv){try{
 CHECK(argc==2);using namespace Fixture;BE_HostApiV1 runtime{};
 runtime.abi_version=1;runtime.resolve_method=ResolveMethod;runtime.resolve_class=ResolveClass;runtime.resolve_field=ResolveField;
 runtime.runtime_invoke=Invoke;runtime.object_new=ObjectNew;runtime.object_unbox=Unbox;runtime.string_new=String;
 runtime.gchandle_new=Pin;runtime.gchandle_free=Free;runtime.field_get_value_object=FieldValue;runtime.copy_managed_string=CopyString;
 runtime.create_hook=Create;runtime.release_module_hooks=Release;
 BE_ThirdPartyHostV1 host{sizeof(BE_ThirdPartyHostV1),1,nullptr,ID,"windows-x64",argv[1],&runtime,nullptr,Fixture::Log,Reply,nullptr,nullptr};
 const auto* entry=BetterEndfield_GetThirdPartyModuleV1();
 CHECK(entry->initialize(&host,"{}")==BE_Result_Ok);
 Wait([]{return phase=="waiting"&&!detail.empty();});CHECK(resolveCalls==0&&allocations==0);
 domainReady=true;attachReady=false;Wait([]{return attaches.load()>0;});CHECK(resolveCalls==0&&allocations==0);
 attachReady=true;failResolve=true;Wait([]{return resolveCalls.load()>0&&detaches.load()>0;});CHECK(!ready&&Active(FrameOwner)==0);
 failResolve=false;Wait(IsReady);Wait([]{return detaches.load()>=2;});CHECK(terrainHitLayout&&Active(FrameOwner)==1&&Active(ID)==13);
 const auto callbacksBefore=created.load();Fixture::Transform model,bone;bone.rotation={0,0,.6f,.8f};
 {std::lock_guard lock(gate);actor.model=&model;actor.bones.resize(1);auto& owned=actor.bones[0];owned.transform=&bone;owned.written=true;owned.lastRotation=bone.rotation;owned.previousRotation={};actor.pins={17,18};CHECK(poseLease.Acquire());}
 std::thread stop([&]{entry->shutdown();Release(nullptr,ID);});stop.join();CHECK(cleanupPending&&poseLease.Owned()&&Active(ID)==0&&Active(FrameOwner)==1);
 reinterpret_cast<InputFn>(Entry(reinterpret_cast<void*>(OriginalInput)))(nullptr,nullptr);CHECK(inputs==1);
 CHECK(entry->initialize(&host,"{\"mode\":\"bow\"}")==BE_Result_Ok);CHECK(pendingAssetInitialization&&!ready&&phase=="waiting_cleanup");
 const auto oldMode=loadedMode;const auto allocBefore=allocations.load();std::this_thread::sleep_for(std::chrono::milliseconds(120));
 {std::lock_guard lock(gate);CHECK(pendingAssetInitialization&&loadedMode==oldMode&&allocations==allocBefore&&poseLease.Owned());}
 Tick();CHECK(!cleanupPending&&!poseLease.Owned()&&Close(bone.rotation,Quat{}));Wait(IsReady);
 CHECK(loadedMode=="bow"&&!pendingAssetInitialization&&Active(FrameOwner)==1&&Active(ID)==13&&created==callbacksBefore+13);
 CHECK(entry->on_message("fixture","{\"action\":\"status\"}")==BE_Result_Ok);
 {std::lock_guard lock(gate);actor.model=&model;CHECK(poseLease.Acquire());}
 entry->shutdown();CHECK(entry->initialize(&host,"{}")==BE_Result_Ok&&pendingAssetInitialization);entry->shutdown();
 CHECK(!initialized&&!ready&&cleanupPending&&Active(ID)==0);Tick();CHECK(!cleanupPending&&!poseLease.Owned());
 CHECK(Active(FrameOwner)==1&&Active(ID)==0&&frames>=2);
 // A deferred asset failure is reported after the old owned pose is restored;
 // it must not reinstall inputs or strand the cross-module lease.
 {std::lock_guard lock(gate);actor.model=&model;CHECK(poseLease.Acquire());cleanupPending=true;}
 auto badHost=host;const auto missing=(std::filesystem::path(argv[1])/"output/fixture-missing-runtime-assets").string();badHost.package_root=missing.c_str();
 CHECK(entry->initialize(&badHost,"{}")==BE_Result_Ok&&pendingAssetInitialization);Tick();Wait([]{return phase=="failed";});
 CHECK(!ready&&!cleanupPending&&!pendingAssetInitialization&&!poseLease.Owned()&&Active(ID)==0);entry->shutdown();
 // Shutdown while a native frame is already in the original function. It
 // retires inputs without waiting on that frame; its return performs cleanup.
 CHECK(entry->initialize(&host,"{}")==BE_Result_Ok);Wait(IsReady);
 {std::lock_guard lock(gate);actor.model=&model;CHECK(poseLease.Acquire());}
 originalTailEntered=false;holdOriginalTail=true;
 std::thread concurrentStop([&]{while(!originalTailEntered.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));entry->shutdown();Release(nullptr,ID);holdOriginalTail=false;});
 Tick();concurrentStop.join();CHECK(!initialized&&!cleanupPending&&!poseLease.Owned()&&Active(ID)==0&&Active(FrameOwner)==1);
 std::cout<<"Host callbacks: attach readiness/failure/retry, exact input retirement, resident frame cleanup, immediate re-enable and cancellation passed (offline managed fixture)\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';if(initialized)Shutdown();return 1;}}
