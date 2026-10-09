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
// What a bound decoder can do. The bit positions match the decoder workstream's proposal so the two sides agree.
enum DecoderCapability : uint32_t {
  decoder_cap_ft8 = 1u << 0,
  decoder_cap_ft4 = 1u << 1,
  decoder_cap_js8 = 1u << 2,                // JS8 frames (all speeds)
  decoder_cap_assisted = 1u << 3,           // can produce AP-assisted decodes (flagged)
  decoder_cap_message_assembly = 1u << 4,   // JS8 assembled multi-frame messages
  decoder_cap_js8_normal = 1u << 5,         // JS8 Normal only (the one submode with an established sync pattern); Fast/40/Slow/60 need decoder_cap_js8
};

struct DecoderBackend {
  void* context = nullptr;
  bool (*begin)(void* context, uint32_t sample_rate_hz) = nullptr;
  void (*reset)(void* context) = nullptr;
  bool (*begin_slot)(void* context, uint32_t slot_epoch) = nullptr;
  bool (*offer_audio)(void* context, const int16_t* samples, size_t count) = nullptr;
  size_t (*finish_slot)(void* context, Decode* output, size_t capacity) = nullptr;

  // Optional multi-mode extension, appended after the required callbacks. None of these is required by
  // backend_valid(): a backend without them behaves exactly as before, as an FT8-only decoder.
  bool (*set_mode)(void* context, DigitalMode mode) = nullptr;
  // Millisecond slot start (FT4's 7.5 s slots do not fall on whole seconds). Preferred over begin_slot when present.
  bool (*begin_slot_ms)(void* context, uint64_t slot_epoch_ms) = nullptr;
  uint32_t (*capabilities)(void* context) = nullptr;
};

inline bool backend_valid(const DecoderBackend& backend) {
  return backend.begin != nullptr && backend.reset != nullptr &&
         backend.begin_slot != nullptr && backend.offer_audio != nullptr &&
         backend.finish_slot != nullptr;
}

// What the bound backend supports. 0 means no decoder is bound (the dashboard stays UNBOUND). A valid backend that
// reports no capabilities is a legacy backend and is treated as FT8-only.
inline uint32_t backend_capabilities(const DecoderBackend& backend) {
  if (!backend_valid(backend)) return 0;
  if (backend.capabilities == nullptr) return decoder_cap_ft8;
  return backend.capabilities(backend.context);
}

// Whether a capability mask covers a mode. JS8 60 additionally stays marked experimental in the UI.
inline bool mode_supported(uint32_t capabilities, DigitalMode mode) {
  switch (mode) {
    case DigitalMode::ft8: return (capabilities & decoder_cap_ft8) != 0;
    case DigitalMode::ft4: return (capabilities & decoder_cap_ft4) != 0;
    case DigitalMode::js8_normal: return (capabilities & (decoder_cap_js8 | decoder_cap_js8_normal)) != 0;
    case DigitalMode::js8_fast:
    case DigitalMode::js8_40:
    case DigitalMode::js8_slow:
    case DigitalMode::js8_60_experimental: return (capabilities & decoder_cap_js8) != 0;
    case DigitalMode::count: break;
  }
  return false;
}

// Requests a mode. A legacy backend (no set_mode) accepts only FT8; an unsupported mode fails without side effects.
inline bool backend_set_mode(const DecoderBackend& backend, DigitalMode mode) {
  if (!backend_valid(backend)) return false;
  if (!mode_supported(backend_capabilities(backend), mode)) return false;
  if (backend.set_mode == nullptr) return mode == DigitalMode::ft8;
  return backend.set_mode(backend.context, mode);
}

// Starts a slot. Uses begin_slot_ms when the backend has it; the legacy whole-second callback can only represent a
// slot that starts on a whole second, so any other start fails rather than being rounded.
inline bool backend_begin_slot(const DecoderBackend& backend, uint64_t slot_epoch_ms) {
  if (!backend_valid(backend)) return false;
  if (backend.begin_slot_ms != nullptr) return backend.begin_slot_ms(backend.context, slot_epoch_ms);
  if (slot_epoch_ms % 1000u != 0) return false;
  return backend.begin_slot(backend.context, static_cast<uint32_t>(slot_epoch_ms / 1000u));
}

}  // namespace orcsdr::ft8
