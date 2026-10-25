#pragma once
// Dashboard host for the P4 console. Each Tab5 dashboard keeps its own source (compiled unchanged);
// a small adapter per dashboard (screen_<name>.cpp) builds that dashboard's Snapshot from the
// receiver state and carries out its Actions, which is the job the Tab5's main.cpp does on the Tab5.
// Everything here runs on the UI task, except where noted.
#include <cstddef>
#include <cstdint>
#include "dashboard_registry.hpp"

namespace screens {

struct Screen {
  orcsdr::dashboards::Id id;
  const char* name;  // serial name, e.g. "FM"
  void (*enter)();   // draws the first frame
  void (*leave)();
  void (*tick)();    // about every 100 ms: rebuild the snapshot and let the dashboard update itself
  void (*tap)(int x, int y);    // a finger lifted at (x, y), full-screen design coordinates (1280x720)
  void (*drag)(int x, int y);   // a finger is down at (x, y); may be null
  // New spectrum frame: `visible` of the 256 levels starting at `first`, floor in dB. May be null.
  void (*spectrum)(const float* levels, size_t first, size_t visible, float floor);
};

const Screen* find(orcsdr::dashboards::Id id);
const Screen* find_name(const char* name);  // case-insensitive
const Screen* const* all(size_t* count);

// Defined by the adapters.
const Screen& fm_screen();
const Screen& am_screen();
const Screen& cb_screen();
const Screen& airband_screen();
const Screen& shortwave_screen();

}  // namespace screens
