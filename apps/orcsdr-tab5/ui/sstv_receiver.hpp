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
  uint32_t audio_overruns = 0;
  uint32_t bad_vis = 0;
  uint32_t good_vis = 0;
  uint32_t frames = 0;
  uint32_t partial_frames = 0;
  uint32_t auto_sync_locks = 0;
  uint32_t observed_sync_pulses = 0;
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
  const uint16_t* pixels = nullptr; // RGB565, owned by receiver and valid until cleared
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
};
void start(uint32_t frequency_hz);
void stop();
bool active();
void set_mode(Mode mode);
Mode selected_mode();
void set_auto_strategy(AutoStrategy strategy);
AutoStrategy auto_strategy();
void set_frequency(uint32_t hz);
// SPSC PCM tap: called from DSP producer at exactly 48000 Hz, *never* draws or blocks.
void offer_audio_48k(const int16_t* samples, size_t count);
// UI task only: bounded decoder service and immutable output snapshots.
void service(size_t budget = 3600);
Metrics metrics();
Image live_image();
Image archived_image(size_t index); // 0 = newest
size_t archive_count();
void clear_gallery();
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
} // namespace orcsdr::sstv
