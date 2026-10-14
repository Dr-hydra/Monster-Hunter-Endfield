#pragma once
#include "targeting.h"
#include <map>
#include <set>

namespace LS {
// Renderer bounds are read from the live model, but they are not authored
// damage hitboxes. This conservative fallback must stay labelled approximate.
inline std::optional<HurtCapsule> BoundsHurtCapsule(Vec3 center,Vec3 extents,uint32_t part=0){
 if(!Finite(center)||!Finite(extents)||extents.x<=0||extents.y<=0||extents.z<=0||
    extents.x>40||extents.y>40||extents.z>40)return std::nullopt;
 const float radius=std::sqrt(extents.x*extents.x+extents.z*extents.z);
 // The axis covers the box's complete vertical span, so its horizontal
 // circumscribed radius also covers the top/bottom corners. It may over-cover
 // the renderer: this is an approximate fallback, not an authored hurtbox.
 const float half=extents.y;
 return HurtCapsule{{center.x,center.y-half,center.z},{center.x,center.y+half,center.z},radius,part,0};
}
// Scene snapshots are transient. The identity registry keeps a generation
// across pointer reuse and disappearance without retaining a managed object.
class TargetIdentities {
 struct Record {uintptr_t pointer=0;uint64_t server=0;uint32_t serverGeneration=0,generation=0;bool present=false;};
 std::map<uint32_t,Record> records_;std::set<uint32_t> seen_;uint32_t serial_=0;
public:
 void Begin(){seen_.clear();}
 uint32_t Observe(uint32_t id,uintptr_t pointer,uint64_t server=0,uint32_t serverGeneration=0){
  if(!id||!pointer)return 0;
  auto& record=records_[id];
  if(!record.present||record.pointer!=pointer||record.server!=server||record.serverGeneration!=serverGeneration){
   if(++serial_==0)++serial_;record.generation=serial_;
  }
  record.pointer=pointer;record.server=server;record.serverGeneration=serverGeneration;record.present=true;seen_.insert(id);
  return record.generation;
 }
 void End(){for(auto&[id,record]:records_)if(!seen_.contains(id))record.present=false;
  for(auto i=records_.begin();records_.size()>4096&&i!=records_.end();){if(!i->second.present)i=records_.erase(i);else ++i;}}
 size_t Size()const{return records_.size();}
 void Clear(){records_.clear();seen_.clear();}
};
} // namespace LS
