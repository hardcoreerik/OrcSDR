#pragma once

/**
 * Restart the whole chip through the RTC watchdog's system-reset action instead of esp_restart().
 *
 * On the ESP32-P4, esp_restart() and crash reboots are CPU-level resets. After one, about one boot
 * in five found the microSD card unresponsive and the shared SDMMC bus wedged (issue #127), while
 * a chip-level reset never did (0 failures in 60 boots). Use this for app-initiated restarts.
 */
[[noreturn]] void orcsdr_restart_clean();

/**
 * Call first thing in app_main(). If the previous reset was CPU-level (esp_restart, panic or a
 * task/interrupt watchdog), restart cleanly once before touching the SD card, and log the original
 * reason. A marker in RTC memory guarantees this cannot loop.
 */
void orcsdr_normalize_reset_at_boot();
