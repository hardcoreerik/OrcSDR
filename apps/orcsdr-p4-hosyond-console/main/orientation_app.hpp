#pragma once

#include <cstdint>
#include "orcsdr_orientation_policy.h"

namespace orcsdr::orientation_app {

struct Status {
  bool enabled;       // persisted user preference; remains true if sensor is unavailable
  bool available;     // LilyGO provider initialized and axes explicitly verified
  bool auto_active;   // enabled && available
  bool manual;        // effective fallback mode (auto_active is false)
  bool sample_valid;  // recent successful sensor read
  int32_t current_degrees;
  int32_t manual_degrees;
  float sensor_x_g, sensor_y_g, sensor_z_g;
};

/* Call after board startup/secondary-I2C setup and NVS initialization, before
   the first rotation is applied. A persisted manual angle overrides the input;
   read status().current_degrees to initialize shared display geometry. */
bool init(int32_t initial_degrees);

/* There is intentionally no guessed LilyGO axis mapping. The profile owner may
   supply one only after physical mounting-axis verification. Passing false (or
   an invalid matrix) keeps automatic rotation unavailable while preserving
   manual rotation and sensor telemetry. */
bool configure_profile_axes(const orcsdr_orientation_config_t *axes,
                            bool physically_verified);

Status status();
bool request_manual(int32_t degrees);
bool request_auto(bool enabled);

/* Nonblocking UI-side poll. Sensor I2C runs on a bounded background task.
   Returns one proposal at a time; caller applies it through shared geometry
   and then acknowledges that exact angle. Touch and critical-update locks
   defer either manual or automatic rotation. */
bool tick(bool touch_active, bool geometry_locked, uint32_t now_ms,
          int32_t *rotation_proposal);
bool acknowledge_applied(int32_t degrees);
void reject_proposal();

}  // namespace orcsdr::orientation_app
