#pragma once
#include <SDL3/SDL.h>
#include "platform/audio.h"
#include <array>
#include <cstdint>

namespace iskra {
// Forest bells, air and footsteps follow the simulation clock and scene events.
class ForestAudio {
public:
    bool open();
    ~ForestAudio();
    void reset();
    bool pause(bool value);
    void frame(const unsigned char* ram, uint64_t tick);
    bool ready() const { return stream != nullptr; }
    SMB_audio_stats stats{};
private:
    struct Voice { double phase=0, step=0, gain=0, decay=0; int age=0; };
    void strike(double midi,double gain,double duration);
    SDL_AudioStream* stream=nullptr;
    std::array<Voice,12> voices{};
    std::array<float,2048> sine{};
    unsigned next=0, noise=37;
    float breeze=0;
    int oldCoins=-1, oldMotion=-1, oldPower=-1, oldEngine=-1, oldMode=-1;
};
}