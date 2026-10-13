#include "../native/audio.h"
#include "../native/audio_timeline.h"
#include "test_support.h"
using namespace LS;
static void Fixture(const std::filesystem::path& path){
 std::vector<unsigned char> data;const auto tag=[&](const char* text){data.insert(data.end(),text,text+4);};const auto u16=[&](uint16_t value){data.push_back(static_cast<unsigned char>(value));data.push_back(static_cast<unsigned char>(value>>8));};
 const auto u32=[&](uint32_t value){for(int i=0;i<4;++i)data.push_back(static_cast<unsigned char>(value>>(8*i)));};
 tag("RIFF");u32(36+8);tag("WAVE");tag("fmt ");u32(16);u16(1);u16(1);u32(24000);u32(48000);u16(2);u16(16);tag("data");u32(8);u16(0);u16(16384);u16(0);u16(49152);
 std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<char*>(data.data()),std::streamsize(data.size()));
}
int main(int argc,char** argv){try{
 CHECK(argc==2);const auto directory=std::filesystem::path(argv[1])/"build/runtime-restore/audio-fixtures";std::filesystem::create_directories(directory);const auto path=directory/"conversion.wav";Fixture(path);
 auto clip=ReadPcmWav(path);CHECK(clip.sampleRate==48000&&clip.channels==2&&clip.samples.size()==16);CHECK(clip.samples[4]>16000&&clip.samples[4]==clip.samples[5]);
 BalanceShowcaseSound(clip);for(auto v:clip.samples)CHECK(std::abs(int(v))<30200);
 {std::ofstream bad(directory/"bad.wav",std::ios::binary);bad<<"RIFFbroken";}CHECK(Throws([&]{ReadPcmWav(directory/"bad.wav");}));
 AudioTimeline timeline;timeline.cues[10]={{0,"first",1},{.2f,"second",1},{.3f,"third",1}};std::vector<std::string> heard;const auto emit=[&](const AudioCue& cue){heard.push_back(cue.clip);};
 timeline.Update(1,10,0,true,emit);CHECK(heard.size()==1);timeline.Update(1,10,.25f,true,emit);CHECK(heard.size()==2&&heard.back()=="second");timeline.Update(1,10,.28f,true,emit);CHECK(heard.size()==2);
 timeline.Mute();timeline.Update(1,10,.35f,true,emit);CHECK(heard.size()==2);timeline.Update(2,10,0,true,emit);CHECK(heard.size()==3&&heard.back()=="first");
 timeline.Update(2,10,.4f,false,emit);timeline.Update(2,10,.5f,true,emit);CHECK(heard.size()==3);timeline.Reset();timeline.Update(3,10,.4f,true,emit);CHECK(heard.size()==6);
 PcmClip silence;silence.samples.resize(48000*2/10);AudioPlayer invalid(UINT(-2));CHECK(!invalid.Play(silence,1)&&invalid.ActiveVoices()==0);invalid.Stop();CHECK(!invalid.Play(silence,std::numeric_limits<float>::quiet_NaN()));
 AudioPlayer player;const bool device=player.Play(silence,1);
 if(device){CHECK(player.Play(silence,.5f));CHECK(player.ActiveVoices()==2);player.Stop();CHECK(player.ActiveVoices()==0);
  CHECK(player.Play(silence,1));Sleep(130);player.Poll();CHECK(player.ActiveVoices()==0);}
 else std::cout<<"No local PCM endpoint: successful playback portion unavailable\n";
 std::cout<<"audio: WAV conversion, cue replay/mute, overlapping PCM stop/device failure passed\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
