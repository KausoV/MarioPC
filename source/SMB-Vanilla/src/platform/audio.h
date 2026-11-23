#ifndef AUDIO_H
#define AUDIO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct SMB_audio;
struct SMB_audio_stats {
  // Counters start at zero after initialization or successful reset.
  uint64_t generated_samples;
  uint64_t queued_samples;
  uint64_t discarded_samples;
  uint64_t overrun_events;
  // This is the SDL INPUT queue, not physical-device or total system latency.
  double current_input_queue_ms;
  double max_input_queue_ms;
  bool paused;
};
bool SMB_audio_init(struct SMB_audio* audio, int32_t samplerate, int32_t maxlatency_ms);
void SMB_audio_fini(struct SMB_audio* audio);
// Discard queued samples and reset the synthesizer without reopening the device.
// The current pause state is retained.
bool SMB_audio_reset(struct SMB_audio* audio);
// Pause only playback, preserving pending samples and the synthesizer. The
// caller suspends normal simulation; reset/load register writes remain valid.
bool SMB_audio_set_paused(struct SMB_audio* audio, bool paused);
bool SMB_audio_get_stats(const struct SMB_audio* audio, struct SMB_audio_stats* stats);
size_t SMB_audio_size(void);
void SMB_audio_write_register(struct SMB_audio* audio, uint16_t addr, uint8_t data);
void SMB_audio_end_frame(struct SMB_audio* audio);
#ifdef __cplusplus
}
#endif
#endif
