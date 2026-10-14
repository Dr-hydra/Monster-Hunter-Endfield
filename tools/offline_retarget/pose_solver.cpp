#include "../../native/retarget.h"
using namespace LS;using namespace LS::Retarget;
// Offline skin audit calls the very same solver as the native runtime. Input
// points are world-space shoulder/elbow/support/dominant; wrists are preserved.
extern "C" __declspec(dllexport) int MheSolveSupportingHand(const float* points,const float* rotations,const float* offset,float strength,const float* pole,float* output){
 try{
  auto v=[](const float*p){return Vec3{p[0],p[1],p[2]};};auto q=[](const float*p){return Normalize(Quat{p[0],p[1],p[2],p[3]});};
  Vec3 shoulder=v(points),elbow=v(points+3),hand=v(points+6),dominant=v(points+9);
  if(!Finite(shoulder)||!Finite(elbow)||!Finite(hand)||!Finite(dominant)||!std::isfinite(strength))return 0;
  auto goal=Lerp(hand,Add(dominant,v(offset)),std::clamp(strength,0.f,1.f));
  auto solved=SolveArm(shoulder,goal,elbow,Len(Sub(elbow,shoulder)),Len(Sub(hand,elbow)),v(pole));
  auto upperDelta=FromTo(Sub(elbow,shoulder),Sub(solved.elbow,shoulder));
  auto movedElbow=Add(shoulder,Rotate(upperDelta,Sub(elbow,shoulder)));
  auto movedHand=Add(movedElbow,Rotate(upperDelta,Sub(hand,elbow)));
  auto upper=Mul(upperDelta,q(rotations));
  auto lower=Mul(FromTo(Sub(movedHand,movedElbow),Sub(solved.hand,movedElbow)),Mul(upperDelta,q(rotations+4)));
  Quat result[]{upper,lower,q(rotations+8)};std::memcpy(output,result,sizeof(result));
  return Finite(upper)&&Finite(lower);
 }catch(...){return 0;}
}
extern "C" __declspec(dllexport) int MheSolveTwoHandGrip(const float* points,const float* rotations,
 const float* offset,float strength,const float* poles,float* output){
 try{
  auto v=[](const float*p){return Vec3{p[0],p[1],p[2]};};auto q=[](const float*p){return Normalize(Quat{p[0],p[1],p[2],p[3]});};
  const auto solved=SolveTwoHandGrip(v(points),v(points+3),v(points+6),v(points+9),v(points+12),v(points+15),
                                  v(offset),strength,v(poles),v(poles+3));
  if(!solved.reachable)return 0;
  for(int side=0;side<2;++side){
   auto shoulder=v(points+side*9),elbow=v(points+side*9+3),hand=v(points+side*9+6);
   const auto&arm=side==0?solved.dominant:solved.support;
   const auto upperDelta=FromTo(Sub(elbow,shoulder),Sub(arm.elbow,shoulder));
   const auto movedElbow=Add(shoulder,Rotate(upperDelta,Sub(elbow,shoulder)));
   const auto movedHand=Add(movedElbow,Rotate(upperDelta,Sub(hand,elbow)));
   auto upper=Mul(upperDelta,q(rotations+side*12));
   auto lower=Mul(FromTo(Sub(movedHand,movedElbow),Sub(arm.hand,movedElbow)),Mul(upperDelta,q(rotations+side*12+4)));
   Quat result[]{upper,lower,q(rotations+side*12+8)};
   if(!Finite(upper)||!Finite(lower))return 0;
   std::memcpy(output+side*12,result,sizeof(result));
  }
  return 1;
 }catch(...){return 0;}
}
