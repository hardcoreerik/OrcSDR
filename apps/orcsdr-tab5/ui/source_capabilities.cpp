#include "source_capabilities.hpp"

namespace orcsdr::source_caps {

Caps remote(uint32_t tuner_type_raw, uint32_t gain_count) {
  Caps caps;
  if (tuner_type_raw == 0 && gain_count == 0) return caps;   // no greeting: the server has told us nothing
  caps.stream = true;
  caps.retune = true;                                         // command 0x01 works on a running stream
  caps.gain = gain_count > 0;                                 // a step table exists only when the server reported gains
  caps.gain_auto = true;                                      // command 0x03 with 0 selects the tuner's own AGC
  caps.rtl_agc = true;                                        // command 0x08
  return caps;
}

}  // namespace orcsdr::source_caps
