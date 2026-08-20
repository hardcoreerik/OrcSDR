#pragma once

#include <cstdint>

#include "home_dashboard.hpp"
#include "screen_controller.hpp"
#include "settings_app.hpp"

namespace orcsdr::navigation {

// Runtime state stays in the radio application; this service owns only the
// single-screen navigation lifecycle and display handoff.
struct Hooks {
  home::Snapshot (*home_snapshot)(bool demo) = nullptr;
  void (*sync_audio)() = nullptr;
  bool (*audio_enabled)() = nullptr;
  uint8_t (*volume)() = nullptr;
  bool (*ensure_speaker_running)(uint8_t volume) = nullptr;
  void (*close_overlays)() = nullptr;
  const settings::State& (*settings_state)() = nullptr;
  void (*persist_settings_open)() = nullptr;
  bool (*disable_graphics)() = nullptr;
  void (*restore_graphics)(bool enabled) = nullptr;
  void (*restore_screen)(screens::Id id) = nullptr;
};

void configure(const Hooks& hooks);
void show_home(bool demo = false);
void draw_home();
void open_settings(settings::Section section);
void close_settings();

}  // namespace orcsdr::navigation
