#pragma once

// What the active receiver source can do, stated by the source itself.
//
// The USB dongle describes itself through the driver's capability bits. A remote rtl_tcp server has no such bits:
// all it offers is the 12-byte greeting (tuner type and number of gain steps) and the five commands the protocol
// defines. This module turns that into the same kind of answer, so a screen asks "can I set gain?" without caring
// whether the dongle is in the Tab5 or across the network, and a unit with no local dongle works at all.
//
// Pure functions, no driver or network access. Only what the protocol can actually carry is claimed: nothing here
// promises a readback (rtl_tcp has none), bias tee, direct sampling control from the dashboards, or an HF path.
// The driver stays independent of OrcSDR; this lives in OrcSDR and adapts to the driver, never the other way.

#include <cstdint>

namespace orcsdr::source_caps {

struct Caps {
  bool stream = false;      // IQ can be received
  bool retune = false;      // the centre frequency can be changed while streaming
  bool gain = false;        // manual tuner gain, with a step table (needs the greeting's gain count)
  bool gain_auto = false;   // the server's tuner AGC (gain mode 0)
  bool rtl_agc = false;     // the RTL2832 digital AGC command
};

// `tuner_type_raw` and `gain_count` come from the greeting; both 0 means no greeting yet, so nothing is offered.
Caps remote(uint32_t tuner_type_raw, uint32_t gain_count);

}  // namespace orcsdr::source_caps
