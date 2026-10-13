#pragma once
#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace LS::CharacterBinding {
struct SkeletonCandidate {
    std::string path;
    unsigned depth=0;
    unsigned mainBoneCoverage=0;
    bool skinned=false;
};
inline std::size_t SelectPrimarySkeleton(const std::vector<SkeletonCandidate>& candidates,
    const std::string& preferredPath={}) {
    std::vector<std::size_t> eligible;
    for(std::size_t i=0;i<candidates.size();++i)
        if(candidates[i].mainBoneCoverage==21&&candidates[i].skinned)eligible.push_back(i);
    if(eligible.empty())throw std::runtime_error("No complete skinned main character skeleton");
    if(!preferredPath.empty()) {
        std::vector<std::size_t> exact;
        for(auto i:eligible)if(candidates[i].path==preferredPath)exact.push_back(i);
        if(exact.size()==1)return exact.front();
        if(exact.size()>1)throw std::runtime_error("Duplicate configured skeleton path");
        // A prefab wrapper can add a path prefix. Only admit a unique shallow
        // complete palette; a deeper same-name soldier is never chosen first.
    }
    unsigned shallowest=std::numeric_limits<unsigned>::max();
    for(auto i:eligible)shallowest=std::min(shallowest,candidates[i].depth);
    std::size_t selected=0,count=0;
    for(auto i:eligible)if(candidates[i].depth==shallowest){selected=i;++count;}
    if(count!=1)throw std::runtime_error("Ambiguous main skeleton; configure its full path");
    return selected;
}
}
