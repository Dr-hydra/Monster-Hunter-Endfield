#pragma once
#include "retarget.h"

namespace LS {
struct GripFrame{Vec3 offset{};float weight=0;};
struct GripTrack{
 float fps=60;std::vector<GripFrame> frames;
 GripFrame Sample(float seconds)const{
  if(frames.empty()||!std::isfinite(seconds)||!std::isfinite(fps)||fps<=0)throw std::runtime_error("Invalid grip sample");
  const double at=std::clamp(double(seconds)*fps,0.,double(frames.size()-1));const size_t i=size_t(at);
  if(i+1==frames.size())return frames[i];const float t=float(at-i);
  return {Lerp(frames[i].offset,frames[i+1].offset,t),frames[i].weight+(frames[i+1].weight-frames[i].weight)*t};
 }
};
// Explicit replacement format: LSG1, count, integer FPS, components=4;
// each little-endian float row is offset XYZ in source metres then weight.
inline GripTrack ReadGrip(const std::filesystem::path& path,size_t expectedFrames){
 std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("Cannot open grip track");
 const auto size=file.tellg();if(size<16||size>4*1024*1024)throw std::runtime_error("Invalid grip size");file.seekg(0);
 std::array<unsigned char,16> header{};file.read(reinterpret_cast<char*>(header.data()),16);
 const auto u32=[&](size_t at){return uint32_t(header[at])|(uint32_t(header[at+1])<<8)|(uint32_t(header[at+2])<<16)|(uint32_t(header[at+3])<<24);};
 const uint32_t count=u32(4),fps=u32(8),components=u32(12);
 if(std::memcmp(header.data(),"LSG1",4)!=0||count!=expectedFrames||!count||count>36000||!fps||fps>240||components!=4||uint64_t(size)!=16+uint64_t(count)*16)throw std::runtime_error("Grip dimensions mismatch");
 GripTrack track;track.fps=float(fps);track.frames.reserve(count);
 for(uint32_t i=0;i<count;++i){float v[4]{};if(!file.read(reinterpret_cast<char*>(v),16))throw std::runtime_error("Truncated grip track");
  GripFrame frame{{v[0],v[1],v[2]},v[3]};
  if(!Finite(frame.offset)||Retarget::Len(frame.offset)>3||!std::isfinite(frame.weight)||frame.weight<0||frame.weight>1)throw std::runtime_error("Invalid grip frame");
  track.frames.push_back(frame);}
 return track;
}
} // namespace LS
