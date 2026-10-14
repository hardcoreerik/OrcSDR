#pragma once
// Native, receive-only SSTV decoder for OrcSDR.  UI and DSP remain separate.
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace orcsdr::sstv {
// Stable mode ordinals; all listed modes can progressively receive luminance.
enum class Mode : uint8_t {
  auto_vis, robot36, robot72, martin_m1, martin_m2,
  scottie_s1, scottie_s2, scottie_dx,
  pd50, pd90, pd120, pd160, pd180, pd240, pd290
};
enum class Page : uint8_t { receive, gallery, analyzer, tune, setup, count };
// VIS is authoritative when valid; sync fallback is always labeled provisional.
enum class AutoStrategy : uint8_t { vis_only, vis_sync };
enum class Identification : uint8_t { none, vis, sync, manual };
enum class EndReason : uint8_t { none, complete, sync_lost, new_vis, retune, aborted, captured };
// The receiver's one lifecycle state, derived from the decoder's own variables so there is
// no second copy to drift. See session_state() for the exact rules.
enum class SessionState : uint8_t {
  searching,         // listening, nothing recognised yet
  vis_acquisition,   // a VIS leader/header is being validated
  sync_analysis,     // repeated 1200 Hz line pulses are being classified (no VIS)
  provisional_lock,  // mode chosen from sync alone; not yet confirmed by further lines
  receiving,         // VIS-identified, or a sync lock confirmed by further lines
  loss_recovery,     // receiving but the last line pulse(s) were missed
  finalizing,        // an image just ended and is being committed
  ready              // image committed; waiting for the next transmission
};
const char* state_name(SessionState state);
struct Profile { const char* name; uint32_t hz; bool usb; const char* description; };
constexpr Profile kProfiles[] = {
    {"HF SSTV", 14230000, true, "14.230 MHz USB - general amateur SSTV"},
    {"ISS SSTV", 145800000, false, "ISS event only - verify current ARISS plan"},
    {"OREGON NET", 145290000, false, "Salem-area SSTV repeater - confirm active net"},
    {"ISS UHF", 437550000, false, "ARISS UHF SSTV when scheduled - verify before reception"},
    {"LAB 145.500", 145500000, false, "Bench test: cabled Robot36 source at 145.500 MHz"}};
const char* mode_name(Mode mode);
struct Metrics {
  float tone_hz = 0;
  float audio_peak = 0;
  uint32_t frames_alive = 0;        // live + gallery + pinned image buffers currently allocated
  uint32_t frame_bytes = 0;         // PSRAM held by those buffers
  uint32_t audio_overruns = 0;      // decoder-rate samples dropped because the ring was full
  uint32_t audio_gaps = 0;          // distinct drop bursts the decoder skipped over in time
  uint32_t audio_gap_samples = 0;   // decoder-rate samples of timeline re-inserted for those gaps
  uint32_t bad_vis = 0;
  uint32_t good_vis = 0;
  uint32_t frames = 0;
  uint32_t partial_frames = 0;
  uint32_t auto_sync_locks = 0;
  uint32_t observed_sync_pulses = 0;
  uint32_t frame_alloc_failures = 0; // pictures that could not start because no image frame could be allocated, even after giving up gallery pictures
  float sync_pulse_ms = 0.0f;     // width of the most recent 1200 Hz pulse
  float sync_tone_hz = 0.0f;      // average frequency of the settled part of recent sync pulses (nominal 1200 Hz); 0 = none yet
  uint16_t sync_tone_pulses = 0;  // how many pulses that average rests on (saturates; reset with each new reception)
  float sync_interval_ms = 0.0f;  // spacing between the two most recent pulse starts
  Identification identification = Identification::none;
  uint8_t identification_score = 0; // heuristic; not a probability
  bool start_unknown = false;
  uint32_t sync_locked_lines = 0;
  uint32_t sync_missed_lines = 0;
  uint16_t session_sync_locked = 0;   // this reception only
  uint16_t session_sync_missed = 0;
  uint32_t session_id = 0;            // reception in progress (0 = none)
  float line_error_ms = 0.0f;
  uint32_t samples = 0;
  uint32_t audio_offered = 0;       // 48 kHz samples handed to the decoder by the radio path (0 = no audio arrives)
  uint16_t row = 0;
  uint16_t rows = 0;
  uint16_t width = 0;
  uint8_t vis = 0;
  uint8_t vis_phase = 0;   // 0=leader 1=break 2=second leader 3=start bit
  uint32_t vis_phase_samples = 0;
  Mode received_mode = Mode::auto_vis;
  bool receiving = false;
  bool active = false;
  bool signal = false;
  bool storage_ok = false;
  SessionState state = SessionState::searching;
  // Decoder cost, measured around service(): worst case matters for the 12 kHz real-time budget.
  uint32_t service_us_last = 0;
  uint32_t service_us_max = 0;
  uint32_t service_us_avg = 0;      // smoothed
  uint8_t service_cpu_pct = 0;      // smoothed share of wall time spent inside service()
  uint16_t ring_fill_max = 0;       // PCM ring high-water mark (capacity 4096)
  char status[56] = "WAITING FOR SSTV";
};
struct Image {
  const uint16_t* pixels = nullptr; // RGB565; valid while the frame is referenced (see pin_archived)
  const void* frame = nullptr;      // opaque reference-counted owner of pixels
  uint16_t width = 0, height = 0;
  uint32_t serial = 0;
  uint32_t frequency_hz = 0;
  Mode mode = Mode::auto_vis;
  uint16_t decoded_rows = 0;
  bool partial = false;
  bool start_unknown = false;
  Identification identification = Identification::none;
  uint8_t identification_score = 0;
  EndReason end_reason = EndReason::none;
  // One reception event. Counters cover this image only (not the receiver lifetime).
  uint32_t session_id = 0;
  uint32_t utc_epoch = 0;        // 0 = clock not verified when the reception ended
  uint8_t vis_code = 0;          // 0 when the mode was not identified from a VIS header
  uint16_t sync_locked = 0;      // lines whose sync pulse matched the predicted position
  uint16_t sync_missed = 0;      // lines committed from the predicted timeline without a sync
};
void start(uint32_t frequency_hz);
void stop();
bool active();
void set_mode(Mode mode);
Mode selected_mode();
void set_auto_strategy(AutoStrategy strategy);
AutoStrategy auto_strategy();
void set_frequency(uint32_t hz);
#ifdef ORCSDR_SSTV_TESTING
void testing_fail_next_frames(unsigned count);   // host tests only: the next `count` image-frame allocations fail
#endif
// Ends the image in progress (kept as a partial gallery entry, EndReason::aborted) and returns to
// searching. Unlike stop()+start() it keeps the audio stream, counters and settings untouched.
void abort_image();
// Offline replay only: no further audio will arrive. Drains the ring, closes a
// line whose luminance scan has already fully elapsed (a recording that stops a
// few milliseconds early still yields its last row; nothing is padded), then
// finishes the picture as complete or partial with EndReason::aborted when it
// was cut short. A live receiver never calls this.
void end_of_stream();
// SPSC PCM tap: called from DSP producer at exactly 48000 Hz, *never* draws or blocks.
void offer_audio_48k(const int16_t* samples, size_t count);
// Called from service() with the decoder-rate (12 kHz) samples it consumes, in batches. Null stops it. The observer
// must be quick and must not call back into the receiver.
void set_audio_observer(void (*observer)(const int16_t* samples, size_t count));
// The on-device self-test generates its own audio. While it runs the live radio audio is dropped, otherwise the
// receiver's noise is mixed into the test signal; the test audio itself uses offer_injected_48k.
void set_live_audio_muted(bool muted);
void offer_injected_48k(const int16_t* samples, size_t count);
// UI task only: bounded decoder service and immutable output snapshots.
void service(size_t budget = 3600);
Metrics metrics();
SessionState session_state();
Image live_image();
Image archived_image(size_t index); // 0 = newest
size_t archive_count();
void clear_gallery();
// Background writers must not read a gallery image directly: take a reference with
// pin_archived() (UI task), use the returned Image from any task, then call
// release_image() exactly once. The pixels stay valid until then even if the
// gallery evicts or clears the entry. A non-pinned Image copy owns nothing.
Image pin_archived(size_t index);
void release_image(const Image& image);
// Mode descriptors are validated against the slowrx mode table and Dayton paper.
struct ModeSpec {
  Mode mode;
  const char* name;
  uint8_t vis;
  uint16_t width, height;
  uint32_t line_us, sync_us, porch_us, sep_us;
  uint32_t pixel_ns;
  uint8_t family; // 0 Robot, 1 Martin, 2 Scottie, 3 PD
};
const ModeSpec* mode_spec(Mode mode);
const ModeSpec* vis_spec(uint8_t code);
// Last committed line, for bounded on-screen repaint.
uint32_t image_revision();
// Supplies verified UTC seconds (0 while the clock is unverified). Called on the UI task when a
// reception ends, so images carry an honest timestamp.
void set_utc_source(uint32_t (*utc_epoch)());
// Pinned copy of the in-progress image (partial, EndReason::captured) for the manual capture
// button; empty if nothing has been decoded yet. Hand it to sstv_archive::enqueue() or
// release_image() it. Saving is always done by the background archive, never on this task.
Image snapshot_live();
bool self_check();
#ifdef ORCSDR_SSTV_TESTING
namespace testing {
// Host tests only: place the decoder's sample clock (for example just before the
// 32-bit wrap at ~99.4 h). Call after start().
void set_tick_base(uint64_t ticks);
} // namespace testing
#endif
} // namespace orcsdr::sstv
