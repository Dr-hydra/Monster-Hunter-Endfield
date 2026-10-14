#include "../native/core.h"
#include <iostream>
#include <functional>
#include <numbers>
#include <sstream>

namespace {
int failures=0;
void Check(bool ok,const char* expression,int line) {if(!ok){++failures;std::cerr<<"line "<<line<<": "<<expression<<'\n';}}
#define CHECK(...) Check(bool(__VA_ARGS__),#__VA_ARGS__,__LINE__)
bool Near(float a,float b,float tolerance=0.001f){return std::abs(a-b)<=tolerance;}
bool Throws(const std::function<void()>& fn){try{fn();return false;}catch(const std::exception&){return true;}}
LS::Clip MakeClip(int slot,float duration=2.f) {
    LS::Clip c;c.slot=slot;c.fps=60;
    for(int i=0;i<=int(duration*c.fps);++i){LS::Pose p;p.rotations.resize(21);p.hip={float(i)/60,0,0};
        const float angle=float(i)*0.003f+slot*0.001f;
        for(auto& q:p.rotations)q={0,std::sin(angle/2),0,std::cos(angle/2)};
        c.frames.push_back(p);}
    return c;
}
LS::Controller MakeController(){LS::Controller c;for(int id:{1,3,101,102,103,104,105,106,107,110,111,112,150,160,306,307,308})c.clips.emplace(id,MakeClip(id));
 c.moves={{LS::Command::Slash,{101,102,103}},{LS::Command::Thrust,{110,111}},{LS::Command::Spirit,{104,105,106,107}},
 {LS::Command::Fade,{112}},{LS::Command::Foresight,{150}},{LS::Command::Sheathe,{306}},{LS::Command::Iai,{307}},
 {LS::Command::IaiSpirit,{308}},{LS::Command::Helm,{160}},{LS::Command::Dodge,{3}},{LS::Command::NormalSheathe,{3}}};return c;}
void MathTest(){
    using namespace LS;
    Quat q{0,0,std::sin(std::numbers::pi_v<float>/4),std::cos(std::numbers::pi_v<float>/4)};
    Vec3 v=Rotate(q,{1,0,0});CHECK(Near(v.x,0)&&Near(v.y,1)&&Near(v.z,0));
    Quat product=Mul(q,Inverse(q));CHECK(Near(product.w,1)&&Near(product.x,0)&&Near(product.y,0)&&Near(product.z,0));
    Quat opposite{-q.x,-q.y,-q.z,-q.w};Quat mid=Slerp(q,opposite,.5f);CHECK(std::abs(Dot(mid,q))>.9999f);
    Quat quarter=Slerp({},q,.5f);v=Rotate(quarter,{1,0,0});CHECK(Near(v.x,std::sqrt(.5f))&&Near(v.y,std::sqrt(.5f)));
    CHECK(Throws([]{Normalize({0,0,0,0});}));
    auto clip=MakeClip(1);CHECK(Near(clip.duration(),2));CHECK(Near(clip.sample(.125f).hip.x,.125f));CHECK(Near(clip.sample(-1).hip.x,0));CHECK(Near(clip.sample(99).hip.x,2));
    CHECK(Throws([&]{clip.sample(std::numeric_limits<float>::quiet_NaN());}));
}
void ControllerTest(){
    // Held keys used to form special sheathe must not override its follow-up.
    {auto c=MakeController();CHECK(c.start());c.input(LS::Command::Sheathe);c.tick(1.4f);
     c.inputBits(LS::SlashBit,LS::SlashBit|LS::SpiritBit);CHECK(c.slot==307);
     CHECK(c.start());c.input(LS::Command::Sheathe);c.tick(1.4f);
     c.inputBits(LS::SpiritBit,LS::SpiritBit|LS::DodgeBit);CHECK(c.slot==308);}
    using namespace LS;
    auto c=MakeController();CHECK(c.start());CHECK(c.active&&c.slot==1);
    c.input(Command::Slash);CHECK(c.slot==101);c.tick(.94f);c.input(Command::Slash);CHECK(c.slot==101);c.tick(.17f);CHECK(c.slot==102);
    c.tick(1.11f);c.input(Command::Slash);CHECK(c.slot==103);c.tick(1.11f);c.input(Command::Slash);CHECK(c.slot==101);
    // Early presses expire instead of being replayed a second later.
    c.tick(.1f);c.input(Command::Spirit);c.tick(1.02f);CHECK(c.slot==101);
    c.input(Command::Spirit);CHECK(c.slot==104);
    // Holding a key must not chain; each link needs another edge.
    c.stop();CHECK(!c.active);CHECK(c.start());c.inputBits(SlashBit,SlashBit);c.tick(.079f);CHECK(c.slot==1);c.tick(.003f);CHECK(c.slot==101);
    for(int i=0;i<100;++i){c.inputBits(0,SlashBit);c.tick(.015f);}CHECK(c.slot==101);
    c.tick(.6f);CHECK(c.slot==1);
    // Second button within 80 ms produces only the chord, without a wrong attack first.
    c.inputBits(SlashBit,SlashBit);c.tick(.04f);CHECK(c.slot==1);c.inputBits(ThrustBit,SlashBit|ThrustBit);CHECK(c.slot==112);c.tick(.04f);CHECK(c.slot==112);
    c.stop();CHECK(c.start());c.inputBits(SpiritBit|ThrustBit,SpiritBit|ThrustBit);CHECK(c.slot==1&&!c.available(Command::Foresight));
    c.input(Command::Slash);c.tick(1.11f);c.inputBits(SpiritBit,SpiritBit);c.tick(.025f);c.inputBits(ThrustBit,ThrustBit|SpiritBit);CHECK(c.slot==150);
    c.stop();CHECK(c.start());c.inputBits(DodgeBit,SpiritBit|DodgeBit);CHECK(c.slot==1&&!c.available(Command::Sheathe));
    c.input(Command::Slash);c.tick(1.11f);c.inputBits(DodgeBit,SpiritBit|DodgeBit);CHECK(c.slot==306);c.tick(5);CHECK(c.sheathed()&&Near(c.seconds,80.f/60));
    c.input(Command::Slash);CHECK(c.slot==307);c.tick(2.1f);CHECK(c.slot==1);
    c.input(Command::Sheathe);c.tick(1.34f);c.input(Command::Spirit);CHECK(c.slot==308);
    // Blending starts continuously and ends at the sampled target pose.
    c.stop();CHECK(c.start());c.tick(.2f);const Pose before=c.output;c.input(Command::Slash);c.tick(.001f);
    CHECK(std::abs(c.output.hip.x-before.hip.x)<.01f);c.tick(.11f);CHECK(Near(c.output.hip.x,before.hip.x+c.clips.at(101).sample(c.seconds).hip.x-c.clips.at(101).frames.front().hip.x));
    for(int i=0;i<100;++i){c.tick(1.11f);c.input(i%2?Command::Slash:Command::Spirit);c.tick(.101f);
        for(auto q:c.output.rotations)CHECK(Finite(q)&&Near(Dot(q,q),1,.0001f));CHECK(Finite(c.output.hip));}
    c.input(Command::Stop);CHECK(!c.active);c.input(Command::Slash);CHECK(!c.active);CHECK(c.start());c.tick(.3f);CHECK(c.slot==1);
    CHECK(Throws([&]{c.tick(-1);}));CHECK(Throws([&]{c.tick(std::numeric_limits<float>::infinity());}));CHECK(Throws([&]{c.tick(std::numeric_limits<float>::max());}));
    CHECK(c.start());c.input(Command::NormalSheathe);c.tick(3);CHECK(c.normalSheathed()&&c.resting()&&c.slot==3);c.tick(5);CHECK(c.slot==3&&Near(c.seconds,2));
    c.input(Command::Slash);CHECK(!c.normalSheathed()&&c.slot==101);
    CHECK(!c.start(999));c.setMove(Command::Helm,{301});CHECK(c.start());c.input(Command::Helm);CHECK(c.slot==1);c.tick(.5f);CHECK(c.slot==1);
}
std::vector<unsigned char> ValidBytes(){
    std::vector<unsigned char>b{'L','S','M','1'};
    auto put=[&](uint32_t v){for(int i=0;i<4;++i)b.push_back(static_cast<unsigned char>(v>>(i*8)));};
    put(2);put(21);put(60);
    auto f=[&](float value){uint32_t bits;std::memcpy(&bits,&value,4);put(bits);};
    for(int frame=0;frame<2;++frame){for(int bone=0;bone<21;++bone){f(0);f(0);f(0);f(1);}f(float(frame));f(0);f(0);}
    return b;
}
void ReaderTest(){
    const auto folder=std::filesystem::temp_directory_path()/"endfield_longsword_core_tests";
    std::filesystem::create_directories(folder);const auto path=folder/"clip_307.lsm";
    auto write=[&](const auto& bytes){std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));};
    auto original=ValidBytes();write(original);auto c=LS::ReadClip(path);CHECK(c.slot==307&&c.frames.size()==2&&c.frames[0].rotations.size()==21);CHECK(Near(c.sample(1.f/120).hip.x,.5f));
    auto bad=original;bad[0]='X';write(bad);CHECK(Throws([&]{LS::ReadClip(path);}));
    bad=original;bad.pop_back();write(bad);CHECK(Throws([&]{LS::ReadClip(path);}));
    bad=original;bad.push_back(0);write(bad);CHECK(Throws([&]{LS::ReadClip(path);}));
    bad=original;bad[4]=255;bad[5]=255;bad[6]=255;bad[7]=127;write(bad);CHECK(Throws([&]{LS::ReadClip(path);}));
    bad=original;bad[8]=22;write(bad);CHECK(Throws([&]{LS::ReadClip(path);}));
    bad=original;bad[12]=0;write(bad);CHECK(Throws([&]{LS::ReadClip(path);}));
    bad=original;for(int i=0;i<16;++i)bad[16+i]=0;write(bad);CHECK(Throws([&]{LS::ReadClip(path);}));
    bad=original;bad[16]=0;bad[17]=0;bad[18]=128;bad[19]=127;write(bad);CHECK(Throws([&]{LS::ReadClip(path);}));
    bad=original;const size_t hipOffset=16+21*16;bad[hipOffset]=0;bad[hipOffset+1]=0;bad[hipOffset+2]=192;bad[hipOffset+3]=127;write(bad);CHECK(Throws([&]{LS::ReadClip(path);}));
    write(original);CHECK(Throws([&]{LS::ReadClip(path,0);}));CHECK(Throws([&]{LS::ReadClip(path,22);}));
    std::filesystem::remove(path);std::filesystem::remove(folder);
}
void ModeControlsTest(){
    using namespace LS;auto c=MakeController();c.longswordRules=false;
    c.moves={{Command::Slash,{101,102}},{Command::Guard,{104}},{Command::Charge,{105}},{Command::Fire,{106}},{Command::Release,{107}}};
    c.inputBindings={{SlashBit,Command::Slash},{ThrustBit,Command::Guard},{SpiritBit,Command::Charge}};
    c.chordBindings={{SlashBit|ThrustBit,Command::Fire}};
    c.holdPolicies[Command::Guard]={30,false,{}};
    c.holdPolicies[Command::Charge]={45,false,{{0,106},{1,107}}};
    CHECK(c.start());c.inputBits(SlashBit|ThrustBit,SlashBit|ThrustBit);CHECK(c.slot==106);
    c.tick(3);CHECK(c.slot==c.idleSlot);c.held(Command::Guard,true);c.tick(2);CHECK(c.slot==104&&Near(c.seconds,.5f));
    CHECK(c.heldSeconds()>1);c.held(Command::Guard,false);CHECK(c.slot==c.idleSlot);
    c.held(Command::Charge,true);c.tick(.8f);CHECK(c.slot==105&&Near(c.seconds,.75f));c.held(Command::Charge,false);CHECK(c.slot==106);
    c.tick(3);c.held(Command::Charge,true);c.tick(1.4f);c.held(Command::Charge,false);CHECK(c.slot==107);
    c.stop();CHECK(!c.holding(Command::Charge)&&Near(c.heldSeconds(),0));
    CHECK(c.start());c.hardwareInput(ThrustBit,ThrustBit);c.tick(.1f);CHECK(c.holding(Command::Guard));
    c.hardwareInput(SlashBit,SlashBit|ThrustBit);CHECK(c.slot==106&&!c.holding(Command::Guard));
    c.hardwareInput(0,0);c.tick(3);
    c.chordBindings[SlashBit|SpiritBit]=Command::Charge;
    c.hardwareInput(SlashBit|SpiritBit,SlashBit|SpiritBit);c.tick(1.2f);CHECK(c.holding(Command::Charge));
    c.hardwareInput(0,0);CHECK(c.slot==107&&!c.holding(Command::Charge));
    c.stop();CHECK(c.start());c.hardwareInput(0,SpiritBit);CHECK(c.slot==c.idleSlot); // no stale held-key start
    CHECK(CommandFromName("reload")==Command::Reload);CHECK(CommandName(Command::Counter)=="counter");CHECK(Throws([]{CommandFromName("missing");}));
}
}
int main(int argc,char** argv){
    try{MathTest();ControllerTest();ReaderTest();ModeControlsTest();
        if(argc>1){size_t count=0;for(const auto& entry:std::filesystem::directory_iterator(argv[1]))if(entry.path().extension()==".lsm"){
            auto clip=LS::ReadClip(entry.path());auto pose=clip.sample(clip.duration()*.4321f);
            CHECK(pose.rotations.size()==21&&LS::Finite(pose.hip));for(auto q:pose.rotations)CHECK(LS::Finite(q)&&Near(LS::Dot(q,q),1,.0001f));++count;}
            CHECK(count>0);std::cout<<"Validated "<<count<<" asset clips\n";}
    }catch(const std::exception& e){++failures;std::cerr<<"Exception: "<<e.what()<<'\n';}
    if(failures){std::cerr<<failures<<" checks failed\n";return 1;}std::cout<<"All core checks passed\n";return 0;
}
