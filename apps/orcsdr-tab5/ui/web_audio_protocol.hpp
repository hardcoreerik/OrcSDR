#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::web_console {

constexpr size_t kAudioPacketFrames = 960;
constexpr size_t kAudioHeaderBytes = 32;
constexpr size_t kAudioPacketBytes = kAudioHeaderBytes + kAudioPacketFrames * 2;
constexpr uint32_t kWebAudioRate = 48000;

// ORCA v1: magic[4], version:u16, header_bytes:u16, generation:u32,
// first_sample:u64, sample_rate:u32, frames:u16, channels:u8, format:u8,
// flags:u32. All integers little-endian. Format 1 = PCM s16, flag 1 = gap.
inline size_t encode_audio_packet(uint8_t* out, size_t capacity,
                                 uint32_t generation, uint64_t position,
                                 bool discontinuity, const int16_t* pcm,
                                 size_t frames) {
  if (!out || !pcm || !frames || frames > kAudioPacketFrames ||
      capacity < kAudioHeaderBytes + frames * 2) return 0;
  size_t at = 0;
  const auto put = [&](uint64_t value, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i) {
      out[at++] = static_cast<uint8_t>(value);
      value >>= 8;
    }
  };
  put('O', 1); put('R', 1); put('C', 1); put('A', 1);
  put(1, 2); put(kAudioHeaderBytes, 2);
  put(generation, 4); put(position, 8); put(kWebAudioRate, 4);
  put(frames, 2); put(1, 1); put(1, 1); put(discontinuity ? 1 : 0, 4);
  for (size_t i = 0; i < frames; ++i) put(static_cast<uint16_t>(pcm[i]), 2);
  return at;
}

}  // namespace orcsdr::web_console
