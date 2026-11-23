#pragma once
#include <cstddef>
#include <cstdint>
#include "dead_air_runes.hpp"

namespace orcsdr::dead_air {

// This model contains observations, not paranormal classifications. No mock
// readings are ever used by the live dashboard.
enum class Page : uint8_t { overview, spectrum, audio, environment, events, review, count };
enum class Sensitivity : uint8_t { quiet, standard, sensitive };
enum class Source : uint8_t { rf, audio, environment, manual, sweep };  // sweep = generated tuning schedule, not a measurement
enum class Kind : uint8_t { rf_rise, audio_burst, temperature_change, humidity_change,
                            pressure_change, manual_marker, sweep_start, sweep_stop };
enum class Status : uint8_t { unavailable, warming_up, current, stale };
// Event-journal (SD) health as reported by the storage worker. "active" only while a file is open.
enum class RecorderState : uint8_t { unavailable, starting, active, failed, stopped };

struct Reading {
  float value = 0.0f;
  float baseline = 0.0f;
  uint32_t age_ms = 0;
  bool present = false;
  bool valid = false;
  bool baseline_valid = false;
};

struct Event {
  uint32_t sequence = 0;
  uint32_t monotonic_ms = 0;
  uint32_t unix_time = 0;  // 0 means wall clock not verified
  uint32_t frequency_hz = 0;
  Source source = Source::rf;
  Kind kind = Kind::rf_rise;
  float value = 0.0f;
  float reference = 0.0f;
  float threshold = 0.0f;
  Sensitivity sensitivity = Sensitivity::standard;
  // Sweep records only (Source::sweep); zero otherwise. frequency_hz is the sweep start (start) or the last hop (stop).
  uint32_t sweep_end_hz = 0, sweep_step_hz = 0, sweep_dwell_ms = 0, sweep_settle_ms = 0;
  uint32_t sweep_hops = 0, sweep_unobserved_ms = 0;
};

// One saved audio clip as shown on the Review tab (newest first).
struct ClipInfo {
  char name[32] = "";
  char sha16[17] = "";          // first 16 hex digits of the file's SHA-256
  uint16_t duration_ds = 0;     // tenths of a second
  bool truncated = false;       // cut short at an interruption in the audio
  char demod[8] = "";           // what the receiver was demodulating
  uint32_t frequency_hz = 0;
  bool speaker_on = false;      // the Tab5 speaker state when the clip was taken
  bool mic = false;             // room audio from the Tab5 microphones, not the receiver
  uint32_t event_seqs[8]{};     // events this clip covers (the first is the one in its file name)
  uint8_t seq_count = 0;
  uint16_t pre_ds = 0;          // tenths of a second from the clip's start to its first trigger (the first event)
};
constexpr size_t kClipInfoCount = 4;
constexpr size_t kClipInfoSeqs = 8;  // capacity of ClipInfo::event_seqs; the clip planner holds the same number

// The last minute of the RF level for the Overview strip: one sample per 500 ms, oldest first.
constexpr size_t kRfHistory = 120;
constexpr int16_t kHistGap = INT16_MIN;  // no valid measurement at that moment (retune, sweep, receiver lost)
enum class HistMark : uint8_t { none, rf_rise, audio_burst, manual };

// Context kept around every logged event for the scrub view: a level every 250 ms, 10 s before the event to 10 s after.
// Index kTracePre - 1 is the sample taken at the event (within one step); kHistGap marks a moment with no measurement.
constexpr size_t kTraceSamples = 80;
constexpr size_t kTracePre = 40;
constexpr uint32_t kTraceStepMs = 250;
struct ScrubView {
  uint32_t seq = 0;            // the event being scrubbed (0 = none)
  bool found = false;          // a trace was kept for it
  uint8_t post_filled = 0;     // samples after the event collected so far (kTraceSamples - kTracePre when complete)
  int16_t rf[kTraceSamples]{};     // tenths of a dBFS
  int16_t audio[kTraceSamples]{};  // tenths of a dBFS
  bool cursor_valid = false;   // a clip of this event is playing and is anchored to it
  int32_t cursor_ms = 0;       // playback position relative to the event (negative = before)
};

// One sweep's measured level per channel (peak while the receiver dwelt there), for the band map.
constexpr size_t kBandMapChannels = 256;
struct BandMapView {
  uint32_t start_hz = 0, step_hz = 0;
  uint16_t count = 0;          // channels in the sweep (0 = no sweep has run)
  uint16_t visited = 0;        // channels measured so far
  int16_t level[kBandMapChannels]{};  // tenths of a dBFS; kHistGap = not measured
};

// Control run state for the control overlay.
struct ControlView {
  uint8_t state = 0;           // 0 never run, 1 running, 2 complete, 3 stopped early
  uint32_t duration_s = 0, elapsed_s = 0, observed_s = 0;
  uint32_t count[3]{};         // false alarms per sensitivity: quiet, standard, sensitive
  bool rate_ok = false;        // enough measured time for a per-hour rate
  float per_hour[3]{};
};

// Session facts for the session card.
struct SessionSummary {
  uint32_t elapsed_s = 0;
  uint32_t events_rf = 0, events_audio = 0, events_marks = 0;
  uint32_t measured_s = 0;     // time with a settled, valid level
  bool have_level = false;
  float level_max_dbfs = 0.0f, level_min_dbfs = 0.0f;
  uint32_t longest_quiet_s = 0;  // longest stretch with no logged event
  uint16_t channels = 0;       // distinct frequencies tuned
  uint32_t sweeps = 0;
  uint32_t clips = 0;
};

class EnvHistory;  // dead_air_envhistory.hpp: minute averages of temperature, humidity and pressure
// VEIL-26 (see dead_air_veil26.hpp): symbolic output of a fixed recipe applied to measured numbers. Everything in a
// VeilFrameView other than frequency_hz, level_dbfs and the timestamps is INTERPRETED, never measured.
struct VeilFrameView {
  bool valid = false;
  uint32_t frame = 0, monotonic_ms = 0, unix_time = 0, frequency_hz = 0;
  uint32_t event_sequence = 0;   // Dead Air event this frame interprets, 0 for a periodic frame
  float level_dbfs = 0.0f;       // measured, relative dBFS
  int32_t freq_code = 0, level_code = 0;
  uint32_t state_in = 0, state = 0;
  uint8_t letter_number = 0;
  char letter = 0;
  uint8_t word_index = 0;
  char word[16] = "";
  uint16_t a1z26 = 0, pythagorean = 0;  // gematria of the word
  char source[6] = "rf";                // where the level came from: rf (receiver) or audio
};
struct VeilView {
  runes::Alphabet alphabet = runes::Alphabet::latin;
  bool enabled = false;
  bool logging = false;          // the interpretation file is open on the SD card
  char intention[25] = "";
  uint32_t seed = 0;
  uint32_t frames = 0, gaps = 0, skipped = 0;          // analysed frames, breaks, observations that could not be used
  uint32_t posted = 0, dropped = 0, written = 0, write_failures = 0;  // interpretation records
  VeilFrameView live;            // newest frame of the stream
  char letters[33] = "";         // newest letters, '|' where a break fell
  uint16_t seq_a1z26 = 0, seq_pythagorean = 0;
  uint32_t inspect_seq = 0;      // event the overlay is inspecting (0 = none)
  VeilFrameView event;           // that event's interpretation (valid = false when it was not analysed)
  char dictionary[16] = "";      // dictionary id: "placeholder-0" until the canonical table is supplied
};

constexpr size_t kBins = 96;
constexpr size_t kEventCapacity = 48;
struct Snapshot {
  bool receiver_ready = false;
  bool recording = false;
  bool scanning = false;
  bool audio_available = false;
  bool sound_enabled = true;
  uint32_t frequency_hz = 0;
  uint32_t sample_rate_sps = 0;
  uint32_t session_elapsed_s = 0;
  uint32_t unix_time = 0;
  int battery_percent = -1;
  char demod[8] = "--";
  Reading rf_level_dbfs{};    // relative dBFS, NEVER dBm
  Reading audio_level_dbfs{}; // relative dBFS
  Reading temperature_c{};
  Reading humidity_rh{};
  Reading pressure_hpa{};
  Reading motion_g{};        // device movement, not room movement
  const EnvHistory* env_history = nullptr;  // owned by the app, read only on the UI task; null before the first session
  uint32_t env_history_seq = 0;             // changes when a charted value does
  // Spectrum and waveform bins are accepted only when a real source supplies
  // them. No fabricated waterfall, FFT, or voice scores.
  float spectrum_dbfs[kBins]{};
  float audio_envelope[kBins]{};
  bool spectrum_valid = false;
  bool audio_envelope_valid = false;
  bool environment_sensor_attached = false;
  uint32_t revision = 0;
  // Event journal health (not audio/IQ recording; those are not implemented).
  RecorderState recorder_state = RecorderState::unavailable;
  uint32_t recorder_session = 0;
  uint32_t recorder_saved = 0;
  uint32_t recorder_missing = 0;  // events lost before reaching the file
  uint32_t recorder_dropped = 0;  // queue-full drops counted at the producer
  bool receiver_settled = false;  // false while a retune or gain change settles
  // Generated sweep profile (a tuning schedule on one tuner; never simultaneous coverage).
  uint32_t spectrum_seq = 0;  // increments once per measured spectrum frame (the waterfall adds one row per frame)
  float audio_peak_dbfs = -100.0f;
  float audio_baseline_dbfs = 0.0f;
  bool audio_baseline_ready = false;
  // RF level history for the Overview strip. Tenths of a dBFS; kHistGap marks a moment with no valid measurement, and
  // the strip never draws a line across one. rf_history_event holds a HistMark per sample.
  int16_t rf_history[kRfHistory]{};
  uint8_t rf_history_event[kRfHistory]{};
  uint8_t rf_history_count = 0;
  uint32_t rf_history_seq = 0;  // increments once per sample
  ScrubView scrub;
  BandMapView map;
  ControlView control;
  SessionSummary summary;
  VeilView veil;
  bool chime_on = false;        // a short tone is played when an event is logged
  // Event-linked clips of the receiver's demodulated audio (microphone clips are listed beside them, flagged `mic`).
  bool clips_enabled = true;
  uint8_t clip_state = 0;       // 0 idle, 1 capturing, 2 writing
  uint32_t clips_saved = 0, clips_failed = 0, clips_refused = 0;
  ClipInfo clips[kClipInfoCount];
  uint8_t clip_count = 0;
  // Optional microphone clips (opt-in, separate files, paused while the speaker is audible).
  uint8_t mic_state = 0;        // 0 off, 1 listening, 2 paused (speaker on), 3 failed
  uint8_t mic_clip_state = 0;   // 0 idle, 1 capturing, 2 writing
  uint32_t mic_clips_saved = 0, mic_clips_failed = 0, mic_clips_refused = 0;
  char mic_error[24] = "";
  uint8_t selected_clip = 0;    // index into clips[] chosen on the Review tab (0 = newest)
  uint8_t replay_state = 0;     // 0 idle, 1 loading, 2 playing, 3 not played
  char replay_note[28] = "";    // why a replay did not play
  uint8_t verify_state = 0;     // selected clip re-checked against the card: 0 not yet, 1 running, 2 matches, 3 mismatch, 4 file missing
  uint32_t clip_event_seqs[16]{};  // events that have a clip, newest first
  uint8_t clip_event_seq_count = 0;
  bool sweeping = false;
  uint8_t sweep_preset = 0;
  uint16_t sweep_dwell_ms = 0;
  uint32_t sweep_hops = 0;
  uint32_t sweep_unobserved_ms = 0;  // summed time between each retune command and the receiver being settled
};

struct RfObservation {
  uint32_t monotonic_ms = 0;
  uint32_t unix_time = 0;
  uint32_t frequency_hz = 0;
  uint32_t configuration_revision = 0;
  float level_dbfs = 0.0f;
  bool valid = false;
  bool settled = false;
};

class EventModel {
 public:
  using EventListener = void (*)(void* context, const Event& event);
  void set_listener(EventListener listener, void* context);
  void reset();
  void set_sensitivity(Sensitivity sensitivity);
  Sensitivity sensitivity() const { return sensitivity_; }
  void observe_rf(const RfObservation& sample);
  // Used by a UI-task callback after an independently verified detector fired.
  // Never use this for interpreted labels or unsupported environmental sensors.
  bool append_measured(const Event& event);
  void mark_manual(uint32_t monotonic_ms, uint32_t unix_time, uint32_t tuned_hz);
  // Records the start or end of a generated sweep. Rejects anything that is not a sweep_start/sweep_stop record.
  bool mark_sweep(const Event& event);
  size_t count() const { return count_; }
  uint32_t total() const { return total_; }
  const Event* newest(size_t index) const;
  bool baseline_ready() const { return baseline_count_ >= kWarmupCount; }
  float rf_baseline_dbfs() const { return baseline_; }
  bool self_check() const;
 private:
  void push(Event event);
  void reset_detector(uint32_t revision, uint32_t frequency);
  static constexpr uint8_t kWarmupCount = 12;
  Event events_[kEventCapacity]{};
  size_t count_ = 0, next_ = 0;
  uint32_t total_ = 0;
  uint32_t config_revision_ = 0, tuned_hz_ = 0;
  uint32_t candidate_since_ = 0, cooldown_until_ = 0;
  uint32_t last_valid_ms_ = 0;
  bool have_valid_sample_ = false;
  uint8_t baseline_count_ = 0;
  float baseline_ = 0.0f;
  bool have_tune_ = false, pending_ = false, latched_ = false;
  Sensitivity sensitivity_ = Sensitivity::standard;
  // Callback must only enqueue, never write to SD or touch the display.
  EventListener listener_ = nullptr;
  void* listener_context_ = nullptr;
};

// A source may be physically present but still warming up or stale. In every
// case except current, the dashboard shows "--" and no event can be generated.
Status reading_status(const Reading& reading, uint32_t stale_after_ms = 10000);
float rf_rise_threshold_db(Sensitivity sensitivity);
const char* source_name(Source source);
const char* kind_name(Kind kind);

}  // namespace orcsdr::dead_air
