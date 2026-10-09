#pragma once

#include "ft8_model.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::ft8 {

// ADIF 3.1.4 text for an OrcSDR "heard" log. OrcSDR is receive-only, so each record says a station was HEARD, never that a
// contact was made; the comment states this so the file cannot be mistaken for a QSO log. No SNR is written (the decoder
// has no calibrated estimator). Pure formatting: no file I/O here.

// The file header. Returns the length written (excluding the terminator), or 0 when `capacity` is too small.
size_t adif_header(char* out, size_t capacity);

// One record. `dial_hz` is the receiver's dial frequency; the RF frequency written is dial_hz + audio_hz. Returns the
// length written, or 0 when the decode has no callsign, the mode has no ADIF form here (JS8 is left out on purpose until
// its ADIF mapping is verified), the frequency is outside a known amateur band, or `capacity` is too small.
size_t adif_heard_record(const Decode& decode, uint32_t dial_hz, char* out, size_t capacity);

// ADIF band name for a frequency in Hz ("40m", "2m", ...), or nullptr when it is outside the amateur bands handled here.
const char* adif_band(uint32_t hz);

bool adif_self_check();

}  // namespace orcsdr::ft8
