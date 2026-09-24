/*
 * OrcSDR Tab5 loading splash — a 1280x720 JPEG embedded in the firmware
 * (main/orcsdr_splash_1280x720.jpg, the README hero image), so every device
 * shows it without any microSD asset.
 */

#include "orcsdr_splash.hpp"

#include <M5Unified.h>

#include "orcsdr_storage.hpp"

#include <driver/gpio.h>
#include <driver/usb_serial_jtag.h>

#include <atomic>
#include <cstdarg>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/* Main app services COM17 while the loading screen owns setup(). */
void orcsdr_splash_poll_serial(void);

extern const uint8_t orcsdr_splash_jpg_start[] asm("_binary_orcsdr_splash_1280x720_jpg_start");
extern const uint8_t orcsdr_splash_jpg_end[] asm("_binary_orcsdr_splash_1280x720_jpg_end");

namespace {

constexpr int kTab5SdPowerPin = 45;
/** Status text sits in the black band at the top of the art. */
constexpr int kStatusY = 18;
constexpr int kStatusBarH = 28;
constexpr int kReadyButtonX = 440;
constexpr int kReadyButtonY = 600;
constexpr int kReadyButtonW = 400;
constexpr int kReadyButtonH = 82;

std::atomic<bool> g_active{false};

/** Log via USB-Serial/JTAG (main redefines Serial only in main.cpp). */
void splash_log(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  if (!usb_serial_jtag_is_driver_installed()) {
    usb_serial_jtag_driver_config_t cfg = {
        .tx_buffer_size = 2048,
        .rx_buffer_size = 256,
    };
    usb_serial_jtag_driver_install(&cfg);
  }
  usb_serial_jtag_write_bytes(buf, static_cast<size_t>(n), pdMS_TO_TICKS(50));
  if (buf[n - 1] != '\n') {
    const char nl = '\n';
    usb_serial_jtag_write_bytes(&nl, 1, pdMS_TO_TICKS(20));
  }
}

/*
 * Boot relies on the splash having powered and mounted the microSD card: the
 * main application adopts this mount afterwards. The artwork no longer comes
 * from SD, but keep this side effect so the startup sequence is unchanged.
 */
void splash_sd_begin() {
  gpio_config_t power{};
  power.pin_bit_mask = 1ULL << kTab5SdPowerPin;
  power.mode = GPIO_MODE_OUTPUT;
  gpio_config(&power);
  gpio_set_level(static_cast<gpio_num_t>(kTab5SdPowerPin), 1);
  delay(80);
  splash_log(orcsdr::storage::mount_tab5_sd() ? "SPLASH_SD ready bus=sdmmc"
                                              : "SPLASH_SD fail sdmmc");
}

void draw_status_strip(const char* msg) {
  const char* text = msg && msg[0] ? msg : "Loading…";
  M5.Display.fillRect(0, kStatusY, M5.Display.width(), kStatusBarH, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.drawString(text, M5.Display.width() / 2, kStatusY + kStatusBarH / 2);
}

void draw_ready_button() {
  M5.Display.fillRoundRect(kReadyButtonX, kReadyButtonY, kReadyButtonW, kReadyButtonH, 18,
                           TFT_DARKCYAN);
  M5.Display.drawRoundRect(kReadyButtonX, kReadyButtonY, kReadyButtonW, kReadyButtonH, 18,
                           TFT_CYAN);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(TFT_WHITE, TFT_DARKCYAN);
  M5.Display.drawString("OrcSDR", kReadyButtonX + kReadyButtonW / 2,
                        kReadyButtonY + kReadyButtonH / 2);
}

void draw_splash_image() {
  const size_t bytes = static_cast<size_t>(orcsdr_splash_jpg_end - orcsdr_splash_jpg_start);
  const uint32_t started_ms = millis();
  M5.Display.fillScreen(TFT_BLACK);
  if (M5.Display.drawJpg(orcsdr_splash_jpg_start, bytes, 0, 0)) {
    splash_log("SPLASH_IMAGE ok bytes=%u ms=%u", static_cast<unsigned>(bytes),
               static_cast<unsigned>(millis() - started_ms));
    return;
  }
  splash_log("SPLASH_IMAGE draw_fail bytes=%u", static_cast<unsigned>(bytes));
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.setTextSize(6);
  M5.Display.drawString("OrcSDR", M5.Display.width() / 2, M5.Display.height() / 2);
}

}  // namespace

bool orcsdr_splash_begin(void) {
  splash_log("SPLASH_BOOT begin (loading screen)");
  uint8_t rotation = M5.Display.getRotation();
  if (rotation != 3) rotation = 1;
  M5.Display.setRotation(rotation);
  splash_log("SPLASH_ROTATION ui=%u", static_cast<unsigned>(rotation));
  g_active.store(true, std::memory_order_release);
  splash_sd_begin();
  draw_splash_image();
  orcsdr_splash_set_status("Starting…");
  splash_log("SPLASH_MODE embedded");
  return true;
}

void orcsdr_splash_set_status(const char* message) {
  splash_log("SPLASH_STATUS %s", message ? message : "");
  if (g_active.load(std::memory_order_acquire)) draw_status_strip(message);
}

void orcsdr_splash_set_ready(bool ready) {
  orcsdr_splash_set_status(ready ? "Ready" : "Loading…");
  if (ready && g_active.load(std::memory_order_acquire)) draw_ready_button();
  splash_log("SPLASH_READY %d", ready ? 1 : 0);
}

bool orcsdr_splash_wait_start(uint32_t auto_enter_ms) {
  splash_log("SPLASH_WAIT start auto_enter_ms=%u", static_cast<unsigned>(auto_enter_ms));
  const uint32_t started_ms = millis();
  bool was_pressed = false;
  while (g_active.load(std::memory_order_acquire)) {
    orcsdr_splash_poll_serial();
    if (!g_active.load(std::memory_order_acquire)) break;
    if (auto_enter_ms != 0 && millis() - started_ms >= auto_enter_ms) {
      splash_log("SPLASH_AUTO_ENTER");
      return true;
    }
    M5.update();
    const auto touch = M5.Touch.getDetail(0);
    const bool pressed = touch.isPressed() || touch.wasPressed();
    if (pressed && !was_pressed &&
        touch.x >= kReadyButtonX && touch.x < kReadyButtonX + kReadyButtonW &&
        touch.y >= kReadyButtonY && touch.y < kReadyButtonY + kReadyButtonH) {
      orcsdr_splash_set_status("Opening OrcSDR…");
      splash_log("SPLASH_START x=%ld y=%ld", static_cast<long>(touch.x),
                 static_cast<long>(touch.y));
      return true;
    }
    was_pressed = pressed;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return false;
}

bool orcsdr_splash_is_active(void) {
  return g_active.load(std::memory_order_acquire);
}

void orcsdr_splash_end(void) {
  g_active.store(false, std::memory_order_release);
  M5.Display.fillScreen(TFT_BLACK);
  splash_log("SPLASH_BOOT done");
}

bool orcsdr_run_boot_splash(void) {
  (void)orcsdr_splash_begin();
  orcsdr_splash_end();
  return true;
}
