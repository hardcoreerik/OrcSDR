#include "waterfall_style.hpp"

#include <algorithm>
#include <cstddef>

namespace orcsdr::waterfall_style {
namespace {

constexpr size_t kScreens = static_cast<size_t>(Screen::count);
constexpr uint8_t kClassic = 0;
constexpr uint8_t kTurbo = 6;

struct State {
  uint8_t palette;
  uint8_t speed;
};

State g_state[kScreens] = {{kClassic, 0}, {kTurbo, 0}};
PersistHook g_hook = nullptr;

// Rows per frame at speed 0 (Normal): Home's frames come at 10/s, LoRa's at up to 20/s.
constexpr int kBaseRows[kScreens] = {2, 1};

constexpr const char* kPaletteNames[kPaletteCount] = {"CLASSIC", "FM",   "FIRE", "ICE",
                                                      "PLASMA",  "GRAY", "TURBO"};
constexpr const char* kSpeedNames[kSpeedCount] = {"NORMAL", "FAST", "FASTER"};

struct Stop {
  float at;
  uint8_t r, g, b;
};

constexpr Stop kFire[] = {{0.0f, 0, 0, 0}, {0.3f, 120, 0, 0}, {0.6f, 255, 90, 0},
                          {0.85f, 255, 220, 0}, {1.0f, 255, 255, 255}};
constexpr Stop kIce[] = {{0.0f, 0, 0, 20}, {0.4f, 0, 60, 200}, {0.75f, 0, 200, 255},
                         {1.0f, 255, 255, 255}};
constexpr Stop kPlasma[] = {{0.0f, 20, 0, 70}, {0.35f, 120, 0, 160}, {0.65f, 230, 60, 90},
                            {0.85f, 255, 170, 0}, {1.0f, 255, 250, 140}};
constexpr Stop kGray[] = {{0.0f, 0, 0, 0}, {1.0f, 255, 255, 255}};
// The LoRa dashboard's original blue, teal, yellow, red, white ramp.
constexpr Stop kTurboStops[] = {{0.0f, 0, 0, 20},     {0.20f, 0, 0, 100},    {0.50f, 0, 180, 255},
                                {0.75f, 255, 255, 0}, {0.90f, 255, 0, 0},    {1.0f, 255, 255, 255}};

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

template <size_t N>
uint16_t ramp(const Stop (&stops)[N], float value) {
  size_t i = 1;
  while (i + 1 < N && value > stops[i].at) ++i;
  const Stop& a = stops[i - 1];
  const Stop& b = stops[i];
  const float t = std::clamp((value - a.at) / (b.at - a.at), 0.0f, 1.0f);
  const auto mix = [t](uint8_t x, uint8_t y) {
    return static_cast<uint8_t>(static_cast<float>(x) + (static_cast<float>(y) - x) * t);
  };
  return rgb565(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b));
}

uint16_t classic(float v) {
  const uint8_t r = v > 0.62f ? static_cast<uint8_t>(std::min(255.0f, (v - 0.62f) * 670)) : 0;
  const uint8_t g = v > 0.25f ? static_cast<uint8_t>(std::min(255.0f, (v - 0.25f) * 520)) : 0;
  const uint8_t b = static_cast<uint8_t>(35 + (1.0f - v) * 150);
  return rgb565(r, g, b);
}

// Same colors as the FM dashboard's waterfall.
uint16_t fm(float v) {
  const uint8_t r = v < 0.5f ? 0 : static_cast<uint8_t>((v - 0.5f) * 510);
  const uint8_t g = v < 0.25f ? 0 : static_cast<uint8_t>(std::min(255.0f, (v - 0.25f) * 510));
  const uint8_t b = v < 0.65f ? static_cast<uint8_t>((0.65f - v) * 390) : 0;
  return rgb565(r, g, b);
}

bool valid(Screen screen) { return static_cast<size_t>(screen) < kScreens; }

void notify(Screen screen) {
  if (g_hook != nullptr) g_hook(screen, pack(screen));
}

}  // namespace

uint8_t default_palette(Screen screen) { return screen == Screen::lora ? kTurbo : kClassic; }

uint8_t palette(Screen screen) { return valid(screen) ? g_state[static_cast<size_t>(screen)].palette : 0; }
uint8_t speed(Screen screen) { return valid(screen) ? g_state[static_cast<size_t>(screen)].speed : 0; }

void set_palette(Screen screen, uint8_t index) {
  if (!valid(screen)) return;
  g_state[static_cast<size_t>(screen)].palette = index < kPaletteCount ? index : 0;
  notify(screen);
}

void set_speed(Screen screen, uint8_t index) {
  if (!valid(screen)) return;
  g_state[static_cast<size_t>(screen)].speed = index < kSpeedCount ? index : 0;
  notify(screen);
}

void next_palette(Screen screen) {
  set_palette(screen, static_cast<uint8_t>((palette(screen) + 1) % kPaletteCount));
}

void next_speed(Screen screen) {
  set_speed(screen, static_cast<uint8_t>((speed(screen) + 1) % kSpeedCount));
}

const char* palette_name(uint8_t index) { return kPaletteNames[index < kPaletteCount ? index : 0]; }
const char* speed_name(uint8_t index) { return kSpeedNames[index < kSpeedCount ? index : 0]; }

int rows_per_frame(Screen screen) {
  return valid(screen) ? kBaseRows[static_cast<size_t>(screen)] + speed(screen) : 1;
}

uint16_t color565(uint8_t palette_index, float level) {
  const float v = std::clamp(level, 0.0f, 1.0f);
  switch (palette_index) {
    case 1: return fm(v);
    case 2: return ramp(kFire, v);
    case 3: return ramp(kIce, v);
    case 4: return ramp(kPlasma, v);
    case 5: return ramp(kGray, v);
    case 6: return ramp(kTurboStops, v);
    default: return classic(v);
  }
}

uint16_t color565(Screen screen, float level) { return color565(palette(screen), level); }

uint8_t pack(Screen screen) {
  return static_cast<uint8_t>((palette(screen) << 4) | (speed(screen) & 0x0F));
}

void unpack(Screen screen, uint8_t value) {
  if (!valid(screen)) return;
  const uint8_t p = value >> 4;
  const uint8_t s = value & 0x0F;
  // 0xFF is an erased/never-written byte; anything out of range keeps the defaults.
  const bool ok = value != 0xFF && p < kPaletteCount && s < kSpeedCount;
  g_state[static_cast<size_t>(screen)] = {ok ? p : default_palette(screen), ok ? s : uint8_t{0}};
}

void set_persist_hook(PersistHook hook) { g_hook = hook; }

}  // namespace orcsdr::waterfall_style
