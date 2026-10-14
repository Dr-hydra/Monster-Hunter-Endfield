#pragma once
#include "core.h"

namespace LS {
struct FingerTrack{
 float fps=60;std::vector<std::vector<Quat>> frames;
 std::vector<Quat> Sample(float seconds)const{
  if(frames.empty()||!std::isfinite(seconds)||seconds<0||!std::isfinite(fps)||fps<=0)throw std::runtime_error("Invalid finger sample");
  const double at=std::clamp(double(seconds)*fps,0.,double(frames.size()-1));const size_t index=size_t(at);
  if(index+1==frames.size())return frames[index];auto result=frames[index];for(size_t i=0;i<result.size();++i)result[i]=Slerp(result[i],frames[index+1][i],float(at-index));return result;
 }
};
inline FingerTrack ReadFingers(const std::filesystem::path& path,size_t expectedFrames,size_t expectedBones){
 std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("Missing source finger track");const auto size=file.tellg();
 if(size<16||size>128*1024*1024)throw std::runtime_error("Invalid LSF size");file.seekg(0);std::array<uint32_t,4> header{};
 if(!file.read(reinterpret_cast<char*>(header.data()),16)||std::memcmp(header.data(),"LSF1",4)||header[1]!=expectedFrames||header[2]!=expectedBones||
   !header[1]||header[1]>36000||!header[2]||header[2]>96||!header[3]||header[3]>240||uint64_t(size)!=16+uint64_t(header[1])*header[2]*16)throw std::runtime_error("Invalid LSF dimensions");
 FingerTrack result;result.fps=float(header[3]);result.frames.reserve(header[1]);
 for(size_t frame=0;frame<header[1];++frame){std::vector<Quat> row(header[2]);if(!file.read(reinterpret_cast<char*>(row.data()),std::streamsize(row.size()*16)))throw std::runtime_error("Truncated LSF");
  for(auto& quaternion:row){if(!Finite(quaternion)||std::abs(Dot(quaternion,quaternion)-1)>.02f)throw std::runtime_error("Invalid LSF quaternion");quaternion=Normalize(quaternion);}result.frames.push_back(std::move(row));}
 return result;
}
inline std::string TargetFingerName(const std::string& semantic){
 const auto split=semantic.find('_');if(split==std::string::npos||semantic.size()<3)return {};
 const auto hand=semantic.substr(0,split),finger=semantic.substr(split+1);if(hand!="left"&&hand!="right")return {};
 if(finger.back()<'0'||finger.back()>'2')return {};const auto kind=finger.substr(0,finger.size()-1);
 const std::map<std::string,int> indices{{"thumb",0},{"index",1},{"middle",2},{"ring",3},{"pinky",4}};
 auto found=indices.find(kind);if(found==indices.end())return {};std::string suffix=std::to_string(found->second);
 if(finger.back()!='0')suffix+=finger.back();return "Bip001_"+std::string(hand=="left"?"L":"R")+"_Finger"+suffix;
}
} // namespace LS
