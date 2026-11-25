#pragma once
// How a receiver stream rate maps onto the demodulator chain. The chain decimates the stream by an integer to a "demod
// rate", then by 5 to the audio rate. Streams that are a whole multiple of 240 kS/s demodulate at 240 kS/s (audio 48 kHz,
// as always). The 2.56 MS/s stream (a whole multiple of 256 kS/s, x10) demodulates at 256 kS/s, which gives 51.2 kHz audio;
// audio_rate_conv turns that into 48 kHz at the source so nothing downstream has to know. Pure, constexpr, no hardware.
#include <cstdint>

namespace orcsdr::rtl_rate_plan {

constexpr uint32_t kDemodRate240 = 240000;
constexpr uint32_t kDemodRate256 = 256000;
constexpr uint32_t kStream2p56M = 2560000;
constexpr uint32_t kAudioDecim = 5;          // demod rate -> audio rate (kFmAudioDecim in main.cpp)
constexpr uint32_t kSpeakerRate = 48000;

// Rate the demodulators run at for this stream rate; 0 = the chain cannot demodulate this stream.
constexpr uint32_t demod_rate(uint32_t stream_rate) {
  if (stream_rate >= kDemodRate240 && stream_rate % kDemodRate240 == 0) return kDemodRate240;
  if (stream_rate == kStream2p56M) return kDemodRate256;   // only this multiple of 256 kS/s is wired in (1.024 and 2.048 MS/s are not)
  return 0;
}
// Integer decimation stream -> demod rate (0 when unsupported). Must stay <= 255.
constexpr uint8_t rf_decimation(uint32_t stream_rate) {
  const uint32_t d = demod_rate(stream_rate);
  if (d == 0) return 0;
  const uint32_t n = stream_rate / d;
  return n <= 255 ? static_cast<uint8_t>(n) : 0;
}
// Rate of the audio the demodulators write.
constexpr uint32_t audio_rate(uint32_t stream_rate) {
  const uint32_t d = demod_rate(stream_rate);
  return d == 0 ? 0 : d / kAudioDecim;
}
// True when the audio must be converted to the speaker's 48 kHz.
constexpr bool audio_needs_conversion(uint32_t stream_rate) {
  const uint32_t a = audio_rate(stream_rate);
  return a != 0 && a != kSpeakerRate;
}
// A one-pole coefficient designed for 48 kHz audio, moved to another audio rate so the corner frequency is unchanged.
inline float rescale_one_pole(float k_at_48k, uint32_t audio_rate_hz);

static_assert(rf_decimation(2400000) == 10 && demod_rate(2400000) == 240000 && audio_rate(2400000) == 48000);
static_assert(rf_decimation(960000) == 4 && audio_rate(960000) == 48000);
static_assert(rf_decimation(240000) == 1);
static_assert(rf_decimation(2560000) == 10 && demod_rate(2560000) == 256000 && audio_rate(2560000) == 51200);
static_assert(rf_decimation(2048000) == 0 && rf_decimation(1024000) == 0 && rf_decimation(3200000) == 0);
static_assert(!audio_needs_conversion(2400000) && audio_needs_conversion(2560000));

}  // namespace orcsdr::rtl_rate_plan

#include <cmath>
namespace orcsdr::rtl_rate_plan {
inline float rescale_one_pole(float k_at_48k, uint32_t audio_rate_hz) {
  if (audio_rate_hz == 0 || audio_rate_hz == kSpeakerRate) return k_at_48k;
  return 1.0f - std::pow(1.0f - k_at_48k, static_cast<float>(kSpeakerRate) / static_cast<float>(audio_rate_hz));
}
}  // namespace orcsdr::rtl_rate_plan
