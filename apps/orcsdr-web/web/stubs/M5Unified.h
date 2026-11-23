// Spike stub: M5.Display is an off-screen M5GFX sprite so the unmodified
// drawing code renders into RAM; JS blits the buffer to a canvas.
#pragma once
#include <stdint.h>
#include "esp_attr.h"   // the device build gets EXT_RAM_BSS_ATTR through the ESP-IDF headers
#include <stdio.h>
#include <string.h>
#include <M5GFX.h>
// What the Tab5's power IC would report; the page has no battery to show (the shell shows the node's own state), so dashboards that
// ask draw no battery.
struct M5Power {
  int getBatteryLevel() const { return -1; }
  bool isCharging() const { return false; }
  int getVBUSVoltage() const { return 0; }
};
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>
// The Tab5's display is a DSI panel the RF visualizer draws to directly; here it is the page's sprite plus a stand-in panel.
struct M5WebDisplay : M5Canvas {
  using M5Canvas::M5Canvas;
  lgfx::Panel_DSI* getPanel() { static lgfx::Panel_DSI panel; return &panel; }
};
struct M5Class { M5WebDisplay Display; M5Power Power; };
#ifndef DEG_TO_RAD
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#endif
template <typename T> static inline T constrain(T v, T lo, T hi) { return v < lo ? lo : v > hi ? hi : v; }   // Arduino
extern M5Class M5;
static inline uint32_t millis() { return (uint32_t)lgfx::millis(); }
