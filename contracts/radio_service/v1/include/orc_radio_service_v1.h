/* OrcSDR Radio Service v1 — PLATFORM-NEUTRAL INTERFACE DESIGN.
 *
 * This header is an additive contract/prototype, not a production provider.
 * Conforming providers own their receiver drivers, buffers and DSP.
 * Internal C ABI only: struct layout is NOT an on-the-wire/network format.
 * No platform SDK, FreeRTOS, C++, Dart or allocation dependency.
 *
 * Every object has a byte-size prefix for forward ABI extension. A caller
 * zeroes output structures and sets struct_size before invoking providers.
 * Functions return zero for success or a negative status code. No pointer
 * returned here is owned by a caller unless explicitly copied.
 */
#ifndef ORC_RADIO_SERVICE_V1_H
#define ORC_RADIO_SERVICE_V1_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ORC_RADIO_V1_ABI_MAJOR 1u
#define ORC_RADIO_V1_ABI_MINOR 0u
#define ORC_RADIO_V1_ID_LENGTH 96u
#define ORC_RADIO_V1_MESSAGE_LENGTH 160u

typedef enum orc_radio_v1_error {
    ORC_RADIO_V1_OK = 0,
    ORC_RADIO_V1_INVALID_ARGUMENT = -1,
    ORC_RADIO_V1_UNSUPPORTED_CAPABILITY = -2,
    ORC_RADIO_V1_OUT_OF_RANGE = -3,
    ORC_RADIO_V1_STALE_GENERATION = -4,
    ORC_RADIO_V1_ATTACHMENT_CHANGED = -5,
    ORC_RADIO_V1_RECEIVER_BUSY = -6,
    ORC_RADIO_V1_UNAUTHORIZED = -7,
    ORC_RADIO_V1_HARDWARE_FAILURE = -8,
    ORC_RADIO_V1_TIMEOUT = -9,
    ORC_RADIO_V1_NOT_READY = -10,
    ORC_RADIO_V1_VERSION_INCOMPATIBLE = -11,
    ORC_RADIO_V1_TRANSPORT_UNAVAILABLE = -12
} orc_radio_v1_error_t;

typedef enum orc_radio_v1_receiver_state {
    ORC_RADIO_V1_ABSENT = 0,
    ORC_RADIO_V1_PRESENT = 1,
    ORC_RADIO_V1_READY = 2,
    ORC_RADIO_V1_CONFIGURING = 3,
    ORC_RADIO_V1_STREAMING = 4,
    ORC_RADIO_V1_STOPPING = 5,
    ORC_RADIO_V1_ERROR_STATE = 6
} orc_radio_v1_receiver_state_t;

typedef enum orc_radio_v1_command_op {
    ORC_RADIO_V1_TUNE = 1,
    ORC_RADIO_V1_SET_RATE = 2,
    ORC_RADIO_V1_SET_GAIN = 3,
    ORC_RADIO_V1_SET_BIAS_TEE = 4,
    ORC_RADIO_V1_START_STREAM = 5,
    ORC_RADIO_V1_STOP_STREAM = 6,
    ORC_RADIO_V1_START_RECORD = 7,
    ORC_RADIO_V1_STOP_RECORD = 8
} orc_radio_v1_command_op_t;

typedef enum orc_radio_v1_outcome {
    ORC_RADIO_V1_UNAVAILABLE = 0,
    ORC_RADIO_V1_PENDING = 1,
    ORC_RADIO_V1_APPLIED = 2,
    ORC_RADIO_V1_FAILED = 3
} orc_radio_v1_outcome_t;

typedef enum orc_radio_v1_frame_kind {
    ORC_RADIO_V1_FRAME_IQ = 1,
    ORC_RADIO_V1_FRAME_SPECTRUM = 2,
    ORC_RADIO_V1_FRAME_AUDIO = 3
} orc_radio_v1_frame_kind_t;

typedef enum orc_radio_v1_frame_format {
    ORC_RADIO_V1_CU8 = 1,
    ORC_RADIO_V1_CS8 = 2,
    ORC_RADIO_V1_CS16LE = 3,
    ORC_RADIO_V1_CF32LE = 4,
    ORC_RADIO_V1_SPECTRUM_F32_DBFS = 5,
    ORC_RADIO_V1_SPECTRUM_F32_RELATIVE = 6,
    ORC_RADIO_V1_AUDIO_S16LE = 7,
    ORC_RADIO_V1_AUDIO_F32LE = 8
} orc_radio_v1_frame_format_t;

enum orc_radio_v1_scope_bit {
    ORC_RADIO_V1_SCOPE_OBSERVE = 1u << 0,
    ORC_RADIO_V1_SCOPE_TUNE = 1u << 1,
    ORC_RADIO_V1_SCOPE_STREAM = 1u << 2,
    ORC_RADIO_V1_SCOPE_RECORD = 1u << 3,
    ORC_RADIO_V1_SCOPE_ADMIN = 1u << 4,
    ORC_RADIO_V1_SCOPE_TX = 1u << 5
};

typedef struct orc_radio_v1_identity {
    uint32_t struct_size;
    char host_id[ORC_RADIO_V1_ID_LENGTH];
    char boot_id[ORC_RADIO_V1_ID_LENGTH];
    char receiver_id[ORC_RADIO_V1_ID_LENGTH];
    char attachment_id[ORC_RADIO_V1_ID_LENGTH];
    char session_id[ORC_RADIO_V1_ID_LENGTH];
    uint64_t generation;
} orc_radio_v1_identity_t;

typedef struct orc_radio_v1_time {
    uint32_t struct_size;
    uint64_t monotonic_ns;
    uint64_t utc_unix_ms;
    uint32_t utc_uncertainty_ms;
    uint8_t utc_valid;
    uint8_t utc_uncertainty_known;
    /* Clock source is represented as its negotiated string in JSON.
       Zero/invalid UTC is NOT silently replaced with local system time. */
} orc_radio_v1_time_t;

typedef struct orc_radio_v1_range {
    uint64_t min_hz;
    uint64_t max_hz;
} orc_radio_v1_range_t;

typedef struct orc_radio_v1_capabilities {
    uint32_t struct_size;
    uint32_t scope_flags_available; /* supported roles are NOT granted roles */
    uint32_t feature_flags;         /* see ORC_RADIO_V1_HAS_* */
    uint32_t tuner_flags;           /* see ORC_RADIO_V1_TUNER_* */
    const orc_radio_v1_range_t *frequency_ranges; /* borrowed during call */
    size_t frequency_range_count;
    const uint32_t *sample_rates_sps; /* borrowed during call */
    size_t sample_rate_count;
    uint8_t tx_supported;            /* never implies permission to transmit */
    uint8_t has_usb;
    uint8_t has_network;
    uint8_t has_display;
} orc_radio_v1_capabilities_t;

enum orc_radio_v1_feature_flag {
    ORC_RADIO_V1_HAS_IQ = 1u << 0,
    ORC_RADIO_V1_HAS_SPECTRUM = 1u << 1,
    ORC_RADIO_V1_HAS_AUDIO = 1u << 2,
    ORC_RADIO_V1_HAS_DECODER_EVENTS = 1u << 3,
    ORC_RADIO_V1_HAS_SCAN = 1u << 4,
    ORC_RADIO_V1_HAS_RECORD = 1u << 5
};
enum orc_radio_v1_tuner_flag {
    ORC_RADIO_V1_TUNER_AUTO_GAIN = 1u << 0,
    ORC_RADIO_V1_TUNER_MANUAL_GAIN = 1u << 1,
    ORC_RADIO_V1_TUNER_RTL_AGC = 1u << 2,
    ORC_RADIO_V1_TUNER_BIAS_TEE = 1u << 3,
    ORC_RADIO_V1_TUNER_DIRECT_SAMPLE = 1u << 4,
    ORC_RADIO_V1_TUNER_PPM_CORRECTION = 1u << 5
};

typedef struct orc_radio_v1_snapshot {
    uint32_t struct_size;
    orc_radio_v1_identity_t identity;
    uint32_t state;                     /* orc_radio_v1_receiver_state_t */
    char owner_id[ORC_RADIO_V1_ID_LENGTH];
    char pending_command_id[ORC_RADIO_V1_ID_LENGTH];
    uint64_t applied_center_frequency_hz; /* zero = unknown, not 0 Hz */
    uint32_t applied_sample_rate_sps;     /* zero = unknown */
    int32_t applied_gain_tenth_db;
    uint8_t gain_known;
} orc_radio_v1_snapshot_t;

typedef struct orc_radio_v1_command {
    uint32_t struct_size;
    orc_radio_v1_identity_t identity; /* mandatory boot/attachment/session */
    char command_id[ORC_RADIO_V1_ID_LENGTH];
    uint32_t operation; /* orc_radio_v1_command_op_t */
    uint32_t scope_flags_granted; /* authenticated authority checked by host */
    uint64_t expected_generation;
    uint64_t requested_frequency_hz;    /* 0 = not specified */
    uint32_t requested_sample_rate_sps; /* 0 = not specified */
    int32_t requested_gain_tenth_db;
    uint8_t gain_known;
    uint8_t requested_enabled;
} orc_radio_v1_command_t;

typedef struct orc_radio_v1_command_result {
    uint32_t struct_size;
    char command_id[ORC_RADIO_V1_ID_LENGTH];
    uint32_t operation;
    uint32_t outcome; /* orc_radio_v1_outcome_t */
    int32_t error; /* orc_radio_v1_error_t; 0 for pending/applied */
    uint64_t result_generation;
    uint64_t applied_center_frequency_hz;
    uint32_t applied_sample_rate_sps;
    uint8_t backend_ack; /* true only with verified hardware completion */
    char ack_source[ORC_RADIO_V1_MESSAGE_LENGTH];
} orc_radio_v1_command_result_t;

typedef struct orc_radio_v1_frame {
    uint32_t struct_size;
    orc_radio_v1_identity_t identity;
    uint64_t sequence;
    orc_radio_v1_time_t time;
    uint32_t kind;    /* orc_radio_v1_frame_kind_t */
    uint32_t format;  /* orc_radio_v1_frame_format_t */
    uint64_t center_frequency_hz;
    uint32_t sample_rate_sps;
    uint64_t span_hz; /* spectrum only; zero otherwise */
    uint32_t channels; /* audio only; zero otherwise */
    uint32_t fft_size; /* spectrum only; zero otherwise */
    uint64_t first_sample_index;
    uint64_t samples_or_frames;
    uint64_t dropped_since_previous; /* source unit specified by kind */
    uint8_t dropped_known;
    const void *bytes; /* borrowed UNTIL callback returns, never retained */
    size_t byte_count;
} orc_radio_v1_frame_t;

typedef void (*orc_radio_v1_frame_callback_t)(
    void *user, const orc_radio_v1_frame_t *frame);

/* Low-rate events such as decoder_event, health and command_result are
 * UTF-8 JSON envelopes matching schema.json; NOT raw I/Q data.
 * bytes are borrowed until callback returns.
 */
typedef void (*orc_radio_v1_json_callback_t)(
    void *user, const char *json, size_t json_bytes);

/* The provider's C ABI vtable. No network is required for any method.
 * Subscribe/unsubscribe is session-scoped; the returned subscription token
 * must be invalidated on attachment/session change.
 * Calls are serialized by a provider, not by this header.
 */
typedef struct orc_radio_v1_provider {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    void *context;
    int (*enumerate)(void *ctx, orc_radio_v1_identity_t *out,
                     size_t capacity, size_t *written, size_t *total);
    int (*capabilities)(void *ctx, const char *receiver_id,
                        orc_radio_v1_capabilities_t *out);
    int (*snapshot)(void *ctx, const char *receiver_id,
                    orc_radio_v1_snapshot_t *out);
    int (*command)(void *ctx, const orc_radio_v1_command_t *command,
                   orc_radio_v1_command_result_t *out);
    int (*subscribe)(void *ctx, const char *receiver_id,
                     orc_radio_v1_frame_callback_t on_frame,
                     orc_radio_v1_json_callback_t on_event,
                     void *user, uint64_t *subscription);
    int (*unsubscribe)(void *ctx, uint64_t subscription);
} orc_radio_v1_provider_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ORC_RADIO_SERVICE_V1_H */
