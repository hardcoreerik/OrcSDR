#pragma once
// ACARS receiver adapter: connects the RTL-SDR IQ stream to the native decoder (acars_core) and the dashboard model.
//
//   DSP task --offer_iq()--> Frontend240k (mix, FIR, /10) --> Decoder --> Model::post_message / post_stats  (lock-free)
//   UI task  --service()--> receiver state, tune report, follows the dashboard's requested channel
//
// Nothing here touches the radio driver or the display. main.cpp supplies what only it can reach through Hooks and forwards
// one call from its DSP loop (offer_iq). The module asks for a 240 kS/s stream (the rate the FT8 and SSTV receivers also use) and
// decodes one channel, placed 60 kHz (a quarter of the sample rate) above the tuned centre so the tuner's DC spike stays out of the channel.
// Receive only: no transmit path exists here.
#include <cstddef>
#include <cstdint>

#include "acars_dashboard_model.hpp"

namespace orcsdr::acars_receiver {

constexpr uint32_t kStreamRate = 240000;      // the only input rate the front end accepts
constexpr int32_t kCarrierOffsetHz = 60000;   // channel = tuned centre + this (fixed by the front end)

// VDL Mode 2 (D8PSK, 25 kHz channels from 136.725 to 136.975 MHz) is a separate receive path chosen by the requested channel.
// It needs a 250 kS/s stream (an exact 5:1 step to the demodulator's 50 kS/s); the channel sits a quarter of that above the centre.
constexpr uint32_t kVdl2StreamRate = 250000;
constexpr int32_t kVdl2CarrierOffsetHz = 62500;
constexpr uint32_t kVdl2MinHz = 136725000u;
constexpr uint32_t kVdl2MaxHz = 136975000u;
constexpr bool is_vdl2_channel(uint32_t hz) { return hz >= kVdl2MinHz && hz <= kVdl2MaxHz; }

struct Hooks {
  bool (*device_ready)() = nullptr;           // an RTL-SDR is enumerated and usable
  // Ask for a stream of sample_rate (kStreamRate, or kVdl2StreamRate on a VDL2 channel) centred here; true = accepted.
  bool (*tune)(uint32_t center_hz, uint32_t sample_rate) = nullptr;
  void (*release)() = nullptr;                // give the radio's sample rate back (only called if tune() was accepted)
  uint32_t (*sample_rate)() = nullptr;        // the stream rate right now
  uint32_t (*tuned_center)() = nullptr;       // the centre the radio was last told to use
  uint32_t (*now_ms)() = nullptr;             // monotonic device clock
  uint32_t (*utc_epoch)() = nullptr;          // wall-clock seconds, 0 when the clock is not valid
};

struct Status {
  bool running = false;
  uint32_t channel_hz = 0;
  uint32_t center_hz = 0;
  uint32_t blocks = 0;               // IQ blocks decoded since start
  uint32_t wrong_rate_blocks = 0;    // blocks offered at another sample rate (not decoded)
  uint32_t settle_skipped = 0;       // blocks dropped right after a retune
  uint32_t discontinuities = 0;      // gaps that reset the decoder
  uint32_t tune_calls = 0;
  uint32_t tune_refused = 0;
  uint32_t msgs_posted = 0;
  uint32_t msgs_dropped = 0;         // model queue full
  uint32_t max_block_us = 0;
  uint32_t avg_block_us = 0;         // mean time in offer_iq per decoded block
  acars::Stats decoder{};
  float level = 0.f;                 // mean envelope of the channel at 24 kS/s (0..1.4); a carrier or noise floor reads here
  // VDL2 path (all zero while an analog channel is tuned). decoder.* above carries: sync = unique-word candidates,
  // crc_ok / crc_rejected = AVLC frames by FCS, framing_rejected = header + FEC failures.
  bool vdl2 = false;
  uint32_t vdl2_header_ok = 0;
  uint32_t vdl2_fec_ok = 0;
  uint32_t vdl2_acars = 0;           // ACARS messages extracted from AVLC frames
  uint32_t vdl2_raw = 0;             // verified data frames kept as raw hex (ATN / CPDLC / ADS-C and anything not ACARS)
  uint32_t vdl2_other = 0;           // verified frames with no data (acknowledgements, XID, ...), counted only
};

void install(const Hooks& hooks);              // before start(); safe to repeat
// Attaches to the dashboard's model, allocates the receiver state (PSRAM) and advertises live reception. False without
// hooks or memory; the dashboard then keeps saying "RECEIVER FEED NOT CONNECTED".
bool start(acars_ui::Model* model);
void stop();                                   // gives the radio back; the allocation is kept
bool running();
// UI task, every tick. Follows the model's requested channel, reports the receiver and tune state.
void service();
// Retune request (UI task). Returns false when the receiver is not running or the radio refused.
bool request_channel(uint32_t frequency_hz);
// DSP task: one CU8 block. Ignored (and counted) at any rate but kStreamRate. Never blocks, never allocates.
void offer_iq(const uint8_t* cu8, size_t bytes, uint32_t sample_rate);
Status status();
void format_status(char* out, size_t size);    // one RTL_ACARS_STATUS line

}  // namespace orcsdr::acars_receiver
