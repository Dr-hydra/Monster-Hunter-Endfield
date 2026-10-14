#pragma once
#include "grip.h"
#include "weapon_pose.h"
#include <nlohmann/json.hpp>

namespace LS::PackedTracks {
using Json=nlohmann::json;
inline uint32_t U32(const unsigned char* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline uint64_t Unsigned(const Json& value){
 if(!value.is_number_integer()||(value.is_number_integer()&&!value.is_number_unsigned()&&value.get<int64_t>()<0))throw std::runtime_error("Invalid packed integer");
 return value.get<uint64_t>();
}
inline int Slot(const std::string& key,const std::map<int,Clip>& clips){
 size_t used=0;int slot=std::stoi(key,&used);
 if(used!=key.size()||slot<0||std::to_string(slot)!=key||!clips.contains(slot))throw std::runtime_error("Invalid packed slot");return slot;
}
struct Bank {
 std::ifstream stream;uint64_t size=0;uint32_t count=0;
 Bank(const std::filesystem::path& root,const Json& manifest,const char* extension,const char* magic,uint32_t stride){
  if(!manifest.at("tracks").is_object()||manifest["tracks"].empty()||manifest["tracks"].size()>4096)throw std::runtime_error("Invalid packed tracks");
  const auto path=std::filesystem::path(manifest.at("bank").get<std::string>());
  if(path.empty()||path.has_parent_path()||path.is_absolute()||path.has_root_name()||path.extension()!=extension)throw std::runtime_error("Invalid packed bank path");
  stream.open(root/path,std::ios::binary|std::ios::ate);if(!stream)throw std::runtime_error("Missing packed bank");
  const auto length=stream.tellg();if(length<16||uint64_t(length)>2ull*1024*1024*1024)throw std::runtime_error("Invalid packed bank size");
  size=uint64_t(length);stream.seekg(0);unsigned char header[16]{};
  if(!stream.read(reinterpret_cast<char*>(header),16)||std::memcmp(header,magic,4)!=0||U32(header+4)!=1||U32(header+12)!=stride)throw std::runtime_error("Invalid packed bank header");
  count=U32(header+8);if(count!=manifest["tracks"].size())throw std::runtime_error("Packed track count mismatch");
 }
 void Read(void* target,size_t bytes){if(!stream.read(reinterpret_cast<char*>(target),std::streamsize(bytes)))throw std::runtime_error("Truncated packed bank");}
 void End(uint64_t cursor){if(cursor!=size||uint64_t(stream.tellg())!=size)throw std::runtime_error("Packed bank trailing bytes");}
};
inline WeaponPose::Attachment Attachment(const float* hand,const float* socket,bool valid){
 // Retain the exact float operations of the original JSON path. The bank
 // contains authored world frames, never pre-rounded relative transforms.
 WeaponPose::Attachment result;if(!valid)return result;
 const Quat handQ=Normalize({hand[3],hand[4],hand[5],hand[6]}),socketQ=Normalize({socket[3],socket[4],socket[5],socket[6]});
 result.position=Rotate(Inverse(handQ),Retarget::Sub({socket[0],socket[1],socket[2]},{hand[0],hand[1],hand[2]}));
 result.rotation=Mul(Inverse(handQ),socketQ);result.valid=true;
 if(!Finite(result.position)||Retarget::Len(result.position)>3)throw std::runtime_error("Invalid packed attachment distance");return result;
}
inline std::map<int,WeaponPose::SourceTrack> ReadSourceWeapon(const std::filesystem::path& root,const Json& manifest,const std::map<int,Clip>& clips){
 static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559);
 if(manifest.at("schema_version")!=3)throw std::runtime_error("Invalid packed source weapon schema");
 Bank bank(root,manifest,".lsw","LSW1",152);std::map<int,Json> entries;
 for(auto it=manifest["tracks"].begin();it!=manifest["tracks"].end();++it)entries.emplace(Slot(it.key(),clips),it.value());
 uint64_t cursor=16;std::map<int,WeaponPose::SourceTrack> output;
 for(const auto& [slot,row]:entries){
  const auto offset=Unsigned(row.at("offset")),count=Unsigned(row.at("frames"));const float fps=row.at("fps").get<float>();
  if(offset!=cursor||!count||count>36000||count!=clips.at(slot).frames.size()||!std::isfinite(fps)||fps<1||fps>240||fps!=clips.at(slot).fps||count*152>bank.size-cursor)throw std::runtime_error("Packed source weapon dimensions mismatch");
  WeaponPose::SourceTrack track;track.fps=fps;track.frames.reserve(size_t(count));
  for(uint64_t f=0;f<count;++f){std::array<unsigned char,152> bytes{};bank.Read(bytes.data(),bytes.size());float values[36]{};std::memcpy(values,bytes.data(),144);
   for(float value:values)if(!std::isfinite(value))throw std::runtime_error("Nonfinite packed source frame");
   const auto mainValid=U32(bytes.data()+144),offValid=U32(bytes.data()+148);if(mainValid>1||offValid>1)throw std::runtime_error("Invalid packed attachment flag");
   WeaponPose::SourceFrame frame{{values[0],values[1],values[2],values[3]},{values[4],values[5],values[6]},values[7]};
   if(std::abs(Dot(frame.rotation,frame.rotation)-1)>.001f||Retarget::Len(frame.supportOffset)>3||frame.supportWeight<0||frame.supportWeight>1)throw std::runtime_error("Invalid packed source sample");
   frame.main=Attachment(values+8,values+15,mainValid!=0);frame.offhand=Attachment(values+22,values+29,offValid!=0);track.frames.push_back(frame);
  }
  cursor+=count*152;output.emplace(slot,std::move(track));
 }
 bank.End(cursor);return output;
}
inline std::map<int,GripTrack> ReadGripBank(const std::filesystem::path& root,const Json& manifest,const std::map<int,Clip>& clips){
 if(manifest.at("schema_version")!=2)throw std::runtime_error("Invalid packed grip schema");
 Bank bank(root,manifest,".lgb","LGB1",0);std::map<int,Json> entries;
 for(auto it=manifest["tracks"].begin();it!=manifest["tracks"].end();++it)entries.emplace(Slot(it.key(),clips),it.value());
 uint64_t cursor=16;std::map<int,GripTrack> output;
 for(const auto& [slot,row]:entries){const auto offset=Unsigned(row.at("offset")),size=Unsigned(row.at("size"));
  if(offset!=cursor||size<16||size>4*1024*1024||size>bank.size-cursor)throw std::runtime_error("Invalid packed grip bounds");
  unsigned char header[16]{};bank.Read(header,16);const uint32_t count=U32(header+4),fps=U32(header+8),components=U32(header+12);
  if(std::memcmp(header,"LSG1",4)!=0||!count||count>36000||count!=clips.at(slot).frames.size()||!fps||fps>240||float(fps)!=clips.at(slot).fps||components!=4||size!=16+uint64_t(count)*16)throw std::runtime_error("Packed grip dimensions mismatch");
  GripTrack track;track.fps=float(fps);track.frames.reserve(count);
  for(uint32_t f=0;f<count;++f){float values[4]{};bank.Read(values,16);GripFrame frame{{values[0],values[1],values[2]},values[3]};
   if(!Finite(frame.offset)||Retarget::Len(frame.offset)>3||!std::isfinite(frame.weight)||frame.weight<0||frame.weight>1)throw std::runtime_error("Invalid packed grip frame");track.frames.push_back(frame);
  }
  cursor+=size;output.emplace(slot,std::move(track));
 }
 bank.End(cursor);return output;
}
} // namespace LS::PackedTracks
