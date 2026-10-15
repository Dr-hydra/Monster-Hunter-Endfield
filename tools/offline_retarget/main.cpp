#include "../../native/retarget.h"
#include <nlohmann/json.hpp>
#include <set>
#include <iostream>
using namespace LS;using Json=nlohmann::json;
Json Read(const std::filesystem::path&p){std::ifstream f(p);if(!f)throw std::runtime_error("json missing "+p.string());Json j;f>>j;return j;}
Vec3 V(const Json&a){return{a[0].get<float>(),a[1].get<float>(),a[2].get<float>()};}
Quat Q(const Json&a){return Normalize({a[0].get<float>(),a[1].get<float>(),a[2].get<float>(),a[3].get<float>()});}
Quat MatrixQ(const Json&m){return Retarget::BasisRotation({m[0][0].get<float>(),m[1][0].get<float>(),m[2][0].get<float>()},{m[0][1].get<float>(),m[1][1].get<float>(),m[2][1].get<float>()});}
Vec3 MatrixP(const Json&m){return{m[0][3].get<float>(),m[1][3].get<float>(),m[2][3].get<float>()};}
void U32(std::ofstream&out,uint32_t v){out.write(reinterpret_cast<const char*>(&v),4);}
int main(int argc,char**argv){try{
 if(argc<3||argc>6)throw std::runtime_error("endfield_retarget_oracle projectRoot outputDirectory [mode] [comma-separated slots] [comma-separated characters]");std::filesystem::path root=argv[1],outRoot=argv[2];
 const std::string selectedMode=argc>=4?argv[3]:"";std::set<int>selectedSlots;
 if(argc>=5){std::stringstream stream(argv[4]);std::string value;while(std::getline(stream,value,','))if(!value.empty())selectedSlots.insert(std::stoi(value));}
 std::set<std::string>selectedCharacters;
 if(argc==6){std::stringstream stream(argv[5]);std::string value;while(std::getline(stream,value,','))if(!value.empty())selectedCharacters.insert(value);}
 auto index=Read(root/"profiles/index.json"),modeRegistry=Read(root/"assets/modes/index.json");
 for(const auto&entry:index["characters"]){auto cid=entry["character_id"].get<std::string>();if(!selectedCharacters.empty()&&!selectedCharacters.contains(cid))continue;auto profile=Read(root/"profiles"/entry["profile"].get<std::string>());auto rig=Read(root/"research/character-coverage"/(cid+"-rig.json"));const auto&rest=rig["registered_model_rest"];
  for(const auto&modeEntry:index["modes"]){auto mode=modeEntry.get<std::string>();if(!selectedMode.empty()&&mode!=selectedMode)continue;auto assets=root/"assets/modes"/mode;for(const auto&r:modeRegistry["modes"])if(r["id"]==mode)assets=root/r["data_root"].get<std::string>();auto source=Read(assets/"source_rig.json");auto moves=Read(assets/"moves.json");const auto order=source["order"].get<std::vector<std::string>>();
   std::vector<std::string>bonePaths;std::vector<Vec3>positions,sourcePositions;std::vector<Quat>original,aligned;
   for(const auto&semantic:order){auto path=profile["bone_paths"][semantic].get<std::string>();bonePaths.push_back(path);positions.push_back(MatrixP(rest[path]));original.push_back(MatrixQ(rest[path]));sourcePositions.push_back(V(source["bones"][semantic]["position"]));}
   aligned=original;auto basis=Retarget::SourceToTargetBodyBasis(Retarget::Sub(sourcePositions[10],sourcePositions[6]),Retarget::Sub(sourcePositions[2],sourcePositions[0]),Retarget::Sub(positions[10],positions[6]),Retarget::Sub(positions[2],positions[0]));
   for(auto[bone,child]:std::array<std::pair<int,int>,6>{{{5,6},{6,7},{7,8},{9,10},{10,11},{11,12}}})aligned[bone]=Retarget::AlignedBoneRest(original[bone],Retarget::Sub(positions[child],positions[bone]),Retarget::Sub(sourcePositions[child],sourcePositions[bone]),basis);
   for(auto[hand,forearm]:std::array<std::pair<int,int>,2>{{{8,7},{12,11}}})aligned[hand]=Retarget::CarryRestAlignment(aligned[forearm],original[forearm],original[hand]);
   for(int side=0;side<2;++side){auto hand=side==0?8:12;auto prefix=side==0?"Bip001_L_Finger":"Bip001_R_Finger";auto finger=[&](int n){auto path=profile["finger_bones"][std::string(prefix)+std::to_string(n)].get<std::string>();return MatrixP(rest[path]);};auto anatomical=source["hand_anatomy"][side==0?"left":"right"];aligned[hand]=Retarget::AlignedPalmRest(original[hand],Retarget::PalmBasis(V(anatomical["forward"]),V(anatomical["across"])),Retarget::PalmBasis(Retarget::Sub(finger(2),positions[hand]),Retarget::Sub(finger(1),finger(4))),basis);}
   auto sourceRight=Retarget::Sub(sourcePositions[10],sourcePositions[6]);sourceRight.y=0;auto targetRight=Retarget::Sub(positions[10],positions[6]);targetRight.y=0;
   auto stageBasis=Retarget::SourceToTargetBodyBasis(sourceRight,{0,1,0},targetRight,{0,1,0});
   float foot=std::min({positions[15].y,positions[16].y,positions[19].y,positions[20].y});foot=std::min(foot,profile.value("ground_foot_mesh_rest_min_y",foot));float sourceFoot=1e6f;for(int i:{15,16,19,20})sourceFoot=std::min(sourceFoot,Rotate(stageBasis,sourcePositions[i]).y);float sourceHeight=Rotate(stageBasis,sourcePositions[0]).y-sourceFoot;const float actual=source.contains("sole_geometry")?source["sole_geometry"].value("hip_height",1.0568615f):1.0568615f;if(actual>.2f&&actual<3.f)sourceHeight=actual;float scale=(positions[0].y-foot)/sourceHeight;if(!std::isfinite(scale)||scale<.25f||scale>4.f)throw std::runtime_error("Unsupported measured character scale");
   std::set<int>slots;auto commands=moves.contains("commands")?moves["commands"]:moves["moves"];for(const auto&[name,sequence]:commands.items()){(void)name;for(const auto&v:sequence)slots.insert(v.get<int>());}if(moves.contains("idle"))slots.insert(moves["idle"].get<int>());
   if(!selectedSlots.empty())slots=selectedSlots;
   auto fingerRoot=mode=="longsword"?root/"assets/modes/longsword":assets;
   Json fingers=std::filesystem::exists(fingerRoot/"finger_tracks.json")?Read(fingerRoot/"finger_tracks.json"):Json{};std::vector<int>fingerIndices;std::vector<std::string>fingerPaths;std::vector<Quat>fingerRest;
   if(fingers.contains("bones")){int i=0;for(const auto&b:fingers["bones"]){auto name=b["name"].get<std::string>();auto side=name.starts_with("left_")?"L":"R";auto pos=name.find('_');auto semantic=name.substr(pos+1);int n=semantic.starts_with("thumb")?0:semantic.starts_with("index")?1:semantic.starts_with("middle")?2:semantic.starts_with("ring")?3:semantic.starts_with("pinky")?4:-1;
     if(n>=0&&!semantic.empty()&&semantic.back()>='0'&&semantic.back()<='2'){int segment=semantic.back()-'0';auto nativeName=std::string("Bip001_")+side+"_Finger"+std::to_string(n)+(segment?std::to_string(segment):"");if(profile["finger_bones"].contains(nativeName)){auto path=profile["finger_bones"][nativeName].get<std::string>();fingerIndices.push_back(i);fingerPaths.push_back(path);int hand=side==std::string("L")?8:12;fingerRest.push_back(Retarget::CarryRestAlignment(aligned[hand],original[hand],MatrixQ(rest[path])));}}
     ++i;}}
   auto destination=outRoot/cid;std::filesystem::create_directories(destination);std::ofstream output(destination/(mode+".ltp"),std::ios::binary);output.write("LTP1",4);U32(output,uint32_t(slots.size()));U32(output,uint32_t(bonePaths.size()+fingerPaths.size()));U32(output,1);
   Json metadata{{"schema",2},{"character_id",cid},{"mode",mode},{"bone_paths",bonePaths},{"finger_paths",fingerPaths},{"source_basis_xyzw",{basis.x,basis.y,basis.z,basis.w}},{"source_stage_basis_xyzw",{stageBasis.x,stageBasis.y,stageBasis.z,stageBasis.w}},{"scale",scale},{"operation","native retarget.h exact functions + source LSM/LSF deltas; registered skin target rest; upright stage motion distinct from body rest basis"},{"runtime_verified",false},{"clips",Json::array()}};
   metadata["target_aligned_rest_xyzw"]=Json::array();metadata["source_rest_xyzw"]=Json::array();
   for(size_t i=0;i<aligned.size();++i){auto a=aligned[i],s=Q(source["bones"][order[i]]["rotation"]);metadata["target_aligned_rest_xyzw"].push_back({a.x,a.y,a.z,a.w});metadata["source_rest_xyzw"].push_back({s.x,s.y,s.z,s.w});}
   uint64_t frames=0;std::map<int,std::string>clipFiles;for(const auto& c:Read(assets/"catalog.json"))clipFiles[c["slot"].get<int>()]=c["file"].get<std::string>();
   for(int slot:slots){auto clip=ReadClip(assets/clipFiles.at(slot));U32(output,uint32_t(slot));U32(output,uint32_t(clip.frames.size()));U32(output,uint32_t(clip.fps));
    std::vector<float>fingerData;uint32_t fingerCount=0;
    if(fingers.contains("tracks")&&fingers["tracks"].contains(std::to_string(slot))){auto path=fingerRoot/fingers["tracks"][std::to_string(slot)]["file"].get<std::string>();std::ifstream f(path,std::ios::binary);char header[16];f.read(header,16);std::memcpy(&fingerCount,header+8,4);fingerData.resize(clip.frames.size()*fingerCount*4);f.read(reinterpret_cast<char*>(fingerData.data()),std::streamsize(fingerData.size()*4));if(!f)throw std::runtime_error("finger dimension mismatch");}
    for(size_t frame=0;frame<clip.frames.size();++frame){for(size_t i=0;i<bonePaths.size();++i){auto q=Retarget::ApplySourceDelta(clip.frames[frame].rotations[i],aligned[i],basis);output.write(reinterpret_cast<const char*>(&q),sizeof(q));}for(size_t i=0;i<fingerPaths.size();++i){Quat delta{};if(fingerCount){auto p=fingerData.data()+(frame*fingerCount+fingerIndices[i])*4;delta={p[0],p[1],p[2],p[3]};}auto q=Retarget::ApplySourceDelta(delta,fingerRest[i],basis);output.write(reinterpret_cast<const char*>(&q),sizeof(q));}auto hip=Retarget::Scale(Rotate(stageBasis,clip.frames[frame].hip),scale);output.write(reinterpret_cast<const char*>(&hip),sizeof(hip));}
    metadata["clips"].push_back({{"slot",slot},{"frames",clip.frames.size()},{"fps",clip.fps},{"finger_frames",fingerCount?clip.frames.size():0}});frames+=clip.frames.size();}
   metadata["frames"]=frames;std::ofstream(destination/(mode+".json"))<<metadata.dump();std::cout<<cid<<' '<<mode<<' '<<frames<<" frames\n";
  }
 }
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
