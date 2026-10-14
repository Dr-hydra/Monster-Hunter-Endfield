#pragma once
#include "core.h"

namespace LS {
// These weights are rebuilt from source hilt geometry and the specific
// target mesh. They are separate from the original animation's IK flags.
struct GeometryContactMask {
 float fps=60;
 std::vector<float> weights;
 float Sample(float seconds)const{
  if(weights.empty()||!std::isfinite(seconds)||seconds<0)return 0;
  const float frame=std::clamp(seconds*fps,0.f,float(weights.size()-1));
  const size_t a=size_t(frame),b=std::min(a+1,weights.size()-1);
  return weights[a]+(weights[b]-weights[a])*(frame-float(a));
 }
};
using GeometryContactModels=std::map<std::string,std::map<std::string,std::map<int,GeometryContactMask>>>;
}
