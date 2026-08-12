/*
 * OrcSDR Tab5 loading splash — ORSPLASH v1 from microSD.
 *
 * Animation (or poster) plays while the app initializes Wi-Fi / RTL / NVS,
 * then keeps looping behind the ready button until the user enters OrcSDR.
 */

#include "orcsdr_splash.hpp"

#include <Arduino.h>
#include <M5Unified.h>
#include <SPI.h>
#include <SD.h>
#include <SD_MMC.h>
#include <FS.h>

#include <driver/jpeg_decode.h>
#include <driver/usb_serial_jtag.h>
#include <esp_heap_caps.h>
#include <esp_cache.h>
#include <esp_timer.h>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>

#include <atomic>
#include <cstdarg>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

/* Main app services COM17 while the loading screen owns setup(). */
void orcsdr_splash_poll_serial(void);

namespace {

#pragma pack(push, 1)
struct OrcSplashHeader {
  char magic[8];
  uint16_t version;
  uint16_t width;
  uint16_t height;
  uint16_t fps;
  uint16_t frame_count;
  uint16_t flags;
  uint32_t index_offset;
  uint32_t data_offset;
  uint32_t payload_bytes;
};

struct OrcSplashFrame {
  uint32_t offset;
  uint32_t length;
};
#pragma pack(pop)

constexpr const char* kSplashPath = "/OrcSDR_Splash_1280x720_60fps_10s.orsplash";
constexpr const char* kSplashPathAlt = "/orcsdr/OrcSDR_Splash_1280x720_60fps_10s.orsplash";
constexpr int kTab5SdCsPin = 42;
constexpr int kTab5SdSckPin = 43;
constexpr int kTab5SdMosiPin = 44;
constexpr int kTab5SdMisoPin = 39;
/** 25 MHz sustains the compact pack; fall back for marginal cards/wiring. */
constexpr uint32_t kSplashSdHz = 25000000;
constexpr int kTab5SdPowerPin = 45;
constexpr size_t kJpegSlotCount = 3;
constexpr size_t kJpegSlotBytes = 220 * 1024;
constexpr size_t kRgbDouble = 2;
constexpr uint32_t kPresentFpsCap = 30;
constexpr uint16_t kNativeWidth = 720;
constexpr uint16_t kNativeHeight = 1280;
/** Status text near the top so it does not fight the living-wave art. */
constexpr int kStatusY = 18;
constexpr int kStatusBarW = 720;
constexpr int kStatusBarH = 28;
constexpr int kReadyButtonX = 440;
constexpr int kReadyButtonY = 600;
constexpr int kReadyButtonW = 400;
constexpr int kReadyButtonH = 82;
constexpr int kReadyNativeX = kNativeWidth - (kReadyButtonY + kReadyButtonH);
constexpr int kReadyNativeY = kReadyButtonX;

struct JpegSlot {
  uint8_t* data = nullptr;
  size_t capacity = 0;
  size_t length = 0;
  uint16_t frame_index = 0;
};

enum class SplashMode : uint8_t { None, Animated, Static };

struct SplashState {
  File file;
  OrcSplashHeader header{};
  OrcSplashFrame* index = nullptr;
  jpeg_decoder_handle_t decoder = nullptr;
  uint8_t* rgb[kRgbDouble]{};
  size_t rgb_bytes = 0;
  JpegSlot jpeg[kJpegSlotCount]{};
  QueueHandle_t free_q = nullptr;
  QueueHandle_t filled_q = nullptr;
  TaskHandle_t reader_task = nullptr;
  TaskHandle_t play_task = nullptr;
  SemaphoreHandle_t display_mutex = nullptr;
  uint8_t* framebuffer = nullptr;
  size_t framebuffer_bytes = 0;
  std::atomic<bool> stop{false};
  std::atomic<bool> reader_done{false};
  std::atomic<bool> play_done{false};
  std::atomic<bool> active{false};
  std::atomic<bool> ready{false};
  /** Redraw static splash controls when this is set. */
  std::atomic<bool> chrome_dirty{true};
  char status[96]{};
  portMUX_TYPE status_mux = portMUX_INITIALIZER_UNLOCKED;
  SplashMode mode = SplashMode::None;
  bool sd_ok = false;
  bool has_poster = false;
  bool use_sdmmc = false;
  fs::FS* fs = nullptr; /* SD or SD_MMC */
};

SplashState g_splash;

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

void copy_status(char* out, size_t out_sz) {
  portENTER_CRITICAL(&g_splash.status_mux);
  strlcpy(out, g_splash.status, out_sz);
  portEXIT_CRITICAL(&g_splash.status_mux);
}

void mark_chrome_dirty() {
  g_splash.chrome_dirty.store(true, std::memory_order_release);
}

void draw_status_strip(const char* msg) {
  const int y = kStatusY;
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.drawString(msg && msg[0] ? msg : "Loading…", 642, y + kStatusBarH / 2 + 2);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawString(msg && msg[0] ? msg : "Loading…", 640, y + kStatusBarH / 2);
}

void draw_ready_button() {
  M5.Display.fillRoundRect(kReadyButtonX, kReadyButtonY, kReadyButtonW,
                           kReadyButtonH, 18, TFT_DARKCYAN);
  M5.Display.drawRoundRect(kReadyButtonX, kReadyButtonY, kReadyButtonW,
                           kReadyButtonH, 18, TFT_CYAN);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(TFT_WHITE, TFT_DARKCYAN);
  M5.Display.drawString("OrcSDR", kReadyButtonX + kReadyButtonW / 2,
                        kReadyButtonY + kReadyButtonH / 2);
}

void paint_chrome_overlay() {
  const bool dirty = g_splash.chrome_dirty.exchange(false, std::memory_order_acq_rel);
  if (g_splash.display_mutex) xSemaphoreTake(g_splash.display_mutex, portMAX_DELAY);
  char status[96];
  copy_status(status, sizeof(status));
  if (status[0] != '\0') draw_status_strip(status);
  if (dirty && g_splash.ready.load(std::memory_order_acquire)) draw_ready_button();
  if (g_splash.display_mutex) xSemaphoreGive(g_splash.display_mutex);
}

void copy_native_frame(const uint8_t* rgb) {
  if (!rgb || !g_splash.framebuffer) return;
  xSemaphoreTake(g_splash.display_mutex, portMAX_DELAY);
  constexpr size_t stride = kNativeWidth * 2;
  const bool ready = g_splash.ready.load(std::memory_order_acquire);
  for (uint16_t y = 0; y < kNativeHeight; ++y) {
    const uint8_t* src = rgb + static_cast<size_t>(y) * stride;
    uint8_t* dst = g_splash.framebuffer + static_cast<size_t>(y) * stride;
    const bool button_row =
        ready && y >= kReadyNativeY && y < kReadyNativeY + kReadyButtonW;
    if (!button_row) {
      memcpy(dst, src, stride);
      continue;
    }
    size_t x = 0;
    if (button_row) {
      memcpy(dst, src, kReadyNativeX * 2);
      x = kReadyNativeX + kReadyButtonH;
    }
    memcpy(dst + x * 2, src + x * 2, (kNativeWidth - x) * 2);
  }
  esp_cache_msync(g_splash.framebuffer, g_splash.framebuffer_bytes,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
  xSemaphoreGive(g_splash.display_mutex);
}

bool hw_decode_frame(const uint8_t* jpeg, size_t jpeg_len, uint8_t* rgb, size_t rgb_cap,
                     const jpeg_decode_cfg_t& cfg) {
  if (!g_splash.decoder || !jpeg || !rgb || jpeg_len == 0) return false;
  jpeg_decode_picture_info_t info{};
  if (jpeg_decoder_get_info(jpeg, static_cast<uint32_t>(jpeg_len), &info) != ESP_OK) {
    return false;
  }
  uint32_t out_size = 0;
  const esp_err_t err =
      jpeg_decoder_process(g_splash.decoder, &cfg, jpeg, static_cast<uint32_t>(jpeg_len), rgb,
                           static_cast<uint32_t>(rgb_cap), &out_size);
  return err == ESP_OK && out_size > 0;
}

bool try_draw_poster_from_sd() {
  if (g_splash.fs == nullptr) return false;
  static const char* kPosters[] = {
      "/OrcSDR_Splash_Poster.jpg",
      "/orcsdr/OrcSDR_Splash_Poster.jpg",
      "/OrcSDR_Splash_1280x720_60fps_10s_Poster.jpg",
      "/orcsdr/OrcSDR_Splash_1280x720_60fps_10s_Poster.jpg",
  };
  for (const char* path : kPosters) {
    if (!g_splash.fs->exists(path)) continue;
    File f = g_splash.fs->open(path, FILE_READ);
    if (!f) continue;
    const size_t n = f.size();
    if (n < 64 || n > 400 * 1024) {
      f.close();
      continue;
    }
    uint8_t* buf = static_cast<uint8_t*>(
        heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buf) buf = static_cast<uint8_t*>(malloc(n));
    if (!buf) {
      f.close();
      continue;
    }
    const size_t got = f.read(buf, n);
    f.close();
    bool ok = false;
    if (got == n) {
      ok = M5.Display.drawJpg(buf, n, 0, 0);
    }
    free(buf);
    if (ok) {
      splash_log("SPLASH_POSTER ok path=%s bytes=%u", path, static_cast<unsigned>(n));
      return true;
    }
    splash_log("SPLASH_POSTER draw_fail path=%s", path);
  }
  return false;
}

void draw_static_base() {
  M5.Display.fillScreen(TFT_BLACK);
  g_splash.has_poster = g_splash.sd_ok && try_draw_poster_from_sd();
  if (!g_splash.has_poster) {
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.setTextSize(5);
    M5.Display.drawString("OrcSDR", 640, 240);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5.Display.drawString("Tab5 + RTL-SDR Blog V4", 640, 320);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.drawString(
        "place OrcSDR_Splash_1280x720_60fps_10s.orsplash on microSD for full animation",
        640, 380);
  }
  paint_chrome_overlay();
}

bool splash_sd_begin() {
  g_splash.sd_ok = false;
  g_splash.fs = nullptr;
  g_splash.use_sdmmc = false;

  /* Tab5 can gate microSD power on GPIO45. */
  pinMode(kTab5SdPowerPin, OUTPUT);
  digitalWrite(kTab5SdPowerPin, HIGH);
  delay(80);

  /* SDIO 4-bit: clk=43 cmd=44 d0=39 d1=40 d2=41 d3=42 (M5 Tab5 pinmap). */
  SD_MMC.setPins(43, 44, 39, 40, 41, 42);
  if (SD_MMC.begin("/sd", false /* mode1bit */, false /* format_if_mount_failed */,
                   SDMMC_FREQ_HIGHSPEED)) {
    g_splash.use_sdmmc = true;
    g_splash.fs = &SD_MMC;
    g_splash.sd_ok = true;
    splash_log("SPLASH_SD ready bus=sdmmc");
    return true;
  }
  splash_log("SPLASH_SD sdmmc_fail → spi");

  SPI.begin(kTab5SdSckPin, kTab5SdMisoPin, kTab5SdMosiPin, kTab5SdCsPin);
  uint32_t actual_hz = kSplashSdHz;
  if (!SD.begin(kTab5SdCsPin, SPI, kSplashSdHz)) {
    delay(50);
    actual_hz = 10000000;
    if (!SD.begin(kTab5SdCsPin, SPI, actual_hz)) {
      actual_hz = 4000000;
      if (!SD.begin(kTab5SdCsPin, SPI, actual_hz)) {
        splash_log("SPLASH_SD fail spi");
        return false;
      }
    }
  }
  g_splash.fs = &SD;
  g_splash.sd_ok = true;
  splash_log("SPLASH_SD ready bus=spi hz=%u", static_cast<unsigned>(actual_hz));
  return true;
}

bool open_splash_file() {
  if (g_splash.fs == nullptr) return false;
  /* Managed COM17 uploads live in /orcsdr and override legacy root assets. */
  if (g_splash.fs->exists(kSplashPathAlt)) {
    g_splash.file = g_splash.fs->open(kSplashPathAlt, FILE_READ);
  } else if (g_splash.fs->exists(kSplashPath)) {
    g_splash.file = g_splash.fs->open(kSplashPath, FILE_READ);
  }
  if (!g_splash.file) {
    splash_log("SPLASH_FILE missing");
    return false;
  }
  splash_log("SPLASH_FILE open size=%u", static_cast<unsigned>(g_splash.file.size()));
  return true;
}

bool load_header_and_index() {
  if (g_splash.file.read(reinterpret_cast<uint8_t*>(&g_splash.header),
                         sizeof(g_splash.header)) != sizeof(g_splash.header)) {
    splash_log("SPLASH_HDR short_read");
    return false;
  }
  if (memcmp(g_splash.header.magic, "ORCSPLSH", 8) != 0 || g_splash.header.version != 1) {
    splash_log("SPLASH_HDR bad");
    return false;
  }
  if (g_splash.header.width != kNativeWidth || g_splash.header.height != kNativeHeight ||
      g_splash.header.frame_count == 0 || g_splash.header.fps == 0) {
    splash_log("SPLASH_HDR geometry");
    return false;
  }
  const size_t index_bytes =
      static_cast<size_t>(g_splash.header.frame_count) * sizeof(OrcSplashFrame);
  g_splash.index = static_cast<OrcSplashFrame*>(
      heap_caps_malloc(index_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_splash.index) g_splash.index = static_cast<OrcSplashFrame*>(malloc(index_bytes));
  if (!g_splash.index) return false;
  if (!g_splash.file.seek(g_splash.header.index_offset)) return false;
  if (g_splash.file.read(reinterpret_cast<uint8_t*>(g_splash.index), index_bytes) !=
      index_bytes) {
    return false;
  }
  /* Validate max frame size fits our JPEG slots. */
  uint32_t max_len = 0;
  for (uint16_t i = 0; i < g_splash.header.frame_count; ++i) {
    if (g_splash.index[i].length > max_len) max_len = g_splash.index[i].length;
  }
  if (max_len > kJpegSlotBytes) {
    splash_log("SPLASH_HDR frame_too_large max=%u slot=%u\n",
                  static_cast<unsigned>(max_len),
                  static_cast<unsigned>(kJpegSlotBytes));
    return false;
  }
  splash_log("SPLASH_HDR ok frames=%u fps=%u max_jpeg=%u\n",
                g_splash.header.frame_count, g_splash.header.fps,
                static_cast<unsigned>(max_len));
  return true;
}

bool alloc_decode_buffers() {
  const size_t rgb_need =
      static_cast<size_t>(g_splash.header.width) * g_splash.header.height * 2;
  g_splash.rgb_bytes = rgb_need + 1280 * 16 * 2;

  jpeg_decode_memory_alloc_cfg_t rx_cfg{};
  rx_cfg.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
  jpeg_decode_memory_alloc_cfg_t tx_cfg{};
  tx_cfg.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER;

  for (size_t i = 0; i < kRgbDouble; ++i) {
    size_t got = 0;
    g_splash.rgb[i] =
        static_cast<uint8_t*>(jpeg_alloc_decoder_mem(g_splash.rgb_bytes, &rx_cfg, &got));
    if (!g_splash.rgb[i]) {
      g_splash.rgb[i] = static_cast<uint8_t*>(
          heap_caps_aligned_alloc(64, g_splash.rgb_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (!g_splash.rgb[i]) {
      splash_log("SPLASH_RGB oom slot=%u\n", static_cast<unsigned>(i));
      return false;
    }
  }
  for (size_t i = 0; i < kJpegSlotCount; ++i) {
    size_t got = 0;
    g_splash.jpeg[i].data =
        static_cast<uint8_t*>(jpeg_alloc_decoder_mem(kJpegSlotBytes, &tx_cfg, &got));
    g_splash.jpeg[i].capacity = got > 0 ? got : kJpegSlotBytes;
    if (!g_splash.jpeg[i].data) {
      g_splash.jpeg[i].data = static_cast<uint8_t*>(
          heap_caps_aligned_alloc(64, kJpegSlotBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      g_splash.jpeg[i].capacity = kJpegSlotBytes;
    }
    if (!g_splash.jpeg[i].data) {
      splash_log("SPLASH_JPEG oom slot=%u\n", static_cast<unsigned>(i));
      return false;
    }
  }
  jpeg_decode_engine_cfg_t eng{};
  eng.intr_priority = 0;
  eng.timeout_ms = 200;
  if (jpeg_new_decoder_engine(&eng, &g_splash.decoder) != ESP_OK || !g_splash.decoder) {
    splash_log("SPLASH_JPEG engine_fail");
    return false;
  }
  return true;
}

void splash_reader_task(void*) {
  uint16_t frame = 0;
  const uint16_t n = g_splash.header.frame_count;
  splash_log("SPLASH_SD_TASK start");
  while (!g_splash.stop.load(std::memory_order_acquire)) {
    JpegSlot* slot = nullptr;
    if (xQueueReceive(g_splash.free_q, &slot, pdMS_TO_TICKS(50)) != pdTRUE) {
      vTaskDelay(1); /* feed IDLE / WDT */
      continue;
    }
    if (g_splash.stop.load(std::memory_order_acquire)) {
      xQueueSend(g_splash.free_q, &slot, 0);
      break;
    }
    const OrcSplashFrame& meta = g_splash.index[frame];
    if (meta.length == 0 || meta.length > slot->capacity) {
      splash_log("SPLASH_READ bad_len frame=%u len=%u\n", frame,
                    static_cast<unsigned>(meta.length));
      g_splash.stop.store(true, std::memory_order_release);
      xQueueSend(g_splash.free_q, &slot, 0);
      break;
    }
    const uint32_t abs_off = g_splash.header.data_offset + meta.offset;
    const int64_t t0 = esp_timer_get_time();
    if (g_splash.file.position() != abs_off && !g_splash.file.seek(abs_off)) {
      splash_log("SPLASH_READ seek_fail frame=%u\n", frame);
      g_splash.stop.store(true, std::memory_order_release);
      xQueueSend(g_splash.free_q, &slot, 0);
      break;
    }
    const size_t got = g_splash.file.read(slot->data, meta.length);
    const int64_t t1 = esp_timer_get_time();
    if (got != meta.length) {
      splash_log("SPLASH_READ fail frame=%u got=%u want=%u\n", frame,
                    static_cast<unsigned>(got), static_cast<unsigned>(meta.length));
      g_splash.stop.store(true, std::memory_order_release);
      xQueueSend(g_splash.free_q, &slot, 0);
      break;
    }
    slot->length = meta.length;
    slot->frame_index = frame;
    if ((frame % 60) == 0) {
      splash_log("SPLASH_SD_READ frame=%u bytes=%u us=%lld\n", frame,
                    static_cast<unsigned>(meta.length),
                    static_cast<long long>(t1 - t0));
    }
    if (xQueueSend(g_splash.filled_q, &slot, pdMS_TO_TICKS(200)) != pdTRUE) {
      xQueueSend(g_splash.free_q, &slot, 0);
      vTaskDelay(1);
      continue;
    }
    frame = static_cast<uint16_t>((frame + 1) % n);
    /* One tick is negligible beside SD latency and guarantees IDLE feeds the WDT. */
    vTaskDelay(1);
  }
  g_splash.reader_done.store(true, std::memory_order_release);
  g_splash.reader_task = nullptr;
  splash_log("SPLASH_SD_TASK end");
  vTaskDelete(nullptr);
}

void splash_play_task(void*) {
  /*
   * Frames are packed in the panel's native portrait memory order. This avoids
   * M5GFX's costly per-pixel landscape rotation and preserves static overlays.
   */
  jpeg_decode_cfg_t dec_cfg{};
  dec_cfg.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
  dec_cfg.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
  dec_cfg.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;

  const uint32_t hdr_fps = g_splash.header.fps ? g_splash.header.fps : 60;
  const uint32_t fps = hdr_fps > kPresentFpsCap ? kPresentFpsCap : hdr_fps;
  const int64_t period_us = 1000000LL / static_cast<int64_t>(fps);
  int64_t next_deadline = esp_timer_get_time();
  size_t rgb_slot = 0;
  uint32_t frames_shown = 0;
  uint32_t fps_window_frames = 0;
  int64_t fps_window_t0 = esp_timer_get_time();
  uint32_t decode_fail = 0;
  bool use_hw = (g_splash.decoder != nullptr);

  splash_log("SPLASH_PLAY start present_fps=%u asset_fps=%u frames=%u path=%s", fps, hdr_fps,
             g_splash.header.frame_count, use_hw ? "hw" : "sw");

  while (!g_splash.stop.load(std::memory_order_acquire)) {
    vTaskDelay(0);

    JpegSlot* slot = nullptr;
    if (xQueueReceive(g_splash.filled_q, &slot, pdMS_TO_TICKS(20)) != pdTRUE) {
      paint_chrome_overlay();
      continue;
    }

    bool painted = false;
    const int64_t tp0 = esp_timer_get_time();
    int64_t decode_us = 0;
    const char* path = "sw";

    if (use_hw) {
      uint8_t* rgb = g_splash.rgb[rgb_slot];
      painted = hw_decode_frame(slot->data, slot->length, rgb, g_splash.rgb_bytes, dec_cfg);
      decode_us = esp_timer_get_time() - tp0;
      if (painted) {
        copy_native_frame(rgb);
        path = "hw";
      } else {
        ++decode_fail;
        if (decode_fail >= 3) {
          splash_log("SPLASH_JPEG hw_fail → sw fallback");
          use_hw = false;
          decode_fail = 0;
        }
      }
    }

    if (!painted) {
      splash_log("SPLASH_JPEG hw_required");
      painted = false;
      path = "sw";
    }

    const int64_t tp1 = esp_timer_get_time();
    if ((frames_shown % 30) == 0) {
      splash_log("SPLASH_JPEG frame=%u bytes=%u decode_us=%lld present_us=%lld path=%s ok=%d",
                 slot->frame_index, static_cast<unsigned>(slot->length),
                 static_cast<long long>(decode_us),
                 static_cast<long long>((tp1 - tp0) - decode_us), path, painted ? 1 : 0);
    }

    if (!painted) {
      ++decode_fail;
      xQueueSend(g_splash.free_q, &slot, 0);
      if (decode_fail > 30) {
        splash_log("SPLASH_JPEG_DECODE abort");
        g_splash.stop.store(true, std::memory_order_release);
        break;
      }
      continue;
    }

    decode_fail = 0;

    /* Soft pace only when ahead of present cap; never slow-mo when behind. */
    const int64_t now = esp_timer_get_time();
    if (now < next_deadline) {
      const int64_t sleep_us = next_deadline - now;
      if (sleep_us > 2000) {
        vTaskDelay(pdMS_TO_TICKS(static_cast<uint32_t>(sleep_us / 1000)));
      }
    }
    next_deadline += period_us;

    paint_chrome_overlay();

    xQueueSend(g_splash.free_q, &slot, 0);
    rgb_slot = (rgb_slot + 1) % kRgbDouble;
    ++frames_shown;
    ++fps_window_frames;

    const int64_t tnow = esp_timer_get_time();
    if (tnow - fps_window_t0 >= 1000000) {
      splash_log("SPLASH_FPS fps=%u shown=%u push_us=%lld path=%s",
                 static_cast<unsigned>(fps_window_frames), static_cast<unsigned>(frames_shown),
                 static_cast<long long>(tp1 - tp0), path);
      fps_window_frames = 0;
      fps_window_t0 = tnow;
    }
  }

  splash_log("SPLASH_PLAY end frames_shown=%u", static_cast<unsigned>(frames_shown));
  g_splash.play_done.store(true, std::memory_order_release);
  g_splash.play_task = nullptr;
  vTaskDelete(nullptr);
}

void free_splash_resources() {
  g_splash.stop.store(true, std::memory_order_release);

  /* Unblock queues so tasks can exit. */
  for (int i = 0; i < 50; ++i) {
    if (g_splash.reader_done.load(std::memory_order_acquire) &&
        (g_splash.play_done.load(std::memory_order_acquire) ||
         g_splash.play_task == nullptr)) {
      break;
    }
    JpegSlot* slot = nullptr;
    if (g_splash.filled_q &&
        xQueueReceive(g_splash.filled_q, &slot, 0) == pdTRUE && g_splash.free_q) {
      xQueueSend(g_splash.free_q, &slot, 0);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (g_splash.reader_task) {
    vTaskDelete(g_splash.reader_task);
    g_splash.reader_task = nullptr;
  }
  if (g_splash.play_task) {
    vTaskDelete(g_splash.play_task);
    g_splash.play_task = nullptr;
  }

  if (g_splash.decoder) {
    jpeg_del_decoder_engine(g_splash.decoder);
    g_splash.decoder = nullptr;
  }
  for (size_t i = 0; i < kRgbDouble; ++i) {
    if (g_splash.rgb[i]) {
      free(g_splash.rgb[i]);
      g_splash.rgb[i] = nullptr;
    }
  }
  for (size_t i = 0; i < kJpegSlotCount; ++i) {
    if (g_splash.jpeg[i].data) {
      free(g_splash.jpeg[i].data);
      g_splash.jpeg[i].data = nullptr;
    }
  }
  if (g_splash.index) {
    free(g_splash.index);
    g_splash.index = nullptr;
  }
  if (g_splash.free_q) {
    vQueueDelete(g_splash.free_q);
    g_splash.free_q = nullptr;
  }
  if (g_splash.filled_q) {
    vQueueDelete(g_splash.filled_q);
    g_splash.filled_q = nullptr;
  }
  if (g_splash.display_mutex) {
    vSemaphoreDelete(g_splash.display_mutex);
    g_splash.display_mutex = nullptr;
  }
  g_splash.framebuffer = nullptr;
  g_splash.framebuffer_bytes = 0;
  if (g_splash.file) g_splash.file.close();
  g_splash.active.store(false, std::memory_order_release);
  splash_log("SPLASH_FREE ok");
}

bool start_animated() {
  if (!g_splash.framebuffer || !g_splash.display_mutex) return false;
  g_splash.free_q = xQueueCreate(kJpegSlotCount, sizeof(JpegSlot*));
  g_splash.filled_q = xQueueCreate(kJpegSlotCount, sizeof(JpegSlot*));
  if (!g_splash.free_q || !g_splash.filled_q) return false;
  for (size_t i = 0; i < kJpegSlotCount; ++i) {
    JpegSlot* p = &g_splash.jpeg[i];
    xQueueSend(g_splash.free_q, &p, 0);
  }
  g_splash.reader_done.store(false, std::memory_order_release);
  g_splash.play_done.store(false, std::memory_order_release);
  /*
   * Keep splash work off core 0: that core owns USB once the radio driver starts.
   * Both splash tasks yield on core 1 until the user enters the app.
   */
  if (xTaskCreatePinnedToCore(splash_reader_task, "splash_sd", 6144, nullptr, 1,
                              &g_splash.reader_task, 1) != pdPASS) {
    return false;
  }
  if (xTaskCreatePinnedToCore(splash_play_task, "splash_play", 8192, nullptr, 2,
                              &g_splash.play_task, 1) != pdPASS) {
    return false;
  }
  g_splash.mode = SplashMode::Animated;
  return true;
}

void reset_state() {
  if (g_splash.file) g_splash.file.close();
  g_splash.header = {};
  g_splash.index = nullptr;
  g_splash.decoder = nullptr;
  for (size_t i = 0; i < kRgbDouble; ++i) g_splash.rgb[i] = nullptr;
  g_splash.rgb_bytes = 0;
  for (size_t i = 0; i < kJpegSlotCount; ++i) g_splash.jpeg[i] = {};
  g_splash.free_q = nullptr;
  g_splash.filled_q = nullptr;
  g_splash.reader_task = nullptr;
  g_splash.play_task = nullptr;
  g_splash.display_mutex = nullptr;
  g_splash.framebuffer = nullptr;
  g_splash.framebuffer_bytes = 0;
  g_splash.stop.store(false, std::memory_order_relaxed);
  g_splash.reader_done.store(false, std::memory_order_relaxed);
  g_splash.play_done.store(false, std::memory_order_relaxed);
  g_splash.active.store(false, std::memory_order_relaxed);
  g_splash.ready.store(false, std::memory_order_relaxed);
  g_splash.chrome_dirty.store(true, std::memory_order_relaxed);
  g_splash.status[0] = '\0';
  g_splash.mode = SplashMode::None;
  g_splash.sd_ok = false;
  g_splash.has_poster = false;
  g_splash.use_sdmmc = false;
  g_splash.fs = nullptr;
}

}  // namespace

bool orcsdr_splash_begin(void) {
  splash_log("SPLASH_BOOT begin (loading screen)");
  reset_state();
  g_splash.display_mutex = xSemaphoreCreateMutex();
  auto* panel_base = M5.Display.getPanel();
  auto* panel = panel_base && panel_base->getBus() &&
                        panel_base->getBus()->busType() == lgfx::bus_type_t::bus_dsi
                    ? static_cast<lgfx::Panel_DSI*>(panel_base)
                    : nullptr;
  if (panel) {
    const auto& detail = panel->config_detail();
    const auto config = panel->config();
    if (detail.buffer && config.panel_width == kNativeWidth &&
        config.panel_height == kNativeHeight) {
      g_splash.framebuffer = static_cast<uint8_t*>(detail.buffer);
      g_splash.framebuffer_bytes = kNativeWidth * kNativeHeight * 2;
    }
  }
  g_splash.active.store(true, std::memory_order_release);
  orcsdr_splash_set_status("Starting…");

  (void)splash_sd_begin();

  if (g_splash.sd_ok && open_splash_file() && load_header_and_index() &&
      alloc_decode_buffers() && start_animated()) {
    splash_log("SPLASH_MODE animated");
    return true;
  }

  splash_log("SPLASH_MODE static (poster or text)");
  /* Tear partial anim resources if any. */
  free_splash_resources();
  reset_state();
  g_splash.display_mutex = xSemaphoreCreateMutex();
  g_splash.active.store(true, std::memory_order_release);
  (void)splash_sd_begin();
  g_splash.mode = SplashMode::Static;
  orcsdr_splash_set_status("Loading…");
  draw_static_base();
  return false;
}

void orcsdr_splash_set_status(const char* message) {
  portENTER_CRITICAL(&g_splash.status_mux);
  strlcpy(g_splash.status, message ? message : "", sizeof(g_splash.status));
  portEXIT_CRITICAL(&g_splash.status_mux);
  mark_chrome_dirty();
  splash_log("SPLASH_STATUS %s", message ? message : "");
  if (g_splash.mode != SplashMode::None && g_splash.active.load(std::memory_order_acquire)) {
    paint_chrome_overlay();
  }
}

void orcsdr_splash_set_ready(bool ready) {
  g_splash.ready.store(ready, std::memory_order_release);
  orcsdr_splash_set_status(ready ? "Ready" : "Loading…");
  splash_log("SPLASH_READY %d", ready ? 1 : 0);
}

bool orcsdr_splash_wait_start(void) {
  bool was_pressed = false;
  splash_log("SPLASH_WAIT start");
  while (g_splash.active.load(std::memory_order_acquire)) {
    orcsdr_splash_poll_serial();
    if (!g_splash.active.load(std::memory_order_acquire)) break;
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
  return g_splash.active.load(std::memory_order_acquire);
}

void orcsdr_splash_end(void) {
  free_splash_resources();
  reset_state();
  M5.Display.fillScreen(TFT_BLACK);
  splash_log("SPLASH_BOOT done");
}

bool orcsdr_run_boot_splash(void) {
  (void)orcsdr_splash_begin();
  orcsdr_splash_end();
  return true;
}
