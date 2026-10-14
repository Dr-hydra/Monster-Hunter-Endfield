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
#include <set>
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
enum class Command { Slash,Thrust,Spirit,Fade,Foresight,Sheathe,Iai,IaiSpirit,Helm,Dodge,Stop,NormalSheathe,
    Charge,Release,Guard,Aim,Fire,Reload,Special,Advance,Backstep,Counter,Draw };
inline const std::map<std::string,Command>& CommandNames(){
    static const std::map<std::string,Command> names{{"slash",Command::Slash},{"thrust",Command::Thrust},
        {"spirit",Command::Spirit},{"fade",Command::Fade},{"foresight",Command::Foresight},{"sheathe",Command::Sheathe},
        {"iai",Command::Iai},{"iai_spirit",Command::IaiSpirit},{"helm",Command::Helm},{"dodge",Command::Dodge},
        {"stop",Command::Stop},{"normal_sheathe",Command::NormalSheathe},{"charge",Command::Charge},
        {"release",Command::Release},{"guard",Command::Guard},{"aim",Command::Aim},{"fire",Command::Fire},
        {"reload",Command::Reload},{"special",Command::Special},{"advance",Command::Advance},
        {"backstep",Command::Backstep},{"counter",Command::Counter},{"draw",Command::Draw}};return names;
}
inline Command CommandFromName(const std::string& name){auto found=CommandNames().find(name);
    if(found==CommandNames().end())throw std::runtime_error("Unknown command: "+name);return found->second;}
inline std::string CommandName(Command command){for(const auto&[name,value]:CommandNames())if(value==command)return name;return "unknown";}
struct HoldRelease{float seconds=0;int slot=0;};
struct HoldPolicy{int frame=-1;bool loop=false;std::vector<HoldRelease> releases;};
struct HoldVariant{int slot=0;std::set<int> fromSlots;HoldPolicy policy;};
struct TapHoldBinding{Command command;float seconds=.35f;};
enum InputBit : uint32_t { SlashBit=1,ThrustBit=2,SpiritBit=4,DodgeBit=8,SheatheBit=16,AimBit=32,ReloadBit=64 };
class Controller {
public:
    std::map<int,Clip> clips;
    std::map<Command,std::vector<int>> moves{
        {Command::Slash,{101,102,107}},{Command::Thrust,{105,112}},{Command::Spirit,{104,106,108,109}},
        {Command::Fade,{103,127,128}},{Command::Foresight,{140}},{Command::Sheathe,{306}},
        {Command::Iai,{307}},{Command::IaiSpirit,{311}},{Command::Helm,{1000}},{Command::Dodge,{9,11}},{Command::NormalSheathe,{3}}};
    bool active=false; int slot=1; float seconds=0; Pose output;
    uint64_t motionSerial=0;
    int idleSlot=1,sheatheHoldFrame=80;
    bool longswordRules=true;
    std::map<Command,HoldPolicy> holdPolicies;
    std::map<Command,std::vector<HoldVariant>> holdVariants;
    std::map<int,std::map<uint32_t,Command>> hardwareRoutes;
    std::map<uint32_t,TapHoldBinding> tapHoldBindings;
    std::map<uint32_t,Command> inputBindings{{SlashBit,Command::Slash},{ThrustBit,Command::Thrust},
        {SpiritBit,Command::Spirit},{DodgeBit,Command::Dodge},{SheatheBit,Command::NormalSheathe}};
    std::map<uint32_t,Command> chordBindings;
    float transitionFraction=0.55f,transitionSeconds=0.10f,bufferSeconds=0.3f,chordSeconds=0.08f;
    void setMove(Command command,std::vector<int> sequence) { moves[command]=std::move(sequence); }
    bool start(int idle=1) {
        stop();idleSlot=idle;
        auto found=clips.find(idle); if(found==clips.end()||found->second.frames.empty())return false;
        active=true;slot=idle;seconds=0;++motionSerial;output=found->second.sample(0);return true;
    }
    void stop() {active=false;seconds=0;pending_=false;edgeBits_=0;clock_=0;blendElapsed_=transitionSeconds;blendFrom_={};rootOffset_={};heldCommands_.clear();holdTime_=0;holding_=false;returning_=false;normalSheathing_=false;activeHoldPolicy_={};tapHoldSince_.clear();tapHoldCancelled_.clear();}
    bool sheathed() const {if(!longswordRules)return false;auto it=moves.find(Command::Sheathe);return it!=moves.end()&&!it->second.empty()&&slot==it->second.front();}
    bool available(Command command)const{return active&&moves.contains(command)&&!moves.at(command).empty()&&canTransition()&&
        !(longswordRules&&resting()&&(command==Command::Sheathe||command==Command::Foresight));}
    bool buffered()const{return pending_||edgeBits_!=0;}
    bool buffered(Command command)const{return pending_&&pendingCommand_==command;}
    float heldSeconds()const{return holding_?holdTime_:0;}
    float motionAge()const{return blendElapsed_;}
    bool holding(Command command)const{return holding_&&holdCommand_==command;}
    const HoldPolicy& HoldRule(Command command)const{return holding(command)?activeHoldPolicy_:holdPolicies.at(command);}
    bool isHeld(Command command)const{return heldCommands_.contains(command);}
    bool normalSheathed()const{const auto* clip=current();return active&&normalSheathing_&&clip&&seconds+1e-5f>=clip->duration();}
    bool resting()const{return slot==idleSlot||normalSheathed();}
    void returnToOrigin(float duration=.5f){if(!std::isfinite(duration)||duration<=0)return;
        returnFrom_=rootOffset_;returnDuration_=duration;returnElapsed_=0;returning_=true;}
    void held(Command command,bool down,bool activate=true){
        if(down){if(heldCommands_.insert(command).second&&activate)input(command);return;}
        const bool was=heldCommands_.erase(command)!=0;
        if(!was||!active)return;
        if(holding_&&holdCommand_==command){
            const auto& rule=activeHoldPolicy_;int release=idleSlot;
            for(const auto& level:rule.releases)if(holdTime_+1e-6f>=level.seconds)release=level.slot;
            holding_=false;holdTime_=0;pending_=false;
            if(clips.contains(release))transition(release);
        }else if(pending_&&pendingCommand_==command)pending_=false;
    }
    // Resolve a combination before starting constituent hold actions. This
    // keeps guard+attack/counter and aim+attack from being swallowed by holds.
    void hardwareInput(uint32_t edges,uint32_t heldBits){
        if(longswordRules){inputBits(edges,heldBits);return;}
        if(auto routes=hardwareRoutes.find(slot);routes!=hardwareRoutes.end())for(const auto&[bits,command]:routes->second)
            if((edges&bits)==bits){input(command);return;}
        edges&=127;heldBits&=127;uint32_t chord=0;Command selected{};unsigned best=0;
        for(const auto&[bits,command]:chordBindings)if((heldBits&bits)==bits){
            unsigned count=0;for(auto b=bits;b;b>>=1)count+=b&1;
            if(count>best){chord=bits;selected=command;best=count;}
        }
        std::set<Command> down;
        for(const auto&[bit,command]:inputBindings)if((heldBits&bit)&&!(chord&bit)&&holdPolicies.contains(command))down.insert(command);
        if(chord&&holdPolicies.contains(selected))down.insert(selected);
        std::set<Command> promoted;
        for(const auto&[bit,behavior]:tapHoldBindings){
            if(!(heldBits&bit)){tapHoldSince_.erase(bit);tapHoldCancelled_.erase(bit);continue;}
            tapHoldSince_.try_emplace(bit,clock_);
            if(edges&~bit)tapHoldCancelled_.insert(bit);
            if(!chord&&!tapHoldCancelled_.contains(bit)&&clock_-tapHoldSince_.at(bit)>=behavior.seconds){down.insert(behavior.command);promoted.insert(behavior.command);}
        }
        // Releases run first; a new chord then owns the resulting transition.
        for(const auto&[command,rule]:holdPolicies)if(!down.contains(command))held(command,false);
        for(auto command:down){bool fresh=false;
            if(chord&&command==selected)fresh=(edges&chord)!=0;
            else for(const auto&[bit,mapped]:inputBindings)if(mapped==command&&(edges&bit))fresh=true;
            if(command==Command::Aim&&(down.contains(Command::Charge)||((edges&SlashBit)&&moves.contains(Command::Fire))))fresh=false;
            held(command,true,fresh);
        }
        for(auto command:promoted)if(!holding(command))input(command);
        if(chord){if((edges&chord)&&!holdPolicies.contains(selected))input(selected);return;}
        for(const auto&[bit,command]:inputBindings)if((edges&bit)&&!holdPolicies.contains(command)){input(command);break;}
    }
    void input(Command command) {
        if(command==Command::Stop){stop();return;} if(!active)return;
        if(sheathed()){if(command==Command::Slash)command=Command::Iai;else if(command==Command::Spirit)command=Command::IaiSpirit;}
        if(canTransition()&&play(command)){pending_=false;return;}
        pending_=true;pendingCommand_=command;pendingUntil_=clock_+std::max(0.f,bufferSeconds);
    }
    void inputBits(uint32_t edges,uint32_t held) {
        if(!active)return; edges&=127;held&=127;if(!edges)return;
        if(!longswordRules){
            const auto combined=edges|held;for(auto it=chordBindings.rbegin();it!=chordBindings.rend();++it)
                if((combined&it->first)==it->first){input(it->second);return;}
            for(auto[bit,command]:inputBindings)if(edges&bit){input(command);break;}
            return;
        }
        edges&=31;held&=31;if(!edges)return;
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
        if(haveChord){edgeBits_=0;
            // The shipped Hunter Notes require these two follow-ups after
            // an attack. Direct pose/demo commands remain separate.
            if(resting()&&(chord==Command::Sheathe||chord==Command::Foresight))return;
            input(chord);return;}
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
    std::set<Command> heldCommands_;bool holding_=false;Command holdCommand_=Command::Charge;float holdTime_=0;
    HoldPolicy activeHoldPolicy_;
    std::map<uint32_t,double> tapHoldSince_;std::set<uint32_t> tapHoldCancelled_;
    bool returning_=false;float returnElapsed_=0,returnDuration_=0;Vec3 returnFrom_{};
    bool normalSheathing_=false;
    const Clip* current()const {auto it=clips.find(slot);return it==clips.end()?nullptr:&it->second;}
    bool canTransition()const {
        const Clip* clip=current();if(!clip)return false;
        if(slot==idleSlot)return true;
        if(normalSheathing_)return normalSheathed();
        if(holding_)return true;
        if(sheathed())return seconds>=std::min(clip->duration(),float(std::max(0,sheatheHoldFrame))/clip->fps);
        return seconds>=clip->duration()*std::clamp(transitionFraction,0.f,1.f);
    }
    bool play(Command command) {
        auto it=moves.find(command);if(it==moves.end()||it->second.empty())return false;
        const auto& sequence=it->second;auto at=std::find(sequence.begin(),sequence.end(),slot);
        int next=sequence.front();if(at!=sequence.end()&&std::next(at)!=sequence.end())next=*std::next(at);
        const HoldPolicy* policy=holdPolicies.contains(command)?&holdPolicies.at(command):nullptr;
        if(auto variants=holdVariants.find(command);variants!=holdVariants.end())for(const auto& variant:variants->second)
            if(variant.fromSlots.contains(slot)){next=variant.slot;policy=&variant.policy;break;}
        auto clip=clips.find(next);if(clip==clips.end()||clip->second.frames.empty())return false;
        if(!output.rotations.empty()&&output.rotations.size()!=clip->second.frames.front().rotations.size())return false;
        transition(next);
        normalSheathing_=command==Command::NormalSheathe;
        if(policy&&heldCommands_.contains(command)){activeHoldPolicy_=*policy;holding_=true;holdCommand_=command;holdTime_=0;}
        return true;
    }
    void transition(int next) {
        holding_=false;holdTime_=0;
        normalSheathing_=false;
        returning_=false;
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
        if(holding_)holdTime_+=dt;
        if(pending_&&clock_>pendingUntil_+1e-7)pending_=false;
        if(pending_&&canTransition()) {Command command=pendingCommand_;pending_=false;if(play(command))clip=current();}
        if(holding_){const auto& rule=activeHoldPolicy_;
            if(rule.loop&&clip->duration()>0)seconds=std::fmod(seconds,clip->duration());
            else seconds=std::min(seconds,rule.frame<0?clip->duration():std::min(clip->duration(),float(rule.frame)/clip->fps));}
        else if(normalSheathing_)seconds=std::min(seconds,clip->duration());
        else if(sheathed())seconds=std::min(seconds,std::min(clip->duration(),float(std::max(0,sheatheHoldFrame))/clip->fps));
        else if(slot==idleSlot) {if(clip->duration()>0)seconds=std::fmod(seconds,clip->duration());else seconds=0;}
        else if(seconds>=clip->duration()) {
            auto idle=clips.find(idleSlot);if(idle==clips.end()||idle->second.frames.empty()){stop();return;}
            transition(idleSlot);clip=&idle->second;
        }
        if(returning_&&resting()){returnElapsed_+=dt;const float t=std::clamp(returnElapsed_/returnDuration_,0.f,1.f);
            const float eased=t*t*(3-2*t);rootOffset_.x=returnFrom_.x*(1-eased);rootOffset_.z=returnFrom_.z*(1-eased);if(t>=1)returning_=false;}
        Pose target=clip->sample(seconds);target.hip.x+=rootOffset_.x;target.hip.z+=rootOffset_.z;blendElapsed_+=dt;
        if(!blendFrom_.rotations.empty()&&transitionSeconds>0&&blendElapsed_<transitionSeconds)
            output=Lerp(blendFrom_,target,blendElapsed_/transitionSeconds);
        else output=std::move(target);
    }
};
} // namespace LS
