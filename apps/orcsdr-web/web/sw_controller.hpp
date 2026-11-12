// The part of OrcSDR's main.cpp that owns Shortwave state (tuning, step, filter, mode/clarifier, audio DSP,
// memories and logbook, band hunt), ported so the shared Shortwave dashboard behaves the same in the browser.
// The page supplies radio inputs and applies the outputs.
#pragma once

#include <cstddef>
#include <cstdint>

#include "shortwave_dashboard.hpp"
#include "nvs_store.hpp"

namespace swweb {

void load(orcsdr::NvsStore& store);
void begin(uint32_t now_ms);
const orcsdr::shortwave::Snapshot& snapshot();

// Radio inputs from the page (named so the page can set them one by one).
void set_num(const char* key, double value);
void set_str(const char* key, const char* value);
void set_gain_step(int index, int tenth_db);

// A dashboard action. Returns true when the page has to do something about it (gain, volume, hunt, export...);
// everything else is handled here.
bool act(const orcsdr::shortwave::Action& action, uint32_t now_ms);

// Outputs.
uint32_t frequency_hz();
int mode();              // 0 AM, 1 USB, 2 LSB (what to demodulate at this frequency)
int clarifier_hz();
uint32_t filter_hz();
uint32_t span_hz();
bool audio_boost();
uint32_t take_tune();    // a pending retune in Hz, or 0

// Band hunt: the page runs a wide session over the band and hands back the spectrum.
bool hunt_band(size_t index, uint32_t* min_hz, uint32_t* max_hz);
void hunt_finish(const float* bins, size_t count, uint32_t sps, uint32_t center_hz);
bool hunt_active();
int hunt_requested_band();     // band index the page should hunt, or -1 (cleared when read)

}  // namespace swweb
