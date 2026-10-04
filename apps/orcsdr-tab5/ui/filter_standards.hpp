#pragma once

#include <cstddef>
#include <cstdint>

// Receiver channel-filter bandwidths that make sense for each kind of signal, and the standard
// one for that kind. Pure data, no display or RTOS, host-tested. Home's filter popup lists the
// presets for the band on screen; the standard value matches what each dashboard defaults to.
namespace orcsdr::filter_standards {

enum class Kind : uint8_t {
  fixed,          // the demodulator sets its own width (P25, LoRa, ADS-B, pager...)
  wfm,            // FM broadcast
  nfm,            // narrow FM voice (weather, public service, generic VHF/UHF)
  am_broadcast,   // medium-wave AM broadcast
  am_shortwave,   // shortwave AM
  cb_am,          // CB, AM
  cb_ssb,         // CB, SSB
  airband_am,     // civil aviation AM (25 kHz or 8.33 kHz raster)
};

constexpr size_t kMaxPresets = 6;

struct Standards {
  const char* name;
  uint32_t standard_hz;                  // 0 for fixed
  uint8_t count;                         // presets, ascending, always containing standard_hz
  uint32_t presets_hz[kMaxPresets];
};

const Standards& standards(Kind kind);

// The preset closest to `bandwidth_hz`; 0 for a fixed kind.
uint32_t nearest_preset_hz(Kind kind, uint32_t bandwidth_hz);

bool self_check();

}  // namespace orcsdr::filter_standards
