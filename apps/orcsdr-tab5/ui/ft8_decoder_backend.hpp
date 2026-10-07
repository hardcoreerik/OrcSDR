#pragma once

#include "ft8_model.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::ft8 {

// Narrow seam between OrcSDR's receiver/runtime and whichever FT8 decoder is
// ultimately selected. The dashboard never depends on a third-party decoder.
//
// Expected input domain for the first implementation: mono signed PCM at
// 12 kHz, already tuned as USB audio so conventional FT8 energy appears in
// the 200-3000 Hz audio passband. A backend may internally buffer one complete
// 15-second slot before returning decodes.
struct DecoderBackend {
  void* context = nullptr;
  bool (*begin)(void* context, uint32_t sample_rate_hz) = nullptr;
  void (*reset)(void* context) = nullptr;
  bool (*begin_slot)(void* context, uint32_t slot_epoch) = nullptr;
  bool (*offer_audio)(void* context, const int16_t* samples, size_t count) = nullptr;
  size_t (*finish_slot)(void* context, Decode* output, size_t capacity) = nullptr;
};

inline bool backend_valid(const DecoderBackend& backend) {
  return backend.begin != nullptr && backend.reset != nullptr &&
         backend.begin_slot != nullptr && backend.offer_audio != nullptr &&
         backend.finish_slot != nullptr;
}

}  // namespace orcsdr::ft8
