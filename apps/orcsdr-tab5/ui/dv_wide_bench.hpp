#pragma once
// Development benchmark for the wide-stream digital tuner (dv::WideDdc + the float channelizer entry): how much of one
// core does tuning a channel out of a 2.4 MS/s stream cost on this chip? Synthetic input, buffers in PSRAM, nothing
// touches the radio, the scanner or any setting. Command: RTL_DV DDCBENCH.
#include <cstdint>

namespace orcsdr::dv_wide_bench {

// `micros` is a free-running microsecond counter. Writes result lines through `write_line`.
void run(uint32_t (*micros)(), void (*write_line)(const char*));

}  // namespace orcsdr::dv_wide_bench
