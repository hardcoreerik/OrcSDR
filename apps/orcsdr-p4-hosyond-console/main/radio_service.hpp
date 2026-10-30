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
// Driver facts for the diagnostic screens (refreshed with the gain cache).
struct Info {
  uint32_t caps = 0;
  int ppm = 0;
  uint32_t bw_requested_hz = 0, bw_applied_hz = 0;  // tuner filter bandwidth (2.4 MS/s only)
  uint32_t bw_list[16] = {};
  uint8_t bw_count = 0;
  uint32_t overruns = 0, consumer_drops = 0;
  char device[48] = {};
  char health[28] = {};
  char version[20] = {};
  char usb_path[24] = {};  // physical hub port ("P<parent>.p<port>"): the only identity that survives replug
};
Info info();
void set_ppm(int ppm);
void set_tuner_bandwidth(uint32_t hz);
// A screen that needs a specific supported rate (RF Lab) holds it here; 0 returns to the automatic policy.
void set_sample_rate_override(uint32_t rate);
// A second consumer of the IQ blocks (RF Lab's analysis). Called from the receiver task; must not block.
void set_aux_iq_sink(IqSink sink);
Gain gain();
void set_gain_auto();
void set_gain_manual(int tenth_db);
void set_rtl_agc(bool enable);
void set_bias_tee(bool enable);
waveshare4b::home_v3::Snapshot snapshot();
// Copies the latest 256-bin relative-intensity FFT; returns true if it is new since the last call.
bool take_bins(uint8_t out[256]);

// ---- Several dongles, and any number of IQ consumers (docs/hardware/IQ_SINKS_DESIGN.md) ----
// Every call above acts on receiver 0. The overloads below take a receiver index. How many receivers this build
// runs is CONFIG_ORCSDR_MAX_RECEIVERS (default 1, at most 4); each extra receiver costs a task and USB DMA buffers.
constexpr int kMaxReceivers = 4;
constexpr int kMaxSinks = 8;  // consumers per receiver
int max_receivers();          // receivers this build runs (1..4)
int receiver_count();         // receivers that currently have a dongle streaming

struct ReceiverInfo {
  int index = 0;
  int state = 0;  // 0 no dongle, 1 scanning, 2 streaming, 3 error
  uint32_t frequency_hz = 0, sample_rate = 0, effective_sps = 0, drops = 0;
  char device[48] = {};
  char usb_path[24] = {};
};
bool receiver_info(int rx, ReceiverInfo* out);

void tune_to(int rx, uint32_t frequency_hz);
void set_sample_rate(int rx, uint32_t rate);
uint32_t sample_rate(int rx);
float level_dbfs(int rx);
Gain gain(int rx);
Info info(int rx);
void set_gain_auto(int rx);
void set_gain_manual(int rx, int tenth_db);
void set_rtl_agc(int rx, bool enable);
void set_bias_tee(int rx, bool enable);
bool take_bins(int rx, uint8_t out[256]);

// What every IQ consumer receives. `data` (CU8) is valid only during the call.
struct IqBlock {
  const uint8_t* data;
  size_t bytes;
  uint32_t sample_rate;  // S/s of this block
  uint32_t center_hz;
  uint32_t sequence;     // per receiver, +1 per block: a gap means blocks were lost before this consumer
  int64_t time_us;       // esp_timer_get_time() when the block was read
  uint8_t receiver;
  uint32_t generation;   // +1 on every stream start, stop, rate change or recovery: a change means "not the same stream as before"
};
using IqConsumer = void (*)(const IqBlock& block, void* context);

// inline_fast: called inside the receiver task, so it must take a few microseconds and never block (audio demodulation
// is the one proven case). queued: the block is copied into the consumer's own buffer and `fn` runs in its own task; a
// consumer that falls behind loses only its own blocks.
enum class SinkKind : uint8_t { inline_fast, queued };
struct SinkSpec {
  const char* name = "sink";    // up to 15 characters, for diagnostics
  IqConsumer fn = nullptr;
  void* context = nullptr;
  SinkKind kind = SinkKind::queued;
  uint8_t receiver = 0;
  uint32_t queue_bytes = 0;     // queued: buffer in PSRAM; 0 = four blocks
  uint8_t task_priority = 3;    // queued: priority of the consumer's task
  int8_t task_core = 1;         // queued: core of the consumer's task
  uint32_t task_stack = 4096;   // queued: stack bytes
};
using SinkHandle = int;  // -1 = invalid
SinkHandle add_sink(const SinkSpec& spec);
// Returns true once no call to this sink is running or can start, so the caller may free its context. The radio loop holds a per-sink
// lock while it calls the sink and removal must take the same lock, so the answer is exact, not a timeout guess. If a callback does not
// return within about 1.5 s (a queued consumer's task within 2 s) the sink is quarantined, its slot is never reused, and false is
// returned: the caller must then NOT free anything the sink may still touch.
bool remove_sink(SinkHandle handle);
struct SinkStats {
  char name[16] = {};
  bool queued = false;
  uint32_t blocks = 0, drops = 0, max_call_us = 0;
};
bool sink_stats(SinkHandle handle, SinkStats* out);
}
