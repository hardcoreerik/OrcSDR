#pragma once
#include "adsb_dashboard.hpp"
#include "flarm_decoder_core.hpp"

namespace orcsdr::flarm {
// UI task initializes once before starting reception. No large static DRAM objects.
bool initialize();
void set_reference(bool valid, int32_t latitude_e7, int32_t longitude_e7);
// DSP task only. Generation and sequence belong to the captured IQ block.
void process(const uint8_t*, size_t, uint32_t generation, uint32_t sequence,
             uint64_t unix_ms, uint32_t now_ms);
adsb::Snapshot snapshot(uint32_t generation, uint32_t now_ms);
} // namespace orcsdr::flarm
