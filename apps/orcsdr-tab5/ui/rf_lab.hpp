#pragma once

#include "orcsdr_storage.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::rf_lab {

enum class Page : uint8_t { live, controls, measurements, records };
enum class GainMode : uint8_t { automatic, manual };

struct Runtime {
  uint32_t frequency_hz = 0;
  uint32_t sample_rate_sps = 0;
  uint32_t capabilities = 0;
  uint32_t usb_overruns = 0;
  uint32_t consumer_drops = 0;
  uint32_t effective_sps = 0;
  int ppm = 0;
  int gain_tenth_db = 0;
  int gain_ladder[28]{};
  uint8_t gain_count = 0;
  GainMode gain_mode = GainMode::automatic;
  bool rtl_agc = false;
  bool bias_tee = false;
  bool source_available = false;
  bool sound_enabled = true;
  uint8_t volume = 0;
  bool sd_writable = false;
  char driver_version[20]{};
  char device[48]{};
  char health[28]{};
};

enum class ActionKind : uint8_t {
  none,
  close,
  tune_hz,
  sample_rate_sps,
  ppm,
  gain_mode,
  gain_tenth_db,
  rtl_agc,
  bias_tee,
};

struct Action {
  ActionKind kind = ActionKind::none;
  int32_t value = 0;
};

bool initialize(storage::FileSystem* filesystem);
void set_runtime(const Runtime& runtime);
bool enter(uint8_t origin_screen, uint8_t origin_tab = 0);
void leave();
bool active();
uint8_t origin_screen();
uint8_t origin_tab();
Page page();
void service_ui(uint32_t now_ms);
void handle_touch(int32_t x, int32_t y, bool pressed, uint32_t now_ms);
bool take_action(Action* action);
bool process_command(const char* command, char* response, size_t response_size);
bool self_check();

}  // namespace orcsdr::rf_lab
