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
static constexpr char kProductV3[] = "Blog V3";
static constexpr char kMfgGeneric[] = "Realtek";
static constexpr char kProductGeneric[] = "RTL2838UHIDIR";

/*
 * Blog V3 / R820T2 low-IF.
 *
 * Keep this separate from the V4 clean-room offset.  The V3 tuner and the
 * RTL2832U DDC must use exactly the same IF or the displayed/decoded RF
 * frequency will be shifted.
 */
static constexpr double kV3R820IfHz = 3570000.0;

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
    volatile bool v3_probe_pending = false;
    uint8_t v3_probe_addr = 0;

    /* Blog V3 / R820T2 software shadow, registers 0x05..0x1f. */
    uint8_t v3_r820_regs[0x20 - 0x05]{};
    bool v3_r820_shadow_valid = false;

    TaskHandle_t host_task = nullptr;
    TaskHandle_t client_task = nullptr;
    TaskHandle_t v3_probe_task = nullptr;
    TaskHandle_t delivery_task = nullptr;
    volatile bool tasks_run = false;

    SemaphoreHandle_t ctrl_sem = nullptr;
    SemaphoreHandle_t ctrl_mutex = nullptr;
    usb_transfer_t *ctrl_xfer = nullptr;
    esp_err_t ctrl_status = ESP_OK;
    bool ctrl_stall = false;
    volatile bool ctrl_inflight = false;

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

    /* Blog V3 IQ conditioning state. */
    /*
     * First-order complex DC blocker state:
     *   y[n] = x[n] - x[n-1] + R*y[n-1]
     *
     * Applied only to Blog V3 copied IQ blocks before they are delivered to
     * spectrum/demod/RDS.  It removes the fixed zero-Hz/DC spike without
     * changing the RF/IF/DDC programming.
     */
    float v3_dc_x1_i = 0.0f;
    float v3_dc_x1_q = 0.0f;
    float v3_dc_y1_i = 0.0f;
    float v3_dc_y1_q = 0.0f;
    bool v3_dc_valid = false;
    uint32_t v3_dc_frequency_hz = 0;
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
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    if (h != nullptr &&
        (current == h->delivery_task || current == h->client_task || current == h->host_task)) {
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

static bool encode_r820_pll_with_if(uint32_t frequency_hz,
                                    double if_offset_hz,
                                    uint8_t *r16_setup,
                                    uint8_t *r16_active,
                                    uint8_t *r20,
                                    uint8_t *r21,
                                    uint8_t *r22)
{
    const double lo_hz = static_cast<double>(frequency_hz) + if_offset_hz;
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

    const uint8_t active =
        static_cast<uint8_t>((((mix_log - 1) & 0x07) << 5) | 0x04);

    *r16_active = active;
    *r16_setup = static_cast<uint8_t>(active + 0x20);
    *r20 = static_cast<uint8_t>((si2c << 6) | ni2c);
    *r21 = static_cast<uint8_t>(nfra & 0xff);
    *r22 = static_cast<uint8_t>((nfra >> 8) & 0xff);
    return true;
}

/* V4 keeps the original measured clean-room IF offset. */
static bool encode_r820_pll(uint32_t frequency_hz,
                            uint8_t *r16_setup,
                            uint8_t *r16_active,
                            uint8_t *r20,
                            uint8_t *r21,
                            uint8_t *r22)
{
    return encode_r820_pll_with_if(frequency_hz, kRtlIfOffsetHz,
                                   r16_setup, r16_active, r20, r21, r22);
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
    h->ctrl_inflight = false;
    xSemaphoreGive(h->ctrl_sem);
}

static esp_err_t ctrl_submit(rtl_sdr_v4_esp_handle *h, uint8_t bm, uint8_t bRequest,
                             uint16_t wValue, uint16_t wIndex, const uint8_t *data,
                             uint16_t wLength, bool expect_stall,
                             uint8_t *response = nullptr,
                             uint16_t response_length = 0)
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

        h->ctrl_inflight = true;
        esp_err_t ret = usb_host_transfer_submit_control(h->client, x);
        if (ret != ESP_OK) {
            h->ctrl_inflight = false;
            final_err = RTL_SDR_V4_ESP_ERR_USB;
            break;
        }
        if (xSemaphoreTake(h->ctrl_sem,
						   pdMS_TO_TICKS(h->cfg.control_timeout_ms + 200)) != pdTRUE) {
			ESP_LOGW(TAG,
					 "EP0 timeout value=0x%04x index=0x%04x; transfer may still be in flight",
					 static_cast<unsigned>(wValue),
					 static_cast<unsigned>(wIndex));

			final_err = RTL_SDR_V4_ESP_ERR_TIMEOUT;
			break;
		}
        if (h->ctrl_status == ESP_OK) {
			if ((bm & USB_BM_REQUEST_TYPE_DIR_IN) != 0 &&
				response != nullptr && response_length > 0) {
				const uint16_t copy_length =
					(response_length < wLength) ? response_length : wLength;

				std::memcpy(response,
							x->data_buffer + sizeof(usb_setup_packet_t),
							copy_length);
			}

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
/* RTL2832U demod register write, matching librtlsdr's wire format. */
static esp_err_t v3_demod_write_reg(rtl_sdr_v4_esp_handle *h,
                                    uint8_t page,
                                    uint8_t addr,
                                    uint16_t value,
                                    uint8_t len)
{
    if (len != 1 && len != 2) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t wValue = static_cast<uint16_t>((static_cast<uint16_t>(addr) << 8) | 0x20u);
    const uint16_t wIndex = static_cast<uint16_t>(0x0010u | page);
    uint8_t data[2]{};
    if (len == 1) {
        data[0] = static_cast<uint8_t>(value & 0xffu);
    } else {
        data[0] = static_cast<uint8_t>((value >> 8) & 0xffu);
        data[1] = static_cast<uint8_t>(value & 0xffu);
    }
    return ctrl_submit(h, 0x40, 0, wValue, wIndex, data, len, false);
}

/*
 * Program the RTL2832U digital down-converter for the same low-IF offset used
 * by encode_r820_pll().  The R820T2 is tuned above the requested RF frequency
 * by kV3R820IfHz, so the RTL2832U must shift that IF back to complex
 * baseband before samples are sent through USB.
 *
 * This mirrors librtlsdr's rtlsdr_set_if_freq() register packing:
 * page 1, registers 0x19..0x1b contain a signed 22-bit phase increment.
 */
static esp_err_t v3_set_if_freq(rtl_sdr_v4_esp_handle *h, int32_t if_hz)
{
    const double scaled =
        (static_cast<double>(if_hz) * static_cast<double>(1u << 22)) /
        kRtlXtalHz;

    const int32_t if_reg = -static_cast<int32_t>(std::lround(scaled));

    esp_err_t err =
        v3_demod_write_reg(h, 1, 0x19,
                           static_cast<uint8_t>((if_reg >> 16) & 0x3f), 1);
    if (err != ESP_OK) return err;

    err = v3_demod_write_reg(h, 1, 0x1a,
                             static_cast<uint8_t>((if_reg >> 8) & 0xff), 1);
    if (err != ESP_OK) return err;

    err = v3_demod_write_reg(h, 1, 0x1b,
                             static_cast<uint8_t>(if_reg & 0xff), 1);
    if (err != ESP_OK) return err;
    return ESP_OK;
}

/*
 * Configure the RTL2832U for the R820T/R820T2 low-IF topology.
 *
 * R82xx is not a Zero-IF tuner.  The RTL2832U receives the tuner's low-IF on
 * the I ADC input and digitally mixes it to complex I/Q baseband.  Therefore
 * the correct path is:
 *
 *   R820T2 LO = requested RF + kV3R820IfHz
 *   RTL2832U I ADC input
 *   RTL2832U low-IF DDC = -kV3R820IfHz
 *   spectrum inversion enabled
 *   complex I/Q USB output
 *
 * The earlier diagnostic build used 0xcd (both ADC inputs).  On this V3 that
 * produced Q=0x80 exactly, proving that configuration was wrong for R820T2.
 */
static esp_err_t v3_prepare_r820_demod_path(rtl_sdr_v4_esp_handle *h)
{
    const int32_t if_hz =
        static_cast<int32_t>(std::lround(kV3R820IfHz));

    /* Default RTL2832 complex-output datapath (opt_adc_iq = 0). */
    esp_err_t err = v3_demod_write_reg(h, 0, 0x06, 0x80, 1);
    if (err != ESP_OK) return err;

    /*
     * R820T/R820T2 low-IF is connected to the I ADC input.  0xcd was the
     * incorrect two-input setting that left every Q sample at 0x80.
     */
    err = v3_demod_write_reg(h, 0, 0x08, 0x4d, 1);
    if (err != ESP_OK) return err;

    /* Disable Zero-IF baseband mode: R820T2 supplies a real low-IF signal. */
    err = v3_demod_write_reg(h, 1, 0xb1, 0x1a, 1);
    if (err != ESP_OK) return err;

    /* Shift the exact same IF used by encode_r820_pll() back to DC. */
    err = v3_set_if_freq(h, if_hz);
    if (err != ESP_OK) return err;

    /* R82xx low-IF path requires spectrum inversion. */
    err = v3_demod_write_reg(h, 1, 0x15, 0x01, 1);
    if (err != ESP_OK) return err;
    return ESP_OK;
}

static esp_err_t prepare_blog_v3_tuner_probe(rtl_sdr_v4_esp_handle *h)
{
    /*
     * Run the common RTL2832U/baseband initialization prefix from the
     * measured table, but stop before its tuner-detection/expected-STALL area.
     *
     * The existing V4 table marks indices 86..91 as expected STALL records,
     * so V3 probing uses only 0..85 here and performs its own R820T2 probe.
     */
    static constexpr size_t kV3CommonInitLast = 85;

    for (size_t i = 0; i <= kV3CommonInitLast; ++i) {
        esp_err_t err = run_record(h, kRtlInitTransfers[i], false);
        if (err != ESP_OK) {
            ESP_LOGW(TAG,
                     "V3 common init record %u failed: %s",
                     static_cast<unsigned>(i),
                     rtl_sdr_v4_esp_err_to_name(err));
            return err;
        }
    }
    return ESP_OK;
}

static bool probe_blog_v3_r820t2(rtl_sdr_v4_esp_handle *h)
{
    if (h == nullptr || h->dev == nullptr) {
        return false;
    }

    static constexpr uint8_t kSelectReg0[1] = {0x00};

    esp_err_t err = ctrl_submit(
        h,
        0x40,
        0,
        0x0034,
        0x0610,
        kSelectReg0,
        1,
        false);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "V3_TUNER_PROBE select failed: %s",
                 rtl_sdr_v4_esp_err_to_name(err));
        return false;
    }

    uint8_t chip_id = 0;

    err = ctrl_submit(
        h,
        0xc0,
        0,
        0x0034,
        0x0600,
        nullptr,
        1,
        false,
        &chip_id,
        sizeof(chip_id));

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "V3_TUNER_PROBE read failed: %s",
                 rtl_sdr_v4_esp_err_to_name(err));
        return false;
    }

    const bool match = (chip_id == 0x96 || chip_id == 0x69);

    return match;
}

static constexpr uint8_t kV3R820ShadowStart = 0x05;
static constexpr uint8_t kV3R820ShadowEnd = 0x1f;

/* Standard R820T/R820T2 reset/init image for registers 0x05..0x1f. */
static constexpr uint8_t kV3R820Init[] = {
    0x83, 0x32, 0x75,             /* 05..07 */
    0xc0, 0x40, 0xd6, 0x6c,       /* 08..0b */
    0xf5, 0x63, 0x75, 0x68,       /* 0c..0f */
    0x6c, 0x83, 0x80, 0x00,       /* 10..13 */
    0x0f, 0x00, 0xc0, 0x30,       /* 14..17 */
    0x48, 0xcc, 0x60, 0x00,       /* 18..1b */
    0x54, 0xae, 0x4a, 0xc0        /* 1c..1f */
};

static int v3_r820_shadow_index(uint8_t reg)
{
    if (reg < kV3R820ShadowStart || reg > kV3R820ShadowEnd) {
        return -1;
    }
    return static_cast<int>(reg - kV3R820ShadowStart);
}

static esp_err_t v3_r820_write_reg_raw(rtl_sdr_v4_esp_handle *h,
                                       uint8_t reg,
                                       uint8_t value)
{
    const uint8_t payload[2] = {reg, value};
    return ctrl_submit(h, 0x40, 0, 0x0034, 0x0610,
                       payload, sizeof(payload), false);
}

static esp_err_t v3_r820_write_reg(rtl_sdr_v4_esp_handle *h,
                                   uint8_t reg,
                                   uint8_t value)
{
    esp_err_t err = v3_r820_write_reg_raw(h, reg, value);
    if (err == ESP_OK && h != nullptr && h->v3_r820_shadow_valid) {
        const int idx = v3_r820_shadow_index(reg);
        if (idx >= 0) {
            h->v3_r820_regs[idx] = value;
        }
    }
    return err;
}

static esp_err_t v3_r820_write_reg_mask(rtl_sdr_v4_esp_handle *h,
                                        uint8_t reg,
                                        uint8_t value,
                                        uint8_t mask)
{
    if (h == nullptr || !h->v3_r820_shadow_valid) {
        ESP_LOGW(TAG,
                 "Blog V3 R820 masked write without valid shadow reg=0x%02x",
                 reg);
        return RTL_SDR_V4_ESP_ERR_NOT_READY;
    }

    const int idx = v3_r820_shadow_index(reg);
    if (idx < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t old_value = h->v3_r820_regs[idx];
    const uint8_t new_value =
        static_cast<uint8_t>((old_value & static_cast<uint8_t>(~mask)) |
                             (value & mask));

    if (new_value == old_value) {
        return ESP_OK;
    }

    esp_err_t err = v3_r820_write_reg_raw(h, reg, new_value);
    if (err == ESP_OK) {
        h->v3_r820_regs[idx] = new_value;
    }
    return err;
}

static esp_err_t v3_r820_full_init(rtl_sdr_v4_esp_handle *h)
{
    if (h == nullptr || h->dev == nullptr) {
        return RTL_SDR_V4_ESP_ERR_USB;
    }

    h->v3_r820_shadow_valid = false;

    for (size_t i = 0; i < std::size(kV3R820Init); ++i) {
        const uint8_t reg =
            static_cast<uint8_t>(kV3R820ShadowStart + i);
        const uint8_t value = kV3R820Init[i];

        esp_err_t err = v3_r820_write_reg_raw(h, reg, value);
        if (err != ESP_OK) {
            ESP_LOGW(TAG,
                     "Blog V3 R820_INIT failed reg=0x%02x value=0x%02x: %s",
                     reg, value, rtl_sdr_v4_esp_err_to_name(err));
            return err;
        }

        h->v3_r820_regs[i] = value;
    }

    h->v3_r820_shadow_valid = true;

    /*
     * Mirror the non-analog initialization tweaks used by the normal R82xx
     * path.  These are masked writes so unrelated bits in the reset image are
     * preserved.
     */
    esp_err_t err = v3_r820_write_reg_mask(h, 0x0c, 0x00, 0x0f);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg_mask(h, 0x1d, 0x00, 0x38);
    if (err != ESP_OK) return err;
    return ESP_OK;
}

static esp_err_t v3_r820_read_status(rtl_sdr_v4_esp_handle *h,
                                     uint8_t start_reg,
                                     uint8_t *out,
                                     uint16_t len)
{
    if (out == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t reg = start_reg;
    esp_err_t err = ctrl_submit(h, 0x40, 0, 0x0034, 0x0610,
                                &reg, 1, false);
    if (err != ESP_OK) {
        return err;
    }

    return ctrl_submit(h, 0xc0, 0, 0x0034, 0x0600,
                       nullptr, len, false, out, len);
}

static esp_err_t v3_r820_pll_diag(rtl_sdr_v4_esp_handle *h,
                                      uint32_t frequency_hz);

static uint8_t v3_bit_reverse8(uint8_t value)
{
    value = static_cast<uint8_t>((value >> 4) | (value << 4));
    value = static_cast<uint8_t>(((value & 0xccu) >> 2) | ((value & 0x33u) << 2));
    value = static_cast<uint8_t>(((value & 0xaau) >> 1) | ((value & 0x55u) << 1));
    return value;
}

/* Configure the R820T2 receive gain state used by the V3 path. */
static esp_err_t v3_r820_set_auto_gain(rtl_sdr_v4_esp_handle *h)
{
    /* Fixed manual gain profile; IF/DDC/tuning are not changed here. */
    static constexpr uint8_t kV3LnaGain = 0x0C;
    static constexpr uint8_t kV3MixerGain = 0x08;
    static constexpr uint8_t kV3VgaGain = 0x08;

    /* LNA manual: bit 4 selects manual control; low nibble is gain index. */
    esp_err_t err = v3_r820_write_reg_mask(
        h, 0x05, static_cast<uint8_t>(0x10u | kV3LnaGain), 0x1f);
    if (err != ESP_OK) return err;

    /* Mixer manual: clear mixer-AGC bit and program the low-nibble index. */
    err = v3_r820_write_reg_mask(h, 0x07, kV3MixerGain, 0x1f);
    if (err != ESP_OK) return err;

    /* Fixed VGA, deliberately low to retain ADC headroom on strong FM. */
    err = v3_r820_write_reg_mask(h, 0x0c, kV3VgaGain, 0x9f);
    if (err != ESP_OK) return err;
    return ESP_OK;
}

/*
 * Channel-filter calibration mirrors the reference R82xx sequence.
 *
 * Calibration LO is 56 MHz.  It is done before the requested receive PLL is
 * programmed, so start-up subsequently restores the user's actual frequency.
 */
static esp_err_t v3_r820_filter_calibrate(rtl_sdr_v4_esp_handle *h)
{
    constexpr uint32_t kCalibrationHz = 56000000u;
    constexpr uint8_t kHpCor = 0x6b;
    constexpr uint8_t kFiltQ = 0x10;

    uint8_t cal_code = 0;

    for (int attempt = 0; attempt < 2; ++attempt) {
        /* Set filter capacitor mode used by the standard calibration path. */
        esp_err_t err = v3_r820_write_reg_mask(h, 0x0b, kHpCor, 0x60);
        if (err != ESP_OK) return err;

        /* Calibration clock on. */
        err = v3_r820_write_reg_mask(h, 0x0f, 0x04, 0x04);
        if (err != ESP_OK) return err;

        /* Crystal cap = 0 pF while calibrating the PLL/filter. */
        err = v3_r820_write_reg_mask(h, 0x10, 0x00, 0x03);
        if (err != ESP_OK) return err;

        err = v3_r820_pll_diag(h, kCalibrationHz);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Blog V3 FILTER_CAL PLL failed attempt=%d: %s",
                     attempt + 1, rtl_sdr_v4_esp_err_to_name(err));
            return err;
        }

        /* Start then stop channel-filter calibration trigger. */
        err = v3_r820_write_reg_mask(h, 0x0b, 0x10, 0x10);
        if (err != ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(2));

        err = v3_r820_write_reg_mask(h, 0x0b, 0x00, 0x10);
        if (err != ESP_OK) return err;

        /* Calibration clock off. */
        err = v3_r820_write_reg_mask(h, 0x0f, 0x00, 0x04);
        if (err != ESP_OK) return err;

        uint8_t status[5] = {0, 0, 0, 0, 0};
        err = v3_r820_read_status(h, 0x00, status, sizeof(status));
        if (err != ESP_OK) return err;

        const uint8_t decoded4 = v3_bit_reverse8(status[4]);
        cal_code = static_cast<uint8_t>(decoded4 & 0x0f);

        if (cal_code != 0 && cal_code != 0x0f) {
            break;
        }
    }

    /* Reference behavior: 0x0f means use the widest/narrowest fallback code 0. */
    if (cal_code == 0x0f) {
        cal_code = 0;
    }

    /*
     * Apply the calibrated fine filter code and the standard <6 MHz filter
     * configuration.  This is intentionally a broad tuner IF; OrcSDR's
     * application-side WBFM filter remains responsible for the ~180 kHz
     * channel selection.
     */
    esp_err_t err =
        v3_r820_write_reg_mask(h, 0x0a,
                               static_cast<uint8_t>(kFiltQ | cal_code), 0x1f);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg_mask(h, 0x0b, kHpCor, 0xef);
    if (err != ESP_OK) return err;

    /* Image negative. */
    err = v3_r820_write_reg_mask(h, 0x07, 0x00, 0x80);
    if (err != ESP_OK) return err;

    /* +3 dB / 6 MHz filter gain selection. */
    err = v3_r820_write_reg_mask(h, 0x06, 0x10, 0x30);
    if (err != ESP_OK) return err;

    /* Channel filter extension enabled. */
    err = v3_r820_write_reg_mask(h, 0x1e, 0x60, 0x60);
    if (err != ESP_OK) return err;

    /* Loop-through off. */
    err = v3_r820_write_reg_mask(h, 0x05, 0x01, 0x80);
    if (err != ESP_OK) return err;

    /* Loop-through attenuation setting. */
    err = v3_r820_write_reg_mask(h, 0x1f, 0x00, 0x80);
    if (err != ESP_OK) return err;

    /* Filter extension widest off. */
    err = v3_r820_write_reg_mask(h, 0x0f, 0x00, 0x80);
    if (err != ESP_OK) return err;

    /* Minimum RF poly-filter current. */
    err = v3_r820_write_reg_mask(h, 0x19, 0x60, 0x60);
    if (err != ESP_OK) return err;
    return ESP_OK;
}

/* Program the R820T2 RF mux/tracking-filter profile for the requested band. */
static esp_err_t v3_r820_rf_diag(rtl_sdr_v4_esp_handle *h,
                                      uint32_t frequency_hz)
{
    if (h == nullptr || !h->v3_r820_shadow_valid) {
        return RTL_SDR_V4_ESP_ERR_NOT_READY;
    }

    struct RfProfile {
        uint32_t start_mhz;
        uint8_t open_d;
        uint8_t rf_mux_poly;
        uint8_t tf_c;
        const char *name;
    };

    /*
     * R82xx frequency ranges needed by OrcSDR bring-up.  Values are the
     * standard R820T/R820T2 mux/tracking-filter selections.
     */
    static constexpr RfProfile kRanges[] = {
        {0,   0x08, 0x02, 0xdf, "LOW-0/50"},
        {50,  0x08, 0x02, 0xbe, "LOW-50/55"},
        {55,  0x08, 0x02, 0x8b, "LOW-55/60"},
        {60,  0x08, 0x02, 0x7b, "LOW-60/65"},
        {65,  0x08, 0x02, 0x69, "LOW-65/70"},
        {70,  0x08, 0x02, 0x58, "LOW-70/75"},
        {75,  0x00, 0x02, 0x44, "VHF-75/90"},
        {90,  0x00, 0x02, 0x34, "FM-90/110"},
        {110, 0x00, 0x02, 0x24, "VHF-110/140"},
        {140, 0x00, 0x02, 0x14, "VHF-140/180"},
        {180, 0x00, 0x02, 0x13, "VHF-180/250"},
        {250, 0x00, 0x02, 0x11, "VHF-250/280"},
        {280, 0x00, 0x02, 0x00, "VHF-280/310"},
        {310, 0x00, 0x41, 0x00, "UHF-310/588"},
        {588, 0x00, 0x40, 0x00, "UHF-588+"},
    };

    const uint32_t mhz = frequency_hz / 1000000u;
    const RfProfile *p = &kRanges[0];
    for (size_t i = 0; i + 1 < std::size(kRanges); ++i) {
        if (mhz < kRanges[i + 1].start_mhz) {
            p = &kRanges[i];
            break;
        }
        p = &kRanges[i + 1];
    }

    esp_err_t err;

    /* r82xx_set_mux(): preserve every unrelated register bit. */
    err = v3_r820_write_reg_mask(h, 0x17, p->open_d, 0x08);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg_mask(h, 0x1a, p->rf_mux_poly, 0xc3);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg(h, 0x1b, p->tf_c);
    if (err != ESP_OK) return err;

    /*
     * Keep the reset-image crystal-cap selection, but force normal low-cap
     * crystal drive (bit 3).  Crucially, do not overwrite the PLL divider bits
     * in R10.
     */
    err = v3_r820_write_reg_mask(h, 0x10, 0x08, 0x0b);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg_mask(h, 0x08, 0x00, 0x3f);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg_mask(h, 0x09, 0x00, 0x3f);
    if (err != ESP_OK) return err;

    /*
     * Normal R82xx system-frequency selection.  This is the important
     * difference from the previous experimental whole-register profile:
     * LNA/mixer thresholds and currents are established while preserving the
     * other control bits.
     */
    err = v3_r820_write_reg_mask(h, 0x1d, 0xe5, 0xc7);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg_mask(h, 0x1c, 0x24, 0xf8);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg(h, 0x0d, 0x53);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg(h, 0x0e, 0x75);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg_mask(h, 0x05, 0x00, 0x60);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg_mask(h, 0x06, 0x00, 0x08);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg_mask(h, 0x11, 0x38, 0x38);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg_mask(h, 0x17, 0x30, 0x30);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg_mask(h, 0x0a, 0x40, 0x60);
    if (err != ESP_OK) return err;

    /* Non-analog tuner path. */
    err = v3_r820_write_reg_mask(h, 0x1d, 0x00, 0x38);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg_mask(h, 0x1c, 0x00, 0x04);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg_mask(h, 0x06, 0x00, 0x40);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg_mask(h, 0x1a, 0x30, 0x30);
    if (err != ESP_OK) return err;
    return ESP_OK;
}

static esp_err_t v3_r820_pll_diag(rtl_sdr_v4_esp_handle *h, uint32_t frequency_hz)
{
    if (h == nullptr || !h->v3_r820_shadow_valid) {
        return RTL_SDR_V4_ESP_ERR_NOT_READY;
    }

    uint8_t r16_setup = 0;
    uint8_t r16_active = 0;
    uint8_t r20 = 0;
    uint8_t r21 = 0;
    uint8_t r22 = 0;

    if (!encode_r820_pll_with_if(frequency_hz,
                                 kV3R820IfHz,
                                 &r16_setup, &r16_active,
                                 &r20, &r21, &r22)) {
        ESP_LOGW(TAG, "Blog V3 TUNE_DIAG PLL encode failed freq=%u",
                 static_cast<unsigned>(frequency_hz));
        return RTL_SDR_V4_ESP_ERR_BAD_FREQ;
    }

    /* Program only the R820T2 PLL registers, preserving unrelated state. */
    /*
     * Program the divider while preserving R10 crystal/drive bits from the
     * R820 shadow.  The earlier V3 path wrote R10 as a whole register, which
     * could erase bit 3 and other tuner state.
     */
    esp_err_t err = v3_r820_write_reg_mask(h, 0x1a, 0x00, 0x0c);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg_mask(h, 0x10, r16_setup, 0xe0);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg(h, 0x14, r20);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg(h, 0x15, r21);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg(h, 0x16, r22);
    if (err != ESP_OK) return err;

    err = v3_r820_write_reg_mask(h, 0x10, r16_active, 0xe0);
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t status[3] = {0, 0, 0};
    err = v3_r820_read_status(h, 0x00, status, sizeof(status));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Blog V3 TUNE_DIAG status read failed: %s",
                 rtl_sdr_v4_esp_err_to_name(err));
        return err;
    }

    /*
     * R82xx status bytes are bit-reversed on the tuner read path.  Decode
     * them explicitly and use the conventional R82xx PLL-lock indication:
     * status register 2, bit 6 after bit reversal.
     */
    const uint8_t st2 = v3_bit_reverse8(status[2]);
    const bool pll_locked = (st2 & 0x40u) != 0;

    if (!pll_locked) {
        ESP_LOGW(TAG, "Blog V3 TUNE_DIAG PLL lock not asserted");
        return RTL_SDR_V4_ESP_ERR_NOT_READY;
    }

    err = v3_r820_write_reg_mask(h, 0x1a, 0x08, 0x08);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Blog V3 TUNE_DIAG autotune final write failed: %s",
                 rtl_sdr_v4_esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
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


static esp_err_t v3_prepare_bulk_endpoint(rtl_sdr_v4_esp_handle *h)
{
    /*
     * RTL2832U USB endpoint-A preparation.
     *
     * The common init already configures the RTL2832U, but librtlsdr performs
     * a mandatory endpoint/buffer reset immediately before reading samples.
     * Mirror that device-side sequence here for V3:
     *
     *   USB_EPA_MAXPKT (0x2158) <- 0x0002
     *   USB_EPA_CTL    (0x2148) <- 0x1002
     *   USB_EPA_CTL    (0x2148) <- 0x0000
     *
     * USBB writes use wIndex 0x0110 and big-endian payload bytes.
     */
    static constexpr uint8_t kMaxPkt[2] = {0x00, 0x02};
    static constexpr uint8_t kResetOn[2] = {0x10, 0x02};
    static constexpr uint8_t kResetOff[2] = {0x00, 0x00};

    esp_err_t err = ctrl_submit(h, 0x40, 0, 0x2158, 0x0110,
                                kMaxPkt, sizeof(kMaxPkt), false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Blog V3 BULK_RESET EPA_MAXPKT failed: %s",
                 rtl_sdr_v4_esp_err_to_name(err));
        return err;
    }

    err = ctrl_submit(h, 0x40, 0, 0x2148, 0x0110,
                      kResetOn, sizeof(kResetOn), false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Blog V3 BULK_RESET assert failed: %s",
                 rtl_sdr_v4_esp_err_to_name(err));
        return err;
    }

    err = ctrl_submit(h, 0x40, 0, 0x2148, 0x0110,
                      kResetOff, sizeof(kResetOff), false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Blog V3 BULK_RESET release failed: %s",
                 rtl_sdr_v4_esp_err_to_name(err));
        return err;
    }

    /*
     * Do not issue host-side endpoint flush/clear here.
     *
     * At first stream start the ESP-IDF host endpoint is not halted and those
     * commands return ESP_ERR_INVALID_STATE.  The RTL2832U device-side EPA
     * reset above is the operation we actually need before the first URBs.
     *
     * Host-side halt/flush/clear remains in stop/recovery paths where the
     * endpoint can legitimately have outstanding or failed transfers.
     */
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Bulk + ring                                                                */
/* -------------------------------------------------------------------------- */

static void v3_dc_block_iq(rtl_sdr_v4_esp_handle *h,
                           uint8_t *data,
                           size_t bytes)
{
    if (h == nullptr || data == nullptr || bytes < 2) {
        return;
    }

    bytes &= ~size_t{1};

    /*
     * R=0.998 gives a corner of roughly 300 Hz at 960 kS/s:
     *   fc ~= (1-R)*Fs/(2*pi)
     *
     * This is intentionally far below the useful WBFM baseband and the
     * 19/57 kHz pilot/RDS content.  It targets only the fixed center/DC spur.
     */
    static constexpr float kDcR = 0.9980f;
    static constexpr float kCenter = 127.5f;

    /*
     * A retune can change the DC operating point abruptly.  Restart the
     * blocker on the first block at the new RF frequency to avoid carrying
     * the previous station's state into the new one.
     */
    if (!h->v3_dc_valid || h->v3_dc_frequency_hz != h->frequency_hz) {
        h->v3_dc_x1_i = static_cast<float>(data[0]) - kCenter;
        h->v3_dc_x1_q = static_cast<float>(data[1]) - kCenter;
        h->v3_dc_y1_i = 0.0f;
        h->v3_dc_y1_q = 0.0f;
        h->v3_dc_valid = true;
        h->v3_dc_frequency_hz = h->frequency_hz;
    }

    float x1_i = h->v3_dc_x1_i;
    float x1_q = h->v3_dc_x1_q;
    float y1_i = h->v3_dc_y1_i;
    float y1_q = h->v3_dc_y1_q;

    for (size_t n = 0; n < bytes; n += 2) {
        const float xi = static_cast<float>(data[n]) - kCenter;
        const float xq = static_cast<float>(data[n + 1]) - kCenter;

        const float yi = xi - x1_i + kDcR * y1_i;
        const float yq = xq - x1_q + kDcR * y1_q;

        x1_i = xi;
        x1_q = xq;
        y1_i = yi;
        y1_q = yq;

        int oi = static_cast<int>(std::lround(yi + kCenter));
        int oq = static_cast<int>(std::lround(yq + kCenter));
        if (oi < 0) oi = 0;
        if (oi > 255) oi = 255;
        if (oq < 0) oq = 0;
        if (oq > 255) oq = 255;

        data[n] = static_cast<uint8_t>(oi);
        data[n + 1] = static_cast<uint8_t>(oq);
    }

    h->v3_dc_x1_i = x1_i;
    h->v3_dc_x1_q = x1_q;
    h->v3_dc_y1_i = y1_i;
    h->v3_dc_y1_q = y1_q;
}

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
            if (std::strcmp(h->info.product, kProductV3) == 0) {
                v3_dc_block_iq(h, slot->data, copy);
            }
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

        /*
         * A STALL/error leaves the endpoint unsuitable for immediate
         * resubmission. Stop this diagnostic stream and let the owner perform
         * endpoint recovery instead of recursively hitting INVALID_STATE.
         */
        h->streaming = false;
        h->pause_resubmit = true;
        if (h->live_urbs > 0) {
            h->live_urbs = h->live_urbs - 1;
        }
        xSemaphoreGive(h->bulk_done_sem);
        return;
    }

    /* Resubmit only while streaming and not draining for stop/retune. */
    if (h->streaming && !h->pause_resubmit) {
        esp_err_t ret = usb_host_transfer_submit(xfer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "bulk resubmit failed: %s", esp_err_to_name(ret));
            h->streaming = false;
            if (h->live_urbs > 0) {
                h->live_urbs = h->live_urbs - 1;
            }
            xSemaphoreGive(h->bulk_done_sem);
        }
        /* still in flight after successful resubmit */
    } else {
        if (h->live_urbs > 0) {
            h->live_urbs = h->live_urbs - 1;
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

    if (std::strcmp(h->info.product, kProductV3) == 0) {
    }

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

    const bool is_v3 = std::strcmp(h->info.product, kProductV3) == 0;
    esp_err_t err = ESP_OK;

    if (is_v3) {

        err = v3_r820_pll_diag(h, freq);

        if (err == ESP_OK) {
            err = v3_r820_rf_diag(h, freq);
        }

        if (err == ESP_OK) {
        }

        if (err == ESP_OK) {
            const bool tuner_ok = probe_blog_v3_r820t2(h);
            if (!tuner_ok) {
                ESP_LOGW(TAG, "Blog V3 HOT_TUNE tuner probe failed after retune");
                err = RTL_SDR_V4_ESP_ERR_NOT_READY;
            }
        }

        if (err == ESP_OK) {
            h->frequency_hz = freq;
            h->metrics.frequency_hz = freq;
            h->pending_retune_hz = 0;
        } else {
            ESP_LOGW(TAG, "Blog V3 HOT_TUNE failed hz=%u err=%s",
                     static_cast<unsigned>(freq),
                     rtl_sdr_v4_esp_err_to_name(err));
            h->pending_retune_hz = 0;
        }
    } else {
        err = run_tune(h, freq);
        if (err == ESP_OK) {
            h->frequency_hz = freq;
            h->metrics.frequency_hz = freq;
            h->pending_retune_hz = 0;
            ESP_LOGI(TAG, "hot retune applied %u Hz", static_cast<unsigned>(freq));
        } else {
            ESP_LOGW(TAG, "hot retune EP0 failed: %s (keep LO)",
                     rtl_sdr_v4_esp_err_to_name(err));
            h->pending_retune_hz = 0;
        }
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
                h->live_urbs = h->live_urbs + 1;
            }
        }

        if (is_v3) {
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
    if (h->bulk != nullptr && h->bulk_num == num && h->bulk_len == len) {
        for (uint32_t i = 0; i < num; ++i) {
            h->bulk[i]->device_handle = h->dev;
            h->bulk[i]->bEndpointAddress = RTL_SDR_V4_ESP_BULK_EP_IN;
            h->bulk[i]->num_bytes = len;
            h->bulk[i]->callback = bulk_cb;
            h->bulk[i]->context = h;
        }
        return ESP_OK;
    }
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
    const bool is_v4 =
        std::strcmp(mfg, kMfg) == 0 &&
        std::strcmp(prod, kProduct) == 0;

    const bool is_v3_named =
        std::strcmp(mfg, kMfg) == 0 &&
        std::strcmp(prod, kProductV3) == 0;

    const bool is_v3_candidate =
        std::strcmp(mfg, kMfgGeneric) == 0 &&
        std::strcmp(prod, kProductGeneric) == 0;

    if (!is_v4 && !is_v3_named && !is_v3_candidate) {
        ESP_LOGW(TAG, "unsupported RTL-SDR identity mfg='%s' product='%s'", mfg, prod);
        return false;
    }

    if (is_v3_named || is_v3_candidate) {
        ESP_LOGW(TAG,
                 "Blog V3 candidate mfg='%s' product='%s'",
                 mfg, prod);
    }
    out->vid = dd->idVendor;
    out->pid = dd->idProduct;
    out->high_speed = (info->speed == USB_SPEED_HIGH);
    out->present = true;
    std::snprintf(out->manufacturer, sizeof(out->manufacturer), "%s", mfg);
    if (is_v3_named || is_v3_candidate) {
        std::snprintf(out->product, sizeof(out->product), "%s", kProductV3);
    } else {
        std::snprintf(out->product, sizeof(out->product), "%s", prod);
    }
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

	if (std::strcmp(di.product, kProductV3) == 0) {
		ESP_LOGW(TAG, "Blog V3 candidate queued for tuner probe");
		h->v3_probe_addr = addr;
		h->v3_probe_pending = true;
        if (h->v3_probe_task != nullptr) {
            xTaskNotifyGive(h->v3_probe_task);
        }

		h->dev = nullptr;
		h->info.present = false;
		usb_host_device_close(h->client, dev);
		return;
	}
	ESP_LOGI(TAG, "RTL-SDR open %s %s serial=%s hs=%d",
			 di.manufacturer, di.product, di.serial,
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

    /*
     * Keep the USB Host Library task dedicated to usb_host_lib_handle_events().
     * Do not run blocking EP0 control transfers here: ctrl_submit() waits for a
     * client callback, while the host library itself must continue servicing
     * USB events.
     */
    while (h->tasks_run) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(pdMS_TO_TICKS(100), &flags);
    }

    vTaskDelete(nullptr);
}

static void v3_probe_task_fn(void *arg)
{
    auto *h = static_cast<rtl_sdr_v4_esp_handle *>(arg);

    while (h->tasks_run) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

        if (!h->tasks_run) {
            continue;
        }

        /*
         * Reuse the blocking-control worker for live retunes so EP0 work never
         * runs from USB callbacks/client tasks and no extra task stack is needed.
         */
        if (h->pending_retune_hz != 0 && h->streaming && !h->v3_probe_pending) {
            const esp_err_t retune_err = apply_pending_retune(h);
            if (retune_err != ESP_OK) {
                ESP_LOGW(TAG, "retune worker failed: %s",
                         rtl_sdr_v4_esp_err_to_name(retune_err));
            }
        }

        if (!h->v3_probe_pending) {
            continue;
        }

        const uint8_t addr = h->v3_probe_addr;
        h->v3_probe_pending = false;
        h->v3_probe_addr = 0;

        /* Let enumeration/root-port activity settle before reopening. */
        vTaskDelay(pdMS_TO_TICKS(100));

        usb_device_handle_t dev = nullptr;
        esp_err_t open_err = usb_host_device_open(h->client, addr, &dev);
        if (open_err != ESP_OK) {
            ESP_LOGW(TAG, "V3_TUNER_PROBE unable to open addr=%u: %s",
                     static_cast<unsigned>(addr), esp_err_to_name(open_err));
            continue;
        }

        h->dev = dev;

        /*
         * The normal V4 path claims interface 0 before running its EP0 init
         * table. Do the same for the V3 probe worker.
         */
        bool probe_iface_claimed = false;
        esp_err_t claim_err = usb_host_interface_claim(h->client, dev, 0, 0);
        if (claim_err != ESP_OK) {
            ESP_LOGW(TAG, "V3 probe interface claim failed: %s",
                     esp_err_to_name(claim_err));
        } else {
            probe_iface_claimed = true;
        }

        bool tuner_ok = false;
        if (probe_iface_claimed) {
            esp_err_t prep = prepare_blog_v3_tuner_probe(h);
            if (prep == ESP_OK) {
                tuner_ok = probe_blog_v3_r820t2(h);
            } else {
                ESP_LOGW(TAG, "Blog V3 tuner probe skipped: common init failed");
            }
        }

        if (tuner_ok) {
            /*
             * Promote the confirmed V3 to the normal active-device state.
             * Keep this device handle open so get_device_info()/start() see a
             * present RTL-SDR instead of timing out as "no_device".
             *
             * The probe claimed interface 0 only for EP0 setup/probing; release
             * it now so rtl_sdr_v4_esp_start() can claim it later. Streaming is
             * still intentionally blocked for V3 by the detection-only guard.
             */

            if (probe_iface_claimed) {
                esp_err_t release_err = usb_host_interface_release(h->client, dev, 0);
                if (release_err != ESP_OK) {
                    ESP_LOGW(TAG, "V3 probe interface release failed: %s",
                             esp_err_to_name(release_err));
                    h->dev = nullptr;
                    h->info.present = false;
                    usb_host_device_close(h->client, dev);
                    continue;
                }
                probe_iface_claimed = false;
            }

            h->dev = dev;
            h->info.present = true;
            h->iface_claimed = false;
            h->device_gone = false;
            h->state = RTL_SDR_V4_ESP_STATE_IDLE;

            ESP_LOGI(TAG, "RTL-SDR open %s %s serial=%s hs=%d",
                     h->info.manufacturer, h->info.product, h->info.serial,
                     static_cast<int>(h->info.high_speed));

            rtl_sdr_v4_esp_event_cb_t cb = h->cfg.event_cb;
            void *ctx = h->cfg.event_ctx;
            if (cb != nullptr) {
                emit_after_unlock(h, RTL_SDR_V4_ESP_EVT_ENUMERATED, &h->info, cb, ctx);
                emit_after_unlock(h, RTL_SDR_V4_ESP_EVT_READY, nullptr, cb, ctx);
            }

            /* Ownership of dev has moved to h->dev. Do not close it here. */
            continue;
        }

        ESP_LOGW(TAG, "Blog V3 tuner probe failed");

        /*
         * Never release/close while an EP0 transfer is still in flight.
         * This prevents the ESP-IDF usbh_dev_close assertion seen earlier.
         */
        if (!h->ctrl_inflight) {
            if (probe_iface_claimed) {
                usb_host_interface_release(h->client, dev, 0);
            }
            h->dev = nullptr;
            h->info.present = false;
            usb_host_device_close(h->client, dev);
        } else {
            ESP_LOGW(TAG,
                     "V3 probe cleanup deferred: EP0 transfer still in flight");
        }
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
            h->v3_r820_shadow_valid = false;
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

    /*
     * Dedicated worker for blocking V3 EP0 probes. Keeping this separate from
     * both host_lib_task_fn() and client_task_fn() ensures both USB event loops
     * continue running while ctrl_submit() waits for completion callbacks.
     */
    if (xTaskCreatePinnedToCore(v3_probe_task_fn, "rtl_v3_probe", 4096, h,
                                kClientPrio - 1, &h->v3_probe_task,
                                kUsbCore) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

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
        const bool is_v3 = std::strcmp(h->info.product, kProductV3) == 0;
        if (!is_v3) {
            run_cleanup_best_effort(h);
        } else {
            ESP_LOGI(TAG, "Blog V3 stop: skip V4 cleanup table");
        }
        usb_host_interface_release(h->client, h->dev, 0);
        h->iface_claimed = false;
    }

    /* Retain the DMA transfer pool for the next stream start. Wi-Fi may claim
     * enough internal memory after boot that recreating this pool fails. */
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
    const bool is_v3 = std::strcmp(handle->info.product, kProductV3) == 0;

    /* Blog V3 uses its own RTL2832U/R820T2 initialization and streaming path. */
    if (is_v3) {
        lk.release();

        esp_err_t ret = usb_host_interface_claim(handle->client, handle->dev, 0, 0);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Blog V3 START_DIAG interface claim failed: %s",
                     esp_err_to_name(ret));
            HandleLock lk2(handle, kQueryLockTicks);
            if (lk2.ok()) {
                set_error_unlocked(handle, RTL_SDR_V4_ESP_ERR_USB);
            }
            return RTL_SDR_V4_ESP_ERR_USB;
        }

        handle->iface_claimed = true;

        ret = prepare_blog_v3_tuner_probe(handle);
        if (ret == ESP_OK) {
            const bool tuner_ok = probe_blog_v3_r820t2(handle);
            if (!tuner_ok) {
                ret = RTL_SDR_V4_ESP_ERR_NOT_READY;
            }
        }

        if (ret == ESP_OK) {
            ret = v3_r820_full_init(handle);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Blog V3 R820_INIT failed: %s",
                         rtl_sdr_v4_esp_err_to_name(ret));
            }
        }

        if (ret == ESP_OK) {
            ret = v3_r820_filter_calibrate(handle);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Blog V3 FILTER_CAL failed: %s",
                         rtl_sdr_v4_esp_err_to_name(ret));
            }
        }

        if (ret == ESP_OK) {
            ret = v3_r820_set_auto_gain(handle);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Blog V3 GAIN setup failed: %s",
                         rtl_sdr_v4_esp_err_to_name(ret));
            }
        }

        if (ret == ESP_OK) {

            ret = run_sample_rate(handle, stream->sample_rate_sps);
            if (ret == ESP_OK) {

                /*
                 * Re-probe the tuner after touching the RTL2832 sample-rate
                 * registers. This verifies the I2C/tuner path remained alive.
                 */
                const bool tuner_still_ok = probe_blog_v3_r820t2(handle);
                if (!tuner_still_ok) {
                    ESP_LOGW(TAG, "Blog V3 RATE_DIAG tuner probe lost after sample-rate");
                    ret = RTL_SDR_V4_ESP_ERR_NOT_READY;
                } else {
                }
            } else {
                ESP_LOGW(TAG, "Blog V3 RATE_DIAG sample-rate failed: %s",
                         rtl_sdr_v4_esp_err_to_name(ret));
            }
        }

        /*
         * Apply the R820T2 low-IF ADC/DDC routing after the sample-rate table,
         * so later RTL2832 demod writes cannot overwrite the IF translation.
         */
        if (ret == ESP_OK) {
            ret = v3_prepare_r820_demod_path(handle);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Blog V3 DEMOD_PATH failed: %s",
                         rtl_sdr_v4_esp_err_to_name(ret));
            }
        }

        if (ret == ESP_OK) {

            ret = v3_r820_pll_diag(handle, freq);

            if (ret == ESP_OK) {
                const bool tuner_still_ok = probe_blog_v3_r820t2(handle);
                if (!tuner_still_ok) {
                    ESP_LOGW(TAG, "Blog V3 TUNE_DIAG tuner probe lost after PLL programming");
                    ret = RTL_SDR_V4_ESP_ERR_NOT_READY;
                } else {
                }
            }
        }

        if (ret == ESP_OK) {
            ret = v3_r820_rf_diag(handle, freq);
            if (ret == ESP_OK) {
                const bool tuner_still_ok = probe_blog_v3_r820t2(handle);
                if (!tuner_still_ok) {
                    ESP_LOGW(TAG, "Blog V3 RF_DIAG tuner probe lost after frontend programming");
                    ret = RTL_SDR_V4_ESP_ERR_NOT_READY;
                } else {
                }
            }
        }

        if (ret == ESP_OK) {

            /*
             * Keep interface 0 claimed and start the same RTL2832U bulk-IN
             * transport used by V4. Only the tuner control path differs.
             *
             * Reset/enable RTL2832U endpoint-A immediately before submitting
             * the first URB. This reset is mandatory on conventional rtl-sdr
             * hosts before sample reads begin.
             */
            ret = v3_prepare_bulk_endpoint(handle);

            if (ret == ESP_OK) {
                ret = ensure_ring(handle, handle->cfg.transfer_bytes);
            }
            if (ret == ESP_OK) {
                ret = alloc_bulk_pool(handle,
                                      static_cast<uint32_t>(handle->cfg.transfer_count),
                                      static_cast<uint32_t>(handle->cfg.transfer_bytes));
            }

            if (ret == ESP_OK && handle->delivery_task == nullptr) {
                handle->tasks_run = true;
                if (xTaskCreatePinnedToCore(delivery_task_fn, "rtl_iq_del", 6144, handle,
                                            kDeliveryPrio, &handle->delivery_task,
                                            kDeliveryCore) != pdPASS) {
                    ret = ESP_ERR_NO_MEM;
                }
            }

            if (ret == ESP_OK) {
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
                    if (!handle->streaming || handle->pause_resubmit) {
                        ESP_LOGW(TAG,
                                 "Blog V3 BULK_DIAG stream stopped before submit[%u]",
                                 static_cast<unsigned>(i));
                        ret = RTL_SDR_V4_ESP_ERR_USB;
                        break;
                    }

                    esp_err_t submit_err = usb_host_transfer_submit(handle->bulk[i]);
                    if (submit_err != ESP_OK) {
                        ESP_LOGW(TAG, "Blog V3 BULK_DIAG submit[%u] failed: %s",
                                 static_cast<unsigned>(i),
                                 esp_err_to_name(submit_err));
                        handle->streaming = false;
                        ret = RTL_SDR_V4_ESP_ERR_USB;
                        break;
                    }
                    handle->live_urbs = handle->live_urbs + 1;
                }
            }

            if (ret == ESP_OK) {
                HandleLock lk2(handle, kQueryLockTicks);
                if (lk2.ok()) {
                    set_error_unlocked(handle, ESP_OK);
                }

                rtl_sdr_v4_esp_event_cb_t cb = handle->cfg.event_cb;
                void *ctx = handle->cfg.event_ctx;
                if (cb != nullptr) {
                    emit_after_unlock(handle, RTL_SDR_V4_ESP_EVT_STREAM_STARTED, nullptr, cb, ctx);
                }
                return ESP_OK;
            }

            /*
             * Start failed after claiming interface 0.  Use the generic stop
             * path to drain anything that was submitted and return to IDLE.
             */
            (void)stop_stream_internal(handle, 1000);
        }

        ESP_LOGW(TAG, "Blog V3 START_DIAG failed: %s",
                 rtl_sdr_v4_esp_err_to_name(ret));
        HandleLock lk2(handle, kQueryLockTicks);
        if (lk2.ok()) {
            set_error_unlocked(handle, ret);
        }
        return ret;
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
            handle->live_urbs = handle->live_urbs + 1;
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
     * Queue retunes to the blocking-control worker. Repeated requests coalesce
     * to the newest pending frequency.
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
        set_error_unlocked(handle, ESP_OK);
    }

    if (handle->v3_probe_task == nullptr) {
        return RTL_SDR_V4_ESP_ERR_NOT_READY;
    }

    xTaskNotifyGive(handle->v3_probe_task);
    return ESP_OK;
}

esp_err_t rtl_sdr_v4_esp_stop(rtl_sdr_v4_esp_handle_t handle, uint32_t timeout_ms)
{
    if (handle == nullptr) {
        return ESP_OK;
    }
    if (!handle_live(handle)) {
        return RTL_SDR_V4_ESP_ERR_STALE_HANDLE;
    }
    esp_err_t re = check_not_reentrant(handle);
    if (re != ESP_OK) {
        return re;
    }
    /* Stop new USB resubmissions before taking the API lock so the higher
     * priority delivery task can drain instead of starving its owner. */
    handle->pause_resubmit = true;
    {
        HandleLock lk(handle, pdMS_TO_TICKS(timeout_ms ? timeout_ms :
                                            RTL_SDR_V4_ESP_DEFAULT_STOP_TIMEOUT_MS));
        if (!lk.ok()) {
            handle->pause_resubmit = false;
            return RTL_SDR_V4_ESP_ERR_TIMEOUT;
        }
        if (handle->state == RTL_SDR_V4_ESP_STATE_IDLE ||
            handle->state == RTL_SDR_V4_ESP_STATE_UNINSTALLED) {
            handle->pause_resubmit = false;
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
