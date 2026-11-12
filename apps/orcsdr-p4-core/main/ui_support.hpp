#pragma once
// Pieces the Tab5's main.cpp supplies to the self-contained apps (ACARS, FT8, SSTV, Dead Air) through their Hooks:
// the shared header (home, mute, settings, volume tray), a persistent preference store and the wall clock.
#include <cstdint>
#include "screen_controller.hpp"

namespace ui_support {

// ---- shared header ----
void draw_header();                    // the Hooks::header_controls function
bool header_overlay_open();            // the volume tray is open over the content
void header_reset();                   // on entering a screen
// Routes a tap that may belong to the header (volume tray, home, mute, settings). True = consumed.
// `first_press` is true only on the first loop of a gesture: a held finger must not repeat a mute toggle.
bool handle_header_touch(int x, int y, bool first_press);
// Call every tick: closes the volume tray after its timeout.
void header_service();
bool sound_enabled();

// ---- preferences (NVS namespace "orcsdr_ui"; keys up to 15 characters) ----
bool get_bool(const char* key, bool fallback);
void put_bool(const char* key, bool value);
uint8_t get_u8(const char* key, uint8_t fallback);
void put_u8(const char* key, uint8_t value);
uint32_t get_u32(const char* key, uint32_t fallback);
void put_u32(const char* key, uint32_t value);

// ---- clock ----
uint32_t utc_epoch();       // seconds since 1970, 0 until the clock has been set (NTP over Wi-Fi)
uint32_t now_ms();          // monotonic

// ---- screen ownership (the apps check orcsdr::screens before they draw) ----
void claim_screen(orcsdr::screens::Id id);

}  // namespace ui_support
