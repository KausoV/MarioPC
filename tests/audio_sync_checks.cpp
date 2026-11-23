#include "platform/audio.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Check { std::string name; bool passed; };
std::vector<Check> checks;
void require(bool result, const std::string& name) {
  checks.push_back({name,result});
  if (!result) throw std::runtime_error(name + " | SDL: " + SDL_GetError());
}
struct Audio {
  SMB_audio* ptr = static_cast<SMB_audio*>(std::calloc(1, SMB_audio_size()));
  explicit Audio(int rate=44100,int limit=60) {
    require(ptr && SMB_audio_init(ptr,rate,limit), "Initialize real SDL audio queue at " + std::to_string(rate));
  }
  ~Audio() { SMB_audio_fini(ptr); std::free(ptr); }
  SMB_audio_stats stats() const {
    SMB_audio_stats value{};
    require(SMB_audio_get_stats(ptr,&value), "Read real SDL input-queue statistics");
    return value;
  }
  void tone() {
    SMB_audio_write_register(ptr,0x4015,0x0f);
    SMB_audio_write_register(ptr,0x4000,0x9f);
    SMB_audio_write_register(ptr,0x4001,0);
    SMB_audio_write_register(ptr,0x4002,0x40);
    SMB_audio_write_register(ptr,0x4003,0x08);
  }
};
bool sameCounters(const SMB_audio_stats& a,const SMB_audio_stats& b) {
  return a.generated_samples==b.generated_samples && a.discarded_samples==b.discarded_samples &&
         a.overrun_events==b.overrun_events && a.max_input_queue_ms==b.max_input_queue_ms;
}
std::uint64_t generatedByOriginalApuTiming(unsigned frames,int rate) {
  // Independent expectation from the existing NTSC clock and Blip's unchanged
  // documented 16-bit clock-to-sample quantization, not the queue-cap policy.
  const std::uint64_t clocks = std::uint64_t(frames/2)*59561 + (frames%2 ? 29781 : 0);
  const auto factor = std::uint64_t(std::floor(double(rate)/1789773.0*65536.0+0.5));
  return clocks*factor/65536;
}
void queueAndPause(int rate) {
  Audio audio(rate);
  require(SMB_audio_set_paused(audio.ptr,true), "Pause playback while retaining the APU");
  audio.tone();
  const auto initial=audio.stats();
  require(initial.paused && initial.queued_samples==0 && initial.generated_samples==0,
          "Initial paused state has empty queue and unadvanced synthesizer");
  for(int i=0;i<3;++i) SMB_audio_end_frame(audio.ptr);
  const auto first=audio.stats();
  require(first.generated_samples==generatedByOriginalApuTiming(3,rate),
          "Three synthesized frames retain original NTSC sample timing");
  require(first.queued_samples==first.generated_samples && first.overrun_events==0 && first.discarded_samples==0,
          "Three complete frames fit without dropping or replacing any audio");
  SMB_audio_end_frame(audio.ptr);
  const auto after=audio.stats();
  const auto newest=after.generated_samples-first.generated_samples;
  require(after.generated_samples==generatedByOriginalApuTiming(4,rate),
          "Overrun handling does not advance or alter the APU clock");
  require(after.overrun_events==1 && after.discarded_samples==first.queued_samples && after.queued_samples==newest,
          "Fourth frame replaces the stale queue with exactly the newest frame");
  require(after.current_input_queue_ms<=60.0 && after.max_input_queue_ms<=60.0,
          "The measured SDL input queue never exceeds the configured 60 ms ceiling");
  SDL_Delay(100);
  const auto held=audio.stats();
  require(held.paused && held.queued_samples==after.queued_samples && sameCounters(held,after),
          "Paused playback retains queued samples and does not synthesize audio during wall-clock time");
  require(SMB_audio_set_paused(audio.ptr,true), "Repeated pause is idempotent");
  require(SMB_audio_set_paused(audio.ptr,false), "Resume existing playback without resetting sound");
  SDL_Delay(160);
  const auto resumed=audio.stats();
  require(!resumed.paused && resumed.queued_samples<held.queued_samples && sameCounters(resumed,held),
          "Resume consumes retained audio without clearing APU state or changing counters");
  require(SMB_audio_set_paused(audio.ptr,true), "Pause before reset/load register restoration");
  require(SMB_audio_reset(audio.ptr), "Reset clears old samples without reopening the device");
  const auto reset=audio.stats();
  require(reset.paused && reset.queued_samples==0 && reset.generated_samples==0 && reset.max_input_queue_ms==0 &&
          reset.discarded_samples==0 && reset.overrun_events==0,
          "Reset retains pause while clearing queue and its reset-scoped statistics");
  audio.tone();
  for(int i=0;i<3;++i) SMB_audio_end_frame(audio.ptr);
  const auto restored=audio.stats();
  require(restored.paused && restored.generated_samples==first.generated_samples && restored.queued_samples==first.queued_samples,
          "Paused load-style APU register replay remains operational and reproduces sample timing");
  for(int i=0;i<200;++i) SMB_audio_end_frame(audio.ptr);
  const auto stress=audio.stats();
  require(stress.generated_samples==generatedByOriginalApuTiming(203,rate) && stress.overrun_events>60 &&
          stress.current_input_queue_ms<=60.0 && stress.max_input_queue_ms<=60.0 &&
          stress.discarded_samples+stress.queued_samples==stress.generated_samples,
          "Long paused-consumer catch-up stays bounded and accounts for every generated sample");
}
void belowOneFrameLimit() {
  Audio audio(44100,1);
  require(SMB_audio_set_paused(audio.ptr,true), "Pause a one-millisecond queue");
  SMB_audio_end_frame(audio.ptr);
  const auto value=audio.stats();
  require(value.queued_samples==44 && value.discarded_samples+44==value.generated_samples &&
          value.overrun_events==1 && value.current_input_queue_ms<=1.0 && value.max_input_queue_ms<=1.0,
          "Even a sub-frame queue limit retains only its exact newest sample budget");
}
void invalidAndFallback() {
  SMB_audio_stats untouched{};
  require(!SMB_audio_init(nullptr,44100,60) && !SMB_audio_reset(nullptr) &&
          !SMB_audio_set_paused(nullptr,true) && !SMB_audio_get_stats(nullptr,&untouched),
          "Null audio handles are rejected safely");
  SMB_audio_fini(nullptr);
  SMB_audio_write_register(nullptr,0x4015,0);
  SMB_audio_end_frame(nullptr);
  auto* raw=static_cast<SMB_audio*>(std::calloc(1,SMB_audio_size()));
  require(raw && !SMB_audio_init(raw,0,60) && !SMB_audio_init(raw,44100,0) &&
          !SMB_audio_get_stats(raw,&untouched), "Invalid sample rate or latency fails safely");
  require(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER,"MarioPC-nonexistent-audio-backend",SDL_HINT_OVERRIDE),
          "Select a deliberately unavailable audio backend for fallback coverage");
  require(!SMB_audio_init(raw,44100,60), "Unavailable audio backend returns failure instead of claiming playback");
  SMB_audio_fini(raw);
  require(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER,"dummy",SDL_HINT_OVERRIDE) && SMB_audio_init(raw,44100,60),
          "Failed initialization releases resources so later audio initialization succeeds");
  require(!SMB_audio_get_stats(raw,nullptr), "Null statistics output is rejected safely");
  SMB_audio_fini(raw); std::free(raw);
}
}
int main(int argc,char** argv) {
  const std::filesystem::path output=argc>1 ? std::filesystem::u8path(argv[1]) : std::filesystem::path("audio-sync-verification");
  int result=0;
  std::string fatal;
  try {
    std::filesystem::create_directories(output);
    require(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER,"dummy",SDL_HINT_OVERRIDE), "Use deterministic SDL dummy audio only for this unit test");
    queueAndPause(44100);
    queueAndPause(48000);
    belowOneFrameLimit();
    invalidAndFallback();
  } catch(const std::exception& error) {
    fatal=error.what(); result=1; std::cerr<<fatal<<'\n';
  }
  std::ofstream report(output/"audio-sync-checks.json",std::ios::binary);
  unsigned failures=0;
  for(const auto& check:checks) if(!check.passed) ++failures;
  report<<"{\n  \"kind\": \"actual SDL dummy audio input-queue integration\",\n"
        <<"  \"native_hardware_listening_verified\": false,\n"
        <<"  \"measured_latency_scope\": \"SDL input queue only\",\n"
        <<"  \"checks\": "<<checks.size()<<",\n  \"failures\": "<<failures<<",\n  \"results\": [\n";
  for(std::size_t i=0;i<checks.size();++i) {
    report<<"    {\"name\": \""<<checks[i].name<<"\", \"passed\": "<<(checks[i].passed?"true":"false")<<"}";
    report<<(i+1<checks.size()?",\n":"\n");
  }
  report<<"  ]\n}\n";
  if(!report) result=2;
  std::cout<<"RESULT "<<checks.size()<<" checks, "<<failures<<" failures\n";
  SDL_Quit(); return result;
}
