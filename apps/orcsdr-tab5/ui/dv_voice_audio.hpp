#pragma once
// Digital Voice speech engine (receive only). Turns qualified DMR voice groups (dv_radio::VoiceMsg, privacy/identity
// already gated by dmr::d1::CallManager) into 48 kHz mono PCM through the OrcSDR AMBE+2 vocoder (components/orcsdr_vocoder).
// Host-portable: no ESP-IDF, no UI, no audio hardware. The firmware voice task owns one Engine and pulls PCM from it into the
// existing speaker path; host tests drive the identical code.
//
//   VoiceMsg (360 ms, 18 frames) -> per-slot codec context -> 160 samples @8 kHz -> 6x polyphase resampler -> playout ring
//
// Rules this module enforces: only kind==group messages produce speech; UNKNOWN/PROTECTED never reach the codec (the call
// manager withholds them, and the codec adapter refuses any privacy but CLEAR); a stream flush, a new call or a lost
// superframe resets the codec so no predictor state bridges a discontinuity; gaps are silence, never repeated speech;
// synthesis is serialized (the pinned mbelib uses rand()). One call at a time is voiced (AUTO sticks with the first active
// slot until it ends); the other slot is metadata only.
#include <cstddef>
#include <cstdint>

#include "dv_radio.hpp"

namespace orcsdr::dv_voice {

enum class Listen : uint8_t { automatic, ts1, ts2, mute };
const char* listen_name(Listen l);

// What the LIVE page shows. Derived, never stored by the UI.
enum class Phase : uint8_t {
  listening,        // enabled, nothing qualified
  acquiring,        // voice on the air seen but not yet qualified (identity / privacy undecided)
  buffering,        // qualified speech received, pre-roll filling
  decoding,         // speech playing
  silence_frame,    // codec silence frames (comfort gap inside a call)
  degraded,         // uncorrectable / concealed frames recently
  call_ended,       // call ended within the last few seconds
  protected_voice,  // PI / protect flag / privacy bit: audio withheld
  privacy_unknown,  // voice present but cannot be qualified: audio withheld
  muted,            // listening mode MUTE or user mute
  unavailable,      // engine not running or no memory
};
const char* phase_name(Phase p);

struct Config {
  uint32_t target_buffer_ms = 250;      // playout starts this long after the first qualified group lands (measured on the Tab5: 100 ms underran on 18 of 20 groups of an SD replay whose group spacing varied 362-443 ms; 250 ms underran on 2 of 26)
  uint32_t ring_ms = 2000;              // playout ring capacity (PSRAM)
  uint32_t call_end_hold_ms = 3000;     // how long CALL ENDED stays visible
  uint32_t degraded_hold_ms = 2000;
};

struct Stats {
  uint64_t groups = 0, groups_dropped_not_selected = 0, frames = 0, frames_speech = 0, frames_silence = 0, frames_error = 0;
  uint64_t frames_concealed = 0;        // silent fill for lost superframes and uncorrectable frames
  uint64_t corrected_bits = 0, total_corrected_bits = 0;
  uint64_t flushes = 0, calls = 0, lost_superframes = 0;
  uint64_t underruns = 0, overruns = 0, samples_out = 0, samples_in = 0;
  uint32_t synth_us_last = 0, synth_us_max = 0, synth_us_avg = 0;   // per group (18 frames)
  uint32_t buffered_ms = 0, buffered_peak_ms = 0, target_buffer_ms = 0;
  uint32_t latency_ms_last = 0;         // group arrival -> first sample of that group leaves the ring
  int32_t peak_last = 0, rms_last = 0;  // of the last group, 8 kHz PCM
  uint64_t clipped_samples = 0;
  uint32_t gap_min_ms = 0, gap_max_ms = 0;   // spacing between consecutive groups of one call (ideal 360): the jitter the buffer must absorb
  uint64_t test_samples = 0;            // injected via inject_test_pcm, NOT decoded speech
};

struct Snapshot {
  Phase phase = Phase::unavailable;
  Listen listen = Listen::automatic;
  bool enabled = false, muted = false, playing = false;
  uint8_t active_slot = 0;              // voiced slot, 0 = none
  uint8_t privacy[3] = {0, 0, 0};
  uint32_t source_id = 0, destination_id = 0;
  bool group_call = false;
  uint8_t colour_code = 0;
  Stats stats;
};

// Optional observer of every frame the engine VOICES for a real call (slot 1 or 2): 8 kHz PCM exactly as it enters the resampler,
// silent = muted silence frame, concealed frame or lost-superframe gap. Never called for the speaker-path test tone.
using SpeechSink = void (*)(uint8_t slot, uint32_t call_serial, uint32_t src, uint32_t dst, const int16_t pcm[160], bool silent,
                            uint32_t now_ms, void* user);

// Optional per-frame observer (tests): 9 wire bytes, 160 PCM samples at 8 kHz, codec flags ("E" error, "S" silence).
using FrameTap = void (*)(uint8_t slot, uint32_t call_serial, const uint8_t wire[9], const int16_t pcm[160], int corrected,
                          int total, const char* flags, void* user);

class Engine {
public:
  Engine() = default;
  ~Engine() { end(); }
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  bool begin(const Config& cfg = Config());   // allocates codec contexts + playout ring (PSRAM on the Tab5)
  void end();
  bool running() const { return ring_ != nullptr; }

  void set_enabled(bool on);
  void set_listen(Listen l);
  void set_muted(bool m);
  void set_frame_tap(FrameTap tap, void* user) { tap_ = tap; tap_user_ = user; }
  void set_speech_sink(SpeechSink sink, void* user) { speech_sink_ = sink; speech_user_ = user; }
  Listen listen() const { return listen_; }
  void set_target_buffer_ms(uint32_t ms) { cfg_.target_buffer_ms = ms > 1000 ? 1000 : ms; }
  uint32_t target_buffer_ms() const { return cfg_.target_buffer_ms; }
  // Playback gain applied to decoded speech on its way to the speaker (the speech sink / recordings stay raw). The vocoder's output is quiet
  // (live DMR call 2026-11-26: peak about 3000 of 32767), so the service raises it; the engine itself defaults to unity. Clipping is counted.
  void set_gain(float g) { gain_ = g < 0.25f ? 0.25f : g > 16.0f ? 16.0f : g; }
  float gain() const { return gain_; }
  float gain_ = 1.0f;

  // Voice task: apply one queued item (any kind). now_ms is a monotonic millisecond clock.
  // `defer` = true only queues a qualified group; service() then synthesizes it a few frames at a time so a pump thread keeps
  // draining the speaker between slices (a whole group is 18 codec frames, about 120 ms of CPU on the Tab5). defer = false
  // (tests, tools) synthesizes it completely before returning.
  void handle(const dv_radio::VoiceMsg& m, uint32_t now_ms, bool defer = false);
  // Synthesize up to max_frames of the pending group. Returns the frames done. busy() is true while a group is pending.
  size_t service(uint32_t now_ms, size_t max_frames);
  bool busy() const { return pending_active_; }
  // Playback: copy up to n 48 kHz mono samples. Returns the real samples produced (0 while buffering / idle). Never pads.
  size_t pull(int16_t* out, size_t n, uint32_t now_ms);
  size_t buffered_samples() const;
  // Speaker-path isolation only (SELFTEST / Stage C): play known-good 8 kHz PCM through the SAME resampler and ring.
  // Counted as test samples, never as decoded speech. n must be a multiple of 160.
  bool inject_test_pcm(const int16_t* pcm8k, size_t n, uint32_t now_ms);
  void reset_stream();                        // drop audio and codec state (retune, DV closed, SDR removed)
  Snapshot snapshot(uint32_t now_ms) const;

  static constexpr uint32_t kOutRate = 48000;

private:
  struct Slot;
  void begin_group(const dv_radio::VoiceMsg& m, uint32_t now_ms);
  void finish_group();
  void emit_silence(uint8_t slot, size_t frames8k);
  void push_frame(uint8_t slot, const int16_t pcm[160], bool silent);
  void drop_playout();
  bool selected(uint8_t slot) const;

  Config cfg_{};
  Listen listen_ = Listen::automatic;
  bool enabled_ = false, muted_ = false;
  Slot* slots_ = nullptr;           // [3] codec contexts + resampler histories
  int16_t* ring_ = nullptr;
  size_t ring_cap_ = 0;
  uint64_t rd_ = 0, wr_ = 0;        // monotonic sample counters
  bool playing_ = false;
  uint32_t first_group_ms_ = 0, start_play_ms_ = 0;
  uint32_t last_group_ms_ = 0, last_end_ms_ = 0, last_degraded_ms_ = 0, last_protected_ms_ = 0, last_unqualified_ms_ = 0;
  uint32_t last_silence_ms_ = 0;
  uint8_t voiced_slot_ = 0;
  uint32_t voiced_serial_ = 0;
  uint32_t src_ = 0, dst_ = 0;
  bool group_call_ = false;
  uint8_t cc_ = 0;
  uint64_t group_first_sample_ = 0;
  uint32_t group_arrival_ms_ = 0;
  bool latency_pending_ = false;
  Stats stats_{};
  uint64_t synth_total_us_ = 0;
  FrameTap tap_ = nullptr;
  void* tap_user_ = nullptr;
  SpeechSink speech_sink_ = nullptr;
  void* speech_user_ = nullptr;
  uint8_t privacy_[3] = {0, 0, 0};
  // Group being synthesized (see service()).
  bool pending_active_ = false;
  dv_radio::VoiceMsg pending_{};
  uint8_t pending_next_ = 0;
  int64_t pending_sq_ = 0;
  int32_t pending_peak_ = 0;
  size_t pending_samples_ = 0;
  bool pending_bad_ = false, pending_silent_only_ = true;
  uint32_t pending_us_ = 0, pending_now_ms_ = 0;
  uint64_t pending_first_sample_ = 0;
};

}  // namespace orcsdr::dv_voice
