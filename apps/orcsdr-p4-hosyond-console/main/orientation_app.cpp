#include "orientation_app.hpp"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "icm20948_orientation.h"
#include "nvs_store.hpp"
#include "orcsdr_board_control.h"
#include "orcsdr_board_profile.h"

namespace orcsdr::orientation_app {
namespace {
constexpr char kTag[] = "ORIENTATION";
constexpr char kNvsNamespace[] = "orcsdr_display";
constexpr char kAutoKey[] = "auto_rot";
constexpr char kManualKey[] = "manual_deg";
constexpr uint32_t kSensorPeriodMs = 50;
constexpr uint32_t kMaxSampleAgeMs = 250;
constexpr uint32_t kSensorTaskStack = 3072;
constexpr UBaseType_t kSensorTaskPriority = 2;

portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE s_sample_mux = portMUX_INITIALIZER_UNLOCKED;
bool s_initialized;
bool s_sensor_started;
bool s_axes_verified;
bool s_policy_ready;
bool s_auto_preference;
bool s_manual_pending;
bool s_unacked_proposal;
int32_t s_manual_preference;
int32_t s_current_degrees;
int32_t s_pending_degrees;
orcsdr_orientation_policy_t s_policy{};

bool s_sample_valid;
float s_sample_x, s_sample_y, s_sample_z;
uint32_t s_sample_time_ms;
TaskHandle_t s_sensor_task;

bool valid_angle(int32_t degrees) {
  return degrees == 0 || degrees == 90 || degrees == 180 || degrees == 270;
}

bool save_bool(const char *key, bool value) {
  orcsdr::NvsStore store;
  if (!store.begin(kNvsNamespace, false)) return false;
  const bool saved = store.put_bool(key, value);
  store.end();
  return saved;
}

bool save_angle(int32_t degrees) {
  orcsdr::NvsStore store;
  if (!store.begin(kNvsNamespace, false)) return false;
  const bool saved = store.put_i32(kManualKey, degrees);
  store.end();
  return saved;
}

void sensor_task(void *) {
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    icm20948_orientation_sample_t sample{};
    const esp_err_t err = icm20948_orientation_read(&sample);
    const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    portENTER_CRITICAL(&s_sample_mux);
    s_sample_valid = err == ESP_OK;
    if (s_sample_valid) {
      s_sample_x = sample.x_g;
      s_sample_y = sample.y_g;
      s_sample_z = sample.z_g;
      s_sample_time_ms = now_ms;
    }
    portEXIT_CRITICAL(&s_sample_mux);
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(kSensorPeriodMs));
  }
}

void read_sample(float *x, float *y, float *z, uint32_t *sample_ms, bool *valid) {
  portENTER_CRITICAL(&s_sample_mux);
  *x = s_sample_x;
  *y = s_sample_y;
  *z = s_sample_z;
  *sample_ms = s_sample_time_ms;
  *valid = s_sample_valid;
  portEXIT_CRITICAL(&s_sample_mux);
}

}  // namespace

bool init(int32_t initial_degrees) {
  if (!valid_angle(initial_degrees)) return false;
  portENTER_CRITICAL(&s_state_mux);
  if (s_initialized) {
    portEXIT_CRITICAL(&s_state_mux);
    return true;
  }
  portEXIT_CRITICAL(&s_state_mux);

  /* Load both preferences before any sensor polling starts. NVS is expected to
     have been initialized by the console's startup path. Failure keeps the
     safe defaults: auto off and the caller's initial angle. */
  bool auto_preference = false;
  int32_t manual_preference = initial_degrees;
  orcsdr::NvsStore store;
  if (store.begin(kNvsNamespace, true)) {
    auto_preference = store.get_bool(kAutoKey, false);
    const int32_t stored_angle = store.get_i32(kManualKey, initial_degrees);
    if (valid_angle(stored_angle)) manual_preference = stored_angle;
    store.end();
  } else {
    ESP_LOGW(kTag, "display preferences unavailable; using auto off and rotation %ld",
             static_cast<long>(initial_degrees));
  }

  const orcsdr_board_profile_t *profile = orcsdr_board_profile();
  bool sensor_started = false;
  if (profile && profile->imu_type == ORCSDR_IMU_ICM20948 &&
      profile->imu_i2c_address == 0x68) {
    i2c_master_bus_handle_t secondary = orcsdr_board_secondary_i2c();
    if (secondary && icm20948_orientation_start(secondary) == ESP_OK) {
      sensor_started = true;
      if (xTaskCreate(sensor_task, "orientation_sensor", kSensorTaskStack, nullptr,
                      kSensorTaskPriority, &s_sensor_task) != pdPASS) {
        icm20948_orientation_stop();
        sensor_started = false;
        ESP_LOGW(kTag, "could not start bounded sensor polling task");
      }
    } else {
      ESP_LOGI(kTag, "LilyGO orientation sensor unavailable; manual rotation remains enabled");
    }
  }

  portENTER_CRITICAL(&s_state_mux);
  s_auto_preference = auto_preference;
  s_manual_preference = manual_preference;
  s_current_degrees = manual_preference;
  s_sensor_started = sensor_started;
  s_initialized = true;
  portEXIT_CRITICAL(&s_state_mux);
  return true;
}

bool configure_profile_axes(const orcsdr_orientation_config_t *axes,
                            bool physically_verified) {
  if (!axes || !physically_verified) {
    portENTER_CRITICAL(&s_state_mux);
    s_axes_verified = false;
    s_policy_ready = false;
    if (s_sensor_started) orcsdr_orientation_policy_enable(&s_policy, false);
    portEXIT_CRITICAL(&s_state_mux);
    return false;
  }

  portENTER_CRITICAL(&s_state_mux);
  if (!s_initialized || !s_sensor_started) {
    s_axes_verified = false;
    s_policy_ready = false;
    portEXIT_CRITICAL(&s_state_mux);
    return false;
  }
  s_policy_ready = orcsdr_orientation_policy_init(&s_policy, axes, s_current_degrees);
  s_axes_verified = s_policy_ready;
  if (s_policy_ready) {
    orcsdr_orientation_policy_enable(&s_policy, s_auto_preference);
  }
  const bool ready = s_policy_ready;
  portEXIT_CRITICAL(&s_state_mux);
  if (ready) ESP_LOGI(kTag, "profile orientation calibration accepted");
  return ready;
}

Status status() {
  Status result{};
  portENTER_CRITICAL(&s_state_mux);
  result.enabled = s_auto_preference;
  result.available = s_sensor_started && s_axes_verified && s_policy_ready;
  result.auto_active = result.enabled && result.available;
  result.manual = !result.auto_active;
  result.current_degrees = s_current_degrees;
  result.manual_degrees = s_manual_preference;
  portEXIT_CRITICAL(&s_state_mux);

  uint32_t sample_ms = 0;
  bool valid = false;
  read_sample(&result.sensor_x_g, &result.sensor_y_g, &result.sensor_z_g,
              &sample_ms, &valid);
  result.sample_valid = valid &&
      static_cast<uint32_t>(esp_timer_get_time() / 1000 - sample_ms) <= kMaxSampleAgeMs;
  return result;
}

bool request_manual(int32_t degrees) {
  if (!valid_angle(degrees) || !save_angle(degrees)) return false;
  portENTER_CRITICAL(&s_state_mux);
  s_manual_preference = degrees;
  s_manual_pending = true;
  portEXIT_CRITICAL(&s_state_mux);
  return true;
}

bool request_auto(bool enabled) {
  if (!save_bool(kAutoKey, enabled)) return false;
  portENTER_CRITICAL(&s_state_mux);
  s_auto_preference = enabled;
  if (s_policy_ready) {
    const bool active = enabled && s_sensor_started && s_axes_verified;
    orcsdr_orientation_policy_enable(&s_policy, active);
  }
  portEXIT_CRITICAL(&s_state_mux);
  return true;
}

bool tick(bool touch_active, bool geometry_locked, uint32_t now_ms,
          int32_t *rotation_proposal) {
  if (!rotation_proposal) return false;
  float x = 0.0f, y = 0.0f, z = 0.0f;
  uint32_t sample_ms = 0;
  bool sample_valid = false;
  read_sample(&x, &y, &z, &sample_ms, &sample_valid);
  sample_valid = sample_valid &&
      static_cast<uint32_t>(now_ms - sample_ms) <= kMaxSampleAgeMs;

  portENTER_CRITICAL(&s_state_mux);
  if (s_unacked_proposal) {
    portEXIT_CRITICAL(&s_state_mux);
    return false;
  }
  if (s_policy_ready) {
    orcsdr_orientation_policy_set_gates(&s_policy, touch_active, geometry_locked);
    if (s_manual_pending) {
      orcsdr_orientation_policy_request_manual(&s_policy, s_manual_preference);
      s_manual_pending = false;
    }
    const bool auto_active = s_auto_preference && s_sensor_started && s_axes_verified;
    int32_t proposal = 0;
    const float sx = sample_valid ? x : 0.0f;
    const float sy = sample_valid ? y : 0.0f;
    const float sz = sample_valid ? z : 0.0f;
    if ((s_policy.pending_manual || auto_active) &&
        orcsdr_orientation_policy_update(&s_policy, sx, sy, sz, now_ms, &proposal)) {
      s_pending_degrees = proposal;
      s_unacked_proposal = true;
      *rotation_proposal = proposal;
      portEXIT_CRITICAL(&s_state_mux);
      return true;
    }
    portEXIT_CRITICAL(&s_state_mux);
    return false;
  }

  if (!s_manual_pending || touch_active || geometry_locked) {
    portEXIT_CRITICAL(&s_state_mux);
    return false;
  }
  if (s_manual_preference == s_current_degrees) {
    s_manual_pending = false;
    portEXIT_CRITICAL(&s_state_mux);
    return false;
  }
  s_manual_pending = false;
  s_pending_degrees = s_manual_preference;
  s_unacked_proposal = true;
  *rotation_proposal = s_pending_degrees;
  portEXIT_CRITICAL(&s_state_mux);
  return true;
}

bool acknowledge_applied(int32_t degrees) {
  portENTER_CRITICAL(&s_state_mux);
  if (!valid_angle(degrees) || !s_unacked_proposal || degrees != s_pending_degrees) {
    portEXIT_CRITICAL(&s_state_mux);
    return false;
  }
  s_current_degrees = degrees;
  s_unacked_proposal = false;
  if (s_policy_ready) {
    s_policy.current_degrees = degrees;
    s_policy.stable_degrees = degrees;
    s_policy.stable_since_ms = 0;
  }
  portEXIT_CRITICAL(&s_state_mux);
  return true;
}

void reject_proposal() {
  portENTER_CRITICAL(&s_state_mux);
  if (s_unacked_proposal) {
    s_unacked_proposal = false;
    if (s_policy_ready) {
      s_policy.current_degrees = s_current_degrees;
      s_policy.stable_degrees = s_current_degrees;
      s_policy.stable_since_ms = 0;
    }
  }
  portEXIT_CRITICAL(&s_state_mux);
}

}  // namespace orcsdr::orientation_app
