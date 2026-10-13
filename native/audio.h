#pragma once
#include "core.h"
#include <Windows.h>
#include <mmsystem.h>
#include <memory>
#include <mutex>

namespace LS {
struct PcmClip{uint32_t sampleRate=48000;uint16_t channels=2;std::vector<int16_t> samples;};
inline PcmClip ReadPcmWav(const std::filesystem::path& path){
 std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("Cannot open WAV");
 const auto size=file.tellg();if(size<44||size>64*1024*1024)throw std::runtime_error("Invalid WAV size");
 std::vector<unsigned char> bytes(static_cast<size_t>(size));file.seekg(0);if(!file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())))throw std::runtime_error("Truncated WAV");
 const auto u16=[&](size_t p){return uint16_t(bytes[p]|(uint16_t(bytes[p+1])<<8));};
 const auto u32=[&](size_t p){return uint32_t(bytes[p])|(uint32_t(bytes[p+1])<<8)|(uint32_t(bytes[p+2])<<16)|(uint32_t(bytes[p+3])<<24);};
 if(std::memcmp(bytes.data(),"RIFF",4)||std::memcmp(bytes.data()+8,"WAVE",4)||uint64_t(u32(4))+8!=bytes.size())throw std::runtime_error("Invalid WAV RIFF");
 size_t data=0,dataSize=0;uint16_t format=0,channels=0,bits=0,align=0;uint32_t rate=0;
 for(size_t at=12;at+8<=bytes.size();){const uint32_t count=u32(at+4);const size_t body=at+8;
  if(count>bytes.size()-body)throw std::runtime_error("Invalid WAV chunk size");
  if(std::memcmp(bytes.data()+at,"fmt ",4)==0){if(count<16)throw std::runtime_error("Invalid WAV format");format=u16(body);channels=u16(body+2);rate=u32(body+4);align=u16(body+12);bits=u16(body+14);
   if(format==0xfffe){if(count<40||u16(body+16)<22)throw std::runtime_error("Invalid extensible WAV");
    // SubFormat GUID tail must be the standard PCM/IEEE namespace.
    constexpr unsigned char tail[]{0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
    if(std::memcmp(bytes.data()+body+28,tail,12)||u16(body+26)!=0)throw std::runtime_error("Unknown WAV subformat");format=u16(body+24);}
  }else if(std::memcmp(bytes.data()+at,"data",4)==0&&!data){data=body;dataSize=count;}
  at=body+count+(count&1u);
 }
 if(!data||!dataSize||(channels!=1&&channels!=2)||rate<8000||rate>192000||(format!=1&&format!=3)||
  (format==1&&bits!=8&&bits!=16&&bits!=24&&bits!=32)||(format==3&&bits!=32)||align!=channels*(bits/8)||dataSize%align)throw std::runtime_error("Unsupported/invalid WAV PCM format");
 const size_t frames=dataSize/align;if(double(frames)/rate>30)throw std::runtime_error("WAV is too long for action audio");
 std::vector<float> source(frames*channels);const size_t width=bits/8;
 for(size_t i=0;i<source.size();++i){const size_t p=data+i*width;float value=0;
  if(format==3){const uint32_t bitsValue=u32(p);std::memcpy(&value,&bitsValue,4);if(!std::isfinite(value))throw std::runtime_error("Nonfinite float WAV");}
  else if(bits==8)value=(float(bytes[p])-128)/128;
  else if(bits==16)value=float(static_cast<int16_t>(u16(p)))/32768;
  else if(bits==24){int32_t v=int32_t(bytes[p])|(int32_t(bytes[p+1])<<8)|(int32_t(bytes[p+2])<<16);if(v&0x800000)v|=~0xffffff;value=float(v)/8388608;}
  else value=float(static_cast<int32_t>(u32(p)))/2147483648.f;
  source[i]=std::clamp(value,-1.f,1.f);
 }
 PcmClip out;const size_t outputFrames=std::max(size_t(1),size_t(std::ceil(double(frames)*out.sampleRate/rate)));out.samples.resize(outputFrames*2);
 for(size_t i=0;i<outputFrames;++i){const double position=std::min(double(frames-1),double(i)*rate/out.sampleRate);const size_t a=size_t(position),b=std::min(a+1,frames-1);const float t=float(position-a);
  for(size_t c=0;c<2;++c){const size_t channel=std::min(c,size_t(channels-1));const float v=source[a*channels+channel]+(source[b*channels+channel]-source[a*channels+channel])*t;
   out.samples[i*2+c]=static_cast<int16_t>(std::clamp(std::lround(v*32767),-32768L,32767L));}}
 return out;
}
inline void BalanceShowcaseSound(PcmClip& clip){
 if(clip.samples.empty())return;double square=0,peak=0;for(const int16_t value:clip.samples){const double x=double(value)/32768;square+=x*x;peak=std::max(peak,std::abs(x));}
 const double rms=std::sqrt(square/clip.samples.size());if(rms<1e-5||peak<1e-5)return;
 const double gain=std::min({3.,.18/rms,.92/peak});for(auto& value:clip.samples)value=static_cast<int16_t>(std::clamp(std::lround(value*gain),-32768L,32767L));
}
// Independent PCM voices share the Windows endpoint mixer, so a later cue
// cannot replace an earlier one (PlaySound's single-channel behaviour).
// No Wwise event, game audio method, native damage or networking API is called.
class AudioPlayer{
 struct Voice{HWAVEOUT device=nullptr;WAVEHDR header{};std::vector<int16_t> samples;bool prepared=false;};
 std::mutex gate_;std::vector<std::unique_ptr<Voice>> voices_;UINT deviceId_;
 static void Dispose(Voice& voice){if(!voice.device)return;waveOutReset(voice.device);if(voice.prepared)waveOutUnprepareHeader(voice.device,&voice.header,sizeof(voice.header));waveOutClose(voice.device);voice.device=nullptr;voice.prepared=false;}
 void PollLocked(){for(auto it=voices_.begin();it!=voices_.end();){if((*it)->header.dwFlags&WHDR_DONE){Dispose(**it);it=voices_.erase(it);}else ++it;}}
public:
 explicit AudioPlayer(UINT device=WAVE_MAPPER):deviceId_(device){}
 ~AudioPlayer(){Stop();}
 AudioPlayer(const AudioPlayer&)=delete;AudioPlayer& operator=(const AudioPlayer&)=delete;
 bool Play(const PcmClip& clip,float gain){
  if(clip.samples.empty()||clip.channels!=2||clip.sampleRate!=48000||clip.samples.size()%2||!std::isfinite(gain)||gain<0||gain>2||clip.samples.size()>48000*2*30)return false;
  if(gain==0)return true;
  std::lock_guard lock(gate_);PollLocked();if(voices_.size()>=16)return false;
  auto voice=std::make_unique<Voice>();voice->samples.reserve(clip.samples.size());
  for(const auto sample:clip.samples)voice->samples.push_back(static_cast<int16_t>(std::clamp(std::lround(float(sample)*gain),-32768L,32767L)));
  WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=2;format.nSamplesPerSec=48000;format.wBitsPerSample=16;format.nBlockAlign=4;format.nAvgBytesPerSec=192000;
  if(waveOutOpen(&voice->device,deviceId_,&format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR)return false;
  voice->header.lpData=reinterpret_cast<LPSTR>(voice->samples.data());voice->header.dwBufferLength=DWORD(voice->samples.size()*sizeof(int16_t));
  if(waveOutPrepareHeader(voice->device,&voice->header,sizeof(voice->header))!=MMSYSERR_NOERROR){Dispose(*voice);return false;}voice->prepared=true;
  if(waveOutWrite(voice->device,&voice->header,sizeof(voice->header))!=MMSYSERR_NOERROR){Dispose(*voice);return false;}
  voices_.push_back(std::move(voice));return true;
 }
 void Poll(){std::lock_guard lock(gate_);PollLocked();}
 void Stop(){std::lock_guard lock(gate_);for(auto& voice:voices_)Dispose(*voice);voices_.clear();}
 size_t ActiveVoices(){std::lock_guard lock(gate_);PollLocked();return voices_.size();}
};
} // namespace LS
