#pragma once
// Native, receive-only SSTV decoder for OrcSDR.  UI and DSP remain separate.
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace orcsdr::sstv {
enum class Mode : uint8_t { auto_vis, robot36, martin_m1, scottie_s1 };
enum class Page : uint8_t { receive, gallery, analyzer, frequencies, setup, count };
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
};
void start(uint32_t frequency_hz);
void stop();
bool active();
void set_mode(Mode mode);
Mode selected_mode();
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
// Save a completed frame as a standard 24-bit BMP under /sd/orcsdr/sstv/.
bool save_bmp(size_t index, char* out_path, size_t path_len);
bool self_check();
} // namespace orcsdr::sstv
