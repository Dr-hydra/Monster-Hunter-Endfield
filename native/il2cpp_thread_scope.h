#pragma once
#include <Windows.h>

namespace LS {
// Native ABI 1 exposes IL2CPP calls, not per-caller thread registration.
// Borrow an existing attachment; detach only the thread registered here.
struct Il2CppThreadApi {
 using GetFn=void*(__cdecl*)();
 using AttachFn=void*(__cdecl*)(void*);
 using DetachFn=void(__cdecl*)(void*);
 GetFn domainGet=nullptr,current=nullptr;AttachFn attach=nullptr;DetachFn detach=nullptr;
 static Il2CppThreadApi Load(){
  const auto module=GetModuleHandleW(L"GameAssembly.dll");if(!module)return {};
  return {reinterpret_cast<GetFn>(GetProcAddress(module,"il2cpp_domain_get")),
   reinterpret_cast<GetFn>(GetProcAddress(module,"il2cpp_thread_current")),
   reinterpret_cast<AttachFn>(GetProcAddress(module,"il2cpp_thread_attach")),
   reinterpret_cast<DetachFn>(GetProcAddress(module,"il2cpp_thread_detach"))};
 }
};
class Il2CppThreadScope {
 Il2CppThreadApi api_;void* owned_=nullptr;bool ready_=false;
 const char* error_="IL2CPP thread exports are unavailable";
public:
 explicit Il2CppThreadScope(Il2CppThreadApi api):api_(api){
  if(!api_.domainGet||!api_.current||!api_.attach||!api_.detach)return;
  if(api_.current()){ready_=true;error_="";return;}
  auto domain=api_.domainGet();if(!domain){error_="IL2CPP domain is not ready";return;}
  owned_=api_.attach(domain);
  if(!owned_){error_="IL2CPP could not attach the module bootstrap thread";return;}
  ready_=true;error_="";
 }
 ~Il2CppThreadScope(){if(owned_)api_.detach(owned_);}
 Il2CppThreadScope(const Il2CppThreadScope&)=delete;
 Il2CppThreadScope& operator=(const Il2CppThreadScope&)=delete;
 bool Ready()const{return ready_;}
 bool OwnsAttachment()const{return owned_!=nullptr;}
 const char* Error()const{return error_;}
};
}
