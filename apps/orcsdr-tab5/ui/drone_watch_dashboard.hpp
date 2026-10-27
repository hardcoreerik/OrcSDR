#pragma once
// One independent OrcSDR dashboard. W01 LIVE is implemented as a truthful
// source-status / empty-state view; Remote ID acquisition has NOT been implemented.
#include <cstdint>
#include "drone_watch_model.hpp"

namespace orcsdr::drone_watch {
struct Hooks {
  int32_t (*battery_percent)() = nullptr;
  bool (*sound_enabled)() = nullptr;
};
enum class Action : uint8_t { none, close };

void install(const Hooks& hooks);
void open();
void close();
bool active();
void draw(bool structural);
void tick(uint32_t now_ms);                 // UI task only; no full-page polling draws
Action touch(int32_t x, int32_t y);        // global header owns Home/volume/settings
void publish_source(const SourceSnapshot& source);  // only a future verified adapter may do this
}  // namespace orcsdr::drone_watch
