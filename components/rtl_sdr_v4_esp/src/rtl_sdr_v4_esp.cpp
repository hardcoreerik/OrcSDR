/*
 * RTL-SDRv4-ESP — hardened public API implementation
 *
 * Stream USB path still gated (NOT_FINISHED / UNSUPPORTED) until extraction
 * completes. Lifecycle, validation, state, locking, and errors are production
 * quality so apps can integrate against a stable contract now.
 */

#include "rtl_sdr_v4_esp.h"

#include <cstdio>
#include <cstring>
#include <new>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "rtl_sdr_v4_transfers.hpp"

static const char *TAG = "rtl_sdr_v4_esp";

static constexpr uint32_t kHandleMagic = 0x52345634u; /* 'R4V4' ish */
static constexpr uint32_t kDefaultStopTimeoutMs = 3000;

static constexpr uint16_t kVid = RTL_SDR_V4_ESP_USB_VID;
static constexpr uint16_t kPid = RTL_SDR_V4_ESP_USB_PID;
static constexpr char kMfg[] = "RTLSDRBlog";
static constexpr char kProduct[] = "Blog V4";

static const uint32_t kAllowRates[] = {
    RTL_SDR_V4_ESP_RATE_960K,
    RTL_SDR_V4_ESP_RATE_1024K,
    RTL_SDR_V4_ESP_RATE_2048K,
};

struct rtl_sdr_v4_esp_handle {
    uint32_t magic = 0;
    SemaphoreHandle_t lock = nullptr;
    rtl_sdr_v4_esp_config_t cfg{};
    rtl_sdr_v4_esp_device_info_t info{};
    rtl_sdr_v4_esp_metrics_t metrics{};
    rtl_sdr_v4_esp_state_t state = RTL_SDR_V4_ESP_STATE_UNINSTALLED;
    esp_err_t last_error = ESP_OK;
    uint32_t frequency_hz = 0;
    uint32_t sample_rate_sps = 0;
    uint32_t stream_start_ms = 0;
};

/* -------------------------------------------------------------------------- */
/* Internal helpers                                                           */
/* -------------------------------------------------------------------------- */

static void set_error(rtl_sdr_v4_esp_handle *h, esp_err_t err)
{
    if (h != nullptr) {
        h->last_error = err;
        h->metrics.last_error = static_cast<uint32_t>(err);
    }
}

static bool handle_ok(const rtl_sdr_v4_esp_handle *h)
{
    return h != nullptr && h->magic == kHandleMagic && h->lock != nullptr;
}

static bool lock_handle(rtl_sdr_v4_esp_handle *h, TickType_t ticks = portMAX_DELAY)
{
    if (!handle_ok(h)) {
        return false;
    }
    return xSemaphoreTake(h->lock, ticks) == pdTRUE;
}

static void unlock_handle(rtl_sdr_v4_esp_handle *h)
{
    if (h != nullptr && h->lock != nullptr) {
        xSemaphoreGive(h->lock);
    }
}

static void emit_event(rtl_sdr_v4_esp_handle *h, rtl_sdr_v4_esp_event_t ev, const void *payload)
{
    if (h == nullptr || h->cfg.event_cb == nullptr) {
        return;
    }
    /* Callback outside lock would be ideal; for skeleton keep simple and document. */
    h->cfg.event_cb(ev, payload, h->cfg.event_ctx);
}

static bool is_xfer_bytes_ok(size_t n)
{
    if (n < RTL_SDR_V4_ESP_MIN_XFER_BYTES || n > RTL_SDR_V4_ESP_MAX_XFER_BYTES) {
        return false;
    }
    /* HS bulk max packet 512; require multiple of 512. */
    return (n % 512u) == 0;
}

/* -------------------------------------------------------------------------- */
/* Version / errors / capabilities                                            */
/* -------------------------------------------------------------------------- */

uint32_t rtl_sdr_v4_esp_get_version(void)
{
    return (static_cast<uint32_t>(RTL_SDR_V4_ESP_VERSION_MAJOR) << 16) |
           (static_cast<uint32_t>(RTL_SDR_V4_ESP_VERSION_MINOR) << 8) |
           static_cast<uint32_t>(RTL_SDR_V4_ESP_VERSION_PATCH);
}

const char *rtl_sdr_v4_esp_get_version_string(void)
{
    return "0.2.0";
}

const char *rtl_sdr_v4_esp_err_to_name(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return "ESP_OK";
    case ESP_ERR_INVALID_ARG:
        return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE:
        return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_NO_MEM:
        return "ESP_ERR_NO_MEM";
    case ESP_ERR_NOT_FINISHED:
        return "ESP_ERR_NOT_FINISHED";
    case ESP_ERR_TIMEOUT:
        return "ESP_ERR_TIMEOUT";
    case RTL_SDR_V4_ESP_ERR_NO_DEVICE:
        return "RTL_SDR_V4_ESP_ERR_NO_DEVICE";
    case RTL_SDR_V4_ESP_ERR_NOT_V4:
        return "RTL_SDR_V4_ESP_ERR_NOT_V4";
    case RTL_SDR_V4_ESP_ERR_BUSY:
        return "RTL_SDR_V4_ESP_ERR_BUSY";
    case RTL_SDR_V4_ESP_ERR_NOT_STREAMING:
        return "RTL_SDR_V4_ESP_ERR_NOT_STREAMING";
    case RTL_SDR_V4_ESP_ERR_BAD_RATE:
        return "RTL_SDR_V4_ESP_ERR_BAD_RATE";
    case RTL_SDR_V4_ESP_ERR_BAD_FREQ:
        return "RTL_SDR_V4_ESP_ERR_BAD_FREQ";
    case RTL_SDR_V4_ESP_ERR_USB:
        return "RTL_SDR_V4_ESP_ERR_USB";
    case RTL_SDR_V4_ESP_ERR_TIMEOUT:
        return "RTL_SDR_V4_ESP_ERR_TIMEOUT";
    case RTL_SDR_V4_ESP_ERR_FAULT:
        return "RTL_SDR_V4_ESP_ERR_FAULT";
    case RTL_SDR_V4_ESP_ERR_NOT_READY:
        return "RTL_SDR_V4_ESP_ERR_NOT_READY";
    case RTL_SDR_V4_ESP_ERR_UNSUPPORTED:
        return "RTL_SDR_V4_ESP_ERR_UNSUPPORTED";
    case RTL_SDR_V4_ESP_ERR_STALE_HANDLE:
        return "RTL_SDR_V4_ESP_ERR_STALE_HANDLE";
    default:
        return esp_err_to_name(err);
    }
}

uint32_t rtl_sdr_v4_esp_get_capabilities(void)
{
    /* STREAM/RETUNE bits reserved until USB extraction lands. */
    return RTL_SDR_V4_ESP_CAP_METRICS | RTL_SDR_V4_ESP_CAP_CUSTOM_HZ;
}

bool rtl_sdr_v4_esp_is_rate_supported(uint32_t sample_rate_sps)
{
    for (uint32_t r : kAllowRates) {
        if (r == sample_rate_sps) {
            return true;
        }
    }
    return false;
}

bool rtl_sdr_v4_esp_normalize_frequency(uint32_t in_hz, uint32_t *out_hz)
{
    if (out_hz == nullptr) {
        return false;
    }
    if (in_hz < RTL_SDR_V4_ESP_FREQ_MIN_HZ || in_hz > RTL_SDR_V4_ESP_FREQ_MAX_HZ) {
        return false;
    }
    uint32_t q = (in_hz / RTL_SDR_V4_ESP_FREQ_QUANT_HZ) * RTL_SDR_V4_ESP_FREQ_QUANT_HZ;
    if (q < RTL_SDR_V4_ESP_FREQ_MIN_HZ) {
        q = RTL_SDR_V4_ESP_FREQ_MIN_HZ;
    }
    if (q > RTL_SDR_V4_ESP_FREQ_MAX_HZ) {
        q = RTL_SDR_V4_ESP_FREQ_MAX_HZ;
    }
    *out_hz = q;
    return true;
}

/* -------------------------------------------------------------------------- */
/* Config                                                                     */
/* -------------------------------------------------------------------------- */

void rtl_sdr_v4_esp_config_default(rtl_sdr_v4_esp_config_t *config)
{
    if (config == nullptr) {
        return;
    }
    std::memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(rtl_sdr_v4_esp_config_t);
    config->host_library_already_installed = false;
    config->transfer_bytes = RTL_SDR_V4_ESP_DEFAULT_XFER_BYTES;
    config->transfer_count = RTL_SDR_V4_ESP_DEFAULT_XFER_COUNT;
    config->control_timeout_ms = 1000;
    config->event_cb = nullptr;
    config->event_ctx = nullptr;
    config->iq_acquire_mode = false;
    config->usb_task_priority = 0;
    config->usb_task_core_id = 0xFF;
}

void rtl_sdr_v4_esp_stream_config_default(rtl_sdr_v4_esp_stream_config_t *stream)
{
    if (stream == nullptr) {
        return;
    }
    std::memset(stream, 0, sizeof(*stream));
    stream->struct_size = sizeof(rtl_sdr_v4_esp_stream_config_t);
    stream->preset = RTL_SDR_V4_ESP_PRESET_KZEL_96_1;
    stream->frequency_hz = 96100000;
    stream->sample_rate_sps = RTL_SDR_V4_ESP_RATE_960K;
    stream->max_bytes = 0;
    stream->timeout_ms = 0;
}

esp_err_t rtl_sdr_v4_esp_config_validate(const rtl_sdr_v4_esp_config_t *config)
{
    if (config == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->struct_size != sizeof(rtl_sdr_v4_esp_config_t)) {
        ESP_LOGE(TAG, "config.struct_size mismatch (got %u want %u)",
                 static_cast<unsigned>(config->struct_size),
                 static_cast<unsigned>(sizeof(rtl_sdr_v4_esp_config_t)));
        return ESP_ERR_INVALID_ARG;
    }
    if (!is_xfer_bytes_ok(config->transfer_bytes)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->transfer_count < RTL_SDR_V4_ESP_MIN_XFER_COUNT ||
        config->transfer_count > RTL_SDR_V4_ESP_MAX_XFER_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->control_timeout_ms == 0 || config->control_timeout_ms > 30000) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->usb_task_core_id != 0xFF && config->usb_task_core_id > 1) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t rtl_sdr_v4_esp_stream_config_validate(const rtl_sdr_v4_esp_stream_config_t *stream)
{
    if (stream == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (stream->struct_size != sizeof(rtl_sdr_v4_esp_stream_config_t)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (stream->preset > RTL_SDR_V4_ESP_PRESET_CUSTOM_HZ) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!rtl_sdr_v4_esp_is_rate_supported(stream->sample_rate_sps)) {
        return RTL_SDR_V4_ESP_ERR_BAD_RATE;
    }
    if (stream->preset == RTL_SDR_V4_ESP_PRESET_CUSTOM_HZ) {
        uint32_t q = 0;
        if (!rtl_sdr_v4_esp_normalize_frequency(stream->frequency_hz, &q)) {
            return RTL_SDR_V4_ESP_ERR_BAD_FREQ;
        }
    }
    if (stream->max_bytes != 0 && (stream->max_bytes % 2u) != 0) {
        return ESP_ERR_INVALID_ARG; /* CU8 IQ pairs */
    }
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* -------------------------------------------------------------------------- */

esp_err_t rtl_sdr_v4_esp_install(const rtl_sdr_v4_esp_config_t *config,
                                 rtl_sdr_v4_esp_handle_t *out_handle)
{
    if (out_handle != nullptr) {
        *out_handle = nullptr;
    }
    if (config == nullptr || out_handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t verr = rtl_sdr_v4_esp_config_validate(config);
    if (verr != ESP_OK) {
        return verr;
    }

    auto *h = new (std::nothrow) rtl_sdr_v4_esp_handle();
    if (h == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    h->lock = xSemaphoreCreateMutex();
    if (h->lock == nullptr) {
        delete h;
        return ESP_ERR_NO_MEM;
    }

    h->magic = kHandleMagic;
    h->cfg = *config;
    h->state = RTL_SDR_V4_ESP_STATE_IDLE;
    h->last_error = ESP_OK;
    h->info.vid = kVid;
    h->info.pid = kPid;
    h->info.present = false;
    h->info.high_speed = true; /* expected when attached on P4 HS */
    std::snprintf(h->info.manufacturer, sizeof(h->info.manufacturer), "%s", kMfg);
    std::snprintf(h->info.product, sizeof(h->info.product), "%s", kProduct);
    std::memset(&h->metrics, 0, sizeof(h->metrics));

    ESP_LOGI(TAG,
             "install v%s ok. init_records=%u cleanup_records=%u "
             "accept %04x:%04x \"%s\"/\"%s\" xfer=%u x %u",
             rtl_sdr_v4_esp_get_version_string(),
             static_cast<unsigned>(std::size(kRtlInitTransfers)),
             static_cast<unsigned>(std::size(kRtlCleanupTransfers)), kVid, kPid, kMfg,
             kProduct, static_cast<unsigned>(config->transfer_count),
             static_cast<unsigned>(config->transfer_bytes));

    *out_handle = h;
    return ESP_OK;
}

esp_err_t rtl_sdr_v4_esp_uninstall(rtl_sdr_v4_esp_handle_t handle)
{
    if (handle == nullptr) {
        return ESP_OK; /* idempotent */
    }
    if (!handle_ok(handle)) {
        return RTL_SDR_V4_ESP_ERR_STALE_HANDLE;
    }

    /* stop without holding lock across delete */
    (void)rtl_sdr_v4_esp_stop(handle, kDefaultStopTimeoutMs);

    if (!lock_handle(handle, pdMS_TO_TICKS(1000))) {
        /* Still destroy to avoid leaks if lock stuck. */
        ESP_LOGW(TAG, "uninstall: lock timeout, forcing destroy");
    }

    handle->magic = 0;
    handle->state = RTL_SDR_V4_ESP_STATE_UNINSTALLED;
    SemaphoreHandle_t lock = handle->lock;
    handle->lock = nullptr;
    unlock_handle(handle); /* may no-op if we failed take — ok if we hold it */

    if (lock != nullptr) {
        /* Ensure we own it before delete */
        (void)xSemaphoreTake(lock, 0);
        vSemaphoreDelete(lock);
    }
    delete handle;
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Queries                                                                    */
/* -------------------------------------------------------------------------- */

rtl_sdr_v4_esp_state_t rtl_sdr_v4_esp_get_state(rtl_sdr_v4_esp_handle_t handle)
{
    if (!handle_ok(handle)) {
        return RTL_SDR_V4_ESP_STATE_UNINSTALLED;
    }
    if (!lock_handle(handle, pdMS_TO_TICKS(50))) {
        return RTL_SDR_V4_ESP_STATE_FAULT;
    }
    rtl_sdr_v4_esp_state_t s = handle->state;
    unlock_handle(handle);
    return s;
}

esp_err_t rtl_sdr_v4_esp_get_last_error(rtl_sdr_v4_esp_handle_t handle)
{
    if (!handle_ok(handle)) {
        return RTL_SDR_V4_ESP_ERR_STALE_HANDLE;
    }
    if (!lock_handle(handle, pdMS_TO_TICKS(50))) {
        return RTL_SDR_V4_ESP_ERR_TIMEOUT;
    }
    esp_err_t e = handle->last_error;
    unlock_handle(handle);
    return e;
}

esp_err_t rtl_sdr_v4_esp_get_device_info(rtl_sdr_v4_esp_handle_t handle,
                                         rtl_sdr_v4_esp_device_info_t *out_info)
{
    if (!handle_ok(handle) || out_info == nullptr) {
        return handle_ok(handle) ? ESP_ERR_INVALID_ARG : RTL_SDR_V4_ESP_ERR_STALE_HANDLE;
    }
    if (!lock_handle(handle)) {
        return RTL_SDR_V4_ESP_ERR_TIMEOUT;
    }
    *out_info = handle->info;
    unlock_handle(handle);
    return ESP_OK;
}

esp_err_t rtl_sdr_v4_esp_get_metrics(rtl_sdr_v4_esp_handle_t handle,
                                     rtl_sdr_v4_esp_metrics_t *out_metrics)
{
    if (!handle_ok(handle) || out_metrics == nullptr) {
        return handle_ok(handle) ? ESP_ERR_INVALID_ARG : RTL_SDR_V4_ESP_ERR_STALE_HANDLE;
    }
    if (!lock_handle(handle)) {
        return RTL_SDR_V4_ESP_ERR_TIMEOUT;
    }
    *out_metrics = handle->metrics;
    if (handle->state == RTL_SDR_V4_ESP_STATE_STREAMING && handle->stream_start_ms != 0) {
        out_metrics->uptime_ms = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS) -
                                 handle->stream_start_ms;
    }
    unlock_handle(handle);
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Streaming                                                                  */
/* -------------------------------------------------------------------------- */

esp_err_t rtl_sdr_v4_esp_start(rtl_sdr_v4_esp_handle_t handle,
                               const rtl_sdr_v4_esp_stream_config_t *stream)
{
    if (!handle_ok(handle) || stream == nullptr) {
        return handle_ok(handle) ? ESP_ERR_INVALID_ARG : RTL_SDR_V4_ESP_ERR_STALE_HANDLE;
    }

    esp_err_t verr = rtl_sdr_v4_esp_stream_config_validate(stream);
    if (verr != ESP_OK) {
        set_error(handle, verr);
        return verr;
    }

    if (!lock_handle(handle)) {
        return RTL_SDR_V4_ESP_ERR_TIMEOUT;
    }

    if (handle->state == RTL_SDR_V4_ESP_STATE_FAULT) {
        set_error(handle, RTL_SDR_V4_ESP_ERR_FAULT);
        unlock_handle(handle);
        return RTL_SDR_V4_ESP_ERR_FAULT;
    }
    if (handle->state == RTL_SDR_V4_ESP_STATE_STREAMING ||
        handle->state == RTL_SDR_V4_ESP_STATE_STOPPING) {
        set_error(handle, RTL_SDR_V4_ESP_ERR_BUSY);
        unlock_handle(handle);
        return RTL_SDR_V4_ESP_ERR_BUSY;
    }
    if (handle->state != RTL_SDR_V4_ESP_STATE_IDLE) {
        set_error(handle, ESP_ERR_INVALID_STATE);
        unlock_handle(handle);
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t freq = stream->frequency_hz;
    switch (stream->preset) {
    case RTL_SDR_V4_ESP_PRESET_KZEL_96_1:
        freq = 96100000;
        break;
    case RTL_SDR_V4_ESP_PRESET_NOAA_162_4:
        freq = 162400000;
        break;
    case RTL_SDR_V4_ESP_PRESET_CUSTOM_HZ:
        if (!rtl_sdr_v4_esp_normalize_frequency(stream->frequency_hz, &freq)) {
            set_error(handle, RTL_SDR_V4_ESP_ERR_BAD_FREQ);
            unlock_handle(handle);
            return RTL_SDR_V4_ESP_ERR_BAD_FREQ;
        }
        break;
    default:
        set_error(handle, ESP_ERR_INVALID_ARG);
        unlock_handle(handle);
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * USB stream extraction incomplete. Refuse with a stable error so apps can
     * feature-detect via capabilities + this code, and never leave half-open USB.
     */
    ESP_LOGW(TAG,
             "start() not fully implemented yet (extract bulk path). "
             "preset=%d freq=%u rate=%u tables init=%u cleanup=%u stalls=%u..%u",
             static_cast<int>(stream->preset), static_cast<unsigned>(freq),
             static_cast<unsigned>(stream->sample_rate_sps),
             static_cast<unsigned>(std::size(kRtlInitTransfers)),
             static_cast<unsigned>(std::size(kRtlCleanupTransfers)),
             static_cast<unsigned>(kRtlInitExpectedStallFirst),
             static_cast<unsigned>(kRtlInitExpectedStallLast));

    handle->frequency_hz = freq;
    handle->sample_rate_sps = stream->sample_rate_sps;
    set_error(handle, RTL_SDR_V4_ESP_ERR_UNSUPPORTED);
    unlock_handle(handle);
    return RTL_SDR_V4_ESP_ERR_UNSUPPORTED;
}

esp_err_t rtl_sdr_v4_esp_retune_hz(rtl_sdr_v4_esp_handle_t handle, uint32_t frequency_hz)
{
    if (!handle_ok(handle)) {
        return RTL_SDR_V4_ESP_ERR_STALE_HANDLE;
    }

    uint32_t q = 0;
    if (!rtl_sdr_v4_esp_normalize_frequency(frequency_hz, &q)) {
        set_error(handle, RTL_SDR_V4_ESP_ERR_BAD_FREQ);
        return RTL_SDR_V4_ESP_ERR_BAD_FREQ;
    }

    if (!lock_handle(handle)) {
        return RTL_SDR_V4_ESP_ERR_TIMEOUT;
    }
    if (handle->state != RTL_SDR_V4_ESP_STATE_STREAMING) {
        set_error(handle, RTL_SDR_V4_ESP_ERR_NOT_STREAMING);
        unlock_handle(handle);
        return RTL_SDR_V4_ESP_ERR_NOT_STREAMING;
    }

    /* Queued retune path lands with stream extraction. */
    set_error(handle, RTL_SDR_V4_ESP_ERR_UNSUPPORTED);
    unlock_handle(handle);
    return RTL_SDR_V4_ESP_ERR_UNSUPPORTED;
}

esp_err_t rtl_sdr_v4_esp_stop(rtl_sdr_v4_esp_handle_t handle, uint32_t timeout_ms)
{
    if (handle == nullptr) {
        return ESP_OK;
    }
    if (!handle_ok(handle)) {
        return RTL_SDR_V4_ESP_ERR_STALE_HANDLE;
    }
    (void)timeout_ms;

    if (!lock_handle(handle)) {
        return RTL_SDR_V4_ESP_ERR_TIMEOUT;
    }

    if (handle->state == RTL_SDR_V4_ESP_STATE_IDLE ||
        handle->state == RTL_SDR_V4_ESP_STATE_UNINSTALLED) {
        unlock_handle(handle);
        return ESP_OK; /* idempotent */
    }

    if (handle->state == RTL_SDR_V4_ESP_STATE_STREAMING ||
        handle->state == RTL_SDR_V4_ESP_STATE_STOPPING ||
        handle->state == RTL_SDR_V4_ESP_STATE_FAULT) {
        handle->state = RTL_SDR_V4_ESP_STATE_STOPPING;
        /* Future: wait for USB cleanup up to timeout_ms */
        handle->state = RTL_SDR_V4_ESP_STATE_IDLE;
        handle->stream_start_ms = 0;
        set_error(handle, ESP_OK);
        unlock_handle(handle);
        emit_event(handle, RTL_SDR_V4_ESP_EVT_STOPPED, nullptr);
        return ESP_OK;
    }

    unlock_handle(handle);
    return ESP_OK;
}

esp_err_t rtl_sdr_v4_esp_reset(rtl_sdr_v4_esp_handle_t handle)
{
    if (!handle_ok(handle)) {
        return RTL_SDR_V4_ESP_ERR_STALE_HANDLE;
    }
    if (!lock_handle(handle)) {
        return RTL_SDR_V4_ESP_ERR_TIMEOUT;
    }
    if (handle->state == RTL_SDR_V4_ESP_STATE_STREAMING ||
        handle->state == RTL_SDR_V4_ESP_STATE_STOPPING) {
        set_error(handle, RTL_SDR_V4_ESP_ERR_BUSY);
        unlock_handle(handle);
        return RTL_SDR_V4_ESP_ERR_BUSY;
    }
    handle->state = RTL_SDR_V4_ESP_STATE_IDLE;
    set_error(handle, ESP_OK);
    std::memset(&handle->metrics, 0, sizeof(handle->metrics));
    unlock_handle(handle);
    return ESP_OK;
}
