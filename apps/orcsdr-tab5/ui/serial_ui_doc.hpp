#pragma once
// UI documentation mode over serial: UI_DOC_LIST, UI_DOC_SHOW <screen-id> <live|demo>, UI_CAPTURE <slug>, UI_DOC_EXIT.
//
// A serial command family moved out of process_command() in main.cpp (see docs/SERIAL_COMMAND_MODULES.md). Only the
// command parsing and replies live here; the screen catalogue, rendering, reception pause and BMP capture stay in
// main.cpp behind hooks (they reach into most dashboards). Every reply and the sign-in rule are the ones the
// dispatcher had. As before, any other line that starts UI_DOC_ is not answered: the ping clock is restarted and the
// line falls through to the rest of the dispatcher (noted, not changed).
//
// The render hooks must stay out of line in main.cpp: inlined into the dispatcher their ~6 KB of locals cut the main
// task's free stack from 6,336 to 2,296 bytes (2026-11-14).
#include <cstddef>
#include <cstdint>

namespace orcsdr::serial_ui_doc {

struct CaptureResult {
  bool ok = false;
  const char* error = nullptr;   // reason when !ok, may be null
  size_t bytes = 0;
  unsigned width = 0;
  unsigned height = 0;
  uint8_t sha256[32]{};
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;                      // as given (Serial.print)
  void (*write_line)(const char* text) = nullptr;                 // then the line ending (Serial.println)
  void (*write_hex)(const uint8_t* value, size_t size) = nullptr;
  void (*ping)() = nullptr;                                       // restart the session's ping clock
  const char* (*firmware_version)() = nullptr;                    // esp_app_get_description()->version
  size_t (*screen_count)() = nullptr;
  void (*screen_at)(size_t index, const char** id, const char** modes) = nullptr;
  bool (*screen_exists)(const char* id, const char* mode) = nullptr;
  void (*enter)() = nullptr;                                      // ui_doc_enter()
  bool (*pause_reception)() = nullptr;                            // false: the stop timed out
  void (*tune_live)(const char* screen_id) = nullptr;             // start that screen's band unless already running
  bool (*render)(const char* screen_id, bool demo) = nullptr;           // ui_doc_render()
  bool (*render_settings)(const char* screen_id, bool demo) = nullptr;  // ui_doc_render_settings()
  void (*keep_graphics_on)() = nullptr;                           // live captures keep the scope painting
  bool (*capture_without_doc_mode)() = nullptr;    // Visualizer / Dead Air: capture as is, reception keeps running
  bool (*lab_active)() = nullptr;                  // RF Lab: capture allowed outside documentation mode
  bool (*doc_screen_shown)() = nullptr;            // documentation mode is on and a screen was rendered
  bool (*valid_slug)(const char* slug) = nullptr;
  bool (*sd_ready)() = nullptr;
  void (*save_capture)(const char* slug, CaptureResult* result) = nullptr;   // BMP to /orcsdr/screenshots/<slug>.bmp
  void (*exit)() = nullptr;                                       // ui_doc_exit()
};

// Returns true when `command` (not null) was answered here; false for lines that are not these commands, including
// an unknown UI_DOC_ line from a signed-in host (see above).
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_ui_doc
