// The part of OrcSDR's main.cpp that owns CB state (mode, clarifier, squelch, scanner, activity monitor,
// persisted settings), ported so the shared CB dashboard behaves the same in the browser. The page supplies the
// radio inputs (spectrum row, signal level) and applies the outputs (frequency, mode, clarifier, squelch).
#pragma once

#include <cstddef>
#include <cstdint>

#include "cb_dashboard.hpp"
#include "nvs_store.hpp"

namespace cbweb {

void load(orcsdr::NvsStore& store);          // settings from storage (call before the first enter)
void begin(uint32_t now_ms);                 // the CB screen is being shown
const orcsdr::cb::Snapshot& snapshot(uint32_t now_ms);

// Radio inputs.
void set_signal(float signal_dbfs, bool squelch_open, bool running, bool sound_enabled);
// `bins`: dB per FFT bin across the receiver window (low to high frequency), `sps` the window width in Hz and
// `center_hz` its centre. Runs the activity monitor and the scanner.
void observe(uint32_t now_ms, const float* bins, size_t count, uint32_t sps, uint32_t center_hz);

// Dashboard actions. Returns true when it changed the radio state the page must apply (tune, mode, clarifier).
void act(const orcsdr::cb::Action& action, uint32_t now_ms);

// Outputs for the page.
uint32_t frequency_hz();
int mode();                  // 0 AM, 1 USB, 2 LSB
int clarifier_hz();
int squelch_dbfs();
uint32_t take_tune();        // a pending retune (scanner or user) in Hz, or 0
bool scanning();

}  // namespace cbweb
