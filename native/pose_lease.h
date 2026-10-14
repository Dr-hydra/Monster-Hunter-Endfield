#pragma once
#include <Windows.h>
#include <mutex>
#include <string>

namespace LS {
// Public and private DLLs open the same kernel object. No C++ singleton or
// thread-owned Win32 mutex can provide this cross-DLL, cross-thread contract.
class PoseLease {
 mutable std::mutex state_;
 HANDLE semaphore_=nullptr;
 bool owned_=false;
 DWORD error_=ERROR_SUCCESS;
 void Close() noexcept {
  std::lock_guard lock(state_);
  if(semaphore_){if(owned_)ReleaseSemaphore(semaphore_,1,nullptr);CloseHandle(semaphore_);}
  semaphore_=nullptr;owned_=false;
 }
public:
 PoseLease()=default;
 PoseLease(const PoseLease&)=delete;
 PoseLease& operator=(const PoseLease&)=delete;
 ~PoseLease(){Close();}
 static std::wstring NameForProcess(){return L"Local\\MHE.PoseLease.v1.pid."+std::to_wstring(GetCurrentProcessId());}
 bool Acquire(){
  std::lock_guard lock(state_);
  if(owned_)return true;
  if(!semaphore_){const auto name=NameForProcess();semaphore_=CreateSemaphoreW(nullptr,1,1,name.c_str());
   if(!semaphore_){error_=GetLastError();return false;}}
  const auto result=WaitForSingleObject(semaphore_,0);
  if(result!=WAIT_OBJECT_0){error_=result==WAIT_TIMEOUT?ERROR_BUSY:GetLastError();return false;}
  owned_=true;error_=ERROR_SUCCESS;return true;
 }
 bool Release(){
  std::lock_guard lock(state_);
  if(!owned_)return true;
  if(!ReleaseSemaphore(semaphore_,1,nullptr)){error_=GetLastError();return false;}
  owned_=false;error_=ERROR_SUCCESS;return true;
 }
 bool Owned()const{std::lock_guard lock(state_);return owned_;}
 DWORD Error()const{std::lock_guard lock(state_);return error_;}
};
} // namespace LS
