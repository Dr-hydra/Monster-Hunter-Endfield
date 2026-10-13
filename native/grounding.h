#pragma once
#include "retarget.h"

namespace LS {
struct TerrainHit{Vec3 point{},normal{};float distance=0;};
inline bool Walkable(const TerrainHit& hit,float expectedY){
 if(!Finite(hit.point)||!Finite(hit.normal)||!std::isfinite(hit.distance)||hit.distance<0||!std::isfinite(expectedY))return false;
 const float n=Retarget::Len(hit.normal);return n>1e-6f&&hit.normal.y/n>=.64f&&hit.point.y<=expectedY+.65f&&hit.point.y>=expectedY-1.2f;
}
struct GroundTrack{
 float fps=60;std::vector<float> clearance;
 float Sample(float seconds)const{
  if(clearance.empty()||!std::isfinite(seconds)||!std::isfinite(fps)||fps<=0)throw std::runtime_error("Invalid ground sample");
  const double at=std::clamp(double(seconds)*fps,0.,double(clearance.size()-1));const size_t i=size_t(at);
  return i+1<clearance.size()?clearance[i]+(clearance[i+1]-clearance[i])*float(at-i):clearance[i];
 }
};
template<size_t N> float FloorCorrection(const std::array<Vec3,N>& feet,const std::array<float,N>& floors,float soleOffset,float clearance){
 if(!std::isfinite(soleOffset)||!std::isfinite(clearance)||clearance<0)throw std::runtime_error("Invalid ground calibration");
 float correction=-std::numeric_limits<float>::infinity();
 for(size_t i=0;i<N;++i){if(!Finite(feet[i])||!std::isfinite(floors[i]))throw std::runtime_error("Invalid foot/terrain sample");correction=std::max(correction,floors[i]+soleOffset+clearance-feet[i].y);}
 return std::isfinite(correction)?correction:0;
}
} // namespace LS
