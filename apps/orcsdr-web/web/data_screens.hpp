// Web counterparts of the parts of OrcSDR's main.cpp that own a decoder dashboard's state (message stores, scan logic, settings),
// behind one small interface so web_main.cpp can route any of them. A data screen shows what a decoder on the node produced: the page
// forwards the node's decode events and live statistics in, the controller builds the dashboard's snapshot, and anything the
// dashboard asks of the radio comes back out as a request the page carries to the node.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "dashboard_registry.hpp"

namespace dscreens {

struct Result {
  bool exit = false;                 // back to Home
  const char* action = nullptr;      // a header action for the page ("sound_toggle", "open_device_settings")
};

struct Screen {
  orcsdr::dashboards::Id id;
  const char* decoder;               // node decoder kind: "pocsag", "p25", ...
  void (*enter)(uint32_t now_ms);
  void (*leave)();
  void (*update)(uint32_t now_ms);   // about 10 Hz while the screen is shown
  Result (*touch)(int x, int y, bool pressed, uint32_t now_ms);
  void (*event)(const char* json_line, uint32_t now_ms);   // a decode event, the live statistics, or an aircraft table
  void (*set_num)(const char* key, double value);          // page inputs: "receiving", "node_clock_ok", ...
  const char* (*take_request)();     // a JSON request for the page, or nullptr: {"op":"start","hz":N,"options":"k=v"} | {"op":"stop"}
  void (*spectrum)(const float* levels, int n, float floor_db);   // optional
  void (*text)(const char* key, const char* value);               // optional: a text the page hands over (a profile file)
  void (*audio)(const float* samples, size_t count);              // optional: 48 kS/s audio the page demodulated (SSTV)
  void (*iq)(const uint8_t* data, size_t count);                  // optional: raw 8-bit IQ blocks from the node's channel (RF Lab)
};

extern const Screen kViz;     // the RF visualizer (not a dashboard of the Home list: the header's VIS button opens it)
extern const Screen kRfLab;
extern const Screen kSettings;   // opened by the header's gear button (and from Home's list)

const Screen* find(orcsdr::dashboards::Id id);
const Screen* find_decoder(const char* decoder);

// A tiny reader for the flat JSON the decoders print ("key":value pairs, numbers or strings, no nesting needed here).
inline const char* field(const char* line, const char* key) {
  char pat[48];
  snprintf(pat, sizeof pat, "\"%s\":", key);
  const char* p = strstr(line, pat);
  return p ? p + strlen(pat) : nullptr;
}
inline double num(const char* line, const char* key, double fallback = 0) {
  const char* p = field(line, key);
  return p ? atof(p) : fallback;
}
inline bool flag(const char* line, const char* key) {
  const char* p = field(line, key);
  return p && !strncmp(p, "true", 4);
}
// String value, JSON escapes \uXXXX decoded to the low byte, others dropped. Returns the length written.
inline size_t str(const char* line, const char* key, char* out, size_t cap) {
  const char* p = field(line, key);
  size_t n = 0;
  if (!p || *p != '"' || !cap) { if (cap) out[0] = 0; return 0; }
  for (++p; *p && *p != '"' && n + 1 < cap; ++p) {
    if (*p == '\\') {
      ++p;
      if (*p == 'u' && p[1] && p[2] && p[3] && p[4]) {
        char hex[5] = {p[1], p[2], p[3], p[4], 0};
        out[n++] = (char)strtol(hex, nullptr, 16);
        p += 4;
      } else if (*p) out[n++] = *p;
    } else out[n++] = *p;
  }
  out[n] = 0;
  return n;
}

}  // namespace dscreens
