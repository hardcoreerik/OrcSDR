#include "filter_standards.hpp"

namespace orcsdr::filter_standards {
namespace {

// Widths are whole kHz (the receiver rounds to 1 kHz) and inside each band's clamp.
constexpr Standards kTable[] = {
    /* fixed        */ {"FIXED BY MODE", 0, 0, {}},
    /* wfm          */ {"WIDE FM BROADCAST", 260000, 4, {150000, 200000, 260000, 300000}},
    /* nfm          */ {"NARROW FM VOICE", 25000, 5, {8000, 12000, 16000, 25000, 50000}},
    /* am_broadcast */ {"AM BROADCAST", 10000, 5, {3000, 4000, 6000, 10000, 15000}},
    /* am_shortwave */ {"SHORTWAVE AM", 6000, 5, {3000, 4000, 6000, 9000, 12000}},
    /* cb_am        */ {"CB AM", 10000, 4, {6000, 8000, 10000, 12000}},
    /* cb_ssb       */ {"CB SSB", 3000, 4, {2400, 3000, 4000, 6000}},
    /* airband_am   */ {"AIRBAND AM", 10000, 4, {6000, 8000, 10000, 12000}},
};

}  // namespace

const Standards& standards(Kind kind) {
  const size_t index = static_cast<size_t>(kind);
  return kTable[index < sizeof(kTable) / sizeof(kTable[0]) ? index : 0];
}

uint32_t nearest_preset_hz(Kind kind, uint32_t bandwidth_hz) {
  const Standards& s = standards(kind);
  if (s.count == 0) return 0;
  uint32_t best = s.presets_hz[0];
  uint32_t best_distance = UINT32_MAX;
  for (uint8_t i = 0; i < s.count; ++i) {
    const uint32_t hz = s.presets_hz[i];
    const uint32_t distance = hz > bandwidth_hz ? hz - bandwidth_hz : bandwidth_hz - hz;
    if (distance < best_distance) {
      best_distance = distance;
      best = hz;
    }
  }
  return best;
}

bool self_check() {
  for (size_t k = 0; k < sizeof(kTable) / sizeof(kTable[0]); ++k) {
    const Standards& s = kTable[k];
    if (s.count > kMaxPresets) return false;
    if (s.count == 0) {
      if (s.standard_hz != 0) return false;
      continue;
    }
    bool has_standard = false;
    for (uint8_t i = 0; i < s.count; ++i) {
      if (s.presets_hz[i] == s.standard_hz) has_standard = true;
      if (i > 0 && s.presets_hz[i] <= s.presets_hz[i - 1]) return false;
    }
    if (!has_standard) return false;
  }
  return true;
}

}  // namespace orcsdr::filter_standards
