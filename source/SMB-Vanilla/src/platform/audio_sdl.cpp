// Audio playback is independent of the native game logic and music tables.
#include "audio.h"
#ifdef USE_SDL2
#  include <SDL.h>
#else
#  include <SDL3/SDL.h>
#endif
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <new>
#include <type_traits>
#include "nes_apu/Nes_Apu.h"

static_assert(std::is_same_v<blip_sample_t, std::int16_t>,
              "blip_sample_t should be int16_t");

class NesAudioImpl {
private:
  Blip_Buffer _buf;
  Nes_Apu _apu;
  blip_time_t _clock;
  int _frame;
  bool _valid;
  static int null_dmc_reader(int unused) {
    (void)unused;
    return 0x55;
  }
public:
  NesAudioImpl(int32_t samplerate) : _buf(), _apu(), _clock(0), _frame(0), _valid(false) {
    _buf.clock_rate(1789773);
    const std::error_condition error = _buf.set_sample_rate(samplerate);
    if (error) {
      printf("ERROR: %s\n", error.message().c_str());
      return;
    }
    _apu.dmc_reader = NesAudioImpl::null_dmc_reader;
    _apu.set_output(&_buf);
    _valid = true;
  }
  bool valid() const { return _valid; }
  void write_register(uint16_t addr, uint8_t data) {
    _apu.write_register(_clock, addr, data);
    // Keep the port's existing register timing and synthesizer unchanged.
    _clock += 4;
  }
  int end_frame_then_read_i16(int16_t* out, size_t count) {
    _clock = 0;
    // Alternate 29780 and 29781 clocks per NTSC frame: average 29780.5.
    _frame = (_frame + 1) % 2;
    const blip_time_t frame_length = _frame == 0 ? 29780 : 29781;
    _apu.end_frame(frame_length);
    _buf.end_frame(frame_length);
    return _buf.read_samples(out, count);
  }
};

struct SMB_audio {
  NesAudioImpl* nesaudio;
  int32_t sample_rate;
#ifdef USE_SDL2
  SDL_AudioDeviceID audio_device_id;
#else
  SDL_AudioStream* audio_stream;
#endif
  uint64_t max_samples_to_be_queued;
  int16_t* readsamples_tmpbuf;
  size_t readsamples_tmpbuf_count;
  uint64_t generated_samples;
  uint64_t discarded_samples;
  uint64_t overrun_events;
  uint64_t max_input_queue_samples;
  bool paused;
  bool audio_subsystem_initialized;
};

static void clear_stats(SMB_audio* audio) {
  audio->generated_samples = 0;
  audio->discarded_samples = 0;
  audio->overrun_events = 0;
  audio->max_input_queue_samples = 0;
}

static int input_queue_bytes(const SMB_audio* audio) {
#ifdef USE_SDL2
  return static_cast<int>(SDL_GetQueuedAudioSize(audio->audio_device_id));
#else
  return SDL_GetAudioStreamQueued(audio->audio_stream);
#endif
}

static bool clear_input_queue(SMB_audio* audio) {
#ifdef USE_SDL2
  SDL_ClearQueuedAudio(audio->audio_device_id);
  return true;
#else
  return SDL_ClearAudioStream(audio->audio_stream);
#endif
}

bool SMB_audio_init(SMB_audio* audio, int32_t samplerate, int32_t maxlatency_ms) {
  if (!audio || samplerate <= 0 || maxlatency_ms <= 0) return false;
  *audio = {};
  audio->sample_rate = samplerate;
  audio->max_samples_to_be_queued = uint64_t(samplerate) * uint64_t(maxlatency_ms) / 1000;
  if (!audio->max_samples_to_be_queued) return false;
  audio->nesaudio = new (std::nothrow) NesAudioImpl(samplerate);
  if (!audio->nesaudio || !audio->nesaudio->valid()) {
    delete audio->nesaudio;
    audio->nesaudio = nullptr;
    return false;
  }
  // A complete frame is approximately samplerate / 60; retain the original margin.
  audio->readsamples_tmpbuf_count = std::max<size_t>(1, size_t(samplerate) * 2 / 60);
  audio->readsamples_tmpbuf = static_cast<int16_t*>(
      malloc(audio->readsamples_tmpbuf_count * sizeof(int16_t)));
  if (!audio->readsamples_tmpbuf) {
    SMB_audio_fini(audio);
    return false;
  }
#ifdef USE_SDL2
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
#else
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
#endif
    printf("ERROR: %s\n", SDL_GetError());
    SMB_audio_fini(audio);
    return false;
  }
  audio->audio_subsystem_initialized = true;
  SDL_AudioSpec spec;
  SDL_zero(spec);
  spec.freq = samplerate;
  spec.channels = 1;
#ifdef USE_SDL2
  spec.format = AUDIO_S16SYS;
  spec.samples = 2048;
  audio->audio_device_id = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
  if (!audio->audio_device_id) {
    printf("ERROR: %s\n", SDL_GetError());
    SMB_audio_fini(audio);
    return false;
  }
  SDL_PauseAudioDevice(audio->audio_device_id, 0);
#else
  spec.format = SDL_AUDIO_S16;
  audio->audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (!audio->audio_stream || !SDL_ResumeAudioStreamDevice(audio->audio_stream)) {
    printf("ERROR: %s\n", SDL_GetError());
    SMB_audio_fini(audio);
    return false;
  }
#endif
  return true;
}

bool SMB_audio_set_paused(SMB_audio* audio, bool paused) {
  if (!audio || !audio->nesaudio) return false;
  if (audio->paused == paused) return true;
#ifdef USE_SDL2
  SDL_PauseAudioDevice(audio->audio_device_id, paused ? 1 : 0);
#else
  const bool success = paused ? SDL_PauseAudioStreamDevice(audio->audio_stream)
                              : SDL_ResumeAudioStreamDevice(audio->audio_stream);
  if (!success) return false;
#endif
  // Do not clear samples or reset the APU. The caller pauses game simulation.
  audio->paused = paused;
  return true;
}

bool SMB_audio_get_stats(const SMB_audio* audio, SMB_audio_stats* stats) {
  if (!audio || !audio->nesaudio || !stats) return false;
  const int queued_bytes = input_queue_bytes(audio);
  if (queued_bytes < 0) return false;
  stats->generated_samples = audio->generated_samples;
  stats->queued_samples = uint64_t(queued_bytes) / sizeof(int16_t);
  stats->discarded_samples = audio->discarded_samples;
  stats->overrun_events = audio->overrun_events;
  stats->current_input_queue_ms = 1000.0 * double(stats->queued_samples) / audio->sample_rate;
  stats->max_input_queue_ms = 1000.0 * double(audio->max_input_queue_samples) / audio->sample_rate;
  stats->paused = audio->paused;
  return true;
}

bool SMB_audio_reset(SMB_audio* audio) {
  if (!audio || !audio->nesaudio) return false;
  auto* replacement = new (std::nothrow) NesAudioImpl(audio->sample_rate);
  if (!replacement) return false;
  if (!replacement->valid() || !clear_input_queue(audio)) {
    delete replacement;
    return false;
  }
  delete audio->nesaudio;
  audio->nesaudio = replacement;
  clear_stats(audio);
  // The device's pause state is preserved, including reset/load from a menu.
  return true;
}

void SMB_audio_fini(SMB_audio* audio) {
  if (!audio) return;
  free(audio->readsamples_tmpbuf);
  audio->readsamples_tmpbuf = nullptr;
#ifdef USE_SDL2
  if (audio->audio_device_id) SDL_CloseAudioDevice(audio->audio_device_id);
  audio->audio_device_id = 0;
#else
  if (audio->audio_stream) SDL_DestroyAudioStream(audio->audio_stream);
  audio->audio_stream = nullptr;
#endif
  delete audio->nesaudio;
  audio->nesaudio = nullptr;
  if (audio->audio_subsystem_initialized) SDL_QuitSubSystem(SDL_INIT_AUDIO);
  audio->audio_subsystem_initialized = false;
}

size_t SMB_audio_size() { return sizeof(SMB_audio); }

void SMB_audio_write_register(SMB_audio* audio, uint16_t addr, uint8_t data) {
  if (audio && audio->nesaudio) audio->nesaudio->write_register(addr, data);
}

void SMB_audio_end_frame(SMB_audio* audio) {
  if (!audio || !audio->nesaudio) return;
  const int generated = audio->nesaudio->end_frame_then_read_i16(
      audio->readsamples_tmpbuf, audio->readsamples_tmpbuf_count);
  if (generated <= 0) return;
  audio->generated_samples += uint64_t(generated);
#ifndef USE_SDL2
  // Queue inspection, stale-queue clearing and append must form one operation.
  // SDL stream locks are recursive, so the queue functions remain safe here.
  if (!SDL_LockAudioStream(audio->audio_stream)) {
    printf("ERROR: %s\n", SDL_GetError());
    return;
  }
#endif
  const int queued_bytes = input_queue_bytes(audio);
  if (queued_bytes < 0) {
    printf("ERROR: %s\n", SDL_GetError());
#ifndef USE_SDL2
    SDL_UnlockAudioStream(audio->audio_stream);
#endif
    return;
  }
  const uint64_t queued_samples = uint64_t(queued_bytes) / sizeof(int16_t);
  const uint64_t keep = std::min<uint64_t>(uint64_t(generated), audio->max_samples_to_be_queued);
  if (queued_samples + uint64_t(generated) > audio->max_samples_to_be_queued) {
    // Keep the newest sound when catching up instead of retaining stale sound
    // and dropping every new frame. Even a sub-frame limit stays exact.
    if (!clear_input_queue(audio)) {
      printf("ERROR: %s\n", SDL_GetError());
#ifndef USE_SDL2
      SDL_UnlockAudioStream(audio->audio_stream);
#endif
      return;
    }
    ++audio->overrun_events;
    audio->discarded_samples += queued_samples + uint64_t(generated) - keep;
  }
  const auto* samples = audio->readsamples_tmpbuf + size_t(generated) - size_t(keep);
  const int bytes = static_cast<int>(keep * sizeof(int16_t));
#ifdef USE_SDL2
  const bool success = SDL_QueueAudio(audio->audio_device_id, samples, bytes) == 0;
#else
  const bool success = SDL_PutAudioStreamData(audio->audio_stream, samples, bytes);
#endif
  if (!success) {
    printf("ERROR: %s\n", SDL_GetError());
  } else {
    const int after_bytes = input_queue_bytes(audio);
    if (after_bytes >= 0) audio->max_input_queue_samples =
        std::max(audio->max_input_queue_samples, uint64_t(after_bytes) / sizeof(int16_t));
  }
#ifndef USE_SDL2
  SDL_UnlockAudioStream(audio->audio_stream);
#endif
}
