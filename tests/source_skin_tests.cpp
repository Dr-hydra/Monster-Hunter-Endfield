#include "source_skin.h"
#include <iostream>

using namespace LS;using namespace LS::SourceSkin;
static void Require(bool value,const char*why){if(!value)throw std::runtime_error(why);}
template<class F>static void Reject(F f,const char*why){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}Require(rejected,why);}
int main(){try{
 auto fixed=Identity(),moving=Identity();moving[0]=0;moving[1]=1;moving[4]=-1;moving[5]=0;moving[12]=2;
 const std::vector<Vec3>points{{1,0,0},{0,1,0}};const std::vector<Indices>indices{{0,1,0,0},{1,0,0,0}};const std::vector<Weights>weights{{.25f,.75f,0,0},{1,0,0,0}};
 auto deformed=Deform(points,indices,weights,{fixed,moving});Require(std::abs(deformed[0].x-1.75f)<1e-6f&&std::abs(deformed[0].y-.75f)<1e-6f,"Own two-bone palette deformation wrong");Require(std::abs(deformed[1].x-1)<1e-6f&&std::abs(deformed[1].y)<1e-6f,"Own rotation+translation wrong");
 auto bindInverse=Identity();bindInverse[12]=-3;const auto p=Point(bindInverse,{3,0,0});Require(std::abs(p.x)<1e-6f,"Original root bind normalization double-applied");
 Track track;track.fps=60;track.frames={{fixed},{moving}};auto halfway=track.Sample(1.f/120);auto hp=Point(halfway[0],{1,0,0});Require(std::abs(hp.x-1.5f)<1e-5f&&std::abs(hp.y-.5f)<1e-5f,"Exact-frame vertex interpolation wrong");Require(track.Sample(100)[0]==moving,"Source skin sample not clamped");
 Reject([&]{track.Sample(-1);},"Negative sample accepted");auto invalid=weights;invalid[0][0]=-.1f;Reject([&]{Deform(points,indices,invalid,{fixed,moving});},"Negative original influence accepted");invalid=weights;invalid[0][1]=.1f;Reject([&]{Deform(points,indices,invalid,{fixed,moving});},"Unnormalized influences accepted");auto badIndices=indices;badIndices[0][0]=99;Reject([&]{Deform(points,badIndices,weights,{fixed,moving});},"Foreign bone palette accepted");moving[15]=0;Reject([&]{Deform(points,indices,weights,{fixed,moving});},"Nonaffine matrix accepted");
 const auto file=std::filesystem::temp_directory_path()/"mhe-source-skin-fixture.lsk";{std::ofstream out(file,std::ios::binary);const std::array<uint32_t,4>h{0x314b534c,2,1,60};out.write(reinterpret_cast<const char*>(h.data()),16);out.write(reinterpret_cast<const char*>(fixed.data()),64);out.write(reinterpret_cast<const char*>(fixed.data()),64);}
 auto parsed=Read(file,2,1);Require(parsed.frames.size()==2&&parsed.frames[0][0]==fixed,"LSK original-frame reader failed");Reject([&]{Read(file,3,1);},"Incorrect frame count accepted");{std::ofstream out(file,std::ios::binary|std::ios::trunc);out.write("LSK1",4);}Reject([&]{Read(file,2,1);},"Truncated track accepted");std::filesystem::remove(file);
 std::cout<<"Source own-palette skinning, inverse bind, exact-frame interpolation and malformed input checks passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
