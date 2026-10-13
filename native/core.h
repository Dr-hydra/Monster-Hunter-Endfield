#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace LS {
struct Vec3 { float x=0, y=0, z=0; };
struct Quat { float x=0, y=0, z=0, w=1; };
inline bool Finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
inline bool Finite(Quat q) { return std::isfinite(q.x)&&std::isfinite(q.y)&&std::isfinite(q.z)&&std::isfinite(q.w); }
inline float Dot(Quat a, Quat b) { return a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w; }
inline Quat Normalize(Quat q) {
    const double n=double(q.x)*q.x+double(q.y)*q.y+double(q.z)*q.z+double(q.w)*q.w;
    if (!Finite(q)||n<1e-20) throw std::runtime_error("invalid quaternion");
    const float s=float(1/std::sqrt(n)); return {q.x*s,q.y*s,q.z*s,q.w*s};
}
inline Quat Inverse(Quat q) { q=Normalize(q); return {-q.x,-q.y,-q.z,q.w}; }
inline Quat Mul(Quat a, Quat b) {
    return Normalize({a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
        a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z});
}
inline Vec3 Lerp(Vec3 a, Vec3 b, float t) { return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t}; }
inline Quat Slerp(Quat a, Quat b, float t) {
    a=Normalize(a); b=Normalize(b); t=std::clamp(t,0.f,1.f);
    float d=Dot(a,b); if(d<0) {b={-b.x,-b.y,-b.z,-b.w};d=-d;}
    d=std::clamp(d,-1.f,1.f);
    if(d>0.9995f) return Normalize({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t});
    const float angle=std::acos(d), denominator=std::sin(angle);
    const float u=std::sin((1-t)*angle)/denominator,v=std::sin(t*angle)/denominator;
    return Normalize({a.x*u+b.x*v,a.y*u+b.y*v,a.z*u+b.z*v,a.w*u+b.w*v});
}
inline Vec3 Rotate(Quat q, Vec3 v) {
    q=Normalize(q);
    const Vec3 t{2*(q.y*v.z-q.z*v.y),2*(q.z*v.x-q.x*v.z),2*(q.x*v.y-q.y*v.x)};
    return {v.x+q.w*t.x+q.y*t.z-q.z*t.y,v.y+q.w*t.y+q.z*t.x-q.x*t.z,v.z+q.w*t.z+q.x*t.y-q.y*t.x};
}
struct Pose { std::vector<Quat> rotations; Vec3 hip; };
inline Pose Lerp(const Pose& a,const Pose& b,float t) {
    if(a.rotations.size()!=b.rotations.size()) throw std::runtime_error("pose bone count mismatch");
    Pose out;out.hip=Lerp(a.hip,b.hip,t);out.rotations.reserve(a.rotations.size());
    for(size_t i=0;i<a.rotations.size();++i) out.rotations.push_back(Slerp(a.rotations[i],b.rotations[i],t));
    return out;
}
struct Clip {
    int slot=0; float fps=60; std::vector<Pose> frames;
    float duration() const { return frames.empty()||!std::isfinite(fps)||fps<=0 ? 0.f : float(frames.size()-1)/fps; }
    Pose sample(float seconds) const {
        if(frames.empty()||!std::isfinite(fps)||fps<=0||!std::isfinite(seconds)) throw std::runtime_error("invalid clip sample");
        const double at=std::clamp(double(seconds)*fps,0.0,double(frames.size()-1));
        const size_t i=size_t(at); if(i+1>=frames.size()) return frames.back();
        return Lerp(frames[i],frames[i+1],float(at-i));
    }
};
inline Clip ReadClip(const std::filesystem::path& path,size_t bone_count=21) {
    static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559);
    if(bone_count==0||bone_count>256) throw std::runtime_error("invalid expected bone count");
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    if(!in) throw std::runtime_error("cannot open LSM clip");
    const auto length=in.tellg(); if(length<16||length>128*1024*1024) throw std::runtime_error("invalid LSM size");
    in.seekg(0); std::array<unsigned char,16> header{};
    if(!in.read(reinterpret_cast<char*>(header.data()),header.size())||std::memcmp(header.data(),"LSM1",4)!=0) throw std::runtime_error("invalid LSM header");
    const auto u32=[&](size_t at) {return uint32_t(header[at])|(uint32_t(header[at+1])<<8)|(uint32_t(header[at+2])<<16)|(uint32_t(header[at+3])<<24);};
    const uint32_t count=u32(4),bones=u32(8),fps=u32(12);
    if(!count||count>36000||bones!=bone_count||fps==0||fps>240) throw std::runtime_error("invalid LSM dimensions");
    const uint64_t expected=16+uint64_t(count)*(uint64_t(bones)*4+3)*4;
    if(uint64_t(length)!=expected) throw std::runtime_error("LSM length does not match dimensions");
    const auto readFloat=[&]() {std::array<unsigned char,4> b{}; if(!in.read(reinterpret_cast<char*>(b.data()),4))throw std::runtime_error("truncated LSM");
        uint32_t bits=uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24); float f;std::memcpy(&f,&bits,4);return f;};
    Clip result; result.fps=float(fps); const std::string stem=path.stem().string();
    if(stem.starts_with("clip_")&&stem.size()>5&&std::all_of(stem.begin()+5,stem.end(),[](char c){return c>='0'&&c<='9';})) {
        try { result.slot=std::stoi(stem.substr(5)); } catch(...) {throw std::runtime_error("invalid clip slot");}
    }
    result.frames.reserve(count);
    for(uint32_t i=0;i<count;++i) {
        Pose pose;pose.rotations.reserve(bones);
        for(uint32_t j=0;j<bones;++j) {
            Quat q{readFloat(),readFloat(),readFloat(),readFloat()};
            if(!Finite(q)||std::abs(double(Dot(q,q))-1)>0.02) throw std::runtime_error("LSM quaternion is not finite and unit length");
            pose.rotations.push_back(Normalize(q));
        }
        pose.hip={readFloat(),readFloat(),readFloat()};
        if(!Finite(pose.hip)||std::max({std::abs(pose.hip.x),std::abs(pose.hip.y),std::abs(pose.hip.z)})>10000) throw std::runtime_error("invalid LSM hip position");
        result.frames.push_back(std::move(pose));
    }
    return result;
}
enum class Command { Slash,Thrust,Spirit,Fade,Foresight,Sheathe,Iai,IaiSpirit,Helm,Dodge,Stop,NormalSheathe };
enum InputBit : uint32_t { SlashBit=1,ThrustBit=2,SpiritBit=4,DodgeBit=8,SheatheBit=16 };
class Controller {
public:
    std::map<int,Clip> clips;
    std::map<Command,std::vector<int>> moves{
        {Command::Slash,{101,102,103}},{Command::Thrust,{110,111}},{Command::Spirit,{104,105,106,107}},
        {Command::Fade,{112}},{Command::Foresight,{150}},{Command::Sheathe,{306}},
        {Command::Iai,{307}},{Command::IaiSpirit,{308}},{Command::Helm,{160}},{Command::Dodge,{3}},{Command::NormalSheathe,{3}}};
    bool active=false; int slot=1; float seconds=0; Pose output;
    uint64_t motionSerial=0;
    int idleSlot=1,sheatheHoldFrame=80;
    float transitionFraction=0.55f,transitionSeconds=0.10f,bufferSeconds=0.3f,chordSeconds=0.08f;
    void setMove(Command command,std::vector<int> sequence) { moves[command]=std::move(sequence); }
    bool start(int idle=1) {
        stop();idleSlot=idle;
        auto found=clips.find(idle); if(found==clips.end()||found->second.frames.empty())return false;
        active=true;slot=idle;seconds=0;++motionSerial;output=found->second.sample(0);return true;
    }
    void stop() {active=false;seconds=0;pending_=false;edgeBits_=0;clock_=0;blendElapsed_=transitionSeconds;blendFrom_={};rootOffset_={};}
    bool sheathed() const {auto it=moves.find(Command::Sheathe);return it!=moves.end()&&!it->second.empty()&&slot==it->second.front();}
    void input(Command command) {
        if(command==Command::Stop){stop();return;} if(!active)return;
        if(sheathed()){if(command==Command::Slash)command=Command::Iai;else if(command==Command::Spirit)command=Command::IaiSpirit;}
        if(canTransition()&&play(command)){pending_=false;return;}
        pending_=true;pendingCommand_=command;pendingUntil_=clock_+std::max(0.f,bufferSeconds);
    }
    void inputBits(uint32_t edges,uint32_t held) {
        if(!active)return; edges&=31;held&=31;if(!edges)return;
        // A held spirit key from special sheathe must not turn the next left
        // click into Helmbreaker. The sheathed state owns these follow-ups.
        if(sheathed()) {
            edgeBits_=0;
            if(edges&SheatheBit)input(Command::NormalSheathe);
            else if(edges&SpiritBit)input(Command::Spirit);
            else if(edges&SlashBit)input(Command::Slash);
            else if(edges&DodgeBit)input(Command::Dodge);
            return;
        }
        const uint32_t combined=(edgeBits_|edges|held);
        Command chord{};bool haveChord=true;
        if((combined&(SpiritBit|DodgeBit))==(SpiritBit|DodgeBit))chord=Command::Sheathe;
        else if((combined&(SpiritBit|ThrustBit))==(SpiritBit|ThrustBit))chord=Command::Foresight;
        else if((combined&(SpiritBit|SlashBit))==(SpiritBit|SlashBit))chord=Command::Helm;
        else if((combined&(SlashBit|ThrustBit))==(SlashBit|ThrustBit))chord=Command::Fade;
        else haveChord=false;
        if(haveChord){edgeBits_=0;input(chord);return;}
        if(!edgeBits_)edgeUntil_=clock_+std::max(0.f,chordSeconds);
        edgeBits_|=edges;
    }
    void tick(float dt) {
        if(!std::isfinite(dt)||dt<0||dt>60)throw std::runtime_error("invalid controller timestep");
        if(!active)return;
        // Substeps preserve input windows and clip boundaries during uneven render frames.
        const float maximum=1.f/120;
        while(dt>maximum){advance(maximum);dt-=maximum;if(!active)return;}
        advance(dt);
    }
private:
    double clock_=0,pendingUntil_=0,edgeUntil_=0;
    bool pending_=false;Command pendingCommand_=Command::Slash;uint32_t edgeBits_=0;
    Pose blendFrom_;float blendElapsed_=0;Vec3 rootOffset_{};
    const Clip* current()const {auto it=clips.find(slot);return it==clips.end()?nullptr:&it->second;}
    bool canTransition()const {
        const Clip* clip=current();if(!clip)return false;
        if(slot==idleSlot)return true;
        if(sheathed())return seconds>=std::min(clip->duration(),float(std::max(0,sheatheHoldFrame))/clip->fps);
        return seconds>=clip->duration()*std::clamp(transitionFraction,0.f,1.f);
    }
    bool play(Command command) {
        auto it=moves.find(command);if(it==moves.end()||it->second.empty())return false;
        const auto& sequence=it->second;auto at=std::find(sequence.begin(),sequence.end(),slot);
        int next=sequence.front();if(at!=sequence.end()&&std::next(at)!=sequence.end())next=*std::next(at);
        auto clip=clips.find(next);if(clip==clips.end()||clip->second.frames.empty())return false;
        if(!output.rotations.empty()&&output.rotations.size()!=clip->second.frames.front().rotations.size())return false;
        transition(next);return true;
    }
    void transition(int next) {
        blendFrom_=output;blendElapsed_=0;
        const auto& initial=clips.at(next).frames.front().hip;
        rootOffset_.x=output.hip.x-initial.x;rootOffset_.z=output.hip.z-initial.z;
        slot=next;seconds=0;++motionSerial;
    }
    void advance(float dt) {
        clock_+=dt;
        if(edgeBits_&&clock_+1e-7>=edgeUntil_) {
            const uint32_t bits=edgeBits_;edgeBits_=0;
            if(bits&SheatheBit)input(Command::NormalSheathe);
            else if(bits&SpiritBit)input(Command::Spirit);
            else if(bits&SlashBit)input(Command::Slash);
            else if(bits&ThrustBit)input(Command::Thrust);
            else if(bits&DodgeBit)input(Command::Dodge);
        }
        const Clip* clip=current();if(!clip||clip->frames.empty()){stop();return;}
        seconds+=dt;
        if(pending_&&clock_>pendingUntil_+1e-7)pending_=false;
        if(pending_&&canTransition()) {Command command=pendingCommand_;pending_=false;if(play(command))clip=current();}
        if(sheathed())seconds=std::min(seconds,std::min(clip->duration(),float(std::max(0,sheatheHoldFrame))/clip->fps));
        else if(slot==idleSlot) {if(clip->duration()>0)seconds=std::fmod(seconds,clip->duration());else seconds=0;}
        else if(seconds>=clip->duration()) {
            auto idle=clips.find(idleSlot);if(idle==clips.end()||idle->second.frames.empty()){stop();return;}
            transition(idleSlot);clip=&idle->second;
        }
        Pose target=clip->sample(seconds);target.hip.x+=rootOffset_.x;target.hip.z+=rootOffset_.z;blendElapsed_+=dt;
        if(!blendFrom_.rotations.empty()&&transitionSeconds>0&&blendElapsed_<transitionSeconds)
            output=Lerp(blendFrom_,target,blendElapsed_/transitionSeconds);
        else output=std::move(target);
    }
};
} // namespace LS
