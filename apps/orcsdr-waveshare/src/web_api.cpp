#include "web_api.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstring>

namespace orcsdr::web {
namespace {

Snapshot g_snapshot{};
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

bool g_loc_configured = false;
double g_lat = 0;
double g_lon = 0;
uint16_t g_range_nm = 25;

}  // namespace

void publish_snapshot(const Snapshot& snapshot) {
  portENTER_CRITICAL(&g_mux);
  g_snapshot = snapshot;
  portEXIT_CRITICAL(&g_mux);
}

void copy_snapshot(Snapshot* out) {
  if (!out) return;
  portENTER_CRITICAL(&g_mux);
  *out = g_snapshot;
  portEXIT_CRITICAL(&g_mux);
}

void set_receiver_location(bool configured, double latitude, double longitude,
                           uint16_t radar_range_nm) {
  portENTER_CRITICAL(&g_mux);
  g_loc_configured = configured;
  g_lat = latitude;
  g_lon = longitude;
  g_range_nm = radar_range_nm == 0 ? 25 : radar_range_nm;
  portEXIT_CRITICAL(&g_mux);
}

void get_receiver_location(bool* configured, double* latitude, double* longitude,
                           uint16_t* radar_range_nm) {
  portENTER_CRITICAL(&g_mux);
  if (configured) *configured = g_loc_configured;
  if (latitude) *latitude = g_lat;
  if (longitude) *longitude = g_lon;
  if (radar_range_nm) *radar_range_nm = g_range_nm;
  portEXIT_CRITICAL(&g_mux);
}

}  // namespace orcsdr::web
