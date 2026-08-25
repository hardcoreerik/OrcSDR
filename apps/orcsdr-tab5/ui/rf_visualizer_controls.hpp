#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::visualizer {

enum class View : uint8_t {
  spectrum,
  waterfall,
  phosphor,
  spectrum3d,
  constellation,
  iqscope,
  polar,
  occupancy,
  peak_average,
  doppler,
  channelizer,
  audio_spectrogram,
  count,
  common = 0xff,
};

enum class ControlKind : uint8_t { toggle, integer, real, choice, action };
enum class ControlGroup : uint8_t { quick, advanced, display, action };

enum Capability : uint32_t {
  cap_none = 0,
  cap_iq = 1u << 0,
  cap_filtered_iq = 1u << 1,
  cap_recovered_symbols = 1u << 2,
  cap_stereo_audio = 1u << 3,
  cap_sd = 1u << 4,
  cap_carrier_frequency = 1u << 5,
  cap_channel_audio = 1u << 6,
};

struct ControlDescriptor {
  View view;
  ControlGroup group;
  ControlKind kind;
  const char* id;
  const char* label;
  float minimum;
  float maximum;
  float step;
  float default_value;
  const char* choices;
  uint32_t required_caps;
  bool apply_on_commit;
  const char* disabled_reason;
};

const ControlDescriptor* controls(size_t* count);
const ControlDescriptor* find_control(const char* id, size_t* index = nullptr);
const char* view_name(View view);
const char* view_slug(View view);
bool parse_view(const char* text, View* view);
bool controls_self_check();

}  // namespace orcsdr::visualizer
