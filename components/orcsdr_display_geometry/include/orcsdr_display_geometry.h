#pragma once
/* Board-independent pixel geometry shared by presentation and input.
 * Rectangles are half-open. Rotation is the clockwise rotation of the VIEW
 * image into the NATIVE framebuffer. Raw touch is calibrated into native
 * pixels first, then uses the inverse of that exact presentation transform.
 * No framebuffer, allocation, driver, layout density or font policy lives here.
 */
#include <stdbool.h>
#include <stdint.h>

#if defined(_WIN32) && defined(ORCSDR_GEOMETRY_EXPORTS)
#define ORCSDR_GEOMETRY_API __declspec(dllexport)
#else
#define ORCSDR_GEOMETRY_API
#endif
#ifdef __cplusplus
extern "C" {
#endif

typedef struct { int32_t x, y; } orcsdr_point_t;
typedef struct { int32_t x, y, width, height; } orcsdr_rect_t;
typedef struct {
    int32_t native_width, native_height;
    int32_t rotation; /* 0, 90, 180, 270 degrees clockwise into native storage */
    int32_t width, height;
    orcsdr_rect_t safe; /* usable bounds in view pixels, after rotating physical insets */
} orcsdr_display_geometry_t;
typedef struct {
    int32_t x_min, x_max, y_min, y_max; /* inclusive raw endpoints, before swapping */
    int32_t swap_axes, mirror_x, mirror_y; /* mirrors apply in native pixel axes */
} orcsdr_touch_calibration_t;

/* Native insets: left, top, right, bottom. Output is unchanged on any error.
 * Panel dimensions are bounded to 32768; empty safe areas are rejected.
 * Reinitialize geometry for rotation; do not edit its derived fields manually.
 */
ORCSDR_GEOMETRY_API bool orcsdr_geometry_init(orcsdr_display_geometry_t *out,
    int32_t native_width, int32_t native_height, int32_t rotation,
    int32_t left, int32_t top, int32_t right, int32_t bottom);
ORCSDR_GEOMETRY_API bool orcsdr_view_to_native(const orcsdr_display_geometry_t *g,
    orcsdr_point_t point, orcsdr_point_t *out);
ORCSDR_GEOMETRY_API bool orcsdr_native_to_view(const orcsdr_display_geometry_t *g,
    orcsdr_point_t point, orcsdr_point_t *out);
/* Clips to view bounds before rotating, including one-pixel edge rectangles.
 * False means no nonempty region: output is unchanged, caller must not flush.
 */
ORCSDR_GEOMETRY_API bool orcsdr_view_rect_to_native(const orcsdr_display_geometry_t *g,
    orcsdr_rect_t rect, orcsdr_rect_t *out);
/* Invalid/out-of-range samples are rejected, not silently clamped to a button.
 * The controller/gesture owner handles release/cancellation, not this transform.
 */
ORCSDR_GEOMETRY_API bool orcsdr_touch_to_view(const orcsdr_display_geometry_t *g,
    const orcsdr_touch_calibration_t *calibration, orcsdr_point_t raw, orcsdr_point_t *out);

#ifdef __cplusplus
}
#endif
