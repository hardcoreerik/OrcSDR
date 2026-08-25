#pragma once

#include "rf_visualizer_controls.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr {
class NvsStore;
}

namespace orcsdr::visualizer {

struct Runtime {
  uint32_t center_hz = 0;
  uint32_t span_hz = 960000;
  uint32_t sample_rate_sps = 960000;
  uint32_t audio_rate_sps = 48000;
  uint32_t filter_bandwidth_hz = 0;
  uint32_t usb_overruns = 0;
  uint32_t consumer_drops = 0;
  uint32_t audio_drops = 0;
  uint8_t volume = 0;
  bool source_available = false;
  bool sound_enabled = true;
  bool sd_writable = false;
  bool stereo_audio = false;
  bool filtered_iq = false;
};

enum class ActionKind : uint8_t {
  none,
  close,
  tune_hz,
  span_hz,
  sound_toggle,
  volume_down,
  volume_up,
};

struct Action {
  ActionKind kind = ActionKind::none;
  uint32_t value = 0;
};

using AudioSink = void (*)(const int16_t* samples, size_t count);

bool initialize(NvsStore* store, AudioSink audio_sink = nullptr);
void set_runtime(const Runtime& runtime);
void offer_iq(const uint8_t* iq, size_t bytes);
void offer_audio(const int16_t* left, const int16_t* right, size_t frames,
                 uint32_t sample_rate_sps);

bool enter(uint8_t origin_screen, uint8_t origin_tab = 0);
void leave();
bool active();
uint8_t origin_screen();
uint8_t origin_tab();
View view();
void service_ui(uint32_t now_ms);
void handle_touch(int32_t x, int32_t y, bool pressed, uint8_t touch_count,
                  int32_t second_x, int32_t second_y, uint32_t now_ms);
bool take_action(Action* action);

bool channel_audio_active();
bool process_command(const char* command, char* response, size_t response_size);
bool self_check();

}  // namespace orcsdr::visualizer
