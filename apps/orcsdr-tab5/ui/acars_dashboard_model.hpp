#pragma once
// ACARS dashboard data model: bounded, allocation-free after construction, no M5GFX / ESP-IDF
// dependency, host-testable. The dashboard never owns an SDR, a tuner session or decoder callbacks;
// it only reads Snapshots produced here.
//
//   decoder callback -> post_message() [bounded SPSC queue] -> service() [UI task] -> Snapshot -> draw
//
// Live and demo data live in two separate stores. Demo records are tagged SourceKind::demo, are
// built only by acars_demo_data and can never enter the live store, a live counter or any archive/feed.
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "acars_core.hpp"

namespace orcsdr::acars_ui {

// Display capacities (not decoder limits).
constexpr size_t kMessageRows = 12;
constexpr size_t kAircraftRows = 8;
constexpr size_t kChannelRows = 6;
// Internal bounds.
constexpr size_t kHistoryCapacity = 48;        // messages kept per store (ring)
constexpr size_t kAircraftTable = 16;          // aircraft tracked per store (LRU)
constexpr size_t kAircraftHistory = 6;         // message references kept per aircraft
constexpr size_t kQueueCapacity = 16;          // decoder -> UI events in flight
constexpr size_t kPreviewChars = 72;
constexpr size_t kInterpretationChars = 64;
constexpr size_t kEscapedMaxChars = 5 * acars::kMaxBlockBytes + 1;   // worst case "<TAB>" per byte (the longest escape)

enum class ReceiverState : uint8_t { disconnected, unavailable, paused, running, error };
enum class SourceKind : uint8_t { live, demo };
enum class Integrity : uint8_t { crc_verified, crc_corrected };
enum class TuneStatus : uint8_t { unavailable, pending, tuned, failed };
enum class Region : uint8_t { north_america, europe, asia_pacific, count };
enum class TextFormat : uint8_t { escaped, plain, count };       // how control bytes are shown
enum class MessageFilter : uint8_t { all, corrected_only, count };
enum class AircraftSort : uint8_t { recent, most_messages, count };
enum class DemoScenario : uint8_t { mixed, full, empty, disconnected, paused, no_time, count };

// Capabilities a runtime may advertise. Defaults describe today's implementation: none.
struct Capabilities {
  bool live_reception = false;
  bool multi_channel = false;
  bool archive = false;
  bool signal_measurements = false;
  bool application_decoding = false;
  bool message_reassembly = false;
  bool adsb_correlation = false;
};
uint32_t pack(const Capabilities& caps);
Capabilities unpack(uint32_t bits);

// One CRC-verified block as shown on screen.
struct MessageRecord {
  uint32_t seq = 0;                // monotonic per store, 0 = empty slot
  SourceKind source = SourceKind::live;
  Integrity integrity = Integrity::crc_verified;
  uint8_t corrected_bits = 0;
  int8_t channel_index = -1;       // index into the active region's presets, -1 = unknown
  bool time_valid = false;         // utc_epoch is a verified wall-clock time
  bool has_text = false;
  bool has_interpretation = false; // an application parser supplied interpretation[]
  uint8_t end_marker = 0;          // 0x03 ETX, 0x17 ETB, anything else is shown as unknown
  char mode = 0;
  char acknowledgement = 0;
  char block_id = 0;
  char label[3]{};
  char tail[8]{};                  // normalised: leading '.' / space padding removed
  char flight[7]{};
  uint32_t frequency_hz = 0;
  uint32_t uptime_ms = 0;          // receive time on the device clock (always known)
  uint32_t utc_epoch = 0;          // seconds, valid only when time_valid
  uint16_t raw_bytes = 0;
  uint16_t text_length = 0;        // authoritative; text[] is not necessarily NUL terminated
  char text[acars::kMaxBlockBytes]{};
  char preview[kPreviewChars]{};   // sanitised single line, built once at ingest
  char interpretation[kInterpretationChars]{};
};

struct AircraftRecord {
  char tail[8]{};
  char flight[7]{};                // latest flight id seen (empty if none)
  char last_label[3]{};
  SourceKind source = SourceKind::live;
  uint32_t message_count = 0;
  uint32_t corrected_count = 0;
  uint32_t last_seen_ms = 0;       // device clock
  uint32_t last_utc = 0;
  bool time_valid = false;
  uint8_t history_count = 0;       // valid entries in history_seq (newest first)
  uint32_t history_seq[kAircraftHistory]{};   // references into the message ring, may be evicted
};

struct ChannelRecord {
  uint32_t frequency_hz = 0;
  char label[24]{};
  bool selected = false;           // the requested channel
  bool receiving = false;          // always false until a runtime reports measured reception
  bool activity_measured = false;  // always false: no channel activity measurement exists yet
  uint32_t decoded_messages = 0;   // CRC-verified messages in the store carrying this frequency
};

struct Preferences {
  Region region = Region::north_america;
  uint8_t preset = 0;              // index into the region's presets
  TextFormat format = TextFormat::escaped;
  bool show_raw_text = true;
  bool highlight_corrected = true;
  MessageFilter filter = MessageFilter::all;
  AircraftSort sort = AircraftSort::recent;
  uint32_t custom_hz = 0;          // operator-entered request on the 25 kHz grid, 0 = use the preset
};
constexpr uint32_t kCustomMinHz = 118000000u;   // VHF airband
constexpr uint32_t kCustomMaxHz = 137000000u;
constexpr uint32_t kCustomStepHz = 25000u;

// Persistent storage is injected (the firmware passes its NVS preferences).
struct Store {
  bool (*get_bool)(const char* key, bool fallback) = nullptr;
  void (*put_bool)(const char* key, bool value) = nullptr;
  uint8_t (*get_u8)(const char* key, uint8_t fallback) = nullptr;
  void (*put_u8)(const char* key, uint8_t value) = nullptr;
  uint32_t (*get_u32)(const char* key, uint32_t fallback) = nullptr;
  void (*put_u32)(const char* key, uint32_t value) = nullptr;
};
Preferences load_preferences(const Store& store);   // invalid values fall back to defaults
void save_preferences(const Store& store, const Preferences& prefs);

struct Preset {
  uint32_t frequency_hz;
  const char* label;
  const char* source;              // where this assignment is documented / how well corroborated
};
const char* region_name(Region region);
size_t preset_count(Region region);
const Preset& preset(Region region, size_t index);

struct Snapshot {
  uint32_t revision = 0;           // changes only when anything shown changes
  uint32_t messages_revision = 0;
  uint32_t status_revision = 0;
  SourceKind source = SourceKind::live;
  DemoScenario scenario = DemoScenario::mixed;
  ReceiverState receiver = ReceiverState::disconnected;
  Capabilities caps{};
  Preferences prefs{};
  acars::Stats stats{};            // decoder counters of the active source (demo: fabricated)
  bool stats_available = false;    // false until a decoder has reported at least once
  uint32_t queue_drops = 0;        // decoder->UI events lost to a full queue (live only)
  uint32_t messages_held = 0;      // messages currently in the store ring
  uint32_t messages_total = 0;     // messages ever ingested by the store
  uint32_t corrected_total = 0;    // of messages_total, verified repairs
  uint32_t requested_hz = 0;       // what the operator asked for (UI request, not a tune)
  uint32_t tuned_hz = 0;           // 0 = no runtime reports a tuned frequency
  TuneStatus tune = TuneStatus::unavailable;
  int32_t battery_percent = -1;
  bool sound_enabled = true;
  size_t message_offset = 0;       // matching messages skipped before messages[0] (scroll position)
  size_t aircraft_offset = 0;
  size_t message_count = 0;        // rows valid in messages[] (newest first, filter applied)
  size_t matching_count = 0;       // all held messages that match the filter
  MessageRecord messages[kMessageRows]{};
  size_t aircraft_count = 0;
  size_t aircraft_total = 0;
  AircraftRecord aircraft[kAircraftRows]{};
  size_t channel_count = 0;
  ChannelRecord channels[kChannelRows]{};
  bool custom_active = false;      // requested_hz comes from prefs.custom_hz
  bool archive_available = false;
  size_t archive_count = 0;
};

// Archive browsing. The synthetic DEMO store answers in DEMO mode; otherwise an ArchiveSource (acars_archive: the saved log,
// newest blocks kept in memory) answers when one is attached.
struct ArchiveQuery {
  char tail[8]{};                  // empty = any
  char label[3]{};                 // empty = any
};

class ArchiveSource {
 public:
  virtual ~ArchiveSource() = default;
  virtual size_t count() const = 0;
  // Newest first, filter applied; `total` receives the number of matches. Returned pointers stay valid until the next add.
  virtual size_t query(const ArchiveQuery& query, size_t offset, const MessageRecord** out, size_t capacity,
                       size_t* total) const = 0;
  virtual bool cycle_tail(char* tail_inout) const = 0;     // any -> first -> ... -> last -> any
  virtual bool cycle_label(char* label_inout) const = 0;
  virtual const MessageRecord* find(uint32_t seq) const = 0;
  virtual uint32_t neighbor(uint32_t seq, bool newer) const = 0;   // 0 when there is none
  virtual const char* status() const = 0;                  // short text for the screen, e.g. "SAVING TO SD"
};

// Sanitising. All output is NUL-terminated and bounded by `capacity` (>= 1). Returns characters
// written (excluding NUL). `truncated` (optional) reports a cut.
// Preview: printable ASCII kept, CR/LF/TAB become a space, any other byte becomes '.'.
size_t sanitize_preview(char* out, size_t capacity, const char* text, size_t length, bool* truncated = nullptr);
// Escaped: printable ASCII kept, CR/LF/TAB shown as <CR>/<LF>/<TAB>, every other byte as <XX> hex.
size_t escape_text(char* out, size_t capacity, const char* text, size_t length, bool* truncated = nullptr);
// Strips leading '.', space and NUL padding that the ACARS address field carries.
void normalise_tail(char* out, size_t capacity, const char* tail7);
const char* end_marker_name(uint8_t marker);   // "ETX", "ETB" or "UNKNOWN"

class Model {
 public:
  Model();
  void reset();                                   // everything, including preferences' effect on state

  // --- Producer side: a decoder task or callback. Lock-free, never blocks, no allocation. ---
  bool post_message(const acars::Message& message, uint32_t uptime_ms, uint32_t utc_epoch);
  void post_stats(const acars::Stats& stats);
  void post_receiver_state(ReceiverState state);
  void post_capabilities(const Capabilities& caps);

  // --- UI task only. ---
  void service();                                  // drain the queue, refresh the live store
  void set_store(const Store& store);
  void load_preferences();
  void set_device_state(int32_t battery_percent, bool sound_enabled);
  bool set_demo(bool enabled, DemoScenario scenario = DemoScenario::mixed);
  bool demo() const { return demo_; }
  DemoScenario scenario() const { return scenario_; }

  // Preference changes: each saves and bumps the revision only when the value really changed.
  bool set_region(Region region);                  // resets the preset to the region's first
  bool set_preset(size_t index);                   // updates the REQUEST; never claims a tune
  bool set_text_format(TextFormat format);
  bool set_show_raw_text(bool show);
  bool set_highlight_corrected(bool on);
  bool set_filter(MessageFilter filter);
  bool set_sort(AircraftSort sort);
  bool set_custom_frequency(uint32_t hz);          // 0 clears; otherwise 118-137 MHz on the 25 kHz grid
  // Scrolling: positive = towards older. Clamped; returns true when the position changed.
  bool scroll_messages(int rows);
  bool scroll_aircraft(int rows);
  // Newer (true) / older (false) held message that matches the filter; 0 when there is none.
  uint32_t neighbor_seq(uint32_t seq, bool newer) const;
  // Synthetic-archive access (demo only; returns 0 / false when the live source is active).
  size_t archive_query(const ArchiveQuery& query, size_t offset, const MessageRecord** out, size_t capacity,
                       size_t* total) const;
  bool archive_cycle_tail(char* tail_inout) const;     // any -> first -> ... -> last -> any
  // Attach the saved-log archive (not owned). Live verified blocks are handed to the sink as they are stored.
  void set_archive(const ArchiveSource* archive);
  const ArchiveSource* archive() const { return archive_; }
  using LiveSink = void (*)(void* context, const MessageRecord& record);
  void set_live_sink(LiveSink sink, void* context);
  bool archive_browsable() const { return demo_ || archive_ != nullptr; }
  bool archive_cycle_label(char* label_inout) const;
  // A runtime reports what the radio actually did. Until then the request stays "unavailable".
  void report_tuned(uint32_t frequency_hz, bool ok);
  void clear_tune();                               // the runtime went away: back to "no runtime reports a tune"

  const Snapshot& snapshot();                      // rebuilt lazily when something changed
  TextFormat snapshot_prefs_format() const { return prefs_.format; }
  const MessageRecord* find_message(uint32_t seq) const;
  // Messages of one aircraft, newest first. Returns the number written (<= capacity).
  size_t aircraft_messages(const char* tail, const MessageRecord** out, size_t capacity) const;
  uint32_t live_ingested() const { return live_.total; }
  uint32_t demo_ingested() const { return demo_store_.total; }

  // Demo-data injection, used only by acars_demo_data.
  bool inject_demo(const MessageRecord& record);
  void inject_demo_stats(const acars::Stats& stats);
  void inject_demo_receiver(ReceiverState state);

 private:
  struct Event {
    enum class Kind : uint8_t { message } kind = Kind::message;
    acars::Message message{};
    uint32_t uptime_ms = 0;
    uint32_t utc_epoch = 0;
  };
  struct Ring {
    MessageRecord records[kHistoryCapacity]{};
    uint32_t next_seq = 1;
    uint32_t total = 0;
    uint32_t corrected = 0;
    size_t held = 0;
    size_t head = 0;                               // next slot to write
    AircraftRecord aircraft[kAircraftTable]{};
    size_t aircraft_used = 0;
    acars::Stats stats{};
    bool stats_available = false;
  };

  static MessageRecord make_record(const acars::Message& message, uint32_t uptime_ms, uint32_t utc_epoch);
  static void clear_ring(Ring& ring);
  void store_record(Ring& ring, const MessageRecord& record);
  void rebuild();
  void bump_status() { ++status_revision_; dirty_ = true; }
  void bump_messages() { ++messages_revision_; dirty_ = true; }
  bool persist_and_bump();
  const Ring& active() const { return demo_ ? demo_store_ : live_; }
  int channel_index_for(uint32_t hz) const;

  // SPSC queue: producer owns head_, consumer owns tail_.
  Event queue_[kQueueCapacity]{};
  std::atomic<uint32_t> queue_head_{0};
  std::atomic<uint32_t> queue_tail_{0};
  std::atomic<uint32_t> queue_drops_{0};
  // Latest-wins mailboxes (seqlock-style, relaxed atomics per field).
  std::atomic<uint32_t> stats_seq_{0};
  std::atomic<uint32_t> stats_words_[7]{};
  std::atomic<uint8_t> receiver_state_{static_cast<uint8_t>(ReceiverState::disconnected)};
  std::atomic<uint32_t> caps_bits_{0};

  const ArchiveSource* archive_ = nullptr;
  LiveSink live_sink_ = nullptr;
  void* live_sink_context_ = nullptr;
  Ring live_{};
  Ring demo_store_{};
  Preferences prefs_{};
  Store store_{};
  Snapshot snapshot_{};
  size_t message_offset_ = 0;
  size_t aircraft_offset_ = 0;
  bool demo_ = false;
  DemoScenario scenario_ = DemoScenario::mixed;
  ReceiverState demo_receiver_ = ReceiverState::running;
  uint32_t tuned_hz_ = 0;
  TuneStatus tune_ = TuneStatus::unavailable;
  int32_t battery_ = -1;
  bool sound_ = true;
  uint32_t status_revision_ = 1;
  uint32_t messages_revision_ = 1;
  uint32_t revision_ = 0;
  bool dirty_ = true;
  uint32_t seen_stats_seq_ = 0;
  ReceiverState seen_receiver_ = ReceiverState::disconnected;
  uint32_t seen_caps_ = 0;
  uint32_t seen_drops_ = 0;
};

bool model_self_check();

}  // namespace orcsdr::acars_ui
