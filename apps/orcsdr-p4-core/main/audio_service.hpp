#pragma once
// Receiver audio on the Waveshare board: IQ from the receiver task -> demodulator
// (components/orcsdr_audio_dsp) -> adaptive resampler -> ES8311 + speaker.
//
// The dongle's clock and the I2S clock differ by a few hundred ppm. Without correction the
// stream buffer would slowly fill or drain and the audio would click about once a second, so a
// small linear-interpolating resampler nudges its ratio to hold the buffer at half full.
#include <cstdint>
#include "orcsdr_dsp/demod.hpp"

namespace audio_service {
bool start();  // opens the speaker output and starts the audio task

// The mode follows Home's active demodulation (wfm / nfm / am); other modes are muted.
void set_mode(orcsdr::dsp::Mode mode, bool supported);
// Home's volume (0-255) and sound on/off.
void set_volume(uint8_t volume_0_255);
void set_sound(bool enabled);

// Test tone straight to the speaker (bypasses the receiver). Returns false if audio is not running.
bool play_tone(uint32_t frequency_hz, uint32_t duration_ms);

// Squelch gate: false mutes the audio (the receiver and demodulator keep running). Defaults open.
void set_gate(bool open);
// false stops demodulating (digital dashboards that have no audio); IQ is discarded and the speaker stays silent.
void set_active(bool active);
// Channel power after the demodulator's channel filter, dBFS.
float channel_dbfs();

struct Status {
  bool running = false;
  bool muted = false;
  uint8_t volume = 0;
  uint32_t blocks = 0;        // IQ blocks demodulated
  uint32_t dropped_blocks = 0;  // IQ blocks that did not fit the stream buffer
  uint32_t underruns = 0;     // times the audio task had no IQ ready
  float buffer_fill = 0.0f;   // 0..1 (the resampler holds it near 0.1)
  float resample_ppm = 0.0f;  // current rate correction
  uint32_t write_us = 0;      // smoothed time inside one 5 ms I2S write (mostly waiting)
  uint32_t resample_us = 0;   // smoothed resample+write time per block
  uint32_t demod_us = 0;      // smoothed demodulation time per 16 KB IQ block (8.5 ms of signal)
};
Status status();
}  // namespace audio_service
