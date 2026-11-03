// Web build: the capability bits of the RTL-SDR driver (esp_rtl_sdr.h in the firmware), so screens written against them compile.
// The node reports its capabilities in /api/v1/tuner ("caps") and the page hands them to the screen.
#pragma once
#include <stdint.h>
enum {
  ESP_RTL_SDR_CAP_STREAM = 1u << 0,
  ESP_RTL_SDR_CAP_RETUNE = 1u << 1,
  ESP_RTL_SDR_CAP_HOTPLUG = 1u << 2,
  ESP_RTL_SDR_CAP_METRICS = 1u << 3,
  ESP_RTL_SDR_CAP_CUSTOM_HZ = 1u << 4,
  ESP_RTL_SDR_CAP_BIAS_TEE = 1u << 5,
  ESP_RTL_SDR_CAP_DIRECT_SAMPLING = 1u << 6,
  ESP_RTL_SDR_CAP_IQ_ACQUIRE = 1u << 7,
  ESP_RTL_SDR_CAP_FREQ_CORRECTION = 1u << 8,
  ESP_RTL_SDR_CAP_MULTI_DEVICE = 1u << 9,
  ESP_RTL_SDR_CAP_SYNC_READ = 1u << 10,
  ESP_RTL_SDR_CAP_CONTINUOUS_RATE = 1u << 11,
  ESP_RTL_SDR_CAP_NEED = 1u << 12,
  ESP_RTL_SDR_CAP_HEALTH = 1u << 13,
  ESP_RTL_SDR_CAP_PASSPORT = 1u << 14,
  ESP_RTL_SDR_CAP_GAIN = 1u << 15,
  ESP_RTL_SDR_CAP_DELIVERY_MODE = 1u << 16,
  ESP_RTL_SDR_CAP_HF_UPCONVERTER = 1u << 17,
  ESP_RTL_SDR_CAP_GAIN_AUTO = 1u << 18,
  ESP_RTL_SDR_CAP_RTL_AGC = 1u << 19,
  ESP_RTL_SDR_CAP_TUNER_BANDWIDTH = 1u << 20,
};
