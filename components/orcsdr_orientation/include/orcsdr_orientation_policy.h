#pragma once

/* Portable, board-independent automatic display rotation policy. */
#include <stdbool.h>
#include <stdint.h>

#if defined(_WIN32) && defined(ORCSDR_ORIENTATION_EXPORTS)
#define ORCSDR_ORIENTATION_API __declspec(dllexport)
#else
#define ORCSDR_ORIENTATION_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Profile supplied matrix: calibrated sensor gravity (g) -> in-plane
       display right/down coordinates. Deliberately no board default. */
    float display_x_from_sensor[3];
    float display_y_from_sensor[3];
    uint32_t dwell_ms;       /* zero selects the conservative 500 ms default */
    float hysteresis_deg;    /* zero selects 12 degrees */
} orcsdr_orientation_config_t;

typedef struct {
    bool enabled; /* Initialize false: auto-rotation is opt-in. */
    bool has_filter;
    bool pending_manual;
    bool touch_active;
    bool geometry_locked;
    int32_t current_degrees;
    int32_t pending_degrees;
    int32_t stable_degrees;
    uint32_t stable_since_ms;
    uint32_t last_sample_ms;
    float filtered[3];
    orcsdr_orientation_config_t config;
} orcsdr_orientation_policy_t;

ORCSDR_ORIENTATION_API bool orcsdr_orientation_policy_init(orcsdr_orientation_policy_t *state,
    const orcsdr_orientation_config_t *profile_axes, int32_t initial_degrees);
ORCSDR_ORIENTATION_API void orcsdr_orientation_policy_enable(orcsdr_orientation_policy_t *state, bool enabled);
ORCSDR_ORIENTATION_API void orcsdr_orientation_policy_set_gates(orcsdr_orientation_policy_t *state,
    bool touch_active, bool geometry_locked);
ORCSDR_ORIENTATION_API bool orcsdr_orientation_policy_request_manual(orcsdr_orientation_policy_t *state,
    int32_t degrees);

/* Sample is calibrated acceleration in g (including gravity). Returns true
   only when a new rotation is ready to apply. Output angles are clockwise
   shared-geometry angles 0/90/180/270. Caller should poll at ~20 Hz. */
ORCSDR_ORIENTATION_API bool orcsdr_orientation_policy_update(orcsdr_orientation_policy_t *state,
    float sensor_x_g, float sensor_y_g, float sensor_z_g, uint32_t now_ms,
    int32_t *proposed_degrees);

#ifdef __cplusplus
}
#endif
