/*
 * RTL-SDRv4-ESP — public C API
 *
 * Standalone ESP-IDF USB Host client for the official RTL-SDR Blog V4
 * (USB 0bda:2838). Clean-room transfer tables live under private/; do not
 * treat this as a librtlsdr port.
 *
 * Ownership: one host client owns interface 0. The application normally owns
 * installation of the ESP-IDF USB Host Library unless config says otherwise.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rtl_sdr_v4_esp_handle *rtl_sdr_v4_esp_handle_t;

/** Allowlisted presets with measured / derived PLL tables. */
typedef enum {
    RTL_SDR_V4_ESP_PRESET_KZEL_96_1 = 0, /**< 96.1 MHz WBFM reference */
    RTL_SDR_V4_ESP_PRESET_NOAA_162_4 = 1, /**< 162.400 MHz NFM reference */
    RTL_SDR_V4_ESP_PRESET_CUSTOM_HZ = 2,  /**< frequency_hz + clean-room PLL pack */
} rtl_sdr_v4_esp_preset_t;

typedef enum {
    RTL_SDR_V4_ESP_EVT_ENUMERATED = 1,
    RTL_SDR_V4_ESP_EVT_READY,
    RTL_SDR_V4_ESP_EVT_IQ_BLOCK,
    RTL_SDR_V4_ESP_EVT_STOPPED,
    RTL_SDR_V4_ESP_EVT_ERROR,
    RTL_SDR_V4_ESP_EVT_DISCONNECTED,
} rtl_sdr_v4_esp_event_t;

typedef struct {
    uint16_t vid;
    uint16_t pid;
    char serial[32];
    char manufacturer[32];
    char product[32];
    bool high_speed;
} rtl_sdr_v4_esp_device_info_t;

typedef struct {
    uint64_t bytes_total;
    uint32_t blocks_total;
    uint32_t short_transfers;
    uint32_t overruns;
    uint32_t consumer_drops;
    uint8_t sample_min;
    uint8_t sample_max;
    double sample_mean;
    uint32_t effective_sps;
    uint32_t frequency_hz;
    uint32_t sample_rate_sps;
} rtl_sdr_v4_esp_metrics_t;

/**
 * IQ block ownership: valid only for the duration of the event callback unless
 * rtl_sdr_v4_esp_acquire_block() is used (when implemented). Callbacks must not
 * block on display, network, or flash.
 */
typedef struct {
    const uint8_t *data;
    size_t bytes;
    uint32_t sequence;
    uint32_t frequency_hz;
    uint32_t sample_rate_sps;
} rtl_sdr_v4_esp_iq_block_t;

typedef void (*rtl_sdr_v4_esp_event_cb_t)(rtl_sdr_v4_esp_event_t event,
                                          const void *payload,
                                          void *user_ctx);

typedef struct {
    /** If true, app already called usb_host_install(); driver only registers a client. */
    bool host_library_already_installed;
    /** Bulk URB size (bytes). 32768 is the measured Tab5 default. */
    size_t transfer_bytes;
    /** Number of driver-owned bulk buffers (>= 2). */
    size_t transfer_count;
    uint32_t control_timeout_ms;
    rtl_sdr_v4_esp_event_cb_t event_cb;
    void *event_ctx;
} rtl_sdr_v4_esp_config_t;

typedef struct {
    rtl_sdr_v4_esp_preset_t preset;
    /** Used when preset == CUSTOM_HZ. Clamped by driver policy. */
    uint32_t frequency_hz;
    /** 960000 is the measured sustainable rate on Tab5. */
    uint32_t sample_rate_sps;
    /** 0 = continuous until stop. Else exact CU8 byte bound. */
    uint64_t max_bytes;
    /** Soft deadline for bounded capture; 0 = no wall clock limit. */
    uint32_t timeout_ms;
} rtl_sdr_v4_esp_stream_config_t;

/**
 * Create driver instance and register USB host client.
 * Does not claim the V4 until one enumerates and start() is called.
 */
esp_err_t rtl_sdr_v4_esp_install(const rtl_sdr_v4_esp_config_t *config,
                                 rtl_sdr_v4_esp_handle_t *out_handle);

/** Query last accepted Blog V4 device, if any. */
esp_err_t rtl_sdr_v4_esp_get_device_info(rtl_sdr_v4_esp_handle_t handle,
                                         rtl_sdr_v4_esp_device_info_t *out_info);

/**
 * Claim interface 0, run clean-room init, set sample rate, tune, start bulk IN.
 * Only one stream at a time per handle.
 */
esp_err_t rtl_sdr_v4_esp_start(rtl_sdr_v4_esp_handle_t handle,
                               const rtl_sdr_v4_esp_stream_config_t *stream);

/**
 * Retune while streaming. Must be called from the driver owner task context
 * or marshalled via the driver command queue — never from a USB completion ISR.
 * frequency_hz is quantized per driver policy (e.g. 1 kHz or 5 kHz).
 */
esp_err_t rtl_sdr_v4_esp_retune_hz(rtl_sdr_v4_esp_handle_t handle, uint32_t frequency_hz);

/** Request stop and wait up to timeout_ms for cleanup. */
esp_err_t rtl_sdr_v4_esp_stop(rtl_sdr_v4_esp_handle_t handle, uint32_t timeout_ms);

esp_err_t rtl_sdr_v4_esp_get_metrics(rtl_sdr_v4_esp_handle_t handle,
                                     rtl_sdr_v4_esp_metrics_t *out_metrics);

/** Tear down client; host library uninstall remains the app's job if shared. */
esp_err_t rtl_sdr_v4_esp_uninstall(rtl_sdr_v4_esp_handle_t handle);

/** Defaults for config structs. */
void rtl_sdr_v4_esp_config_default(rtl_sdr_v4_esp_config_t *config);
void rtl_sdr_v4_esp_stream_config_default(rtl_sdr_v4_esp_stream_config_t *stream);

#ifdef __cplusplus
}
#endif
