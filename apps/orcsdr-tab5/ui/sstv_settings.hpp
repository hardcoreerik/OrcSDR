#pragma once
// SSTV user settings: detection strategy, manual mode and auto-save. Storage is injected
// (the firmware passes its NVS preferences), so loading, sanitising and applying the
// settings can be tested on the host.
//
// "Manual" is a VIS match filter: the receiver only starts an image when a valid VIS header
// names the selected mode. Decoding a chosen mode without any VIS header is not implemented,
// so the UI must not claim it.
#include "sstv_receiver.hpp"
#include <cstdint>

namespace orcsdr::sstv_settings {

enum class Detection : uint8_t {
  automatic = 0,  // VIS first, line-sync recovery when the header is missing (default)
  vis_only = 1,   // a valid VIS header is required; no sync fallback
  manual = 2      // VIS header must match the chosen mode (a filter, not a forced decode)
};

struct Settings {
  Detection detection = Detection::automatic;
  orcsdr::sstv::Mode manual_mode = orcsdr::sstv::Mode::robot36;
  bool autosave = true;
};

struct Store {
  bool (*get_bool)(const char* key, bool fallback) = nullptr;
  void (*put_bool)(const char* key, bool value) = nullptr;
  uint8_t (*get_u8)(const char* key, uint8_t fallback) = nullptr;
  void (*put_u8)(const char* key, uint8_t value) = nullptr;
  uint32_t (*get_u32)(const char* key, uint32_t fallback) = nullptr;
  void (*put_u32)(const char* key, uint32_t value) = nullptr;
};

Settings load(const Store& store);                 // invalid stored values fall back to the defaults
void save(const Store& store, const Settings& settings);
// Pushes the detection settings into the receiver. Never restarts it or drops an image.
void apply(const Settings& settings);

Settings with_detection(Settings settings, Detection detection);
// Choosing a mode selects manual detection and remembers the mode.
Settings with_manual_mode(Settings settings, orcsdr::sstv::Mode mode);
bool valid_manual_mode(uint8_t ordinal);           // 1..14: every concrete mode, not auto_vis

}  // namespace orcsdr::sstv_settings
