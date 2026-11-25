#include "iskra_audio.h"
#include <algorithm>
#include <cmath>

namespace iskra {
namespace {
constexpr int rate=44100;
constexpr double fps=60.0988138974405;
struct Theme { int root; double beat; int bass[4], tune[8]; };
constexpr Theme themes[] = {
    {62,.54,{0,-5,-2,-7},{12,-1,19,16,-1,14,-1,9}},
    {60,.61,{0,-4,-7,-2},{15,-1,19,-1,12,10,-1,14}},
    {65,.50,{0,-5,-3,-7},{16,19,-1,21,19,-1,14,12}}
};
}
bool ForestAudio::open(){
    if(!SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
    SDL_AudioSpec spec{SDL_AUDIO_F32,1,rate};
    stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
    if(stream && !SDL_ResumeAudioStreamDevice(stream)){SDL_DestroyAudioStream(stream);stream=nullptr;}
    for(size_t i=0;i<sine.size();++i) sine[i]=float(std::sin(6.283185307179586*i/sine.size()));
    return ready();
}
ForestAudio::~ForestAudio(){if(stream) SDL_DestroyAudioStream(stream);}
void ForestAudio::reset(){if(stream) SDL_ClearAudioStream(stream); voices={}; oldCoins=oldMotion=oldPower=oldEngine=oldMode=-1;}
bool ForestAudio::pause(bool value){
    if(!stream) return true;
    if(!(value?SDL_PauseAudioStreamDevice(stream):SDL_ResumeAudioStreamDevice(stream))) return false;
    stats.paused=value; return true;
}
void ForestAudio::strike(double midi,double gain,double duration){
    auto& v=voices[next++%voices.size()];
    v={0,2048.*440.*std::exp2((midi-69.)/12.)/rate,gain,std::exp(-6./(rate*duration)),0};
}
void ForestAudio::frame(const unsigned char* r,uint64_t tick){
    if(!stream || stats.paused) return;
    const int mode=r[0x770],motion=r[0x1d],coins=r[0x75e],power=r[0x756],engine=r[0xe];
    const auto& theme=themes[(r[0x75f]+(r[0x74e]==2))%3];
    const int period=int(std::round(theme.beat*fps));
    const bool active=(mode==1 && r[0x772]==3);
    if((active || mode==0 || mode==2) && tick%period==0){
        const auto beat=tick/period;
        if(beat%4==0) strike(theme.root-24+theme.bass[beat/4%4],.09,2.4);
        const int note=theme.tune[beat%8];
        if(note>=0) strike(theme.root+note,mode==0?.032:.065,1.1);
    }
    if(active && oldMode==mode){
        if(oldCoins>=0 && oldCoins!=coins){strike(88,.12,.40);strike(95,.06,.65);}
        if(motion==1 && oldMotion==0) strike(71,.07,.17);
        if(oldPower>=0 && oldPower!=power){strike(power>oldPower?83:48,.15,.7);strike(power>oldPower?90:43,.06,.6);}
        if(engine==11 && oldEngine!=engine) strike(42,.14,1.);
    }
    if(mode==2 && oldMode!=2){strike(74,.09,1.4);strike(81,.09,1.8);strike(90,.07,1.2);}
    oldCoins=coins;oldMotion=motion;oldPower=power;oldEngine=engine;oldMode=mode;
    std::array<float,736> buffer{};
    const int count=int((uint64_t)((tick+1)*rate/fps)-(uint64_t)(tick*rate/fps));
    for(int i=0;i<count;++i){
        noise=noise*1664525u+1013904223u;
        breeze=.98f*breeze+.02f*(float(noise>>16)/32768.f-1.f);
        double mix=breeze*.033;
        for(auto& v:voices) if(v.gain>.00004){
            const int pos=int(v.phase)&2047;
            mix+=v.gain*std::min(1.,v.age++/190.)*(sine[pos]+.15*sine[(pos*2)&2047]);
            v.phase+=v.step; if(v.phase>=2048.)v.phase-=2048.;
            v.gain*=v.decay;
        }
        buffer[i]=float(mix);
    }
    auto queued=std::max(0,SDL_GetAudioStreamQueued(stream))/int(sizeof(float));
    if(queued+count>rate*60/1000){SDL_ClearAudioStream(stream);stats.discarded_samples+=queued;++stats.overrun_events;queued=0;}
    SDL_PutAudioStreamData(stream,buffer.data(),count*sizeof(float));stats.generated_samples+=count;
    stats.queued_samples=queued+count;stats.current_input_queue_ms=1000.*stats.queued_samples/rate;
    stats.max_input_queue_ms=std::max(stats.max_input_queue_ms,stats.current_input_queue_ms);
}
}