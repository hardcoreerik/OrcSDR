#pragma once
// AIS radio adapter: connects the receiver's IQ stream to the portable AIS pipeline and publishes bounded copies for the UI.
//
//   DSP task --offer_iq()--> ais_rx::Pipeline (GMSK / HDLC / FCS / parser / station store)  --publish (every 500 ms)--> feed
//   UI task  --service()--> link state, re-tune when the stream was moved away   --copy_feed()--> ais_ui::Snapshot copy
//
// It never touches the radio driver, the network or the display. The application supplies what only it can reach through
// Hooks and forwards one call from its DSP loop (offer_iq). The module asks for a 240 kS/s stream centred on 162.000 MHz on
// whichever receiver source is selected (local USB or rtl_tcp) and decodes nothing else: blocks at another rate or centre are
// counted and refused, never resampled or guessed. Receive only.
//
// Sample continuity: the application's radio-session generation does not change on a reconnect, a lost buffer or a hot
// retune, so this module keeps its own epoch. It advances when a tune is accepted, when note_discontinuity() is called and
// when the application's lost-sample counter moves; the pipeline resets its DSP and framing state on every change.
#include <cstddef>
#include <cstdint>

#include "ais_dashboard.hpp"
#include "ais_receive_pipeline.hpp"

namespace orcsdr::ais_radio {

constexpr uint32_t kStreamRate = ais_rx::kInputRateHz;   // the only input rate the decoder accepts
constexpr uint32_t kCenterHz = ais_rx::kCenterHz;        // AIS 1 and AIS 2 sit 25 kHz either side
constexpr uint32_t kTypeSlots = 29;                      // message types 1..28 (index 0 = payload the parser refused)

struct Hooks {
  bool (*device_ready)() = nullptr;                       // a receiver source (USB or remote) is usable
  bool (*tune)(uint32_t center_hz, uint32_t sample_rate) = nullptr;   // ask for the stream; true = accepted
  void (*release)() = nullptr;                            // give the sample rate back (only after an accepted tune)
  uint32_t (*tuned_center)() = nullptr;                   // the centre the radio was last told to use
  uint64_t (*now_ms)() = nullptr;                         // monotonic device clock, never wall time
  uint32_t (*micros)() = nullptr;                         // optional: block timing
  uint64_t (*lost_samples)() = nullptr;                   // optional: monotonic count of IQ samples the pipeline lost
  bool (*remote_source)() = nullptr;                      // optional: the stream comes from rtl_tcp
};

enum class Link : uint8_t {
  stopped,        // not started
  no_memory,      // the decoder state could not be allocated
  no_receiver,    // no receiver source is usable (disconnected)
  tuning,         // tune requested; no usable block yet
  receiving,      // 162.000 MHz / 240 kS/s blocks are being decoded
  wrong_stream,   // blocks arrive at another rate or centre (refused); a re-tune is pending
  stalled,        // was receiving; no blocks now (source reconnecting or paused)
};
const char* link_name(Link link);

struct Status {
  Link link = Link::stopped;
  bool running = false, remote = false, test_replay = false;
  uint32_t epoch = 0;                // sample-continuity generation handed to the pipeline
  uint32_t blocks = 0;               // IQ blocks decoded
  uint32_t wrong_rate_blocks = 0;    // refused: another sample rate
  uint32_t wrong_center_blocks = 0;  // refused: radio centred elsewhere
  uint32_t settle_skipped = 0;       // dropped right after a tune
  uint32_t loss_events = 0;          // times the lost-sample counter moved (each resets the decoder)
  uint32_t discontinuities = 0;      // note_discontinuity() calls (source reconnect)
  uint32_t tune_calls = 0, tune_refused = 0;
  uint32_t publishes = 0, publish_skipped = 0;
  uint32_t max_block_us = 0, avg_block_us = 0;
  uint64_t samples = 0;              // IQ pairs decoded
  uint64_t lost_samples = 0;         // lost-sample counter movement since start
  uint64_t packet_digest = 0;        // order-independent sum of a 64-bit hash of every CRC-valid packet (channel + payload)
  uint32_t type_counts[kTypeSlots]{};
  uint32_t stations = 0;
  uint32_t state_bytes = 0;          // decoder + feed memory held (PSRAM on the device)
  uint32_t alloc_failed_bytes = 0;   // size of the last allocation that failed
  ais_rx::PipelineStats pipeline{};
};

void install(const Hooks& hooks);
// UI task. Allocates the decoder state (PSRAM on the device, never internal RAM) and asks for the stream.
// False when hooks are missing or memory is unavailable; link() then says which.
bool start();
// UI task. Gives the sample rate back and frees every allocation. Safe when not running.
void stop();
bool running();

// DSP task only. Never blocks, draws or allocates.
void offer_iq(const uint8_t* cu8, size_t bytes, uint32_t sample_rate);
// Any task: the sample sequence was broken upstream (for example an rtl_tcp reconnect). Only sets a flag.
void note_discontinuity();

// UI task: link state and self-healing re-tune.
void service();
// UI task. Copies the newest published targets, journal and counters into *out when something newer than *sequence exists.
// Only the decoder-owned fields are written (targets, messages, events, per-channel counters, iq_drops, sample_rate_sps,
// elapsed_s, test_replay); the caller owns the rest. Returns false when nothing new or the publisher holds the buffer.
bool copy_feed(ais_ui::Snapshot* out, uint32_t* sequence);

// Marks everything shown as replayed test data. Set only by the bench harness; cleared by stop().
void set_test_replay(bool enabled);

Status status();
void format_status(char* out, size_t size);   // one "RTL_AIS_STATUS ..." line

// Hash used for Status::packet_digest; a host decoder computes the same value from the same IQ file.
uint64_t packet_hash(const ais_rx::Packet& packet);

}  // namespace orcsdr::ais_radio
