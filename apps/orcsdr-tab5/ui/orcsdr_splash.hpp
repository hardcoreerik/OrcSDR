#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Loading splash: a 1280x720 JPEG embedded in the firmware
 * (main/orcsdr_splash_1280x720.jpg). No microSD asset is needed.
 *
 * Boot sequence:
 *   1) orcsdr_splash_begin()      — draw the image; also powers and mounts microSD
 *   2) initialize RTL host, NVS, … while the image is shown
 *   3) orcsdr_splash_set_status() — optional status line at the top of the art
 *   4) orcsdr_splash_set_ready()  — reveal the OrcSDR button
 *   5) orcsdr_splash_wait_start() — wait for the button (or the auto-enter timeout)
 *   6) orcsdr_splash_end()        — clear the screen and enter the home UI
 *
 * Call M5.Display.setRotation() to the UI landscape value (1 or 3) before
 * begin() so the image matches Home/FM.
 */
bool orcsdr_splash_begin(void);
void orcsdr_splash_set_status(const char *message);
/** Reveal the OrcSDR start button. */
void orcsdr_splash_set_ready(bool ready);
/**
 * Block until the OrcSDR button is tapped. auto_enter_ms > 0 continues on its
 * own after that long so an unattended reboot still reaches Home; 0 waits.
 */
bool orcsdr_splash_wait_start(uint32_t auto_enter_ms);
bool orcsdr_splash_is_active(void);
void orcsdr_splash_end(void);

/** Legacy one-shot (begin → end). Prefer staged API. */
bool orcsdr_run_boot_splash(void);

#ifdef __cplusplus
}
#endif
