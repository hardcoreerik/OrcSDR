#pragma once
// Digital Voice receive attachment. A READ-ONLY client of the existing receiver owner: it never opens the radio driver or
// USB. It asks for one 240 kS/s stream through Hooks (the same tune/release rules ACARS and AIS use), receives CU8 blocks from
// the DSP task, channelizes to 48 kS/s and runs the native DMR and NXDN48/96 decoders. Validated events cross to the UI
// task through a bounded single-producer ring; the UI never sees decoder internals.
//
//   DSP task --offer_iq()--> Channelizer 240k->48k --> bounded sample ring        (the DSP task never decodes)
//   worker   (own core)   --> DMR / NXDN48 / NXDN96 decoders --> event ring + atomics
//   UI task  --service() / pop_event() / status()
// Without a worker (host tests) the decoders run inline in offer_iq; the logic is identical.
//
// Continuity: a retune, a lost-sample counter movement or a >250 ms gap resets the channelizer and every decoder and bumps
// the epoch; no decoder ever bridges samples across it. Blocks at another rate are counted and refused, never resampled.
// Memory: all decoder state lives in PSRAM (the Tab5 internal boot pool is nearly full). Receive only.
#include <cstddef>
#include <cstdint>

#include "digital_voice_dashboard_model.hpp"
#include "dv_iq_rec.hpp"

namespace orcsdr::dv_radio {

constexpr uint32_t kStreamRate = 240000;
constexpr int32_t kChannelOffsetHz = 60000;
constexpr uint8_t kMaskDmr = 1, kMaskNxdn48 = 2, kMaskNxdn96 = 4, kMaskAuto = 7;

struct Hooks {
  bool (*device_ready)() = nullptr;
  bool (*tune)(uint32_t center_hz, uint32_t sample_rate) = nullptr;   // true = accepted
  void (*release)() = nullptr;
  uint32_t (*now_ms)() = nullptr;
  uint32_t (*micros)() = nullptr;
  uint64_t (*lost_samples)() = nullptr;                                // monotonic count of IQ samples lost upstream
  uint32_t (*utc_epoch)() = nullptr;                                   // wall-clock seconds for IQ file names, 0 = unknown
  // Optional decode worker on another core/thread. spawn runs fn(arg) until it returns; join waits for that return.
  bool (*spawn_worker)(void (*fn)(void*), void* arg) = nullptr;
  void (*join_worker)() = nullptr;
  void (*sleep_ms)(uint32_t ms) = nullptr;
};
// ESP-IDF only (dv_radio_task.cpp): fills the three worker hooks with a PSRAM-stack FreeRTOS task pinned to core 0.
void fill_platform_hooks(Hooks& hooks);

enum class Link : uint8_t { stopped, no_memory, no_receiver, tuning, receiving, wrong_stream, stalled };
const char* link_name(Link link);

// One decoder event that met its protocol's own integrity gates. Field validity flags are explicit: 0 is a valid value.
struct Event {
  uint32_t seq = 0, t_ms = 0, frequency_hz = 0, epoch = 0;
  digital_voice_ui::Protocol protocol = digital_voice_ui::Protocol::unknown;
  digital_voice_ui::Evidence evidence = digital_voice_ui::Evidence::none;
  uint32_t source_id = 0, destination_id = 0;
  bool has_source = false, has_destination = false, group_call = false;
  uint8_t colour_code = 0, slot = 0, ran = 0, nxdn_rf_channel = 0;
  bool colour_code_valid = false, slot_valid = false, ran_valid = false, nxdn_rf_valid = false;
  bool replay = false;               // from a recorded file fed through begin_replay(), NOT from the radio
  // DMR burst detail for the data log (valid only for protocol == dmr; every field is what the decoder reported).
  bool has_data_type = false, late_entry = false, info_valid = false;
  uint8_t data_type = 0, flco = 0, feature_id = 0, service_options = 0;
  uint8_t info[12] = {};             // the 96 BPTC-decoded information bits of a data burst, MSB first
};

struct Status {
  Link link = Link::stopped;
  bool running = false;
  uint32_t channel_hz = 0, epoch = 0;
  uint32_t blocks = 0, wrong_rate_blocks = 0, settle_skipped = 0, discontinuities = 0, loss_events = 0;
  uint32_t tune_calls = 0, tune_refused = 0;
  uint32_t events = 0, events_dropped = 0;
  uint32_t dmr_acquisitions = 0, dmr_identities = 0, dmr_events = 0;
  uint32_t nxdn48_frames = 0, nxdn48_sacch_valid = 0, nxdn96_frames = 0, nxdn96_sacch_valid = 0;
  uint32_t max_block_us = 0, avg_block_us = 0;
  uint32_t level_milli = 0;          // relative channel level x1000; NOT calibrated
  uint64_t samples_in = 0;
  uint32_t state_bytes = 0, alloc_failed_bytes = 0;
  uint32_t ring_pending = 0;         // 48 kS/s samples waiting for the worker
  uint32_t ring_fill_max = 0;        // high-water mark of ring_pending since start
  uint32_t ring_drops = 0;           // slices dropped because the worker fell behind (each forces a decoder reset)
  uint32_t worker_busy_permille = 0; // share of wall time the worker spent decoding since start (1000 = one full core)
  bool worker = false;
  bool replay = false;
  uint32_t verified_ms = 0;          // monotonic time of the newest verified event since the last tune, 0 = none
  uint32_t identity_ms = 0;          // newest identity-validated event or qualified voice group since the last tune, 0 = none
  bool candidate = false;            // sync-level activity seen since the last tune
  uint8_t mask = 0;
};

// ---- Native voice path (receive only). The worker qualifies BS voice superframes (privacy/identity/continuity gates in
// dmr::d1::CallManager) and queues them here for the voice audio task. Only CLEAR, positively identified groups arrive as
// kind==group; everything else is a notice. Single consumer.
enum class VoiceKind : uint8_t { group, flush, call_ended, protected_voice, unqualified };
constexpr size_t kVoicePayloadBytes = 162;      // 6 bursts x 27 bytes of over-the-air voice bits
struct VoiceMsg {
  VoiceKind kind = VoiceKind::flush;
  uint8_t slot = 0, colour_code = 0;
  bool group_call = false, new_call = false;
  uint16_t lost_superframes = 0;
  uint32_t source_id = 0, destination_id = 0;
  uint32_t call_serial = 0;                      // increments per call; stable within one call
  uint32_t t_ms = 0, frequency_hz = 0;
  uint8_t payload[kVoicePayloadBytes] = {};
};
struct VoiceStatus {
  uint32_t anchors = 0, groups_seen = 0, qualified = 0, protected_groups = 0, unqualified = 0;
  uint32_t calls_started = 0, calls_ended = 0, lost_superframes = 0;
  uint32_t emb_abandoned = 0, sync_abandoned = 0, embedded_lc_ok = 0, embedded_lc_bad = 0;
  uint32_t queue_drops = 0, queue_pending = 0, flushes = 0;
  uint8_t privacy[3] = {0, 0, 0};                // index = slot; 0 unknown, 1 clear, 2 protected
  bool active[3] = {false, false, false};
};
bool pop_voice(VoiceMsg* out);                   // voice task: oldest queued item
void set_voice_queue_enabled(bool enabled);      // false = qualify and count but do not queue (no audio consumer running)
VoiceStatus voice_status();

// DEVELOPMENT: live IQ capture around call-like events (SETUP toggle "SAVE DMR IQ (DEV)"). See dv_iq_rec.hpp. The UI task writes the file.
void iq_set_enabled(bool on);
bool iq_enabled();
bool iq_ready(const uint8_t** data, size_t* bytes, char* name, size_t name_size);
void iq_saving();
void iq_snap();                  // development: capture around now on the live channel (RTL_DV IQ SNAP)
void iq_release(bool saved);
dv_iq_rec::Stats iq_stats();

void install(const Hooks& hooks);
bool start();                          // UI task: allocate PSRAM state. False without hooks/memory (link() says which).
void stop();                           // UI task: give the sample rate back, free state
bool running();
void set_protocol_mask(uint8_t mask);  // which decoders run (CPU budget); measured on the Tab5: DMR 0.26, NXDN96 0.85, NXDN48 1.29 of one core
void request_decoder_reset();          // UI task: drop queued samples and restart every decoder (used when the mask changes)
// UI task: tune the receiver so `channel_hz` lands 60 kHz above the stream centre. True when the owner accepted.
bool tune_channel(uint32_t channel_hz);
// Replay: feed a recorded 240 kS/s CU8 file (channel 60 kHz above the capture centre) through the same chain WITHOUT touching
// the receiver. Events are marked replay. Live blocks from the DSP task are ignored while a replay runs.
bool begin_replay(uint32_t channel_hz);
void feed_replay(const uint8_t* cu8, size_t bytes);
void service();                        // UI task: link state, stall detection
bool pop_event(Event* out);            // UI task: oldest unread event
Status status();
// DSP task: one CU8 block. Never blocks, never allocates, never calls the UI.
void offer_iq(const uint8_t* cu8, size_t bytes, uint32_t sample_rate);
void format_status(char* out, size_t size);   // one "RTL_DV_STATUS ..." line

}  // namespace orcsdr::dv_radio
