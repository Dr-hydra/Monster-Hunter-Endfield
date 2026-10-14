#pragma once
#include "core.h"

namespace LS::SourceSkin {
using Matrix=std::array<float,16>;
inline Matrix Identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
inline bool Valid(const Matrix&m){for(float v:m)if(!std::isfinite(v)||std::abs(v)>100000)return false;return std::abs(m[3])<.001f&&std::abs(m[7])<.001f&&std::abs(m[11])<.001f&&std::abs(m[15]-1)<.001f;}
inline Vec3 Point(const Matrix&m,Vec3 p){return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};}
struct Track {
 float fps=60;std::vector<std::vector<Matrix>> frames;
 std::vector<Matrix> Sample(float seconds)const{
  if(frames.empty()||!std::isfinite(seconds)||seconds<0||!std::isfinite(fps)||fps<=0)throw std::runtime_error("Invalid source skin sample");
  const double position=std::clamp(double(seconds)*fps,0.,double(frames.size()-1));const size_t i=size_t(position);
  if(i+1==frames.size())return frames[i];auto result=frames[i];const float amount=float(position-i);
  // Vertex-wise interpolation between exact 60 Hz original deformations. It
  // does not invent a native controller's transition/RTPC interpolation.
  for(size_t b=0;b<result.size();++b)for(size_t k=0;k<16;++k)result[b][k]+=(frames[i+1][b][k]-result[b][k])*amount;
  return result;
 }
};
inline Track Read(const std::filesystem::path&path,size_t expectedFrames,size_t expectedBones){
 std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("Missing source skin track");const auto size=file.tellg();
 if(size<16||size>256*1024*1024)throw std::runtime_error("Invalid source skin size");file.seekg(0);std::array<uint32_t,4>header{};
 if(!file.read(reinterpret_cast<char*>(header.data()),16)||std::memcmp(header.data(),"LSK1",4)||header[1]!=expectedFrames||header[2]!=expectedBones||
  !header[1]||header[1]>36000||!header[2]||header[2]>128||!header[3]||header[3]>240||uint64_t(size)!=16+uint64_t(header[1])*header[2]*64)throw std::runtime_error("Invalid source skin dimensions");
 Track result;result.fps=float(header[3]);result.frames.resize(header[1],std::vector<Matrix>(header[2]));
 for(auto&row:result.frames){if(!file.read(reinterpret_cast<char*>(row.data()),std::streamsize(row.size()*sizeof(Matrix))))throw std::runtime_error("Truncated source skin track");for(const auto&m:row)if(!Valid(m))throw std::runtime_error("Invalid source skin affine matrix");}
 return result;
}
using Indices=std::array<uint32_t,4>;using Weights=std::array<float,4>;
inline std::vector<Vec3> Deform(const std::vector<Vec3>&vertices,const std::vector<Indices>&indices,const std::vector<Weights>&weights,const std::vector<Matrix>&pose){
 if(vertices.empty()||vertices.size()>250000||indices.size()!=vertices.size()||weights.size()!=vertices.size()||pose.empty()||pose.size()>128)throw std::runtime_error("Invalid source skin buffers");
 for(const auto&m:pose)if(!Valid(m))throw std::runtime_error("Invalid source skin pose");std::vector<Vec3>out(vertices.size());
 for(size_t i=0;i<vertices.size();++i){if(!Finite(vertices[i]))throw std::runtime_error("Invalid source skin vertex");float total=0;
  for(size_t j=0;j<4;++j){const float w=weights[i][j];if(!std::isfinite(w)||w<0||w>1||indices[i][j]>=pose.size())throw std::runtime_error("Invalid source skin influence");total+=w;if(w==0)continue;const auto p=Point(pose[indices[i][j]],vertices[i]);out[i].x+=w*p.x;out[i].y+=w*p.y;out[i].z+=w*p.z;}
  if(std::abs(total-1)>.002f||!Finite(out[i]))throw std::runtime_error("Invalid source skin weight sum");
 }
 return out;
}
} // namespace LS::SourceSkin
