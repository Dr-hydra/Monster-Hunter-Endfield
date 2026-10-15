#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace LS {
// IL2CPP reports boxed field offsets. Derive the header at runtime and reject
// layouts that cannot fit the caller-owned out buffer before invoking Physics.
struct RaycastHitLayout {
 static constexpr size_t Capacity=128;
 size_t size=0,point=0,normal=0,distance=0;
 bool Configure(int32_t valueSize,size_t header,int32_t pointOffset,int32_t normalOffset,int32_t distanceOffset){
  *this={};
  if(valueSize<=0||valueSize>Capacity||header==0||header>64)return false;
  const std::array<int32_t,3> offsets{pointOffset,normalOffset,distanceOffset};
  const std::array<size_t,3> widths{12,12,4};std::array<size_t,3> relative{};
  for(size_t i=0;i<offsets.size();++i){
   if(offsets[i]<0||static_cast<size_t>(offsets[i])<header)return false;
   relative[i]=static_cast<size_t>(offsets[i])-header;
   if(relative[i]%alignof(float)||relative[i]>static_cast<size_t>(valueSize)||widths[i]>static_cast<size_t>(valueSize)-relative[i])return false;
   for(size_t j=0;j<i;++j)if(relative[i]<relative[j]+widths[j]&&relative[j]<relative[i]+widths[i])return false;
  }
  size=static_cast<size_t>(valueSize);point=relative[0];normal=relative[1];distance=relative[2];return true;
 }
 explicit operator bool()const{return size!=0;}
};
inline bool NativeHookFlagsMatch(uint32_t flags,bool isStatic){
 return !(flags&0x400u)&&((flags&0x10u)!=0)==isStatic;
}
}
