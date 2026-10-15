#include "../native/il2cpp_thread_scope.h"
#include "test_support.h"
#include <atomic>
#include <iostream>
#include <string>
#include <thread>

namespace {
thread_local void* current=nullptr;
std::atomic<int> attaches=0,detaches=0;
bool domainReady=true,attachSucceeds=true;
void* __cdecl Domain(){return domainReady?reinterpret_cast<void*>(1):nullptr;}
void* __cdecl Current(){return current;}
void* __cdecl Attach(void* domain){CHECK(domain==reinterpret_cast<void*>(1));++attaches;
 if(!attachSucceeds)return nullptr;current=reinterpret_cast<void*>(static_cast<uintptr_t>(GetCurrentThreadId()));return current;}
void __cdecl Detach(void* thread){CHECK(thread==current);++detaches;current=nullptr;}
LS::Il2CppThreadApi Api(){return {Domain,Current,Attach,Detach};}
}
int main(){try{
 {LS::Il2CppThreadScope scope({});CHECK(!scope.Ready()&&!scope.OwnsAttachment()&&attaches==0&&detaches==0);}
 domainReady=false;{LS::Il2CppThreadScope scope(Api());CHECK(!scope.Ready()&&attaches==0);}domainReady=true;
 attachSucceeds=false;{LS::Il2CppThreadScope scope(Api());CHECK(!scope.Ready()&&attaches==1&&detaches==0);}attachSucceeds=true;
 {LS::Il2CppThreadScope outer(Api());CHECK(outer.Ready()&&outer.OwnsAttachment()&&attaches==2);
  {LS::Il2CppThreadScope borrowed(Api());CHECK(borrowed.Ready()&&!borrowed.OwnsAttachment()&&attaches==2);}
  CHECK(Current()!=nullptr&&detaches==0);
 }CHECK(Current()==nullptr&&detaches==1);
 try{LS::Il2CppThreadScope scope(Api());CHECK(scope.Ready());throw std::runtime_error("fixture");}catch(const std::runtime_error&){}
 CHECK(Current()==nullptr&&attaches==3&&detaches==2);
 current=reinterpret_cast<void*>(9);{LS::Il2CppThreadScope scope(Api());CHECK(scope.Ready()&&!scope.OwnsAttachment());}CHECK(Current()==reinterpret_cast<void*>(9)&&detaches==2);current=nullptr;
 std::thread a([]{LS::Il2CppThreadScope scope(Api());CHECK(scope.Ready()&&scope.OwnsAttachment());});
 std::thread b([]{LS::Il2CppThreadScope scope(Api());CHECK(scope.Ready()&&scope.OwnsAttachment());});a.join();b.join();
 CHECK(Current()==nullptr&&attaches==5&&detaches==4);
 std::cout<<"IL2CPP attachment ownership, missing exports, readiness failure, exceptions and independent threads passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
