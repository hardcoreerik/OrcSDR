#pragma once

#include <cstdint>

// Waterfall palette and scroll speed, shared by every dashboard that draws a waterfall.
// Each dashboard keeps its own choice (Home, LoRa, ...) so one screen's taste does not change another's,
// and a persistence hook lets the application store the choice without this module knowing how.
// Pure C++: no display or ESP-IDF headers, so it is host-tested.
namespace orcsdr::waterfall_style {

enum class Screen : uint8_t { home = 0, lora, count };

constexpr uint8_t kPaletteCount = 7;
constexpr uint8_t kSpeedCount = 3;

// What a screen starts with before anything is loaded or chosen.
uint8_t default_palette(Screen screen);

uint8_t palette(Screen screen);
uint8_t speed(Screen screen);
void set_palette(Screen screen, uint8_t index);   // out of range wraps to 0
void set_speed(Screen screen, uint8_t index);
void next_palette(Screen screen);
void next_speed(Screen screen);

const char* palette_name(uint8_t index);
const char* speed_name(uint8_t index);

// Waterfall rows added per spectrum frame at the screen's current speed.
int rows_per_frame(Screen screen);

// RGB565 for a 0..1 signal level in the screen's palette.
uint16_t color565(Screen screen, float level);
uint16_t color565(uint8_t palette_index, float level);

// One byte per screen (palette in the high nibble, speed in the low) for storage.
uint8_t pack(Screen screen);
void unpack(Screen screen, uint8_t value);   // invalid values fall back to the defaults

// Called after a change made through set_/next_ (not unpack) with the screen and its packed byte.
using PersistHook = void (*)(Screen screen, uint8_t packed);
void set_persist_hook(PersistHook hook);

}  // namespace orcsdr::waterfall_style
