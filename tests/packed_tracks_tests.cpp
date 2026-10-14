#include "../native/module.cpp"
#include "test_support.h"

namespace {
Json ReadJson(const std::filesystem::path& path){std::ifstream file(path);if(!file)throw std::runtime_error("Missing test JSON "+path.string());Json value;file>>value;return value;}
void WriteJson(const std::filesystem::path& path,const Json& value){std::ofstream file(path);file<<value.dump();if(!file)throw std::runtime_error("Cannot write test JSON");}
void WriteBytes(const std::filesystem::path& path,const std::vector<unsigned char>& data){std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));if(!file)throw std::runtime_error("Cannot write bank fixture");}
void Put32(std::vector<unsigned char>& data,size_t at,uint32_t value){for(int i=0;i<4;++i)data.at(at+i)=static_cast<unsigned char>((value>>(i*8))&255);}
void PutFloat(std::vector<unsigned char>& data,size_t at,float value){std::memcpy(data.data()+at,&value,4);}
bool SameFloat(float a,float b){return std::memcmp(&a,&b,4)==0;}
bool Same(Vec3 a,Vec3 b){return SameFloat(a.x,b.x)&&SameFloat(a.y,b.y)&&SameFloat(a.z,b.z);}
bool Same(Quat a,Quat b){return SameFloat(a.x,b.x)&&SameFloat(a.y,b.y)&&SameFloat(a.z,b.z)&&SameFloat(a.w,b.w);}
void SourceExact(const std::map<int,WeaponPose::SourceTrack>& old,const std::map<int,WeaponPose::SourceTrack>& packed){
 CHECK(old.size()==packed.size());
 for(const auto&[slot,track]:old){const auto& other=packed.at(slot);CHECK(SameFloat(track.fps,other.fps)&&track.frames.size()==other.frames.size());
  for(size_t i=0;i<track.frames.size();++i){const auto& a=track.frames[i];const auto& b=other.frames[i];CHECK(Same(a.rotation,b.rotation)&&Same(a.supportOffset,b.supportOffset)&&SameFloat(a.supportWeight,b.supportWeight));
   CHECK(a.main.valid==b.main.valid&&Same(a.main.position,b.main.position)&&Same(a.main.rotation,b.main.rotation));
   CHECK(a.offhand.valid==b.offhand.valid&&Same(a.offhand.position,b.offhand.position)&&Same(a.offhand.rotation,b.offhand.rotation));
  }
 }
}
void GripExact(const std::map<int,GripTrack>& old,const std::map<int,GripTrack>& packed){
 CHECK(old.size()==packed.size());for(const auto&[slot,track]:old){const auto& other=packed.at(slot);CHECK(SameFloat(track.fps,other.fps)&&track.frames.size()==other.frames.size());
  for(size_t i=0;i<track.frames.size();++i)CHECK(Same(track.frames[i].offset,other.frames[i].offset)&&SameFloat(track.frames[i].weight,other.frames[i].weight));}
}
size_t NegativeBanks(const std::filesystem::path& fixture){
 std::filesystem::create_directories(fixture);std::map<int,Clip> clips;clips[7].fps=60;clips[7].frames.resize(1);
 Json weapon={{"schema_version",3},{"bank","minimal.lsw"},{"tracks",{{"7",{{"fps",60},{"frames",1},{"offset",16}}}}}};
 std::vector<unsigned char> source(168);std::memcpy(source.data(),"LSW1",4);Put32(source,4,1);Put32(source,8,1);Put32(source,12,152);PutFloat(source,16+12,1);
 WriteBytes(fixture/"minimal.lsw",source);const auto minimal=PackedTracks::ReadSourceWeapon(fixture,weapon,clips);CHECK(!minimal.at(7).frames[0].main.valid);
 size_t rejected=0;
 const auto badSource=[&](std::vector<unsigned char> bytes,Json manifest){WriteBytes(fixture/"minimal.lsw",bytes);CHECK(Throws([&]{PackedTracks::ReadSourceWeapon(fixture,manifest,clips);}));++rejected;};
 for(size_t at:{size_t(0),size_t(4),size_t(8),size_t(12)}){auto bytes=source;bytes[at]^=127;badSource(bytes,weapon);}
 {auto bytes=source;bytes.pop_back();badSource(bytes,weapon);bytes=source;bytes.push_back(0);badSource(bytes,weapon);}
 for(const auto&[key,value]:std::vector<std::pair<std::string,Json>>{{"offset",15},{"offset",17},{"offset",-1},{"offset",16.5},{"frames",0},{"frames",36001},{"frames",2},{"fps",30},{"fps",0},{"fps",241}}){auto manifest=weapon;manifest["tracks"]["7"][key]=value;badSource(source,manifest);}
 {auto manifest=weapon;manifest["tracks"]["007"]=manifest["tracks"]["7"];manifest["tracks"].erase("7");badSource(source,manifest);}
 {auto manifest=weapon;manifest["bank"]="../minimal.lsw";badSource(source,manifest);}
 for(const auto&[at,value]:std::vector<std::pair<size_t,float>>{{16,std::numeric_limits<float>::quiet_NaN()},{16+12,0.f},{16+16,4.f},{16+28,2.f}}){auto bytes=source;PutFloat(bytes,at,value);badSource(bytes,weapon);}
 {auto bytes=source;Put32(bytes,16+144,2);badSource(bytes,weapon);bytes=source;Put32(bytes,16+144,1);badSource(bytes,weapon);}
 {auto bytes=source;Put32(bytes,16+144,1);PutFloat(bytes,16+14*4,1);PutFloat(bytes,16+21*4,1);PutFloat(bytes,16+15*4,4);badSource(bytes,weapon);}
 Json grip={{"schema_version",2},{"bank","minimal.lgb"},{"tracks",{{"7",{{"offset",16},{"size",32}}}}}};
 std::vector<unsigned char> gs(48);std::memcpy(gs.data(),"LGB1",4);Put32(gs,4,1);Put32(gs,8,1);std::memcpy(gs.data()+16,"LSG1",4);Put32(gs,20,1);Put32(gs,24,60);Put32(gs,28,4);
 WriteBytes(fixture/"minimal.lgb",gs);CHECK(PackedTracks::ReadGripBank(fixture,grip,clips).at(7).frames.size()==1);
 const auto badGrip=[&](std::vector<unsigned char> bytes,Json manifest){WriteBytes(fixture/"minimal.lgb",bytes);CHECK(Throws([&]{PackedTracks::ReadGripBank(fixture,manifest,clips);}));++rejected;};
 for(size_t at:{size_t(0),size_t(4),size_t(8),size_t(12),size_t(16),size_t(20),size_t(24),size_t(28)}){auto bytes=gs;bytes[at]^=127;badGrip(bytes,grip);}
 {auto bytes=gs;bytes.pop_back();badGrip(bytes,grip);bytes=gs;bytes.push_back(0);badGrip(bytes,grip);}
 for(const auto&[key,value]:std::vector<std::pair<std::string,Json>>{{"offset",15},{"offset",17},{"offset",-1},{"size",31},{"size",33},{"size",0},{"size",5000000}}){auto manifest=grip;manifest["tracks"]["7"][key]=value;badGrip(gs,manifest);}
 for(const auto&[at,value]:std::vector<std::pair<size_t,float>>{{32,std::numeric_limits<float>::infinity()},{32,4.f},{44,2.f},{44,-1.f}}){auto bytes=gs;PutFloat(bytes,at,value);badGrip(bytes,grip);}
 // A two-track manifest must cover contiguous sections in numeric slot order.
 clips[8]=clips[7];auto twice=weapon;twice["tracks"]["8"]={{"fps",60},{"frames",1},{"offset",168}};auto two=source;two.insert(two.end(),source.begin()+16,source.end());Put32(two,8,2);
 WriteBytes(fixture/"minimal.lsw",two);CHECK(PackedTracks::ReadSourceWeapon(fixture,twice,clips).size()==2);
 for(int offset:{16,167,169}){auto manifest=twice;manifest["tracks"]["8"]["offset"]=offset;badSource(two,manifest);}
 return rejected;
}
void ModeRollback(const std::filesystem::path& project,const std::filesystem::path& packed,const std::filesystem::path& fixture){
 ReadMode("longsword");const auto oldWeapons=sourceWeaponTracks;const auto oldGrip=gripTracks;const auto oldSlots=controller.clips.size(),oldFingers=fingerTracks.size(),oldSounds=soundClips.size();const auto oldRoot=activeAssetRoot,oldAudio=activeAudioRoot;const auto oldMode=loadedMode;const auto oldConfig=config;const auto oldDemo=fullDemoSequence;const auto oldSole=sourceSoleHeight;
 const auto definition=modeDefinitions.at("light_bowgun");const auto sourceRoot=project/std::filesystem::path(definition.at("data_root").get<std::string>());const auto packedRoot=packed/std::filesystem::path(definition.at("data_root").get<std::string>());const auto target=fixture/"rollback-mode";std::filesystem::create_directories(target);
 std::filesystem::copy(sourceRoot,target,std::filesystem::copy_options::recursive|std::filesystem::copy_options::overwrite_existing);
 for(const auto* file:{"source_weapon_tracks.json","source_weapon_tracks.lsw","grip_tracks.json","grip_tracks.lgb"})std::filesystem::copy_file(packedRoot/file,target/file,std::filesystem::copy_options::overwrite_existing);
 modeDefinitions["light_bowgun"]["data_root"]=std::filesystem::relative(target,project).generic_string();
 const auto unchanged=[&]{CHECK(loadedMode==oldMode&&activeAssetRoot==oldRoot&&activeAudioRoot==oldAudio);CHECK(controller.clips.size()==oldSlots&&fingerTracks.size()==oldFingers&&soundClips.size()==oldSounds&&soundReady);CHECK(config.mode==oldConfig.mode&&config.keys==oldConfig.keys&&config.mode_keys==oldConfig.mode_keys&&fullDemoSequence==oldDemo&&SameFloat(sourceSoleHeight,oldSole));SourceExact(oldWeapons,sourceWeaponTracks);GripExact(oldGrip,gripTracks);};
 {std::fstream file(target/"source_weapon_tracks.lsw",std::ios::in|std::ios::out|std::ios::binary);file.put('!');}
 CHECK(Throws([&]{ReadMode("light_bowgun");}));unchanged();
 std::filesystem::copy_file(packedRoot/"source_weapon_tracks.lsw",target/"source_weapon_tracks.lsw",std::filesystem::copy_options::overwrite_existing);
 {std::fstream file(target/"grip_tracks.lgb",std::ios::in|std::ios::out|std::ios::binary);file.put('!');}
 CHECK(Throws([&]{ReadMode("light_bowgun");}));unchanged();
 std::filesystem::copy_file(packedRoot/"grip_tracks.lgb",target/"grip_tracks.lgb",std::filesystem::copy_options::overwrite_existing);ReadMode("light_bowgun");CHECK(loadedMode=="light_bowgun"&&sourceWeaponTracks.size()==controller.clips.size()&&gripTracks.size()==controller.clips.size());
 modeDefinitions["light_bowgun"]=definition;
}
}

int main(int argc,char** argv){try{
 if(argc!=4)throw std::runtime_error("Expected projectRoot packedRoot fixtureRoot");const std::filesystem::path project=argv[1],packed=argv[2],fixture=argv[3];packageRoot=project;ReadAssets();
 Json results=Json::array();size_t tracks=0,frames=0,grips=0,gripFrames=0;
 for(const auto* mode:{"longsword","sword_shield","greatsword","lance","light_bowgun","bow"}){ReadMode(mode);const auto root=packed/std::filesystem::path(modeDefinitions.at(mode).at("data_root").get<std::string>());
  const auto weapons=PackedTracks::ReadSourceWeapon(root,ReadJson(root/"source_weapon_tracks.json"),controller.clips);SourceExact(sourceWeaponTracks,weapons);
  const auto held=PackedTracks::ReadGripBank(root,ReadJson(root/"grip_tracks.json"),controller.clips);GripExact(gripTracks,held);
  size_t count=0,gcount=0;for(const auto&[slot,track]:weapons){(void)slot;count+=track.frames.size();}for(const auto&[slot,track]:held){(void)slot;gcount+=track.frames.size();}
  tracks+=weapons.size();frames+=count;grips+=held.size();gripFrames+=gcount;
  results.push_back({{"mode",mode},{"source_tracks",weapons.size()},{"source_frames",count},{"grip_tracks",held.size()},{"grip_frames",gcount},{"all_source_float_bits_equal",true},{"all_derived_attachment_float_bits_equal",true},{"all_grip_float_bits_equal",true}});
  std::cout<<mode<<" exact native source/attachments "<<weapons.size()<<" tracks "<<count<<" frames; grip "<<held.size()<<" tracks "<<gcount<<" frames\n";
 }
 const auto rejected=NegativeBanks(fixture/"negative");ModeRollback(project,packed,fixture);
 WriteJson(fixture/"packed-native-roundtrip.json",{{"schema",1},{"status","passed"},{"modes",results},{"source_tracks",tracks},{"source_frames",frames},{"grip_tracks",grips},{"grip_frames",gripFrames},{"negative_cases_rejected",rejected},{"corrupt_source_bank_mode_rollback",true},{"corrupt_grip_bank_mode_rollback",true},{"valid_packed_mode_loaded",true},{"runtime_game_launched",false},{"hashes_computed",false}});
 std::cout<<"Packed native byte-exact roundtrip, "<<rejected<<" corruption cases, two full ReadMode rollbacks and valid packed mode load passed\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
