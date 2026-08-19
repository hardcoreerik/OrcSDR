#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Loading splash (ORSPLASH on SD, or static poster / text fallback).
 *
 * Boot sequence:
 *   1) orcsdr_splash_begin()      — start animation / poster (non-blocking for boot work)
 *   2) initialize Wi-Fi, RTL host, NVS, … while splash runs
 *   3) orcsdr_splash_set_status() — optional status pill at the top of the art
 *   4) orcsdr_splash_set_ready()  — reveal the OrcSDR button
 *   5) orcsdr_splash_wait_start() — keep looping until the button is tapped
 *   6) orcsdr_splash_end()        — stop playback, free buffers, enter home UI
 *
 * Call M5.Display.setRotation() to the UI landscape value (1 or 3) before
 * begin() so the animation matches Home/FM.
 */
bool orcsdr_splash_begin(void);
void orcsdr_splash_set_status(const char *message);
/** Reveal or hide the OrcSDR start button. */
void orcsdr_splash_set_ready(bool ready);
/** Block while the animation loops, returning after the ready button is tapped. */
bool orcsdr_splash_wait_start(void);
bool orcsdr_splash_is_active(void);
void orcsdr_splash_end(void);

/** Legacy one-shot (begin → end). Prefer staged API. */
bool orcsdr_run_boot_splash(void);

#ifdef __cplusplus
}
#endif
