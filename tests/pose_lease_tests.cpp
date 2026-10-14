#include "../native/pose_lease.h"
#include "test_support.h"
#include <memory>
#include <thread>

int main(){try{
 LS::PoseLease first,second;CHECK(!first.Owned()&&!second.Owned());
 CHECK(first.Acquire()&&first.Acquire()&&first.Owned());
 CHECK(!second.Acquire()&&second.Error()==ERROR_BUSY&&!second.Owned());
 CHECK(second.Release());CHECK(!second.Acquire()); // non-owner release does not unlock another DLL
 const auto name=LS::PoseLease::NameForProcess();CHECK(name.find(std::to_wstring(GetCurrentProcessId()))!=std::wstring::npos);
 const auto observed=OpenSemaphoreW(SYNCHRONIZE|SEMAPHORE_MODIFY_STATE,FALSE,name.c_str());CHECK(observed);
 CHECK(WaitForSingleObject(observed,0)==WAIT_TIMEOUT); // independent OS handle sees the same count
 CHECK(first.Release()&&first.Release()&&!first.Owned());
 CHECK(WaitForSingleObject(observed,0)==WAIT_OBJECT_0&&!second.Acquire());
 CHECK(ReleaseSemaphore(observed,1,nullptr)&&second.Acquire());
 DWORD releaseThread=0;bool released=false;
 std::thread release([&]{releaseThread=GetCurrentThreadId();released=second.Release();});release.join();
 CHECK(releaseThread!=GetCurrentThreadId()&&released&&!second.Owned()&&first.Acquire());
 {LS::PoseLease rejected;CHECK(!rejected.Acquire());}CHECK(!second.Acquire());
 CHECK(first.Release());
 {auto owner=std::make_unique<LS::PoseLease>();CHECK(owner->Acquire()&&!second.Acquire());owner.reset();}
 CHECK(second.Acquire());
 CHECK(second.Release());CHECK(CloseHandle(observed));
 // Repeated acquire/fail/release exercises a retained named handle without
 // accumulating semaphore tokens above the single-owner maximum.
 for(int i=0;i<2000;++i){CHECK(first.Acquire()&&!second.Acquire());CHECK(first.Release()&&second.Acquire());CHECK(second.Release());}
 std::cout<<"pose lease: independent Win32 handles, nonblocking ownership, cross-thread release, destruction and 2000 cycles passed\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
