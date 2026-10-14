#pragma once
#include "retarget.h"

namespace LS::WeaponPose {
struct Bounds{Vec3 center{},extents{};};
struct AxisEvidence{Vec3 axis{};float length=0;};
// Bounds provide a local-practice blade axis, never an MHW mounting rotation.
inline AxisEvidence InferAxis(Vec3 a,Vec3 b,Vec3 grip,float width){
 using namespace Retarget;
 if(!Finite(a)||!Finite(b)||!Finite(grip)||!std::isfinite(width)||width<0)return {};
 const float length=Len(Sub(b,a));if(length<.15f||length<std::max(.01f,width)*1.5f)return {};
 const float da=Len(Sub(a,grip)),db=Len(Sub(b,grip));
 if(std::abs(da-db)<length*.08f)return {}; // a central grip has ambiguous sign
 return {Normalize(Sub(da>db?a:b,grip)),std::max(da,db)};
}
struct Attachment{Vec3 position{};Quat rotation{};bool valid=false;};
struct SourceFrame{Quat rotation{};Vec3 supportOffset{};float supportWeight=0;Attachment main,offhand;};
inline Attachment Blend(const Attachment&a,const Attachment&b,float t){if(!a.valid||!b.valid)return b;return {Lerp(a.position,b.position,t),Slerp(a.rotation,b.rotation,t),true};}
inline SourceFrame Blend(const SourceFrame& a,const SourceFrame& b,float t){t=std::clamp(t,0.f,1.f);return {Slerp(a.rotation,b.rotation,t),Lerp(a.supportOffset,b.supportOffset,t),a.supportWeight+(b.supportWeight-a.supportWeight)*t,Blend(a.main,b.main,t),Blend(a.offhand,b.offhand,t)};}
struct SourceTrack{
 float fps=60;std::vector<SourceFrame> frames;
 SourceFrame Sample(float seconds)const{
  if(frames.empty()||!std::isfinite(fps)||fps<=0||!std::isfinite(seconds))throw std::runtime_error("Invalid source hand track");
  const double at=std::clamp(double(seconds)*fps,0.,double(frames.size()-1));const size_t i=size_t(at);
  return i+1<frames.size()?Blend(frames[i],frames[i+1],float(at-i)):frames.back();
 }
};
class SourceBlend{
 uint64_t motion_=0;bool valid_=false;SourceFrame previous_{},from_{};
public:
 SourceFrame Sample(uint64_t motion,float seconds,float blendSeconds,const SourceFrame& frame){
  if(!std::isfinite(seconds)||!std::isfinite(blendSeconds)||seconds<0||blendSeconds<0)throw std::runtime_error("Invalid source hand blend");
  if(!valid_){valid_=true;motion_=motion;previous_=from_=frame;return frame;}
  if(motion_!=motion){motion_=motion;from_=previous_;}
  previous_=blendSeconds>0?Blend(from_,frame,seconds/blendSeconds):frame;return previous_;
 }
 void Reset(){valid_=false;motion_=0;previous_=from_={};}
};
} // namespace LS::WeaponPose
