#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::web_console {

constexpr size_t kSpectrumBins = 64;
constexpr size_t kRecentSlots = 8;

struct Snapshot {
  char wifi_ip[16]{};
  char mode[12]{};
  char clock[12]{};
  char date[20]{};
  char program_service[9]{};
  char radio_text[65]{};
  char pi_code[5]{};
  char recent_id[kRecentSlots][12]{};
  char recent_title[kRecentSlots][16]{};
  uint32_t frequency_hz = 0;
  uint32_t requested_frequency_hz = 0;
  uint32_t span_hz = 960000;
  uint32_t step_hz = 12500;
  uint32_t filter_bandwidth_hz = 0;
  uint32_t effective_sps = 0;
  int32_t battery_percent = -1;
  float signal_dbfs = -90.0f;
  float left_dbfs = -90.0f;
  float right_dbfs = -90.0f;
  uint8_t volume = 0;
  uint8_t recent_count = 0;
  uint8_t spectrum[kSpectrumBins]{};
  uint8_t spectrum_count = 0;
  bool wifi_connected = false;
  bool usb_connected = false;
  bool rtl_ready = false;
  bool receiving = false;
  bool sound_enabled = true;
  bool stereo = false;
  bool rds_carrier = false;
  bool rds_locked = false;
  bool recording = false;
  bool enabled = false;
};

enum class CommandKind : uint8_t {
  none,
  volume_down,
  volume_up,
  sound_toggle,
  span_down,
  span_up,
  step_down,
  step_up,
  tune,
  open
};

struct Command {
  CommandKind kind = CommandKind::none;
  uint32_t value = 0;
  char id[16]{};
};

void set_enabled(bool enabled);
bool enabled();
bool listening();
void poll(bool wifi_connected);
void update(const Snapshot& snapshot);
bool take_command(Command* command);
void tap_audio(const int16_t* samples, size_t frames, size_t stride);
void update_spectrum(const float* levels, size_t count);
size_t copy_audio(int16_t* output, size_t max_samples);
size_t copy_spectrum(uint8_t* output, size_t max_bins);
void format_url(char* out, size_t out_size, const char* ip);
bool self_check();

}  // namespace orcsdr::web_console
