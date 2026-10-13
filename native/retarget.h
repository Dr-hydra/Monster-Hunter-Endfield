#pragma once
#include "core.h"

namespace LS::Retarget {
inline Vec3 Add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 Sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 Scale(Vec3 a,float s){return {a.x*s,a.y*s,a.z*s};}
inline float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float Len(Vec3 a){return std::sqrt(Dot(a,a));}
inline Vec3 Normalize(Vec3 a){const float n=Len(a);if(!Finite(a)||!std::isfinite(n)||n<1e-8f)throw std::runtime_error("Degenerate retarget direction");return Scale(a,1/n);}
inline Quat FromTo(Vec3 from,Vec3 to){
 from=Normalize(from);to=Normalize(to);const float d=std::clamp(Dot(from,to),-1.f,1.f);
 if(d>1-1e-7f)return {};
 if(d< -1+1e-6f){Vec3 orthogonal=Cross(from,std::abs(from.x)<.7f?Vec3{1,0,0}:Vec3{0,1,0});orthogonal=Normalize(orthogonal);return {orthogonal.x,orthogonal.y,orthogonal.z,0};}
 const auto axis=Cross(from,to);return LS::Normalize({axis.x,axis.y,axis.z,1+d});
}
// Columns are a right-handed, orthonormal frame. No scale/shear is retained.
inline Quat BasisRotation(Vec3 x,Vec3 y){
 x=Normalize(x);y=Normalize(Sub(y,Scale(x,Dot(y,x))));const auto z=Normalize(Cross(x,y));
 const float m00=x.x,m01=y.x,m02=z.x,m10=x.y,m11=y.y,m12=z.y,m20=x.z,m21=y.z,m22=z.z;
 Quat q;const float trace=m00+m11+m22;
 if(trace>0){const float s=2*std::sqrt(trace+1);q={(m21-m12)/s,(m02-m20)/s,(m10-m01)/s,s/4};}
 else if(m00>m11&&m00>m22){const float s=2*std::sqrt(1+m00-m11-m22);q={s/4,(m01+m10)/s,(m02+m20)/s,(m21-m12)/s};}
 else if(m11>m22){const float s=2*std::sqrt(1+m11-m00-m22);q={(m01+m10)/s,s/4,(m12+m21)/s,(m02-m20)/s};}
 else{const float s=2*std::sqrt(1+m22-m00-m11);q={(m02+m20)/s,(m12+m21)/s,s/4,(m10-m01)/s};}
 return LS::Normalize(q);
}
inline Quat SourceToTargetBodyBasis(Vec3 sourceRight,Vec3 sourceUp,Vec3 targetRight,Vec3 targetUp){return Mul(BasisRotation(targetRight,targetUp),Inverse(BasisRotation(sourceRight,sourceUp)));}
inline Quat AlignedBoneRest(Quat targetRest,Vec3 targetDirection,Vec3 sourceDirection,Quat basis){return Mul(FromTo(targetDirection,Rotate(basis,sourceDirection)),targetRest);}
inline Quat CarryRestAlignment(Quat alignedParent,Quat parentRest,Quat childRest){return Mul(Mul(alignedParent,Inverse(parentRest)),childRest);}
inline Quat PalmBasis(Vec3 forward,Vec3 across){return BasisRotation(across,forward);}
inline Quat AlignedPalmRest(Quat targetRest,Quat sourcePalm,Quat targetPalm,Quat basis){return Mul(Mul(Mul(basis,sourcePalm),Inverse(targetPalm)),targetRest);}
// LSM stores world rest deltas, not absolute source/local rotations.
inline Quat ApplySourceDelta(Quat sourceDelta,Quat targetAlignedRest,Quat basis){return Mul(Mul(Mul(basis,sourceDelta),Inverse(basis)),targetAlignedRest);}
struct ArmSolution{Vec3 elbow{},hand{};bool clamped=false;};
inline ArmSolution SolveArm(Vec3 shoulder,Vec3 goal,Vec3 currentElbow,float upper,float lower,Vec3 fallbackPole){
 if(!Finite(shoulder)||!Finite(goal)||!Finite(currentElbow)||!std::isfinite(upper)||!std::isfinite(lower)||upper<1e-6f||lower<1e-6f)throw std::runtime_error("Invalid arm IK input");
 auto direction=Sub(goal,shoulder);const float requested=Len(direction);
 if(requested<1e-7f)direction=Sub(currentElbow,shoulder);
 direction=Normalize(direction);
 const float maximum=upper+lower,minimum=std::abs(upper-lower);
 const float epsilon=std::min(1e-4f,std::min(upper,lower)*.001f);
 const float distance=std::clamp(requested,minimum+epsilon,maximum-epsilon);
 Vec3 pole=Sub(currentElbow,shoulder);pole=Sub(pole,Scale(direction,Dot(pole,direction)));
 if(Len(pole)<1e-6f){pole=Sub(fallbackPole,Scale(direction,Dot(fallbackPole,direction)));}
 if(Len(pole)<1e-6f)pole=Cross(direction,std::abs(direction.y)<.7f?Vec3{0,1,0}:Vec3{1,0,0});
 pole=Normalize(pole);
 const float projection=(upper*upper-lower*lower+distance*distance)/(2*distance);
 const float height=std::sqrt(std::max(0.f,upper*upper-projection*projection));
 return {Add(shoulder,Add(Scale(direction,projection),Scale(pole,height))),Add(shoulder,Scale(direction,distance)),std::abs(requested-distance)>1e-6f};
}
} // namespace LS::Retarget
