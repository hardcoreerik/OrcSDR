/*
 * RTL-SDRv4-ESP — Gate 2 streaming implementation (v0.4)
 *
 * Clean-room USB Host client: multi-URB bulk IQ, dual-core delivery ring,
 * measured EP0 tables. Not a librtlsdr port.
 *
 * Core 0: USB host lib + client/owner (events, EP0, URB submit/resubmit)
 * Core 1: IQ delivery task posts EVT_IQ_BLOCK (keep callback light!)
 * App should run demod/play at high prio on core 1 and graphics at low prio.
 */

#include "rtl_sdr_v4_esp.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <new>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#include "rtl_sdr_v4_transfers.hpp"

static const char *TAG = "rtl_sdr_v4_esp";

static constexpr uint32_t kHandleMagic = 0x52345634u;
static constexpr TickType_t kQueryLockTicks = pdMS_TO_TICKS(50);
static constexpr TickType_t kApiLockTicks = portMAX_DELAY;
static constexpr TickType_t kUninstallLockTicks = pdMS_TO_TICKS(2000);
static constexpr size_t kCtrlXferBytes = 64 + sizeof(usb_setup_packet_t);
static constexpr size_t kRingDepth = 6;
static constexpr int kUsbCore = 0;
static constexpr int kDeliveryCore = 1;
static constexpr UBaseType_t kUsbPrio = 20;
static constexpr UBaseType_t kClientPrio = 19;
/* Delivery only posts IQ; app audio task should be >= this and graphics much lower. */
static constexpr UBaseType_t kDeliveryPrio = 18;

static constexpr uint16_t kVid = RTL_SDR_V4_ESP_USB_VID;
static constexpr uint16_t kPid = RTL_SDR_V4_ESP_USB_PID;
static constexpr char kMfg[] = "RTLSDRBlog";
static constexpr char kProduct[] = "Blog V4";

static const uint32_t kAllowRates[] = {
    RTL_SDR_V4_ESP_RATE_960K,
    RTL_SDR_V4_ESP_RATE_1024K,
    RTL_SDR_V4_ESP_RATE_2048K,
};

struct IqSlot {
    uint8_t *data = nullptr;
    size_t capacity = 0;
    size_t bytes = 0;
    uint32_t sequence = 0;
    uint32_t frequency_hz = 0;
    uint32_t sample_rate_sps = 0;
    int64_t host_timestamp_us = 0;
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
    uint32_t in_callback_depth = 0;
    bool destroying = false;

    bool owns_host = false;
    bool host_installed = false;
    bool client_registered = false;
    usb_host_client_handle_t client = nullptr;
    usb_device_handle_t dev = nullptr;
    bool iface_claimed = false;
    uint8_t pending_addr = 0;
    bool device_gone = false;

    TaskHandle_t host_task = nullptr;
    TaskHandle_t client_task = nullptr;
    TaskHandle_t delivery_task = nullptr;
    volatile bool tasks_run = false;

    SemaphoreHandle_t ctrl_sem = nullptr;
    SemaphoreHandle_t ctrl_mutex = nullptr;
    usb_transfer_t *ctrl_xfer = nullptr;
    esp_err_t ctrl_status = ESP_OK;
    bool ctrl_stall = false;

    usb_transfer_t **bulk = nullptr;
    uint32_t bulk_num = 0;
    uint32_t bulk_len = 0;
    volatile bool streaming = false;
    /** Live bulk URBs currently submitted (not yet completed without resubmit). */
    volatile uint32_t live_urbs = 0;
    /** When true, bulk_cb must not resubmit (stop or retune drain). */
    volatile bool pause_resubmit = false;
    SemaphoreHandle_t bulk_done_sem = nullptr;

    IqSlot ring[kRingDepth]{};
    QueueHandle_t free_q = nullptr;
    QueueHandle_t filled_q = nullptr;
    uint32_t iq_sequence = 0;

    /** LO request; applied by retune path after bulk drain (never EP0 mid-bulk). */
    volatile uint32_t pending_retune_hz = 0;
};

/* -------------------------------------------------------------------------- */
/* RAII lock                                                                  */
/* -------------------------------------------------------------------------- */

class HandleLock {
public:
    explicit HandleLock(rtl_sdr_v4_esp_handle *h, TickType_t ticks = kApiLockTicks) : h_(h)
    {
        if (h_ == nullptr || h_->magic != kHandleMagic || h_->lock == nullptr) {
            h_ = nullptr;
            return;
        }
        if (xSemaphoreTake(h_->lock, ticks) != pdTRUE) {
            h_ = nullptr;
            timed_out_ = true;
            return;
        }
        owned_ = true;
    }
    ~HandleLock() { release(); }
    HandleLock(const HandleLock &) = delete;
    HandleLock &operator=(const HandleLock &) = delete;
    bool ok() const { return owned_ && h_ != nullptr; }
    bool timed_out() const { return timed_out_; }
    void release()
    {
        if (owned_ && h_ != nullptr && h_->lock != nullptr) {
            xSemaphoreGive(h_->lock);
        }
        owned_ = false;
        h_ = nullptr;
    }

private:
    rtl_sdr_v4_esp_handle *h_ = nullptr;
    bool owned_ = false;
    bool timed_out_ = false;
};

static bool handle_live(const rtl_sdr_v4_esp_handle *h)
{
    return h != nullptr && h->magic == kHandleMagic && h->lock != nullptr;
}

static bool handle_ok(const rtl_sdr_v4_esp_handle *h)
{
    return handle_live(h) && !h->destroying;
}

static void set_error_unlocked(rtl_sdr_v4_esp_handle *h, esp_err_t err)
{
    if (h != nullptr) {
        h->last_error = err;
        h->metrics.last_error = static_cast<uint32_t>(err);
    }
}

static esp_err_t check_not_reentrant(const rtl_sdr_v4_esp_handle *h)
{
    if (h != nullptr && h->in_callback_depth > 0) {
        return RTL_SDR_V4_ESP_ERR_REENTRANT;
    }
    return ESP_OK;
}

static uint32_t now_ms(void)
{
    return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static bool is_xfer_bytes_ok(size_t n)
{
    if (n < RTL_SDR_V4_ESP_MIN_XFER_BYTES || n > RTL_SDR_V4_ESP_MAX_XFER_BYTES) {
        return false;
    }
    return (n % 512u) == 0;
}

static void emit_after_unlock(rtl_sdr_v4_esp_handle *h,
                              rtl_sdr_v4_esp_event_t ev,
                              const void *payload,
                              rtl_sdr_v4_esp_event_cb_t cb,
                              void *ctx)
{
    if (cb == nullptr || h == nullptr) {
        return;
    }
    if (handle_live(h)) {
        HandleLock lk(h, kQueryLockTicks);
        if (lk.ok()) {
            h->in_callback_depth++;
        }
    }
    cb(ev, payload, ctx);
    if (handle_live(h)) {
        HandleLock lk(h, kQueryLockTicks);
        if (lk.ok() && h->in_callback_depth > 0) {
            h->in_callback_depth--;
        }
    }
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
    return RTL_SDR_V4_ESP_VERSION_STRING;
}

const char *rtl_sdr_v4_esp_err_to_name(esp_err_t err)
{
    switch (err) {
    case ESP_OK: return "ESP_OK";
    case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_INVALID_SIZE: return "ESP_ERR_INVALID_SIZE";
    case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
    case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
    case RTL_SDR_V4_ESP_ERR_NO_DEVICE: return "RTL_SDR_V4_ESP_ERR_NO_DEVICE";
    case RTL_SDR_V4_ESP_ERR_NOT_V4: return "RTL_SDR_V4_ESP_ERR_NOT_V4";
    case RTL_SDR_V4_ESP_ERR_BUSY: return "RTL_SDR_V4_ESP_ERR_BUSY";
    case RTL_SDR_V4_ESP_ERR_NOT_STREAMING: return "RTL_SDR_V4_ESP_ERR_NOT_STREAMING";
    case RTL_SDR_V4_ESP_ERR_BAD_RATE: return "RTL_SDR_V4_ESP_ERR_BAD_RATE";
    case RTL_SDR_V4_ESP_ERR_BAD_FREQ: return "RTL_SDR_V4_ESP_ERR_BAD_FREQ";
    case RTL_SDR_V4_ESP_ERR_USB: return "RTL_SDR_V4_ESP_ERR_USB";
    case RTL_SDR_V4_ESP_ERR_TIMEOUT: return "RTL_SDR_V4_ESP_ERR_TIMEOUT";
    case RTL_SDR_V4_ESP_ERR_FAULT: return "RTL_SDR_V4_ESP_ERR_FAULT";
    case RTL_SDR_V4_ESP_ERR_NOT_READY: return "RTL_SDR_V4_ESP_ERR_NOT_READY";
    case RTL_SDR_V4_ESP_ERR_UNSUPPORTED: return "RTL_SDR_V4_ESP_ERR_UNSUPPORTED";
    case RTL_SDR_V4_ESP_ERR_STALE_HANDLE: return "RTL_SDR_V4_ESP_ERR_STALE_HANDLE";
    case RTL_SDR_V4_ESP_ERR_REENTRANT: return "RTL_SDR_V4_ESP_ERR_REENTRANT";
    case RTL_SDR_V4_ESP_ERR_NOT_CLAIMED: return "RTL_SDR_V4_ESP_ERR_NOT_CLAIMED";
    default: return esp_err_to_name(err);
    }
}

const char *rtl_sdr_v4_esp_state_to_name(rtl_sdr_v4_esp_state_t state)
{
    switch (state) {
    case RTL_SDR_V4_ESP_STATE_UNINSTALLED: return "UNINSTALLED";
    case RTL_SDR_V4_ESP_STATE_IDLE: return "IDLE";
    case RTL_SDR_V4_ESP_STATE_STREAMING: return "STREAMING";
    case RTL_SDR_V4_ESP_STATE_STOPPING: return "STOPPING";
    case RTL_SDR_V4_ESP_STATE_FAULT: return "FAULT";
    default: return "UNKNOWN";
    }
}

uint32_t rtl_sdr_v4_esp_get_capabilities(void)
{
    return RTL_SDR_V4_ESP_CAP_STREAM | RTL_SDR_V4_ESP_CAP_RETUNE |
           RTL_SDR_V4_ESP_CAP_METRICS | RTL_SDR_V4_ESP_CAP_CUSTOM_HZ |
           RTL_SDR_V4_ESP_CAP_HOTPLUG;
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

esp_err_t rtl_sdr_v4_esp_get_supported_rates(uint32_t *out_rates, size_t max_count,
                                             size_t *out_count)
{
    const size_t total = sizeof(kAllowRates) / sizeof(kAllowRates[0]);
    if (out_count == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (max_count == 0) {
        *out_count = total;
        return ESP_OK;
    }
    if (out_rates == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t n = (max_count < total) ? max_count : total;
    for (size_t i = 0; i < n; ++i) {
        out_rates[i] = kAllowRates[i];
    }
    *out_count = total;
    return (max_count < total) ? ESP_ERR_INVALID_SIZE : ESP_OK;
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

esp_err_t rtl_sdr_v4_esp_preset_frequency_hz(rtl_sdr_v4_esp_preset_t preset, uint32_t *out_hz)
{
    if (out_hz == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (preset) {
    case RTL_SDR_V4_ESP_PRESET_KZEL_96_1:
        *out_hz = RTL_SDR_V4_ESP_PRESET_KZEL_HZ;
        return ESP_OK;
    case RTL_SDR_V4_ESP_PRESET_NOAA_162_4:
        *out_hz = RTL_SDR_V4_ESP_PRESET_NOAA_HZ;
        return ESP_OK;
    default:
        return ESP_ERR_INVALID_ARG;
    }
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
    stream->frequency_hz = RTL_SDR_V4_ESP_PRESET_KZEL_HZ;
    stream->sample_rate_sps = RTL_SDR_V4_ESP_RATE_960K;
}

esp_err_t rtl_sdr_v4_esp_config_validate(const rtl_sdr_v4_esp_config_t *config)
{
    if (config == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->struct_size != sizeof(rtl_sdr_v4_esp_config_t)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!is_xfer_bytes_ok(config->transfer_bytes)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->transfer_count < RTL_SDR_V4_ESP_MIN_XFER_COUNT ||
        config->transfer_count > RTL_SDR_V4_ESP_MAX_XFER_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->control_timeout_ms == 0 ||
        config->control_timeout_ms > RTL_SDR_V4_ESP_MAX_TIMEOUT_MS) {
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
        return ESP_ERR_INVALID_ARG;
    }
    if (stream->timeout_ms > RTL_SDR_V4_ESP_MAX_TIMEOUT_MS) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t resolve_stream_frequency(const rtl_sdr_v4_esp_stream_config_t *stream,
                                          uint32_t *out_hz)
{
    switch (stream->preset) {
    case RTL_SDR_V4_ESP_PRESET_KZEL_96_1:
        *out_hz = RTL_SDR_V4_ESP_PRESET_KZEL_HZ;
        return ESP_OK;
    case RTL_SDR_V4_ESP_PRESET_NOAA_162_4:
        *out_hz = RTL_SDR_V4_ESP_PRESET_NOAA_HZ;
        return ESP_OK;
    case RTL_SDR_V4_ESP_PRESET_CUSTOM_HZ:
        if (!rtl_sdr_v4_esp_normalize_frequency(stream->frequency_hz, out_hz)) {
            return RTL_SDR_V4_ESP_ERR_BAD_FREQ;
        }
        return ESP_OK;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

/* -------------------------------------------------------------------------- */
/* Clean-room PLL pack (measured Tab5 path)                                   */
/* -------------------------------------------------------------------------- */

static bool encode_r820_pll(uint32_t frequency_hz, uint8_t *r16_setup, uint8_t *r16_active,
                            uint8_t *r20, uint8_t *r21, uint8_t *r22)
{
    const double lo_hz = static_cast<double>(frequency_hz) + kRtlIfOffsetHz;
    static constexpr uint16_t kMixCandidates[] = {2, 4, 8, 16, 32, 64, 128, 256, 512, 1024};
    uint16_t chosen = 0;
    for (const uint16_t candidate : kMixCandidates) {
        const double vco = lo_hz * candidate;
        if (vco >= 1.77e9 && vco <= 3.90e9) {
            chosen = candidate;
            break;
        }
    }
    if (chosen == 0) {
        return false;
    }
    const double n = (lo_hz * chosen) / (2.0 * kRtlXtalHz);
    int nint = static_cast<int>(std::floor(n));
    int nfra = static_cast<int>(std::lround((n - nint) * 65536.0));
    if (nfra >= 65536) {
        ++nint;
        nfra = 0;
    }
    if (nfra < 0 || nint < 13) {
        return false;
    }
    const int packed = nint - 13;
    const int ni2c = packed >> 2;
    const int si2c = packed & 3;
    if (ni2c < 0 || ni2c > 63) {
        return false;
    }
    int mix_log = 0;
    for (uint16_t value = chosen; value > 1; value >>= 1) {
        ++mix_log;
    }
    const uint8_t active = static_cast<uint8_t>((((mix_log - 1) & 0x07) << 5) | 0x04);
    *r16_active = active;
    *r16_setup = static_cast<uint8_t>(active + 0x20);
    *r20 = static_cast<uint8_t>((si2c << 6) | ni2c);
    *r21 = static_cast<uint8_t>(nfra & 0xff);
    *r22 = static_cast<uint8_t>((nfra >> 8) & 0xff);
    return true;
}

/* -------------------------------------------------------------------------- */
/* USB control                                                                */
/* -------------------------------------------------------------------------- */

static void ctrl_cb(usb_transfer_t *xfer)
{
    auto *h = static_cast<rtl_sdr_v4_esp_handle *>(xfer->context);
    if (h == nullptr) {
        return;
    }
    h->ctrl_status = (xfer->status == USB_TRANSFER_STATUS_COMPLETED) ? ESP_OK : ESP_FAIL;
    h->ctrl_stall = (xfer->status == USB_TRANSFER_STATUS_STALL);
    xSemaphoreGive(h->ctrl_sem);
}

static esp_err_t ctrl_submit(rtl_sdr_v4_esp_handle *h, uint8_t bm, uint8_t bRequest,
                             uint16_t wValue, uint16_t wIndex, const uint8_t *data,
                             uint16_t wLength, bool expect_stall)
{
    if (h->ctrl_xfer == nullptr || h->dev == nullptr) {
        return RTL_SDR_V4_ESP_ERR_USB;
    }
    xSemaphoreTake(h->ctrl_mutex, portMAX_DELAY);

    esp_err_t final_err = ESP_FAIL;
    for (int attempt = 0; attempt < 2; ++attempt) {
        usb_transfer_t *x = h->ctrl_xfer;
        auto *setup = reinterpret_cast<usb_setup_packet_t *>(x->data_buffer);
        setup->bmRequestType = bm;
        setup->bRequest = bRequest;
        setup->wValue = wValue;
        setup->wIndex = wIndex;
        setup->wLength = wLength;
        if ((bm & USB_BM_REQUEST_TYPE_DIR_IN) == 0 && wLength > 0 && data != nullptr) {
            std::memcpy(x->data_buffer + sizeof(usb_setup_packet_t), data, wLength);
        }
        x->num_bytes = sizeof(usb_setup_packet_t) + wLength;
        x->device_handle = h->dev;
        x->bEndpointAddress = 0;
        x->callback = ctrl_cb;
        x->context = h;
        x->timeout_ms = h->cfg.control_timeout_ms;

        h->ctrl_status = ESP_FAIL;
        h->ctrl_stall = false;
        xSemaphoreTake(h->ctrl_sem, 0);

        esp_err_t ret = usb_host_transfer_submit_control(h->client, x);
        if (ret != ESP_OK) {
            final_err = RTL_SDR_V4_ESP_ERR_USB;
            break;
        }
        if (xSemaphoreTake(h->ctrl_sem, pdMS_TO_TICKS(h->cfg.control_timeout_ms + 200)) !=
            pdTRUE) {
            final_err = RTL_SDR_V4_ESP_ERR_TIMEOUT;
            break;
        }
        if (h->ctrl_status == ESP_OK) {
            final_err = ESP_OK;
            break;
        }
        if (h->ctrl_stall) {
            if (expect_stall) {
                final_err = ESP_OK;
                break;
            }
            /* V4 EP0 STALL: yield for USBH recovery; retry once */
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        final_err = RTL_SDR_V4_ESP_ERR_USB;
        break;
    }

    xSemaphoreGive(h->ctrl_mutex);
    return final_err;
}

static esp_err_t run_record(rtl_sdr_v4_esp_handle *h, const RtlControlRecord &rec,
                            bool expect_stall)
{
    return ctrl_submit(h, rec.request_type, 0, rec.value, rec.index, rec.data, rec.length,
                       expect_stall);
}

static esp_err_t run_init_table(rtl_sdr_v4_esp_handle *h)
{
    for (size_t i = 0; i < std::size(kRtlInitTransfers); ++i) {
        const bool stall = i >= kRtlInitExpectedStallFirst && i <= kRtlInitExpectedStallLast;
        esp_err_t e = run_record(h, kRtlInitTransfers[i], stall);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "init record %u failed: %s", static_cast<unsigned>(i),
                     rtl_sdr_v4_esp_err_to_name(e));
            return e;
        }
    }
    return ESP_OK;
}

static esp_err_t run_sample_rate(rtl_sdr_v4_esp_handle *h, uint32_t sample_rate_sps)
{
    constexpr uint64_t kRtlClockHz = 28800000ull;
    uint32_t ratio = static_cast<uint32_t>((kRtlClockHz << 22) / sample_rate_sps);
    ratio &= 0x0ffffffcu;
    for (size_t i = kRtlSampleRateFirst; i <= kRtlSampleRateLast; ++i) {
        RtlControlRecord rec = kRtlInitTransfers[i];
        if (i == kRtlSampleRateRatioHighIndex) {
            rec.data[0] = static_cast<uint8_t>(ratio >> 24);
            rec.data[1] = static_cast<uint8_t>(ratio >> 16);
        } else if (i == kRtlSampleRateRatioLowIndex) {
            rec.data[0] = static_cast<uint8_t>(ratio >> 8);
            rec.data[1] = static_cast<uint8_t>(ratio);
        }
        esp_err_t e = run_record(h, rec, false);
        if (e != ESP_OK) {
            return e;
        }
    }
    return ESP_OK;
}

static esp_err_t run_tune(rtl_sdr_v4_esp_handle *h, uint32_t frequency_hz)
{
    uint8_t r16_setup = 0, r16_active = 0, r20 = 0, r21 = 0, r22 = 0;
    if (!encode_r820_pll(frequency_hz, &r16_setup, &r16_active, &r20, &r21, &r22)) {
        return RTL_SDR_V4_ESP_ERR_BAD_FREQ;
    }
    ESP_LOGI(TAG, "tune %u Hz r16=%02x/%02x r20=%02x r21=%02x r22=%02x",
             static_cast<unsigned>(frequency_hz), r16_setup, r16_active, r20, r21, r22);
    for (size_t i = 0; i < std::size(kRtlFinalTuneTemplate); ++i) {
        RtlControlRecord rec = kRtlFinalTuneTemplate[i];
        if (i == 3 || i == 7) {
            rec.data[1] = r16_setup;
        }
        if (i == 12) {
            rec.data[1] = r16_active;
        }
        if (i == 13) {
            rec.data[1] = r20;
        }
        if (i == 15) {
            rec.data[1] = r22;
        }
        if (i == 16) {
            rec.data[1] = r21;
        }
        esp_err_t e = run_record(h, rec, false);
        if (e != ESP_OK) {
            return e;
        }
    }
    return ESP_OK;
}

static esp_err_t run_uhf_frontend(rtl_sdr_v4_esp_handle *h)
{
    constexpr RtlControlRecord kUhf[] = {
        {0x0074, 0x0610, 0x40, 2, {0x17, 0x28}},
        {0x0074, 0x0610, 0x40, 2, {0x1a, 0x68}},
        {0x0074, 0x0610, 0x40, 2, {0x1b, 0x00}},
        {0x0074, 0x0610, 0x40, 2, {0x05, 0x83}},
        {0x0074, 0x0610, 0x40, 2, {0x0c, 0x6b}},
    };
    for (const auto &record : kUhf) {
        esp_err_t err = run_record(h, record, false);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static void run_cleanup_best_effort(rtl_sdr_v4_esp_handle *h)
{
    for (const auto &rec : kRtlCleanupTransfers) {
        (void)run_record(h, rec, true);
    }
}

/* -------------------------------------------------------------------------- */
/* Bulk + ring                                                                */
/* -------------------------------------------------------------------------- */

static void bulk_cb(usb_transfer_t *xfer)
{
    auto *h = static_cast<rtl_sdr_v4_esp_handle *>(xfer->context);
    if (h == nullptr) {
        return;
    }

    if (xfer->status == USB_TRANSFER_STATUS_COMPLETED && xfer->actual_num_bytes > 0 &&
        h->streaming && !h->pause_resubmit) {
        IqSlot *slot = nullptr;
        if (xQueueReceive(h->free_q, &slot, 0) == pdTRUE && slot != nullptr) {
            const size_t n = static_cast<size_t>(xfer->actual_num_bytes);
            const size_t copy = (n <= slot->capacity) ? n : slot->capacity;
            std::memcpy(slot->data, xfer->data_buffer, copy);
            slot->bytes = copy;
            slot->sequence = ++h->iq_sequence;
            slot->frequency_hz = h->frequency_hz;
            slot->sample_rate_sps = h->sample_rate_sps;
            slot->host_timestamp_us = esp_timer_get_time();
            if (xQueueSend(h->filled_q, &slot, 0) != pdTRUE) {
                (void)xQueueSend(h->free_q, &slot, 0);
                h->metrics.overruns++;
            } else {
                h->metrics.bytes_total += copy;
                h->metrics.blocks_total++;
                if (copy > 0) {
                    uint8_t mn = 255, mx = 0;
                    for (size_t i = 0; i < copy; i += 64) {
                        const uint8_t v = slot->data[i];
                        if (v < mn) {
                            mn = v;
                        }
                        if (v > mx) {
                            mx = v;
                        }
                    }
                    if (h->metrics.blocks_total == 1) {
                        h->metrics.sample_min = mn;
                        h->metrics.sample_max = mx;
                    } else {
                        if (mn < h->metrics.sample_min) {
                            h->metrics.sample_min = mn;
                        }
                        if (mx > h->metrics.sample_max) {
                            h->metrics.sample_max = mx;
                        }
                    }
                }
            }
        } else {
            h->metrics.overruns++;
            h->metrics.consumer_drops++;
        }
    } else if (xfer->status != USB_TRANSFER_STATUS_CANCELED &&
               xfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGW(TAG, "bulk status=%d bytes=%d", xfer->status, xfer->actual_num_bytes);
    }

    /* Resubmit only while streaming and not draining for stop/retune. */
    if (h->streaming && !h->pause_resubmit) {
        esp_err_t ret = usb_host_transfer_submit(xfer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "bulk resubmit failed: %s", esp_err_to_name(ret));
            h->streaming = false;
            if (h->live_urbs > 0) {
                h->live_urbs--;
            }
            xSemaphoreGive(h->bulk_done_sem);
        }
        /* still in flight after successful resubmit */
    } else {
        if (h->live_urbs > 0) {
            h->live_urbs--;
        }
        xSemaphoreGive(h->bulk_done_sem);
    }
}

/** Drain outstanding bulks (no resubmit), apply LO, resubmit. Must NOT run on USB client task. */
static esp_err_t apply_pending_retune(rtl_sdr_v4_esp_handle *h)
{
    if (h == nullptr || !h->streaming) {
        return RTL_SDR_V4_ESP_ERR_NOT_STREAMING;
    }
    const uint32_t freq = h->pending_retune_hz;
    if (freq == 0) {
        return ESP_OK;
    }

    h->pause_resubmit = true;

    /* Wait until all live URBs complete without resubmit (safe EP0 window). */
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(800);
    while (h->live_urbs > 0 && xTaskGetTickCount() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (h->live_urbs > 0 && h->dev != nullptr) {
        usb_host_endpoint_halt(h->dev, RTL_SDR_V4_ESP_BULK_EP_IN);
        usb_host_endpoint_flush(h->dev, RTL_SDR_V4_ESP_BULK_EP_IN);
        usb_host_endpoint_clear(h->dev, RTL_SDR_V4_ESP_BULK_EP_IN);
        const TickType_t d2 = xTaskGetTickCount() + pdMS_TO_TICKS(300);
        while (h->live_urbs > 0 && xTaskGetTickCount() < d2) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        h->live_urbs = 0;
    }

    if (!h->streaming) {
        h->pause_resubmit = false;
        h->pending_retune_hz = 0;
        return RTL_SDR_V4_ESP_ERR_NOT_STREAMING;
    }

    esp_err_t err = run_tune(h, freq);
    if (err == ESP_OK) {
        h->frequency_hz = freq;
        h->metrics.frequency_hz = freq;
        h->pending_retune_hz = 0;
        ESP_LOGI(TAG, "hot retune applied %u Hz", static_cast<unsigned>(freq));
    } else {
        ESP_LOGW(TAG, "hot retune EP0 failed: %s (keep LO)", rtl_sdr_v4_esp_err_to_name(err));
        h->pending_retune_hz = 0;
    }

    /* Resume multi-URB stream */
    h->pause_resubmit = false;
    if (h->streaming && h->bulk != nullptr) {
        h->live_urbs = 0;
        for (uint32_t i = 0; i < h->bulk_num; ++i) {
            if (h->bulk[i] == nullptr) {
                continue;
            }
            h->bulk[i]->device_handle = h->dev;
            h->bulk[i]->bEndpointAddress = RTL_SDR_V4_ESP_BULK_EP_IN;
            h->bulk[i]->num_bytes = h->bulk_len;
            h->bulk[i]->callback = bulk_cb;
            h->bulk[i]->context = h;
            if (usb_host_transfer_submit(h->bulk[i]) == ESP_OK) {
                h->live_urbs++;
            }
        }
    }

    if (err == ESP_OK) {
        rtl_sdr_v4_esp_event_cb_t cb = h->cfg.event_cb;
        void *ctx = h->cfg.event_ctx;
        if (cb != nullptr) {
            uint32_t f = h->frequency_hz;
            emit_after_unlock(h, RTL_SDR_V4_ESP_EVT_RETUNED, &f, cb, ctx);
        }
    }
    return err;
}

static void delivery_task_fn(void *arg)
{
    auto *h = static_cast<rtl_sdr_v4_esp_handle *>(arg);
    while (h->tasks_run) {
        IqSlot *slot = nullptr;
        if (xQueueReceive(h->filled_q, &slot, pdMS_TO_TICKS(50)) != pdTRUE || slot == nullptr) {
            continue;
        }
        rtl_sdr_v4_esp_iq_block_t block{};
        block.data = slot->data;
        block.bytes = slot->bytes;
        block.sequence = slot->sequence;
        block.frequency_hz = slot->frequency_hz;
        block.sample_rate_sps = slot->sample_rate_sps;
        block.host_timestamp_us = slot->host_timestamp_us;

        rtl_sdr_v4_esp_event_cb_t cb = nullptr;
        void *ctx = nullptr;
        {
            HandleLock lk(h, kQueryLockTicks);
            if (lk.ok()) {
                cb = h->cfg.event_cb;
                ctx = h->cfg.event_ctx;
            }
        }
        if (cb != nullptr) {
            emit_after_unlock(h, RTL_SDR_V4_ESP_EVT_IQ_BLOCK, &block, cb, ctx);
        }
        (void)xQueueSend(h->free_q, &slot, portMAX_DELAY);
    }
    vTaskDelete(nullptr);
}

static void free_bulk_pool(rtl_sdr_v4_esp_handle *h)
{
    if (h->bulk != nullptr) {
        for (uint32_t i = 0; i < h->bulk_num; ++i) {
            if (h->bulk[i] != nullptr) {
                usb_host_transfer_free(h->bulk[i]);
                h->bulk[i] = nullptr;
            }
        }
        free(h->bulk);
        h->bulk = nullptr;
    }
    h->bulk_num = 0;
}

static esp_err_t alloc_bulk_pool(rtl_sdr_v4_esp_handle *h, uint32_t num, uint32_t len)
{
    free_bulk_pool(h);
    h->bulk = static_cast<usb_transfer_t **>(calloc(num, sizeof(usb_transfer_t *)));
    if (h->bulk == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    h->bulk_num = num;
    h->bulk_len = len;
    for (uint32_t i = 0; i < num; ++i) {
        esp_err_t ret = usb_host_transfer_alloc(len, 0, &h->bulk[i]);
        if (ret != ESP_OK) {
            free_bulk_pool(h);
            return ret;
        }
        h->bulk[i]->device_handle = h->dev;
        h->bulk[i]->bEndpointAddress = RTL_SDR_V4_ESP_BULK_EP_IN;
        h->bulk[i]->num_bytes = len;
        h->bulk[i]->callback = bulk_cb;
        h->bulk[i]->context = h;
    }
    return ESP_OK;
}

static esp_err_t ensure_ring(rtl_sdr_v4_esp_handle *h, size_t slot_bytes)
{
    if (h->free_q != nullptr) {
        return ESP_OK;
    }
    h->free_q = xQueueCreate(kRingDepth, sizeof(IqSlot *));
    h->filled_q = xQueueCreate(kRingDepth, sizeof(IqSlot *));
    if (h->free_q == nullptr || h->filled_q == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < kRingDepth; ++i) {
        h->ring[i].capacity = slot_bytes;
        h->ring[i].data = static_cast<uint8_t *>(
            heap_caps_malloc(slot_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (h->ring[i].data == nullptr) {
            h->ring[i].data =
                static_cast<uint8_t *>(heap_caps_malloc(slot_bytes, MALLOC_CAP_INTERNAL));
        }
        if (h->ring[i].data == nullptr) {
            return ESP_ERR_NO_MEM;
        }
        IqSlot *p = &h->ring[i];
        xQueueSend(h->free_q, &p, 0);
    }
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* USB client / host tasks                                                    */
/* -------------------------------------------------------------------------- */

static void str_desc_ascii(const usb_str_desc_t *d, char *out, size_t out_sz)
{
    if (out_sz == 0) {
        return;
    }
    if (d == nullptr) {
        out[0] = '\0';
        return;
    }
    const size_t nchars = (d->bLength > 2) ? (d->bLength - 2) / 2 : 0;
    const size_t n = (nchars < out_sz - 1) ? nchars : out_sz - 1;
    for (size_t i = 0; i < n; ++i) {
        const uint16_t v = d->wData[i];
        out[i] = (v >= 32 && v <= 126) ? static_cast<char>(v) : '?';
    }
    out[n] = '\0';
}

static bool accept_v4(const usb_device_desc_t *dd, const usb_device_info_t *info,
                      rtl_sdr_v4_esp_device_info_t *out)
{
    if (dd->idVendor != kVid || dd->idProduct != kPid) {
        return false;
    }
    char mfg[48]{}, prod[48]{}, ser[32]{};
    str_desc_ascii(info->str_desc_manufacturer, mfg, sizeof(mfg));
    str_desc_ascii(info->str_desc_product, prod, sizeof(prod));
    str_desc_ascii(info->str_desc_serial_num, ser, sizeof(ser));
    if (std::strcmp(mfg, kMfg) != 0 || std::strcmp(prod, kProduct) != 0) {
        return false;
    }
    out->vid = dd->idVendor;
    out->pid = dd->idProduct;
    out->high_speed = (info->speed == USB_SPEED_HIGH);
    out->present = true;
    std::snprintf(out->manufacturer, sizeof(out->manufacturer), "%s", mfg);
    std::snprintf(out->product, sizeof(out->product), "%s", prod);
    std::snprintf(out->serial, sizeof(out->serial), "%s", ser);
    return true;
}

static void try_open_device(rtl_sdr_v4_esp_handle *h, uint8_t addr)
{
    if (h->dev != nullptr) {
        return;
    }
    usb_device_handle_t dev = nullptr;
    if (usb_host_device_open(h->client, addr, &dev) != ESP_OK) {
        return;
    }
    const usb_device_desc_t *dd = nullptr;
    usb_device_info_t info{};
    if (usb_host_get_device_descriptor(dev, &dd) != ESP_OK ||
        usb_host_device_info(dev, &info) != ESP_OK) {
        usb_host_device_close(h->client, dev);
        return;
    }
    rtl_sdr_v4_esp_device_info_t di{};
    if (!accept_v4(dd, &info, &di)) {
        ESP_LOGW(TAG, "reject USB %04x:%04x", dd->idVendor, dd->idProduct);
        usb_host_device_close(h->client, dev);
        return;
    }
    h->dev = dev;
    h->info = di;
    ESP_LOGI(TAG, "V4 open %s %s serial=%s hs=%d", di.manufacturer, di.product, di.serial,
             static_cast<int>(di.high_speed));

    rtl_sdr_v4_esp_event_cb_t cb = h->cfg.event_cb;
    void *ctx = h->cfg.event_ctx;
    if (cb) {
        emit_after_unlock(h, RTL_SDR_V4_ESP_EVT_ENUMERATED, &h->info, cb, ctx);
        emit_after_unlock(h, RTL_SDR_V4_ESP_EVT_READY, nullptr, cb, ctx);
    }
}

static void client_event_cb(const usb_host_client_event_msg_t *event, void *arg)
{
    auto *h = static_cast<rtl_sdr_v4_esp_handle *>(arg);
    if (h == nullptr || event == nullptr) {
        return;
    }
    if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        h->pending_addr = event->new_dev.address;
    } else if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE &&
               event->dev_gone.dev_hdl == h->dev) {
        h->device_gone = true;
    }
}

static void host_lib_task_fn(void *arg)
{
    auto *h = static_cast<rtl_sdr_v4_esp_handle *>(arg);
    while (h->tasks_run) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(pdMS_TO_TICKS(100), &flags);
    }
    vTaskDelete(nullptr);
}

static void client_task_fn(void *arg)
{
    auto *h = static_cast<rtl_sdr_v4_esp_handle *>(arg);
    while (h->tasks_run) {
        usb_host_client_handle_events(h->client, pdMS_TO_TICKS(20));
        if (h->pending_addr != 0) {
            const uint8_t a = h->pending_addr;
            h->pending_addr = 0;
            try_open_device(h, a);
        }
        if (h->device_gone) {
            h->device_gone = false;
            h->streaming = false;
            if (h->iface_claimed && h->dev != nullptr) {
                usb_host_interface_release(h->client, h->dev, 0);
                h->iface_claimed = false;
            }
            if (h->dev != nullptr) {
                usb_host_device_close(h->client, h->dev);
                h->dev = nullptr;
            }
            h->info.present = false;
            h->state = RTL_SDR_V4_ESP_STATE_IDLE;
            rtl_sdr_v4_esp_event_cb_t cb = h->cfg.event_cb;
            void *ctx = h->cfg.event_ctx;
            if (cb) {
                emit_after_unlock(h, RTL_SDR_V4_ESP_EVT_DISCONNECTED, nullptr, cb, ctx);
            }
        }
    }
    vTaskDelete(nullptr);
}

static esp_err_t start_usb_stack(rtl_sdr_v4_esp_handle *h)
{
    h->tasks_run = true;
    h->owns_host = !h->cfg.host_library_already_installed;

    if (h->owns_host) {
        usb_host_config_t hc{};
        hc.intr_flags = ESP_INTR_FLAG_LEVEL1;
        /* Tab5 path: peripheral_map 0 selects default HS controller on P4 */
        hc.peripheral_map = 0;
        esp_err_t ret = usb_host_install(&hc);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "usb_host_install: %s", esp_err_to_name(ret));
            return ret;
        }
        h->host_installed = true;
        if (xTaskCreatePinnedToCore(host_lib_task_fn, "rtl_usb_lib", 4096, h, kUsbPrio,
                                    &h->host_task, kUsbCore) != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }

    usb_host_client_config_t cc{};
    cc.is_synchronous = false;
    cc.max_num_event_msg = 8;
    cc.async.client_event_callback = client_event_cb;
    cc.async.callback_arg = h;
    esp_err_t ret = usb_host_client_register(&cc, &h->client);
    if (ret != ESP_OK) {
        return ret;
    }
    h->client_registered = true;

    const UBaseType_t prio =
        h->cfg.usb_task_priority ? h->cfg.usb_task_priority : kClientPrio;
    const BaseType_t core =
        (h->cfg.usb_task_core_id == 0xFF) ? kUsbCore : h->cfg.usb_task_core_id;
    if (xTaskCreatePinnedToCore(client_task_fn, "rtl_usb_cli", 6144, h, prio, &h->client_task,
                                core) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    /* Scan already-attached devices */
    uint8_t addrs[8];
    int n = 0;
    if (usb_host_device_addr_list_fill(sizeof(addrs), addrs, &n) == ESP_OK) {
        for (int i = 0; i < n; ++i) {
            try_open_device(h, addrs[i]);
            if (h->dev != nullptr) {
                break;
            }
        }
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
    h->ctrl_sem = xSemaphoreCreateBinary();
    h->ctrl_mutex = xSemaphoreCreateMutex();
    h->bulk_done_sem = xSemaphoreCreateCounting(RTL_SDR_V4_ESP_MAX_XFER_COUNT, 0);
    if (h->lock == nullptr || h->ctrl_sem == nullptr || h->ctrl_mutex == nullptr ||
        h->bulk_done_sem == nullptr) {
        delete h;
        return ESP_ERR_NO_MEM;
    }

    h->magic = kHandleMagic;
    h->cfg = *config;
    h->state = RTL_SDR_V4_ESP_STATE_IDLE;
    h->info.vid = kVid;
    h->info.pid = kPid;
    std::snprintf(h->info.manufacturer, sizeof(h->info.manufacturer), "%s", kMfg);
    std::snprintf(h->info.product, sizeof(h->info.product), "%s", kProduct);

    if (usb_host_transfer_alloc(kCtrlXferBytes, 0, &h->ctrl_xfer) != ESP_OK) {
        h->magic = 0;
        delete h;
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = start_usb_stack(h);
    if (ret != ESP_OK) {
        rtl_sdr_v4_esp_uninstall(h);
        return ret;
    }

    ESP_LOGI(TAG, "install v%s caps=0x%08x xfer=%ux%u", rtl_sdr_v4_esp_get_version_string(),
             static_cast<unsigned>(rtl_sdr_v4_esp_get_capabilities()),
             static_cast<unsigned>(config->transfer_count),
             static_cast<unsigned>(config->transfer_bytes));
    *out_handle = h;
    return ESP_OK;
}

static esp_err_t stop_stream_internal(rtl_sdr_v4_esp_handle *h, uint32_t timeout_ms);

esp_err_t rtl_sdr_v4_esp_uninstall(rtl_sdr_v4_esp_handle_t handle)
{
    if (handle == nullptr) {
        return ESP_OK;
    }
    if (handle->magic != kHandleMagic || handle->lock == nullptr) {
        return RTL_SDR_V4_ESP_ERR_STALE_HANDLE;
    }
    {
        HandleLock lk(handle, kUninstallLockTicks);
        if (!lk.ok()) {
            return RTL_SDR_V4_ESP_ERR_TIMEOUT;
        }
        if (handle->destroying) {
            return RTL_SDR_V4_ESP_ERR_BUSY;
        }
        if (handle->in_callback_depth > 0) {
            return RTL_SDR_V4_ESP_ERR_REENTRANT;
        }
        handle->destroying = true;
    }

    (void)stop_stream_internal(handle, RTL_SDR_V4_ESP_DEFAULT_STOP_TIMEOUT_MS);
    handle->tasks_run = false;
    vTaskDelay(pdMS_TO_TICKS(50));

    if (handle->iface_claimed && handle->dev != nullptr) {
        usb_host_interface_release(handle->client, handle->dev, 0);
        handle->iface_claimed = false;
    }
    if (handle->dev != nullptr) {
        usb_host_device_close(handle->client, handle->dev);
        handle->dev = nullptr;
    }
    if (handle->client_registered) {
        usb_host_client_deregister(handle->client);
        handle->client_registered = false;
    }
    if (handle->owns_host && handle->host_installed) {
        usb_host_uninstall();
        handle->host_installed = false;
    }

    free_bulk_pool(handle);
    if (handle->ctrl_xfer) {
        usb_host_transfer_free(handle->ctrl_xfer);
        handle->ctrl_xfer = nullptr;
    }
    for (size_t i = 0; i < kRingDepth; ++i) {
        free(handle->ring[i].data);
        handle->ring[i].data = nullptr;
    }
    if (handle->free_q) {
        vQueueDelete(handle->free_q);
    }
    if (handle->filled_q) {
        vQueueDelete(handle->filled_q);
    }

    HandleLock lk(handle, kUninstallLockTicks);
    handle->magic = 0;
    handle->state = RTL_SDR_V4_ESP_STATE_UNINSTALLED;
    SemaphoreHandle_t lock = handle->lock;
    handle->lock = nullptr;
    lk.release();
    if (handle->ctrl_sem) {
        vSemaphoreDelete(handle->ctrl_sem);
    }
    if (handle->ctrl_mutex) {
        vSemaphoreDelete(handle->ctrl_mutex);
    }
    if (handle->bulk_done_sem) {
        vSemaphoreDelete(handle->bulk_done_sem);
    }
    if (lock) {
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
    HandleLock lk(handle, kQueryLockTicks);
    if (!lk.ok()) {
        return RTL_SDR_V4_ESP_STATE_FAULT;
    }
    return handle->state;
}

esp_err_t rtl_sdr_v4_esp_get_last_error(rtl_sdr_v4_esp_handle_t handle)
{
    if (!handle_ok(handle)) {
        return RTL_SDR_V4_ESP_ERR_STALE_HANDLE;
    }
    HandleLock lk(handle, kQueryLockTicks);
    if (!lk.ok()) {
        return RTL_SDR_V4_ESP_ERR_TIMEOUT;
    }
    return handle->last_error;
}

esp_err_t rtl_sdr_v4_esp_get_device_info(rtl_sdr_v4_esp_handle_t handle,
                                         rtl_sdr_v4_esp_device_info_t *out_info)
{
    if (out_info == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!handle_ok(handle)) {
        return RTL_SDR_V4_ESP_ERR_STALE_HANDLE;
    }
    HandleLock lk(handle, kQueryLockTicks);
    if (!lk.ok()) {
        return RTL_SDR_V4_ESP_ERR_TIMEOUT;
    }
    *out_info = handle->info;
    return ESP_OK;
}

esp_err_t rtl_sdr_v4_esp_get_metrics(rtl_sdr_v4_esp_handle_t handle,
                                     rtl_sdr_v4_esp_metrics_t *out_metrics)
{
    if (out_metrics == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!handle_ok(handle)) {
        return RTL_SDR_V4_ESP_ERR_STALE_HANDLE;
    }
    HandleLock lk(handle, kQueryLockTicks);
    if (!lk.ok()) {
        return RTL_SDR_V4_ESP_ERR_TIMEOUT;
    }
    *out_metrics = handle->metrics;
    out_metrics->frequency_hz = handle->frequency_hz;
    out_metrics->sample_rate_sps = handle->sample_rate_sps;
    if (handle->state == RTL_SDR_V4_ESP_STATE_STREAMING && handle->stream_start_ms != 0) {
        out_metrics->uptime_ms = now_ms() - handle->stream_start_ms;
        if (out_metrics->uptime_ms > 0) {
            out_metrics->effective_sps = static_cast<uint32_t>(
                (handle->metrics.bytes_total * 500ull) / out_metrics->uptime_ms);
        }
    }
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Streaming                                                                  */
/* -------------------------------------------------------------------------- */

static esp_err_t stop_stream_internal(rtl_sdr_v4_esp_handle *h, uint32_t timeout_ms)
{
    if (timeout_ms == 0) {
        timeout_ms = RTL_SDR_V4_ESP_DEFAULT_STOP_TIMEOUT_MS;
    }
    if (timeout_ms > RTL_SDR_V4_ESP_MAX_TIMEOUT_MS) {
        timeout_ms = RTL_SDR_V4_ESP_MAX_TIMEOUT_MS;
    }

    const bool was_streaming = h->streaming || h->state == RTL_SDR_V4_ESP_STATE_STREAMING ||
                               h->state == RTL_SDR_V4_ESP_STATE_STOPPING;
    h->state = RTL_SDR_V4_ESP_STATE_STOPPING;
    h->pause_resubmit = true;
    h->streaming = false;
    h->pending_retune_hz = 0;

    if (h->dev != nullptr && h->bulk_num > 0) {
        usb_host_endpoint_halt(h->dev, RTL_SDR_V4_ESP_BULK_EP_IN);
        usb_host_endpoint_flush(h->dev, RTL_SDR_V4_ESP_BULK_EP_IN);
        usb_host_endpoint_clear(h->dev, RTL_SDR_V4_ESP_BULK_EP_IN);
    }

    /* Drain completion callbacks (bounded). */
    const uint32_t need = h->bulk_num > 0 ? h->bulk_num : 1;
    const TickType_t slice = pdMS_TO_TICKS(timeout_ms / need + 20);
    for (uint32_t i = 0; i < need; ++i) {
        (void)xSemaphoreTake(h->bulk_done_sem, slice);
    }
    h->live_urbs = 0;
    h->pause_resubmit = false;

    if (h->iface_claimed && h->dev != nullptr) {
        run_cleanup_best_effort(h);
        usb_host_interface_release(h->client, h->dev, 0);
        h->iface_claimed = false;
    }

    free_bulk_pool(h);
    h->stream_start_ms = 0;
    h->state = RTL_SDR_V4_ESP_STATE_IDLE;
    set_error_unlocked(h, ESP_OK);

    if (was_streaming && !h->destroying) {
        rtl_sdr_v4_esp_event_cb_t cb = h->cfg.event_cb;
        void *ctx = h->cfg.event_ctx;
        if (cb) {
            emit_after_unlock(h, RTL_SDR_V4_ESP_EVT_STOPPED, nullptr, cb, ctx);
        }
    }
    return ESP_OK;
}

esp_err_t rtl_sdr_v4_esp_start(rtl_sdr_v4_esp_handle_t handle,
                               const rtl_sdr_v4_esp_stream_config_t *stream)
{
    if (stream == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!handle_ok(handle)) {
        return RTL_SDR_V4_ESP_ERR_STALE_HANDLE;
    }

    esp_err_t verr = rtl_sdr_v4_esp_stream_config_validate(stream);
    if (verr != ESP_OK) {
        HandleLock lk(handle, kQueryLockTicks);
        if (lk.ok()) {
            set_error_unlocked(handle, verr);
        }
        return verr;
    }
    uint32_t freq = 0;
    verr = resolve_stream_frequency(stream, &freq);
    if (verr != ESP_OK) {
        return verr;
    }

    HandleLock lk(handle);
    if (!lk.ok()) {
        return RTL_SDR_V4_ESP_ERR_TIMEOUT;
    }
    esp_err_t re = check_not_reentrant(handle);
    if (re != ESP_OK) {
        set_error_unlocked(handle, re);
        return re;
    }
    if (handle->state == RTL_SDR_V4_ESP_STATE_FAULT) {
        set_error_unlocked(handle, RTL_SDR_V4_ESP_ERR_FAULT);
        return RTL_SDR_V4_ESP_ERR_FAULT;
    }
    if (handle->state == RTL_SDR_V4_ESP_STATE_STREAMING ||
        handle->state == RTL_SDR_V4_ESP_STATE_STOPPING) {
        set_error_unlocked(handle, RTL_SDR_V4_ESP_ERR_BUSY);
        return RTL_SDR_V4_ESP_ERR_BUSY;
    }
    if (handle->dev == nullptr || !handle->info.present) {
        set_error_unlocked(handle, RTL_SDR_V4_ESP_ERR_NO_DEVICE);
        return RTL_SDR_V4_ESP_ERR_NO_DEVICE;
    }

    /* USB work without holding API mutex across long EP0 sequences */
    lk.release();

    esp_err_t ret = ESP_OK;
    do {
        ret = usb_host_interface_claim(handle->client, handle->dev, 0, 0);
        if (ret != ESP_OK) {
            ret = RTL_SDR_V4_ESP_ERR_USB;
            break;
        }
        handle->iface_claimed = true;

        ret = run_init_table(handle);
        if (ret != ESP_OK) {
            break;
        }
        ret = run_sample_rate(handle, stream->sample_rate_sps);
        if (ret != ESP_OK) {
            break;
        }
        ret = run_tune(handle, freq);
        if (ret != ESP_OK) {
            break;
        }
        if (freq >= 300000000u) {
            ret = run_uhf_frontend(handle);
            if (ret != ESP_OK) {
                break;
            }
        }

        ret = ensure_ring(handle, handle->cfg.transfer_bytes);
        if (ret != ESP_OK) {
            break;
        }
        ret = alloc_bulk_pool(handle, static_cast<uint32_t>(handle->cfg.transfer_count),
                              static_cast<uint32_t>(handle->cfg.transfer_bytes));
        if (ret != ESP_OK) {
            break;
        }

        if (handle->delivery_task == nullptr) {
            handle->tasks_run = true;
            if (xTaskCreatePinnedToCore(delivery_task_fn, "rtl_iq_del", 6144, handle,
                                        kDeliveryPrio, &handle->delivery_task,
                                        kDeliveryCore) != pdPASS) {
                ret = ESP_ERR_NO_MEM;
                break;
            }
        }

        handle->frequency_hz = freq;
        handle->sample_rate_sps = stream->sample_rate_sps;
        handle->metrics.frequency_hz = freq;
        handle->metrics.sample_rate_sps = stream->sample_rate_sps;
        handle->metrics.bytes_total = 0;
        handle->metrics.blocks_total = 0;
        handle->metrics.overruns = 0;
        handle->metrics.consumer_drops = 0;
        handle->stream_start_ms = now_ms();
        handle->pending_retune_hz = 0;
        handle->pause_resubmit = false;
        handle->live_urbs = 0;
        handle->streaming = true;
        handle->state = RTL_SDR_V4_ESP_STATE_STREAMING;

        for (uint32_t i = 0; i < handle->bulk_num; ++i) {
            ret = usb_host_transfer_submit(handle->bulk[i]);
            if (ret != ESP_OK) {
                handle->streaming = false;
                ret = RTL_SDR_V4_ESP_ERR_USB;
                break;
            }
            handle->live_urbs++;
        }
        if (ret != ESP_OK) {
            break;
        }

        HandleLock lk2(handle);
        if (lk2.ok()) {
            set_error_unlocked(handle, ESP_OK);
        }
        rtl_sdr_v4_esp_event_cb_t cb = handle->cfg.event_cb;
        void *ctx = handle->cfg.event_ctx;
        if (cb) {
            emit_after_unlock(handle, RTL_SDR_V4_ESP_EVT_STREAM_STARTED, nullptr, cb, ctx);
        }
        ESP_LOGI(TAG, "stream start freq=%u rate=%u urbs=%ux%u", static_cast<unsigned>(freq),
                 static_cast<unsigned>(stream->sample_rate_sps),
                 static_cast<unsigned>(handle->bulk_num),
                 static_cast<unsigned>(handle->bulk_len));
        return ESP_OK;
    } while (0);

    (void)stop_stream_internal(handle, 1000);
    HandleLock lk3(handle);
    if (lk3.ok()) {
        set_error_unlocked(handle, ret);
        if (ret == RTL_SDR_V4_ESP_ERR_USB) {
            handle->state = RTL_SDR_V4_ESP_STATE_FAULT;
        } else {
            handle->state = RTL_SDR_V4_ESP_STATE_IDLE;
        }
    }
    return ret;
}

esp_err_t rtl_sdr_v4_esp_retune_hz(rtl_sdr_v4_esp_handle_t handle, uint32_t frequency_hz)
{
    if (!handle_ok(handle)) {
        return RTL_SDR_V4_ESP_ERR_STALE_HANDLE;
    }
    uint32_t q = 0;
    if (!rtl_sdr_v4_esp_normalize_frequency(frequency_hz, &q)) {
        return RTL_SDR_V4_ESP_ERR_BAD_FREQ;
    }

    /*
     * Queue is always safe (including from event callback). EP0 apply must not
     * run on the USB client task or nested under in_callback_depth — so if we
     * are re-entered, only queue and return ESP_OK; app/owner will apply.
     */
    {
        HandleLock lk(handle, kQueryLockTicks);
        if (!lk.ok()) {
            return RTL_SDR_V4_ESP_ERR_TIMEOUT;
        }
        if (handle->state != RTL_SDR_V4_ESP_STATE_STREAMING || !handle->streaming) {
            set_error_unlocked(handle, RTL_SDR_V4_ESP_ERR_NOT_STREAMING);
            return RTL_SDR_V4_ESP_ERR_NOT_STREAMING;
        }
        handle->pending_retune_hz = q;
        if (handle->in_callback_depth > 0) {
            /* Coalesce; delivery/app task must call again or service pending. */
            set_error_unlocked(handle, ESP_OK);
            return ESP_OK;
        }
    }

    /* Apply outside lock: drains bulks, EP0 tune, resubmits. */
    return apply_pending_retune(handle);
}

esp_err_t rtl_sdr_v4_esp_stop(rtl_sdr_v4_esp_handle_t handle, uint32_t timeout_ms)
{
    if (handle == nullptr) {
        return ESP_OK;
    }
    if (!handle_live(handle)) {
        return RTL_SDR_V4_ESP_ERR_STALE_HANDLE;
    }
    {
        HandleLock lk(handle);
        if (!lk.ok()) {
            return RTL_SDR_V4_ESP_ERR_TIMEOUT;
        }
        if (!handle->destroying) {
            esp_err_t re = check_not_reentrant(handle);
            if (re != ESP_OK) {
                set_error_unlocked(handle, re);
                return re;
            }
        }
        if (handle->state == RTL_SDR_V4_ESP_STATE_IDLE ||
            handle->state == RTL_SDR_V4_ESP_STATE_UNINSTALLED) {
            return ESP_OK;
        }
    }
    return stop_stream_internal(handle, timeout_ms);
}

esp_err_t rtl_sdr_v4_esp_reset(rtl_sdr_v4_esp_handle_t handle)
{
    if (!handle_ok(handle)) {
        return RTL_SDR_V4_ESP_ERR_STALE_HANDLE;
    }
    HandleLock lk(handle);
    if (!lk.ok()) {
        return RTL_SDR_V4_ESP_ERR_TIMEOUT;
    }
    if (handle->state == RTL_SDR_V4_ESP_STATE_STREAMING ||
        handle->state == RTL_SDR_V4_ESP_STATE_STOPPING) {
        set_error_unlocked(handle, RTL_SDR_V4_ESP_ERR_BUSY);
        return RTL_SDR_V4_ESP_ERR_BUSY;
    }
    handle->state = RTL_SDR_V4_ESP_STATE_IDLE;
    std::memset(&handle->metrics, 0, sizeof(handle->metrics));
    set_error_unlocked(handle, ESP_OK);
    return ESP_OK;
}

esp_err_t rtl_sdr_v4_esp_release_iq_block(rtl_sdr_v4_esp_handle_t handle,
                                          const rtl_sdr_v4_esp_iq_block_t *block)
{
    if (block == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)handle;
    return ESP_OK;
}
