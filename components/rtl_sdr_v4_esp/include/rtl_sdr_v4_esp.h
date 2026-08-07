/**
 * @file rtl_sdr_v4_esp.h
 * @brief RTL-SDRv4-ESP — production public C API
 *
 * Standalone ESP-IDF USB Host client for the official RTL-SDR Blog V4
 * (USB 0bda:2838). Transfer sequences are clean-room / measured — this is
 * not a librtlsdr port.
 *
 * ---------------------------------------------------------------------------
 * Lifecycle (per handle)
 * ---------------------------------------------------------------------------
 *
 *   UNINSTALLED
 *        | install()
 *        v
 *   IDLE  <---------------------------------------------+
 *    | start()                                          |
 *    v                                                  |
 *   STREAMING ---- retune_hz() (in-stream, queued)      |
 *    |                                                  |
 *    +---- stop()  -------------------------------------+
 *    |
 *    +---- disconnect / fatal error ----> FAULT
 *                                            | recover/stop/uninstall
 *                                            v
 *                                         IDLE / destroyed
 *
 *   uninstall() is always safe from IDLE, STREAMING, or FAULT (idempotent
 *   after first successful destroy). stop() is idempotent when not streaming.
 *
 * ---------------------------------------------------------------------------
 * Threading model (must work every time)
 * ---------------------------------------------------------------------------
 *
 * - All public functions are **thread-safe** with respect to each other for a
 *   given handle (internal mutex), except where noted.
 * - IQ / event callbacks run on the **driver USB owner task** (or a dedicated
 *   delivery task). Callbacks must:
 *     - return quickly (no display paint, flash write, or long network block)
 *     - not call install/uninstall/start/stop recursively on the same handle
 *     - treat IQ block pointers as valid only until the callback returns
 *       unless acquire/release is used (when enabled)
 * - Never call retune_hz / start / stop from a USB completion ISR.
 * - Application may call get_state / get_metrics / get_device_info from any
 *   task at any time.
 *
 * ---------------------------------------------------------------------------
 * Ownership
 * ---------------------------------------------------------------------------
 *
 * - One handle owns one logical V4 session (interface 0).
 * - USB Host Library: if host_library_already_installed is false, the driver
 *   installs/uninstalls the host stack; if true, the app owns install and must
 *   keep the stack alive for the handle lifetime.
 * - Only one stream per handle.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Version                                                                    */
/* -------------------------------------------------------------------------- */

/** Semantic version of this public header / binary API. */
#define RTL_SDR_V4_ESP_VERSION_MAJOR 0
#define RTL_SDR_V4_ESP_VERSION_MINOR 2
#define RTL_SDR_V4_ESP_VERSION_PATCH 0

#define RTL_SDR_V4_ESP_VERSION_NUMBER                                      \
    ((RTL_SDR_V4_ESP_VERSION_MAJOR * 10000) +                              \
     (RTL_SDR_V4_ESP_VERSION_MINOR * 100) + RTL_SDR_V4_ESP_VERSION_PATCH)

/**
 * Pack/unpack helpers for rtl_sdr_v4_esp_get_version().
 * Example: 0x00020000 => 0.2.0 when using (maj<<16)|(min<<8)|patch layout below.
 */
uint32_t rtl_sdr_v4_esp_get_version(void);

/** Human-readable version string, e.g. "0.2.0". Never NULL. */
const char *rtl_sdr_v4_esp_get_version_string(void);

/* -------------------------------------------------------------------------- */
/* Errors (component-specific; also use standard esp_err_t)                     */
/* -------------------------------------------------------------------------- */

/** Base for component errors (avoid clash with IDF core). */
#define RTL_SDR_V4_ESP_ERR_BASE           0x12A00

#define RTL_SDR_V4_ESP_ERR_NO_DEVICE      (RTL_SDR_V4_ESP_ERR_BASE + 1)
#define RTL_SDR_V4_ESP_ERR_NOT_V4         (RTL_SDR_V4_ESP_ERR_BASE + 2)
#define RTL_SDR_V4_ESP_ERR_BUSY           (RTL_SDR_V4_ESP_ERR_BASE + 3)
#define RTL_SDR_V4_ESP_ERR_NOT_STREAMING  (RTL_SDR_V4_ESP_ERR_BASE + 4)
#define RTL_SDR_V4_ESP_ERR_BAD_RATE       (RTL_SDR_V4_ESP_ERR_BASE + 5)
#define RTL_SDR_V4_ESP_ERR_BAD_FREQ       (RTL_SDR_V4_ESP_ERR_BASE + 6)
#define RTL_SDR_V4_ESP_ERR_USB            (RTL_SDR_V4_ESP_ERR_BASE + 7)
#define RTL_SDR_V4_ESP_ERR_TIMEOUT        (RTL_SDR_V4_ESP_ERR_BASE + 8)
#define RTL_SDR_V4_ESP_ERR_FAULT          (RTL_SDR_V4_ESP_ERR_BASE + 9)
#define RTL_SDR_V4_ESP_ERR_NOT_READY      (RTL_SDR_V4_ESP_ERR_BASE + 10)
#define RTL_SDR_V4_ESP_ERR_UNSUPPORTED    (RTL_SDR_V4_ESP_ERR_BASE + 11)
#define RTL_SDR_V4_ESP_ERR_STALE_HANDLE   (RTL_SDR_V4_ESP_ERR_BASE + 12)

/** Convert esp_err_t (including component codes) to a stable string. */
const char *rtl_sdr_v4_esp_err_to_name(esp_err_t err);

/* -------------------------------------------------------------------------- */
/* Constants (policy)                                                         */
/* -------------------------------------------------------------------------- */

/** Official Blog V4 USB identity (measured). */
#define RTL_SDR_V4_ESP_USB_VID            0x0BDA
#define RTL_SDR_V4_ESP_USB_PID            0x2838

/** Measured sustainable sample rate on Tab5 continuous path (Hz). */
#define RTL_SDR_V4_ESP_RATE_960K          960000u
/** Allowlisted higher rates for future HS Ethernet apps (may require eth). */
#define RTL_SDR_V4_ESP_RATE_1024K         1024000u
#define RTL_SDR_V4_ESP_RATE_2048K         2048000u

/** Frequency policy (Hz) for CUSTOM_HZ until calibrated wider bands are proven. */
#define RTL_SDR_V4_ESP_FREQ_MIN_HZ        24000000u
#define RTL_SDR_V4_ESP_FREQ_MAX_HZ        1766000000u
/** Quantization applied by retune_hz / start (Hz). */
#define RTL_SDR_V4_ESP_FREQ_QUANT_HZ      1000u

/** Bulk transfer defaults (bytes). Must be multiple of 512 for HS bulk. */
#define RTL_SDR_V4_ESP_DEFAULT_XFER_BYTES 32768u
#define RTL_SDR_V4_ESP_MIN_XFER_BYTES     512u
#define RTL_SDR_V4_ESP_MAX_XFER_BYTES     262144u
#define RTL_SDR_V4_ESP_DEFAULT_XFER_COUNT 3u
#define RTL_SDR_V4_ESP_MIN_XFER_COUNT     2u
#define RTL_SDR_V4_ESP_MAX_XFER_COUNT     8u

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

typedef struct rtl_sdr_v4_esp_handle *rtl_sdr_v4_esp_handle_t;

typedef enum {
    RTL_SDR_V4_ESP_STATE_UNINSTALLED = 0,
    RTL_SDR_V4_ESP_STATE_IDLE = 1,
    RTL_SDR_V4_ESP_STATE_STREAMING = 2,
    RTL_SDR_V4_ESP_STATE_STOPPING = 3,
    RTL_SDR_V4_ESP_STATE_FAULT = 4,
} rtl_sdr_v4_esp_state_t;

/** Allowlisted presets with measured / derived PLL tables. */
typedef enum {
    RTL_SDR_V4_ESP_PRESET_KZEL_96_1 = 0, /**< 96.1 MHz reference (measured) */
    RTL_SDR_V4_ESP_PRESET_NOAA_162_4 = 1, /**< 162.400 MHz reference (measured) */
    RTL_SDR_V4_ESP_PRESET_CUSTOM_HZ = 2,  /**< frequency_hz via driver PLL pack */
} rtl_sdr_v4_esp_preset_t;

typedef enum {
    RTL_SDR_V4_ESP_EVT_ENUMERATED = 1, /**< payload: device_info */
    RTL_SDR_V4_ESP_EVT_READY = 2,      /**< device accepted, not yet streaming */
    RTL_SDR_V4_ESP_EVT_STREAM_STARTED = 3,
    RTL_SDR_V4_ESP_EVT_IQ_BLOCK = 4,   /**< payload: iq_block (borrowed) */
    RTL_SDR_V4_ESP_EVT_STOPPED = 5,
    RTL_SDR_V4_ESP_EVT_ERROR = 6,      /**< payload: error_info */
    RTL_SDR_V4_ESP_EVT_DISCONNECTED = 7,
    RTL_SDR_V4_ESP_EVT_RETUNED = 8,    /**< payload: uint32_t frequency_hz */
} rtl_sdr_v4_esp_event_t;

/**
 * Capability bits returned by rtl_sdr_v4_esp_get_capabilities().
 * Apps must check flags rather than assuming features exist.
 */
typedef enum {
    RTL_SDR_V4_ESP_CAP_STREAM = 1u << 0,       /**< start/stop bulk IQ */
    RTL_SDR_V4_ESP_CAP_RETUNE = 1u << 1,       /**< in-stream retune_hz */
    RTL_SDR_V4_ESP_CAP_HOTPLUG = 1u << 2,      /**< disconnect/reconnect events */
    RTL_SDR_V4_ESP_CAP_METRICS = 1u << 3,      /**< get_metrics live */
    RTL_SDR_V4_ESP_CAP_CUSTOM_HZ = 1u << 4,    /**< CUSTOM_HZ preset */
    RTL_SDR_V4_ESP_CAP_BIAS_TEE = 1u << 5,     /**< reserved; not yet measured */
    RTL_SDR_V4_ESP_CAP_DIRECT_SAMPLING = 1u << 6, /**< reserved; not claimed */
} rtl_sdr_v4_esp_cap_t;

typedef struct {
    uint16_t vid;
    uint16_t pid;
    char serial[32];
    char manufacturer[48];
    char product[48];
    bool high_speed;
    bool present; /**< false if no V4 currently attached */
} rtl_sdr_v4_esp_device_info_t;

typedef struct {
    uint64_t bytes_total;
    uint32_t blocks_total;
    uint32_t short_transfers;
    uint32_t overruns;       /**< USB side could not keep consumer fed / free slots */
    uint32_t consumer_drops; /**< app too slow (if ring drops newest/oldest) */
    uint8_t sample_min;
    uint8_t sample_max;
    float sample_mean; /**< not double: stable ABI, enough precision */
    uint32_t effective_sps;
    uint32_t frequency_hz;
    uint32_t sample_rate_sps;
    uint32_t last_error;     /**< last component/esp error code */
    uint32_t uptime_ms;      /**< stream uptime while streaming */
} rtl_sdr_v4_esp_metrics_t;

/**
 * Borrowed IQ view. Valid only for the duration of EVT_IQ_BLOCK callback
 * unless documented acquire/release is used.
 *
 * Format: interleaved unsigned IQ (I0,Q0,I1,Q1,...) CU8.
 */
typedef struct {
    const uint8_t *data;
    size_t bytes;          /**< always even; multiple of 2 */
    uint32_t sequence;     /**< monotonic per stream, wraps */
    uint32_t frequency_hz; /**< LO after last successful tune */
    uint32_t sample_rate_sps;
    int64_t host_timestamp_us; /**< esp_timer_get_time() style; 0 if unknown */
} rtl_sdr_v4_esp_iq_block_t;

typedef struct {
    esp_err_t code;
    char message[96];
} rtl_sdr_v4_esp_error_info_t;

/**
 * Event callback.
 * @param event  Event kind.
 * @param payload  Event-specific pointer (may be NULL). See event enum.
 * @param user_ctx  Value from config.event_ctx.
 */
typedef void (*rtl_sdr_v4_esp_event_cb_t)(rtl_sdr_v4_esp_event_t event,
                                          const void *payload,
                                          void *user_ctx);

/**
 * Install configuration.
 *
 * struct_size must be set to sizeof(rtl_sdr_v4_esp_config_t) so future fields
 * remain backward compatible when apps are recompiled against newer headers.
 */
typedef struct {
    size_t struct_size; /**< MUST be sizeof(rtl_sdr_v4_esp_config_t) */
    /** App already called usb_host_install(); driver only registers a client. */
    bool host_library_already_installed;
    /** Bulk URB size (bytes). Must be multiple of 512 for HS. */
    size_t transfer_bytes;
    /** Driver-owned bulk buffers (>= 2). */
    size_t transfer_count;
    uint32_t control_timeout_ms;
    /** Optional. May be NULL if app only uses poll/metrics. */
    rtl_sdr_v4_esp_event_cb_t event_cb;
    void *event_ctx;
    /**
     * If true, EVT_IQ_BLOCK is still delivered but app must call
     * release_iq_block() — reserved; currently ignored (always borrow mode).
     */
    bool iq_acquire_mode;
    /** Task priority for USB owner (0 = driver default). */
    uint8_t usb_task_priority;
    /** Core affinity: 0 or 1, or 0xFF = no affinity. */
    uint8_t usb_task_core_id;
} rtl_sdr_v4_esp_config_t;

typedef struct {
    size_t struct_size; /**< MUST be sizeof(rtl_sdr_v4_esp_stream_config_t) */
    rtl_sdr_v4_esp_preset_t preset;
    /**
     * Required for CUSTOM_HZ. For named presets, ignored (driver uses fixed LO).
     * Quantized to RTL_SDR_V4_ESP_FREQ_QUANT_HZ.
     */
    uint32_t frequency_hz;
    /**
     * Sample rate (Hz). Must be allowlisted; use RTL_SDR_V4_ESP_RATE_* or
     * rtl_sdr_v4_esp_is_rate_supported().
     */
    uint32_t sample_rate_sps;
    /** 0 = continuous until stop. Else exact CU8 byte bound. */
    uint64_t max_bytes;
    /** Soft wall-clock limit for bounded capture; 0 = none. */
    uint32_t timeout_ms;
} rtl_sdr_v4_esp_stream_config_t;

/* -------------------------------------------------------------------------- */
/* Config helpers                                                             */
/* -------------------------------------------------------------------------- */

/**
 * Zero and fill defaults. Always call before setting fields.
 * Sets struct_size correctly.
 */
void rtl_sdr_v4_esp_config_default(rtl_sdr_v4_esp_config_t *config);
void rtl_sdr_v4_esp_stream_config_default(rtl_sdr_v4_esp_stream_config_t *stream);

/**
 * Validate config without installing. Returns ESP_OK or ESP_ERR_INVALID_ARG /
 * component error. Does not require a handle.
 */
esp_err_t rtl_sdr_v4_esp_config_validate(const rtl_sdr_v4_esp_config_t *config);
esp_err_t rtl_sdr_v4_esp_stream_config_validate(const rtl_sdr_v4_esp_stream_config_t *stream);

/** True if sample_rate_sps is on the allowlist for this build. */
bool rtl_sdr_v4_esp_is_rate_supported(uint32_t sample_rate_sps);

/**
 * Clamp and quantize frequency to driver policy.
 * Returns false if out of absolute range before clamp is impossible.
 */
bool rtl_sdr_v4_esp_normalize_frequency(uint32_t in_hz, uint32_t *out_hz);

/** Capability bitmask for this binary (see rtl_sdr_v4_esp_cap_t). */
uint32_t rtl_sdr_v4_esp_get_capabilities(void);

/* -------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * Create handle and prepare USB client registration path.
 * On success *out_handle is non-NULL. On failure *out_handle is NULL.
 *
 * Does not require a dongle present. Device attach is reported via events
 * when USB streaming is fully extracted.
 */
esp_err_t rtl_sdr_v4_esp_install(const rtl_sdr_v4_esp_config_t *config,
                                 rtl_sdr_v4_esp_handle_t *out_handle);

/**
 * Destroy handle. Safe to call with NULL (returns ESP_OK).
 * If streaming, performs stop first (best effort).
 * Always releases resources even if stop fails (returns last error if any).
 */
esp_err_t rtl_sdr_v4_esp_uninstall(rtl_sdr_v4_esp_handle_t handle);

/* -------------------------------------------------------------------------- */
/* Queries                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * Current state. Returns UNINSTALLED for NULL/stale handles without crashing.
 */
rtl_sdr_v4_esp_state_t rtl_sdr_v4_esp_get_state(rtl_sdr_v4_esp_handle_t handle);

/** Last error stored on handle (0 if none). */
esp_err_t rtl_sdr_v4_esp_get_last_error(rtl_sdr_v4_esp_handle_t handle);

/**
 * Copy device info. present=false if no accepted V4 is attached.
 * Thread-safe snapshot.
 */
esp_err_t rtl_sdr_v4_esp_get_device_info(rtl_sdr_v4_esp_handle_t handle,
                                         rtl_sdr_v4_esp_device_info_t *out_info);

/** Thread-safe metrics snapshot. */
esp_err_t rtl_sdr_v4_esp_get_metrics(rtl_sdr_v4_esp_handle_t handle,
                                     rtl_sdr_v4_esp_metrics_t *out_metrics);

/* -------------------------------------------------------------------------- */
/* Streaming                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * Start IQ stream: claim interface, clean-room init, sample rate, tune, bulk IN.
 *
 * @return
 *  - ESP_OK on success
 *  - ESP_ERR_INVALID_ARG / BAD_RATE / BAD_FREQ
 *  - RTL_SDR_V4_ESP_ERR_BUSY if already streaming
 *  - RTL_SDR_V4_ESP_ERR_NO_DEVICE if no V4
 *  - RTL_SDR_V4_ESP_ERR_UNSUPPORTED / ESP_ERR_NOT_FINISHED while USB path
 *    extraction is incomplete (skeleton builds)
 *  - RTL_SDR_V4_ESP_ERR_USB / TIMEOUT / FAULT on hardware failure
 *
 * On failure, handle remains IDLE (or FAULT if unrecoverable).
 */
esp_err_t rtl_sdr_v4_esp_start(rtl_sdr_v4_esp_handle_t handle,
                               const rtl_sdr_v4_esp_stream_config_t *stream);

/**
 * Request in-stream retune. Frequency is normalized (quantized/clamped).
 * Implementation queues the request and applies it only when no bulk URB is
 * outstanding (safe for continuous operation).
 *
 * @return ESP_OK if accepted (applied or queued);
 *         ERR_NOT_STREAMING / BAD_FREQ / FAULT / UNSUPPORTED otherwise.
 */
esp_err_t rtl_sdr_v4_esp_retune_hz(rtl_sdr_v4_esp_handle_t handle, uint32_t frequency_hz);

/**
 * Stop stream and run cleanup. Idempotent if already idle.
 * Blocks up to timeout_ms for USB cleanup (0 = driver default).
 */
esp_err_t rtl_sdr_v4_esp_stop(rtl_sdr_v4_esp_handle_t handle, uint32_t timeout_ms);

/**
 * Clear FAULT back to IDLE if hardware allows (no open stream).
 * If still broken, returns ERR_FAULT.
 */
esp_err_t rtl_sdr_v4_esp_reset(rtl_sdr_v4_esp_handle_t handle);

#ifdef __cplusplus
}
#endif
