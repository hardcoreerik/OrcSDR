/*
 * RTL-SDRv4-ESP — component skeleton
 *
 * Full USB host stream logic is being extracted from the measured OrcSDR Tab5
 * application (formerly OrcLink firmware/m5tab5/ui). This file provides:
 *   - install/uninstall handle lifecycle
 *   - config defaults
 *   - identity filter constants
 *   - hooks for the clean-room transfer tables
 *
 * Streaming start/stop currently returns ESP_ERR_NOT_FINISHED until the
 * extraction gate in docs/PORTING.md is complete. Apps may still build and
 * link against this component.
 */

#include "rtl_sdr_v4_esp.h"

#include <cstdlib>
#include <cstring>

#include "esp_check.h"
#include "esp_log.h"

// Clean-room EP0 tables (C++ constexpr). Private to this component.
#include "rtl_sdr_v4_transfers.hpp"

#include <cstdio>

static const char *TAG = "rtl_sdr_v4_esp";

// Official Blog V4 identity filter (measured).
static constexpr uint16_t kVid = 0x0bda;
static constexpr uint16_t kPid = 0x2838;
static constexpr char kMfg[] = "RTLSDRBlog";
static constexpr char kProduct[] = "Blog V4";

struct rtl_sdr_v4_esp_handle {
    rtl_sdr_v4_esp_config_t cfg;
    rtl_sdr_v4_esp_device_info_t info{};
    rtl_sdr_v4_esp_metrics_t metrics{};
    bool installed = false;
    bool streaming = false;
    uint32_t frequency_hz = 0;
};

void rtl_sdr_v4_esp_config_default(rtl_sdr_v4_esp_config_t *config)
{
    if (config == nullptr) {
        return;
    }
    std::memset(config, 0, sizeof(*config));
    config->host_library_already_installed = false;
    config->transfer_bytes = 32768;
    config->transfer_count = 3;
    config->control_timeout_ms = 1000;
}

void rtl_sdr_v4_esp_stream_config_default(rtl_sdr_v4_esp_stream_config_t *stream)
{
    if (stream == nullptr) {
        return;
    }
    std::memset(stream, 0, sizeof(*stream));
    stream->preset = RTL_SDR_V4_ESP_PRESET_KZEL_96_1;
    stream->frequency_hz = 96100000;
    stream->sample_rate_sps = 960000;
    stream->max_bytes = 0;
    stream->timeout_ms = 0;
}

esp_err_t rtl_sdr_v4_esp_install(const rtl_sdr_v4_esp_config_t *config,
                                 rtl_sdr_v4_esp_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(config != nullptr && out_handle != nullptr, ESP_ERR_INVALID_ARG, TAG,
                        "null arg");
    ESP_RETURN_ON_FALSE(config->transfer_bytes >= 512, ESP_ERR_INVALID_ARG, TAG,
                        "transfer_bytes");
    ESP_RETURN_ON_FALSE(config->transfer_count >= 2, ESP_ERR_INVALID_ARG, TAG,
                        "transfer_count");

    auto *h = new (std::nothrow) rtl_sdr_v4_esp_handle();
    ESP_RETURN_ON_FALSE(h != nullptr, ESP_ERR_NO_MEM, TAG, "handle");
    h->cfg = *config;
    h->installed = true;
    h->info.vid = kVid;
    h->info.pid = kPid;
    std::snprintf(h->info.manufacturer, sizeof(h->info.manufacturer), "%s", kMfg);
    std::snprintf(h->info.product, sizeof(h->info.product), "%s", kProduct);

    // Prove private tables are linked (sizes used only for log).
    ESP_LOGI(TAG,
             "install ok (skeleton). init_records=%u cleanup_records=%u "
             "accept only %04x:%04x %s %s",
             static_cast<unsigned>(std::size(kRtlInitTransfers)),
             static_cast<unsigned>(std::size(kRtlCleanupTransfers)), kVid, kPid, kMfg,
             kProduct);
    ESP_LOGW(TAG, "stream path not extracted yet — rtl_sdr_v4_esp_start() returns NOT_FINISHED");

    *out_handle = h;
    return ESP_OK;
}

esp_err_t rtl_sdr_v4_esp_get_device_info(rtl_sdr_v4_esp_handle_t handle,
                                         rtl_sdr_v4_esp_device_info_t *out_info)
{
    ESP_RETURN_ON_FALSE(handle != nullptr && out_info != nullptr, ESP_ERR_INVALID_ARG, TAG,
                        "null arg");
    ESP_RETURN_ON_FALSE(handle->installed, ESP_ERR_INVALID_STATE, TAG, "not installed");
    *out_info = handle->info;
    return ESP_OK;
}

esp_err_t rtl_sdr_v4_esp_start(rtl_sdr_v4_esp_handle_t handle,
                               const rtl_sdr_v4_esp_stream_config_t *stream)
{
    ESP_RETURN_ON_FALSE(handle != nullptr && stream != nullptr, ESP_ERR_INVALID_ARG, TAG,
                        "null arg");
    ESP_RETURN_ON_FALSE(handle->installed, ESP_ERR_INVALID_STATE, TAG, "not installed");
    ESP_RETURN_ON_FALSE(!handle->streaming, ESP_ERR_INVALID_STATE, TAG, "already streaming");

    // Extraction gate: full claim/init/tune/bulk lives in apps/orcsdr-tab5 today.
    ESP_LOGW(TAG,
             "start deferred: extract USB path from apps/orcsdr-tab5 (see docs/PORTING.md). "
             "preset=%d freq=%u rate=%u init_stalls=%u..%u",
             static_cast<int>(stream->preset), static_cast<unsigned>(stream->frequency_hz),
             static_cast<unsigned>(stream->sample_rate_sps),
             static_cast<unsigned>(kRtlInitExpectedStallFirst),
             static_cast<unsigned>(kRtlInitExpectedStallLast));
    return ESP_ERR_NOT_FINISHED;
}

esp_err_t rtl_sdr_v4_esp_retune_hz(rtl_sdr_v4_esp_handle_t handle, uint32_t frequency_hz)
{
    ESP_RETURN_ON_FALSE(handle != nullptr, ESP_ERR_INVALID_ARG, TAG, "null handle");
    ESP_RETURN_ON_FALSE(handle->streaming, ESP_ERR_INVALID_STATE, TAG, "not streaming");
    ESP_LOGW(TAG, "retune_hz(%u) not extracted yet", static_cast<unsigned>(frequency_hz));
    return ESP_ERR_NOT_FINISHED;
}

esp_err_t rtl_sdr_v4_esp_stop(rtl_sdr_v4_esp_handle_t handle, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(handle != nullptr, ESP_ERR_INVALID_ARG, TAG, "null handle");
    (void)timeout_ms;
    handle->streaming = false;
    return ESP_OK;
}

esp_err_t rtl_sdr_v4_esp_get_metrics(rtl_sdr_v4_esp_handle_t handle,
                                     rtl_sdr_v4_esp_metrics_t *out_metrics)
{
    ESP_RETURN_ON_FALSE(handle != nullptr && out_metrics != nullptr, ESP_ERR_INVALID_ARG, TAG,
                        "null arg");
    *out_metrics = handle->metrics;
    return ESP_OK;
}

esp_err_t rtl_sdr_v4_esp_uninstall(rtl_sdr_v4_esp_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != nullptr, ESP_ERR_INVALID_ARG, TAG, "null handle");
    handle->streaming = false;
    handle->installed = false;
    delete handle;
    return ESP_OK;
}
