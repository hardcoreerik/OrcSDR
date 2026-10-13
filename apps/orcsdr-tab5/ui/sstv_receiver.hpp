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
enum class Page : uint8_t { receive, gallery, analyzer, frequencies, setup, count };
// VIS is authoritative when valid; sync fallback is always labeled provisional.
enum class AutoStrategy : uint8_t { vis_only, vis_sync };
enum class Identification : uint8_t { none, vis, sync, manual };
enum class EndReason : uint8_t { none, complete, sync_lost, new_vis, retune, aborted };
struct Profile { const char* name; uint32_t hz; bool usb; const char* description; };
constexpr Profile kProfiles[] = {
    {"HF SSTV", 14230000, true, "14.230 MHz USB - general amateur SSTV"},
    {"ISS SSTV", 145800000, false, "ISS event only - verify current ARISS plan"},
    {"OREGON NET", 145290000, false, "Salem-area SSTV repeater - confirm active net"},
    {"ISS UHF", 437550000, false, "ARISS UHF SSTV when scheduled - verify before reception"}};
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
  float sync_pulse_ms = 0.0f;     // width of the most recent 1200 Hz pulse
  float sync_interval_ms = 0.0f;  // spacing between the two most recent pulse starts
  Identification identification = Identification::none;
  uint8_t identification_score = 0; // heuristic; not a probability
  bool start_unknown = false;
  uint32_t sync_locked_lines = 0;
  uint32_t sync_missed_lines = 0;
  float line_error_ms = 0.0f;
  uint32_t samples = 0;
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
// Offline replay only: no further audio will arrive. Drains the ring, closes a
// line whose luminance scan has already fully elapsed (a recording that stops a
// few milliseconds early still yields its last row; nothing is padded), then
// finishes the picture as complete or partial with EndReason::aborted when it
// was cut short. A live receiver never calls this.
void end_of_stream();
// SPSC PCM tap: called from DSP producer at exactly 48000 Hz, *never* draws or blocks.
void offer_audio_48k(const int16_t* samples, size_t count);
// UI task only: bounded decoder service and immutable output snapshots.
void service(size_t budget = 3600);
Metrics metrics();
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
// Save a completed frame as a standard 24-bit BMP under /sd/orcsdr/sstv/.
// index == SIZE_MAX captures the in-progress image (marked partial).
// When utc_epoch==0 the clock is explicitly unverified in the index.
bool save_bmp(size_t index, char* out_path, size_t path_len, uint32_t utc_epoch = 0);
bool self_check();
#ifdef ORCSDR_SSTV_TESTING
namespace testing {
// Host tests only: place the decoder's sample clock (for example just before the
// 32-bit wrap at ~99.4 h). Call after start().
void set_tick_base(uint64_t ticks);
} // namespace testing
#endif
} // namespace orcsdr::sstv
