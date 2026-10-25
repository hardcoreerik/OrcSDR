#pragma once
// Receiver service: owns the esp-rtl-sdr driver and publishes bounded snapshots. It never
// draws and never waits for the UI; the UI timer pulls snapshots. Derived from the radio
// task of the Waveshare 4B app (recovered from closed PR #197, b7660c58) with the board
// code removed.
#include <cstddef>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "home_v3_ui.hpp"

namespace orcsdr::radio {
bool start(QueueHandle_t commands);
// Receives every block of CU8 IQ as the receiver task reads it (called from that task; must not block).
using IqSink = void (*)(const uint8_t* iq, size_t bytes);
void set_iq_sink(IqSink sink);
// Tunes to an absolute frequency (applied by the receiver task; retunes live if streaming).
void tune_to(uint32_t frequency_hz);
// RMS level of the latest IQ block, dBFS (-120 when there is no signal path yet).
float level_dbfs();
// Sample rate of the IQ stream in samples per second.
uint32_t sample_rate();
// Requests 960 kS/s (rate <= 1 MS/s) or 2.4 MS/s; the stream restarts briefly.
void set_sample_rate(uint32_t rate);
// Receiver controls (tuner gain, AGC, bias tee). Reads come from a cache the receiver task refreshes
// about twice a second; writes are queued for that task and applied while streaming. Automatic gain is the
// tuner's own AGC, so it adapts to the dongle, antenna and location (nothing is tuned to one setup).
struct Gain {
  bool available = false;       // the dongle exposes a tuner gain ladder
  bool auto_available = false;  // tuner AGC can be selected
  bool auto_mode = true;
  int tenth_db = 0;             // current manual gain, 1/10 dB
  int steps[32] = {};           // the ladder, 1/10 dB
  uint8_t step_count = 0;
  bool rtl_agc_available = false;
  bool rtl_agc = false;
  bool bias_available = false;
  bool bias = false;
};
Gain gain();
void set_gain_auto();
void set_gain_manual(int tenth_db);
void set_rtl_agc(bool enable);
void set_bias_tee(bool enable);
waveshare4b::home_v3::Snapshot snapshot();
// Copies the latest 256-bin relative-intensity FFT; returns true if it is new since the last call.
bool take_bins(uint8_t out[256]);
}
