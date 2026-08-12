/*
 * OrcSDR Waveshare ESP32-P4 multi-mode port (codex/ads-b-dashboard).
 *
 * Board: Waveshare ESP32-P4-Module-DEV-KIT + MPI2418-style ILI9341 (GPIO header)
 * Display/touch map: OrcLink firmware/waveshare-p4 (measured)
 * RF: components/rtl_sdr_v4_esp + Tab5 adsb_decoder
 *
 * USB host (hardware-verified): lower-left Type-A next to Ethernet RJ45.
 * USB OTG jumper must be HOST.
 */

#include <Arduino.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "adsb_decoder.hpp"
#include "rtl_sdr_v4_esp.h"
#include "web_api.hpp"
#include "web_server.hpp"
#include "fm_pcm.hpp"

namespace {

/* ---- Waveshare / OrcLink measured ILI9341 + XPT2046 map ---- */
constexpr spi_host_device_t kLcdHost = SPI2_HOST;
constexpr int kLcdSclk = 0;
constexpr int kLcdMosi = 3;
constexpr int kLcdMiso = 2;
constexpr int kLcdDc = 6;
constexpr int kLcdReset = 20;
constexpr int kLcdCs = 36;
constexpr int kTouchCs = 32;
constexpr int kTouchIrq = 21;
constexpr int kWidth = 320;
constexpr int kHeight = 240;

constexpr uint32_t kAdsbHz = 1090000000u;
constexpr uint32_t kDefaultFmHz = 96100000u;
constexpr uint32_t kDefaultWxHz = 162400000u;
constexpr uint32_t kFmMinHz = 88000000u;
constexpr uint32_t kFmMaxHz = 108000000u;
constexpr uint32_t kWxMinHz = 162400000u;
constexpr uint32_t kWxMaxHz = 162550000u;
constexpr uint32_t kFmStepHz = 200000u;
constexpr uint32_t kWxStepHz = 25000u;

constexpr size_t kIqBlockBytes = 32768;
constexpr uint8_t kIqBlockCount = 4;
constexpr size_t kTrackCount = 16;
constexpr uint32_t kTrackTtlMs = 60000;
constexpr int kSpectrumBins = 48;

enum class Mode : uint8_t { Adsb = 0, Fm, Wx, Status, Count };

esp_lcd_panel_io_handle_t panel_io = nullptr;
spi_device_handle_t touch_device = nullptr;
uint16_t* framebuffer = nullptr;
bool display_ready = false;
bool touch_ready = false;

rtl_sdr_v4_esp_handle_t g_rtl = nullptr;
std::atomic<bool> g_device_ready{false};
std::atomic<bool> g_streaming{false};
std::atomic<uint32_t> g_iq_blocks{0};
std::atomic<uint32_t> g_iq_drops{0};
std::atomic<uint64_t> g_iq_bytes{0};
std::atomic<float> g_signal_dbfs{-90.0f};
std::atomic<uint32_t> g_effective_sps{0};

char g_status[48] = "boot";
char g_product[48] = "-";

Mode g_mode = Mode::Adsb;
uint32_t g_freq_hz = kAdsbHz;
bool mode_change_pending = false;
bool start_pending = false;
bool stop_pending = false;

orcsdr::adsb_rx::Decoder adsb_decoder;
uint8_t* iq_blocks[kIqBlockCount]{};
size_t iq_sizes[kIqBlockCount]{};
QueueHandle_t iq_free = nullptr;
QueueHandle_t iq_ready = nullptr;

float spectrum_bins[kSpectrumBins]{};
portMUX_TYPE spectrum_mux = portMUX_INITIALIZER_UNLOCKED;

struct Track {
  bool used = false;
  uint32_t icao = 0;
  char callsign[9]{};
  bool has_callsign = false;
  int altitude_ft = 0;
  bool has_altitude = false;
  int speed_kts = 0;
  bool has_speed = false;
  int heading_deg = 0;
  bool has_heading = false;
  int vertical_rate_fpm = 0;
  bool has_vertical_rate = false;
  float latitude = 0;
  float longitude = 0;
  bool has_position = false;
  orcsdr::adsb_rx::Frame even_cpr{};
  orcsdr::adsb_rx::Frame odd_cpr{};
  bool has_even_cpr = false;
  bool has_odd_cpr = false;
  uint32_t last_ms = 0;
  uint32_t messages = 0;
};

Track tracks[kTrackCount]{};
portMUX_TYPE tracks_mux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> total_messages{0};
std::atomic<uint32_t> total_crc_ok{0};
std::atomic<uint32_t> track_revision{0};
uint32_t last_ui_revision = 0;
uint32_t last_ui_ms = 0;

char serial_line[160]{};
size_t serial_len = 0;

/* ---- drawing ---- */
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

void fill_rect(int x, int y, int w, int h, uint16_t color) {
  if (!framebuffer) return;
  const int x0 = constrain(x, 0, kWidth);
  const int y0 = constrain(y, 0, kHeight);
  const int x1 = constrain(x + w, 0, kWidth);
  const int y1 = constrain(y + h, 0, kHeight);
  for (int row = y0; row < y1; ++row) {
    uint16_t* line = framebuffer + row * kWidth;
    for (int col = x0; col < x1; ++col) line[col] = color;
  }
}

struct Glyph {
  char character;
  uint8_t rows[7];
};

constexpr Glyph kGlyphs[] = {
    {'0', {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}},
    {'1', {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'2', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}},
    {'3', {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e}},
    {'4', {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}},
    {'5', {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e}},
    {'6', {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e}},
    {'7', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}},
    {'9', {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e}},
    {'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
    {'B', {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}},
    {'C', {0x0f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0f}},
    {'D', {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e}},
    {'E', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}},
    {'F', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}},
    {'G', {0x0f, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0f}},
    {'H', {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
    {'I', {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'J', {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0e}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}},
    {'M', {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11}},
    {'O', {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
    {'P', {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}},
    {'Q', {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d}},
    {'R', {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}},
    {'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}},
    {'T', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x0a, 0x0a, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a}},
    {'X', {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f}},
    {'-', {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c}},
    {':', {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00}},
    {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
    {'+', {0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f}},
    {'%', {0x19, 0x1a, 0x02, 0x04, 0x08, 0x0b, 0x13}},
};

const uint8_t* glyph_rows(char c) {
  if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  for (const Glyph& g : kGlyphs) {
    if (g.character == c) return g.rows;
  }
  return nullptr;
}

void draw_text(int x, int y, const char* text, int scale, uint16_t color) {
  while (*text) {
    if (*text == ' ') {
      x += 6 * scale;
      ++text;
      continue;
    }
    const uint8_t* rows = glyph_rows(*text++);
    if (!rows) {
      x += 6 * scale;
      continue;
    }
    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        if (rows[row] & (1 << (4 - col))) {
          fill_rect(x + col * scale, y + row * scale, scale, scale, color);
        }
      }
    }
    x += 6 * scale;
  }
}

void flush_framebuffer() {
  if (!display_ready || !framebuffer || !panel_io) return;
  const uint8_t columns[] = {0x00, 0x00, 0x01, 0x3f};
  const uint8_t rows[] = {0x00, 0x00, 0x00, 0xef};
  esp_lcd_panel_io_tx_param(panel_io, 0x2a, columns, sizeof(columns));
  esp_lcd_panel_io_tx_param(panel_io, 0x2b, rows, sizeof(rows));
  esp_lcd_panel_io_tx_color(panel_io, 0x2c, framebuffer,
                            kWidth * kHeight * sizeof(uint16_t));
}

esp_err_t initialize_display() {
  spi_bus_config_t bus_config{};
  bus_config.mosi_io_num = kLcdMosi;
  bus_config.miso_io_num = kLcdMiso;
  bus_config.sclk_io_num = kLcdSclk;
  bus_config.quadwp_io_num = -1;
  bus_config.quadhd_io_num = -1;
  bus_config.max_transfer_sz = kWidth * kHeight * sizeof(uint16_t);
  esp_err_t result = spi_bus_initialize(kLcdHost, &bus_config, SPI_DMA_CH_AUTO);
  if (result != ESP_OK) return result;

  esp_lcd_panel_io_spi_config_t io_config{};
  io_config.cs_gpio_num = kLcdCs;
  io_config.dc_gpio_num = kLcdDc;
  io_config.spi_mode = 0;
  io_config.pclk_hz = 20 * 1000 * 1000;
  io_config.trans_queue_depth = 1;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  result = esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(kLcdHost),
                                    &io_config, &panel_io);
  if (result != ESP_OK) return result;

  gpio_set_direction(static_cast<gpio_num_t>(kLcdReset), GPIO_MODE_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(kLcdReset), 0);
  delay(20);
  gpio_set_level(static_cast<gpio_num_t>(kLcdReset), 1);
  delay(120);

  auto command = [](int value, const uint8_t* parameters = nullptr,
                    size_t parameter_count = 0) {
    return esp_lcd_panel_io_tx_param(panel_io, value, parameters, parameter_count);
  };

  const uint8_t power_control_1[] = {0x23};
  const uint8_t power_control_2[] = {0x10};
  const uint8_t vcom_control_1[] = {0x3e, 0x28};
  const uint8_t vcom_control_2[] = {0x86};
  const uint8_t memory_access[] = {0x28};
  const uint8_t pixel_format[] = {0x55};
  const uint8_t frame_rate[] = {0x00, 0x18};
  const uint8_t display_function[] = {0x08, 0x82, 0x27};
  const uint8_t gamma_select[] = {0x01};
  const uint8_t positive_gamma[] = {0x0f, 0x31, 0x2b, 0x0c, 0x0e, 0x08, 0x4e, 0xf1,
                                    0x37, 0x07, 0x10, 0x03, 0x0e, 0x09, 0x00};
  const uint8_t negative_gamma[] = {0x00, 0x0e, 0x14, 0x03, 0x11, 0x07, 0x31, 0xc1,
                                    0x48, 0x08, 0x0f, 0x0c, 0x31, 0x36, 0x0f};

  if ((result = command(0x01)) != ESP_OK) return result;
  delay(150);
  if ((result = command(0x28)) != ESP_OK) return result;
  if ((result = command(0xc0, power_control_1, sizeof(power_control_1))) != ESP_OK)
    return result;
  if ((result = command(0xc1, power_control_2, sizeof(power_control_2))) != ESP_OK)
    return result;
  if ((result = command(0xc5, vcom_control_1, sizeof(vcom_control_1))) != ESP_OK)
    return result;
  if ((result = command(0xc7, vcom_control_2, sizeof(vcom_control_2))) != ESP_OK)
    return result;
  if ((result = command(0x36, memory_access, sizeof(memory_access))) != ESP_OK)
    return result;
  if ((result = command(0x3a, pixel_format, sizeof(pixel_format))) != ESP_OK)
    return result;
  if ((result = command(0xb1, frame_rate, sizeof(frame_rate))) != ESP_OK) return result;
  if ((result = command(0xb6, display_function, sizeof(display_function))) != ESP_OK)
    return result;
  if ((result = command(0x26, gamma_select, sizeof(gamma_select))) != ESP_OK)
    return result;
  if ((result = command(0xe0, positive_gamma, sizeof(positive_gamma))) != ESP_OK)
    return result;
  if ((result = command(0xe1, negative_gamma, sizeof(negative_gamma))) != ESP_OK)
    return result;
  if ((result = command(0x11)) != ESP_OK) return result;
  delay(120);
  if ((result = command(0x29)) != ESP_OK) return result;
  delay(20);

  framebuffer = static_cast<uint16_t*>(heap_caps_malloc(
      kWidth * kHeight * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (!framebuffer) return ESP_ERR_NO_MEM;
  display_ready = true;
  return ESP_OK;
}

esp_err_t initialize_touch() {
  spi_device_interface_config_t touch_config{};
  touch_config.clock_speed_hz = 2 * 1000 * 1000;
  touch_config.mode = 0;
  touch_config.spics_io_num = kTouchCs;
  touch_config.queue_size = 1;
  esp_err_t result = spi_bus_add_device(kLcdHost, &touch_config, &touch_device);
  if (result != ESP_OK) return result;
  gpio_set_direction(static_cast<gpio_num_t>(kTouchIrq), GPIO_MODE_INPUT);
  gpio_set_pull_mode(static_cast<gpio_num_t>(kTouchIrq), GPIO_PULLUP_ONLY);
  touch_ready = true;
  return ESP_OK;
}

bool read_touch(uint16_t* raw_x, uint16_t* raw_y) {
  if (!touch_ready || gpio_get_level(static_cast<gpio_num_t>(kTouchIrq)) != 0)
    return false;
  auto read_axis = [](uint8_t command) {
    uint8_t transmit[3] = {command, 0, 0};
    uint8_t receive[3]{};
    spi_transaction_t transaction{};
    transaction.length = 24;
    transaction.tx_buffer = transmit;
    transaction.rx_buffer = receive;
    if (spi_device_transmit(touch_device, &transaction) != ESP_OK) return uint16_t{0};
    return static_cast<uint16_t>((receive[1] << 8 | receive[2]) >> 3);
  };
  *raw_x = read_axis(0xd0);
  *raw_y = read_axis(0x90);
  return *raw_x != 0 && *raw_y != 0;
}

const char* mode_name(Mode mode) {
  switch (mode) {
    case Mode::Adsb: return "ADSB";
    case Mode::Fm: return "FM";
    case Mode::Wx: return "WX";
    case Mode::Status: return "STATUS";
    default: return "?";
  }
}

uint32_t default_freq_for_mode(Mode mode) {
  switch (mode) {
    case Mode::Adsb: return kAdsbHz;
    case Mode::Fm: return kDefaultFmHz;
    case Mode::Wx: return kDefaultWxHz;
    default: return 0;
  }
}

uint32_t stream_rate_for_mode(Mode mode) {
  return mode == Mode::Adsb ? RTL_SDR_V4_ESP_RATE_2048K : RTL_SDR_V4_ESP_RATE_960K;
}

void set_status(const char* text) {
  strlcpy(g_status, text, sizeof(g_status));
  Serial.printf("STATUS %s\n", g_status);
}

void update_signal_from_iq(const uint8_t* data, size_t bytes) {
  if (!data || bytes < 4) return;
  double acc = 0.0;
  size_t pairs = 0;
  float local_bins[kSpectrumBins]{};
  const size_t step = 8;  // every 4th complex sample
  for (size_t i = 0; i + 1 < bytes; i += step) {
    const float fi = (static_cast<int>(data[i]) - 128) * (1.0f / 128.0f);
    const float fq = (static_cast<int>(data[i + 1]) - 128) * (1.0f / 128.0f);
    const float p = fi * fi + fq * fq;
    acc += p;
    ++pairs;
    const int bin = static_cast<int>((pairs * kSpectrumBins) % kSpectrumBins);
    local_bins[bin] += p;
  }
  if (pairs == 0) return;
  const float mean = static_cast<float>(acc / static_cast<double>(pairs));
  const float dbfs = 10.0f * log10f(mean + 1.0e-12f);
  g_signal_dbfs.store(dbfs, std::memory_order_relaxed);

  portENTER_CRITICAL(&spectrum_mux);
  for (int i = 0; i < kSpectrumBins; ++i) {
    spectrum_bins[i] = spectrum_bins[i] * 0.75f + local_bins[i] * 0.25f;
  }
  portEXIT_CRITICAL(&spectrum_mux);
}

void draw_spectrum(int x, int y, int w, int h) {
  fill_rect(x, y, w, h, rgb565(10, 16, 12));
  float bins[kSpectrumBins]{};
  portENTER_CRITICAL(&spectrum_mux);
  memcpy(bins, spectrum_bins, sizeof(bins));
  portEXIT_CRITICAL(&spectrum_mux);

  float peak = 1.0e-9f;
  for (float v : bins)
    if (v > peak) peak = v;
  const int bar_w = max(1, w / kSpectrumBins);
  for (int i = 0; i < kSpectrumBins; ++i) {
    const int bh = static_cast<int>((bins[i] / peak) * (h - 2));
    if (bh <= 0) continue;
    const uint16_t color = bh > h * 2 / 3 ? rgb565(157, 255, 0) : rgb565(80, 160, 200);
    fill_rect(x + i * bar_w, y + h - bh - 1, bar_w - 1, bh, color);
  }
}

void draw_header() {
  const uint16_t green = rgb565(157, 255, 0);
  const uint16_t muted = rgb565(122, 138, 106);
  const uint16_t panel = rgb565(16, 24, 14);
  fill_rect(0, 0, kWidth, 28, panel);
  fill_rect(0, 26, kWidth, 2, rgb565(118, 185, 0));
  draw_text(8, 6, "ORCSDR", 2, green);
  draw_text(120, 8, mode_name(g_mode), 2, green);
  draw_text(220, 10, "TOUCH=MODE", 1, muted);
}

void draw_adsb_body() {
  const uint16_t green = rgb565(157, 255, 0);
  const uint16_t white = rgb565(232, 238, 228);
  const uint16_t muted = rgb565(122, 138, 106);
  const uint16_t red = rgb565(245, 120, 100);
  char line[48];

  draw_text(8, 36, "1090 MHZ  2.048 MS/S", 1, muted);
  const bool ready = g_device_ready.load(std::memory_order_acquire);
  const bool streaming = g_streaming.load(std::memory_order_acquire);
  draw_text(8, 52, ready ? (streaming ? "STREAMING" : "READY") : "WAITING RTL", 2,
            ready ? green : red);
  draw_text(8, 74, g_status, 1, white);

  snprintf(line, sizeof(line), "MSG %u  CRC %u  DROP %u",
           total_messages.load(std::memory_order_relaxed),
           total_crc_ok.load(std::memory_order_relaxed),
           g_iq_drops.load(std::memory_order_relaxed));
  draw_text(8, 92, line, 1, muted);

  draw_text(8, 112, "ICAO   CALL  ALT", 1, muted);
  fill_rect(8, 124, 304, 1, rgb565(46, 58, 46));

  Track snapshot[6]{};
  size_t count = 0;
  portENTER_CRITICAL(&tracks_mux);
  for (const auto& t : tracks) {
    if (!t.used || count >= 6) continue;
    snapshot[count++] = t;
  }
  portEXIT_CRITICAL(&tracks_mux);

  if (count == 0) {
    draw_text(8, 140, "NO AIRCRAFT YET", 2, muted);
  } else {
    for (size_t i = 0; i < count; ++i) {
      const Track& t = snapshot[i];
      char icao[8];
      snprintf(icao, sizeof(icao), "%06X", t.icao);
      char call[9];
      strlcpy(call, t.has_callsign ? t.callsign : "------", sizeof(call));
      char alt[12];
      if (t.has_altitude)
        snprintf(alt, sizeof(alt), "%5d", t.altitude_ft);
      else
        strlcpy(alt, "  ---", sizeof(alt));
      snprintf(line, sizeof(line), "%s %s %s", icao, call, alt);
      draw_text(8, 132 + static_cast<int>(i) * 16, line, 1, white);
    }
  }
}

void draw_radio_body() {
  const uint16_t green = rgb565(157, 255, 0);
  const uint16_t white = rgb565(232, 238, 228);
  const uint16_t muted = rgb565(122, 138, 106);
  const uint16_t red = rgb565(245, 120, 100);
  char line[48];

  const char* label = g_mode == Mode::Fm ? "FM BROADCAST" : "NOAA WX";
  draw_text(8, 36, label, 1, muted);

  if (g_mode == Mode::Fm) {
    snprintf(line, sizeof(line), "%3u.%01u MHZ", g_freq_hz / 1000000,
             (g_freq_hz / 100000) % 10);
  } else {
    snprintf(line, sizeof(line), "%u.%03u MHZ", g_freq_hz / 1000000,
             (g_freq_hz / 1000) % 1000);
  }
  draw_text(8, 52, line, 3, white);

  const bool ready = g_device_ready.load(std::memory_order_acquire);
  const bool streaming = g_streaming.load(std::memory_order_acquire);
  draw_text(8, 90, ready ? (streaming ? "STREAMING" : "READY") : "WAITING RTL", 2,
            ready ? green : red);

  const float dbfs = g_signal_dbfs.load(std::memory_order_relaxed);
  snprintf(line, sizeof(line), "SIG %.1f DBFS  SPS %u", static_cast<double>(dbfs),
           g_effective_sps.load(std::memory_order_relaxed));
  draw_text(8, 114, line, 1, muted);
  draw_text(8, 130, g_status, 1, white);

  draw_spectrum(8, 150, 304, 72);
  draw_text(8, 226, "SERIAL: MODE FREQ+/- STEP START STOP", 1, muted);
}

void draw_status_body() {
  const uint16_t green = rgb565(157, 255, 0);
  const uint16_t white = rgb565(232, 238, 228);
  const uint16_t muted = rgb565(122, 138, 106);
  char line[48];

  draw_text(8, 40, "DEVICE", 2, green);
  draw_text(8, 66, "WAVESHARE ESP32-P4", 1, white);
  draw_text(8, 82, g_product, 1, muted);
  snprintf(line, sizeof(line), "DRIVER %s", rtl_sdr_v4_esp_get_version_string());
  draw_text(8, 98, line, 1, muted);
  snprintf(line, sizeof(line), "HEAP %u KB", ESP.getFreeHeap() / 1024);
  draw_text(8, 114, line, 1, muted);
  snprintf(line, sizeof(line), "RTL %s",
           g_device_ready.load() ? (g_streaming.load() ? "STREAM" : "READY") : "ABSENT");
  draw_text(8, 130, line, 2, g_device_ready.load() ? green : rgb565(245, 120, 100));
  draw_text(8, 156, "WEB UI", 1, muted);
  snprintf(line, sizeof(line), "http://%s/", orcsdr::web::local_ip());
  draw_text(8, 172, line, 1, orcsdr::web::http_ready() ? green : white);
  draw_text(8, 192, "ETH", 1, muted);
  draw_text(40, 192, orcsdr::web::ethernet_link_up() ? "LINK" : "DOWN", 1,
            orcsdr::web::ethernet_link_up() ? green : rgb565(245, 120, 100));
  draw_text(8, 212, "USB: LOWER-LEFT BY ETH", 1, muted);
  draw_text(8, 226, "WEB /FM RADIO IN BROWSER", 1, muted);
}

void draw_dashboard(bool force) {
  if (!display_ready) return;
  const uint32_t rev = track_revision.load(std::memory_order_acquire);
  const uint32_t now = millis();
  if (!force && rev == last_ui_revision && (now - last_ui_ms) < 250) return;
  last_ui_revision = rev;
  last_ui_ms = now;

  fill_rect(0, 0, kWidth, kHeight, rgb565(14, 14, 14));
  draw_header();
  switch (g_mode) {
    case Mode::Adsb:
      draw_adsb_body();
      break;
    case Mode::Fm:
    case Mode::Wx:
      draw_radio_body();
      break;
    case Mode::Status:
    default:
      draw_status_body();
      break;
  }
  flush_framebuffer();
}

void on_adsb_frame(const orcsdr::adsb_rx::Frame& frame, void*) {
  if (frame.icao == 0) return;
  total_messages.fetch_add(1, std::memory_order_relaxed);
  if (frame.bit_length > 0) total_crc_ok.fetch_add(1, std::memory_order_relaxed);

  const uint32_t now = millis();
  orcsdr::adsb_rx::Frame even_cpr{}, odd_cpr{};
  bool try_cpr = false;
  bool use_odd = false;

  portENTER_CRITICAL(&tracks_mux);
  Track* slot = nullptr;
  Track* oldest = &tracks[0];
  for (auto& candidate : tracks) {
    if (candidate.used && candidate.icao == frame.icao) {
      slot = &candidate;
      break;
    }
    if (!candidate.used) {
      if (!slot) slot = &candidate;
    } else if (candidate.last_ms < oldest->last_ms) {
      oldest = &candidate;
    }
  }
  if (!slot) slot = oldest;
  const bool added = !slot->used || slot->icao != frame.icao;
  if (added) {
    *slot = Track{};
    slot->used = true;
    slot->icao = frame.icao;
  }
  slot->last_ms = now;
  slot->messages += 1;
  if (frame.has_callsign) {
    strlcpy(slot->callsign, frame.callsign, sizeof(slot->callsign));
    slot->has_callsign = true;
  }
  if (frame.has_altitude) {
    slot->altitude_ft = frame.altitude_ft;
    slot->has_altitude = true;
  }
  if (frame.has_speed) {
    slot->speed_kts = frame.speed_kts;
    slot->has_speed = true;
  }
  if (frame.has_heading) {
    slot->heading_deg = frame.heading_deg;
    slot->has_heading = true;
  }
  if (frame.has_vertical_rate) {
    slot->vertical_rate_fpm = frame.vertical_rate_fpm;
    slot->has_vertical_rate = true;
  }
  if (frame.has_cpr) {
    if (frame.cpr_odd) {
      slot->odd_cpr = frame;
      slot->has_odd_cpr = true;
    } else {
      slot->even_cpr = frame;
      slot->has_even_cpr = true;
    }
    if (slot->has_even_cpr && slot->has_odd_cpr) {
      even_cpr = slot->even_cpr;
      odd_cpr = slot->odd_cpr;
      use_odd = frame.cpr_odd;
      try_cpr = true;
    }
  }
  portEXIT_CRITICAL(&tracks_mux);

  if (try_cpr) {
    double lat = 0, lon = 0;
    if (orcsdr::adsb_rx::decode_global_cpr(even_cpr, odd_cpr, use_odd, &lat, &lon)) {
      portENTER_CRITICAL(&tracks_mux);
      for (auto& t : tracks) {
        if (t.used && t.icao == frame.icao) {
          t.latitude = static_cast<float>(lat);
          t.longitude = static_cast<float>(lon);
          t.has_position = true;
          break;
        }
      }
      portEXIT_CRITICAL(&tracks_mux);
    }
  }

  track_revision.fetch_add(1, std::memory_order_release);

  Serial.printf("ADSB_FRAME icao=%06X call=%s alt=%d\n", frame.icao,
                frame.has_callsign ? frame.callsign : "-",
                frame.has_altitude ? frame.altitude_ft : -1);
}

void adsb_decoder_task(void*) {
  uint8_t index = 0;
  for (;;) {
    if (xQueueReceive(iq_ready, &index, portMAX_DELAY) == pdTRUE) {
      if (g_mode == Mode::Adsb) {
        adsb_decoder.process_cu8(iq_blocks[index], iq_sizes[index], on_adsb_frame, nullptr);
      }
      (void)xQueueSend(iq_free, &index, portMAX_DELAY);
    }
  }
}

void expire_tracks(uint32_t now) {
  bool changed = false;
  portENTER_CRITICAL(&tracks_mux);
  for (auto& t : tracks) {
    if (t.used && (now - t.last_ms) > kTrackTtlMs) {
      t = Track{};
      changed = true;
    }
  }
  portEXIT_CRITICAL(&tracks_mux);
  if (changed) track_revision.fetch_add(1, std::memory_order_release);
}

bool stop_stream() {
  if (!g_rtl) return true;
  if (!g_streaming.load(std::memory_order_acquire)) return true;
  const esp_err_t err = rtl_sdr_v4_esp_stop(g_rtl, 2000);
  Serial.printf("RTL_STOP %s\n", rtl_sdr_v4_esp_err_to_name(err));
  g_streaming.store(false, std::memory_order_release);
  set_status(err == ESP_OK ? "stopped" : "stop err");
  return err == ESP_OK;
}

bool start_stream() {
  if (!g_rtl || !g_device_ready.load(std::memory_order_acquire)) {
    set_status("no device");
    return false;
  }
  if (g_mode == Mode::Status) {
    set_status("status mode");
    return false;
  }
  if (g_streaming.load(std::memory_order_acquire)) (void)stop_stream();

  if (g_mode == Mode::Adsb) {
    adsb_decoder.reset();
    total_messages.store(0, std::memory_order_relaxed);
    total_crc_ok.store(0, std::memory_order_relaxed);
    portENTER_CRITICAL(&tracks_mux);
    for (auto& t : tracks) t = Track{};
    portEXIT_CRITICAL(&tracks_mux);
  }
  if (g_mode == Mode::Fm || g_mode == Mode::Wx) {
    orcsdr::fm::reset();
  }
  g_iq_drops.store(0, std::memory_order_relaxed);
  g_iq_blocks.store(0, std::memory_order_relaxed);
  g_iq_bytes.store(0, std::memory_order_relaxed);
  track_revision.fetch_add(1, std::memory_order_release);

  rtl_sdr_v4_esp_stream_config_t st;
  rtl_sdr_v4_esp_stream_config_default(&st);
  st.preset = RTL_SDR_V4_ESP_PRESET_CUSTOM_HZ;
  st.frequency_hz = g_freq_hz;
  st.sample_rate_sps = stream_rate_for_mode(g_mode);
  const esp_err_t err = rtl_sdr_v4_esp_start(g_rtl, &st);
  Serial.printf("RTL_START %s mode=%s rate=%u frequency_hz=%u\n",
                rtl_sdr_v4_esp_err_to_name(err), mode_name(g_mode), st.sample_rate_sps,
                st.frequency_hz);
  if (err != ESP_OK) {
    set_status("start failed");
    g_streaming.store(false, std::memory_order_release);
    return false;
  }
  g_streaming.store(true, std::memory_order_release);
  char buf[32];
  snprintf(buf, sizeof(buf), "%s live", mode_name(g_mode));
  set_status(buf);
  return true;
}

void request_mode(Mode mode) {
  if (mode == g_mode) {
    /* Web UI may re-select FM while idle — restart stream. */
    if (mode != Mode::Status && !g_streaming.load(std::memory_order_acquire) &&
        g_device_ready.load(std::memory_order_acquire)) {
      start_pending = true;
      Serial.printf("MODE %s restart_stream frequency_hz=%u\n", mode_name(g_mode), g_freq_hz);
    }
    return;
  }
  g_mode = mode;
  if (mode != Mode::Status) {
    g_freq_hz = default_freq_for_mode(mode);
  }
  mode_change_pending = true;
  track_revision.fetch_add(1, std::memory_order_release);
  Serial.printf("MODE %s frequency_hz=%u\n", mode_name(g_mode), g_freq_hz);
}

void step_frequency(int direction) {
  if (g_mode == Mode::Fm) {
    const int64_t next = static_cast<int64_t>(g_freq_hz) + direction * static_cast<int64_t>(kFmStepHz);
    g_freq_hz = static_cast<uint32_t>(constrain(next, static_cast<int64_t>(kFmMinHz),
                                                static_cast<int64_t>(kFmMaxHz)));
  } else if (g_mode == Mode::Wx) {
    const int64_t next = static_cast<int64_t>(g_freq_hz) + direction * static_cast<int64_t>(kWxStepHz);
    g_freq_hz = static_cast<uint32_t>(constrain(next, static_cast<int64_t>(kWxMinHz),
                                                static_cast<int64_t>(kWxMaxHz)));
  } else {
    return;
  }
  Serial.printf("FREQ %u\n", g_freq_hz);
  if (g_streaming.load(std::memory_order_acquire) && g_rtl) {
    const esp_err_t err = rtl_sdr_v4_esp_retune_hz(g_rtl, g_freq_hz);
    Serial.printf("RTL_RETUNE %s frequency_hz=%u\n", rtl_sdr_v4_esp_err_to_name(err),
                  g_freq_hz);
  }
  track_revision.fetch_add(1, std::memory_order_release);
}

void print_help() {
  Serial.println("ORCSDR_WAVESHARE commands:");
  Serial.println("  MODE ADSB|FM|WX|STATUS");
  Serial.println("  FREQ <hz>");
  Serial.println("  +  /  -     step FM 200kHz or WX 25kHz");
  Serial.println("  START / STOP");
  Serial.println("  HELP");
  Serial.println("Touch cycles modes. USB host: lower-left Type-A by Ethernet.");
}

void process_command(char* command) {
  if (strcmp(command, "HELP") == 0 || strcmp(command, "?") == 0) {
    print_help();
    return;
  }
  if (strcmp(command, "START") == 0) {
    start_pending = true;
    return;
  }
  if (strcmp(command, "STOP") == 0) {
    stop_pending = true;
    return;
  }
  if (strcmp(command, "+") == 0) {
    step_frequency(+1);
    return;
  }
  if (strcmp(command, "-") == 0) {
    step_frequency(-1);
    return;
  }
  if (strncmp(command, "MODE ", 5) == 0) {
    const char* name = command + 5;
    if (strcmp(name, "ADSB") == 0) request_mode(Mode::Adsb);
    else if (strcmp(name, "FM") == 0) request_mode(Mode::Fm);
    else if (strcmp(name, "WX") == 0) request_mode(Mode::Wx);
    else if (strcmp(name, "STATUS") == 0) request_mode(Mode::Status);
    else Serial.println("MODE_INVALID");
    return;
  }
  if (strncmp(command, "FREQ ", 5) == 0) {
    const uint32_t hz = static_cast<uint32_t>(strtoul(command + 5, nullptr, 10));
    if (hz < RTL_SDR_V4_ESP_FREQ_MIN_HZ || hz > RTL_SDR_V4_ESP_FREQ_MAX_HZ) {
      Serial.println("FREQ_INVALID");
      return;
    }
    g_freq_hz = hz;
    Serial.printf("FREQ %u\n", g_freq_hz);
    if (g_streaming.load(std::memory_order_acquire) && g_rtl) {
      const esp_err_t err = rtl_sdr_v4_esp_retune_hz(g_rtl, g_freq_hz);
      Serial.printf("RTL_RETUNE %s frequency_hz=%u\n", rtl_sdr_v4_esp_err_to_name(err),
                    g_freq_hz);
    }
    track_revision.fetch_add(1, std::memory_order_release);
    return;
  }
  Serial.println("CMD_UNKNOWN use HELP");
}

void poll_serial() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      serial_line[serial_len] = '\0';
      if (serial_len > 0) process_command(serial_line);
      serial_len = 0;
      continue;
    }
    if (serial_len + 1 < sizeof(serial_line)) serial_line[serial_len++] = c;
    else serial_len = 0;
  }
}

void on_rtl_event(rtl_sdr_v4_esp_event_t event, const void* payload, void*) {
  switch (event) {
    case RTL_SDR_V4_ESP_EVT_READY:
    case RTL_SDR_V4_ESP_EVT_ENUMERATED: {
      g_device_ready.store(true, std::memory_order_release);
      const auto* info = static_cast<const rtl_sdr_v4_esp_device_info_t*>(payload);
      if (info) {
        strlcpy(g_product, info->product[0] ? info->product : "Blog V4", sizeof(g_product));
        Serial.printf("RTL_SDR_PROBE_OK v4=true vid=%04x pid=%04x hs=%d product=%s\n",
                      info->vid, info->pid, info->high_speed ? 1 : 0, g_product);
      } else {
        Serial.println("RTL_SDR_PROBE_OK v4=true");
      }
      set_status("V4 ready");
      if (g_mode != Mode::Status) start_pending = true;
      track_revision.fetch_add(1, std::memory_order_release);
      break;
    }
    case RTL_SDR_V4_ESP_EVT_DISCONNECTED:
      g_device_ready.store(false, std::memory_order_release);
      g_streaming.store(false, std::memory_order_release);
      set_status("disconnected");
      Serial.println("RTL_SDR_DISCONNECTED");
      track_revision.fetch_add(1, std::memory_order_release);
      break;
    case RTL_SDR_V4_ESP_EVT_IQ_BLOCK: {
      const auto* iq = static_cast<const rtl_sdr_v4_esp_iq_block_t*>(payload);
      if (!iq || !iq->data || iq->bytes == 0) break;
      const size_t n = iq->bytes <= kIqBlockBytes ? iq->bytes : kIqBlockBytes;
      if (g_mode == Mode::Fm || g_mode == Mode::Wx) {
        update_signal_from_iq(iq->data, n);
        orcsdr::fm::process_cu8(iq->data, n, g_mode == Mode::Fm);
      }
      if (g_mode == Mode::Adsb) {
        update_signal_from_iq(iq->data, n);
        if (iq_free && iq_ready) {
          uint8_t index = 0;
          if (xQueueReceive(iq_free, &index, 0) == pdTRUE) {
            std::memcpy(iq_blocks[index], iq->data, n);
            iq_sizes[index] = n;
            (void)xQueueSend(iq_ready, &index, 0);
            g_iq_blocks.fetch_add(1, std::memory_order_relaxed);
            g_iq_bytes.fetch_add(n, std::memory_order_relaxed);
          } else {
            g_iq_drops.fetch_add(1, std::memory_order_relaxed);
          }
        }
      } else {
        g_iq_blocks.fetch_add(1, std::memory_order_relaxed);
        g_iq_bytes.fetch_add(n, std::memory_order_relaxed);
      }
      (void)rtl_sdr_v4_esp_release_iq_block(g_rtl, iq);
      break;
    }
    case RTL_SDR_V4_ESP_EVT_STREAM_STARTED:
      g_streaming.store(true, std::memory_order_release);
      break;
    case RTL_SDR_V4_ESP_EVT_STOPPED:
      g_streaming.store(false, std::memory_order_release);
      break;
    case RTL_SDR_V4_ESP_EVT_ERROR: {
      const auto* err = static_cast<const rtl_sdr_v4_esp_error_info_t*>(payload);
      Serial.printf("RTL_ERROR %s\n", err ? err->message : "unknown");
      set_status("rtl error");
      break;
    }
    case RTL_SDR_V4_ESP_EVT_RETUNED:
      if (payload) {
        g_freq_hz = *static_cast<const uint32_t*>(payload);
        track_revision.fetch_add(1, std::memory_order_release);
      }
      break;
    default:
      break;
  }
}

void publish_web_snapshot() {
  orcsdr::web::Snapshot snap{};
  strlcpy(snap.mode, mode_name(g_mode), sizeof(snap.mode));
  strlcpy(snap.product, g_product, sizeof(snap.product));
  strlcpy(snap.status, g_status, sizeof(snap.status));
  strlcpy(snap.ip, orcsdr::web::local_ip(), sizeof(snap.ip));
  snap.eth_link = orcsdr::web::ethernet_link_up();
  snap.rtl_ready = g_device_ready.load(std::memory_order_acquire);
  snap.streaming = g_streaming.load(std::memory_order_acquire);
  snap.frequency_hz = g_freq_hz;
  snap.sample_rate_sps = stream_rate_for_mode(g_mode);
  snap.effective_sps = g_effective_sps.load(std::memory_order_relaxed);
  snap.iq_drops = g_iq_drops.load(std::memory_order_relaxed);
  snap.free_heap = ESP.getFreeHeap();
  snap.total_messages = total_messages.load(std::memory_order_relaxed);
  snap.total_crc_ok = total_crc_ok.load(std::memory_order_relaxed);
  {
    const auto& st = adsb_decoder.stats();
    snap.adsb_preambles = st.preambles;
    snap.adsb_frames = st.frames;
    snap.adsb_df17 = st.df17;
    snap.adsb_mag_min = st.magnitude_min;
    snap.adsb_mag_max = st.magnitude_max;
  }
  snap.revision = track_revision.load(std::memory_order_relaxed);
  snap.live = snap.streaming && g_mode == Mode::Adsb && snap.total_crc_ok > 0;
  snap.strongest_signal_dbfs = g_signal_dbfs.load(std::memory_order_relaxed);

  static uint32_t rate_window_start_ms = 0;
  static uint32_t rate_window_msgs = 0;
  static float last_message_rate = 0.0f;
  const uint32_t now = millis();
  if (rate_window_start_ms == 0) {
    rate_window_start_ms = now;
    rate_window_msgs = snap.total_messages;
  } else if (now - rate_window_start_ms >= 2000) {
    const uint32_t delta = snap.total_messages - rate_window_msgs;
    last_message_rate =
        delta * 1000.0f / static_cast<float>(now - rate_window_start_ms);
    rate_window_start_ms = now;
    rate_window_msgs = snap.total_messages;
  }
  snap.message_rate = last_message_rate;

  orcsdr::web::get_receiver_location(&snap.location_configured, &snap.latitude,
                                     &snap.longitude, &snap.radar_range_nm);

  uint8_t count = 0;
  portENTER_CRITICAL(&tracks_mux);
  for (const auto& t : tracks) {
    if (!t.used || count >= orcsdr::web::kMaxAircraft) continue;
    auto& out = snap.aircraft[count++];
    out.icao = t.icao;
    strlcpy(out.callsign, t.callsign, sizeof(out.callsign));
    out.has_callsign = t.has_callsign;
    out.altitude_ft = t.altitude_ft;
    out.has_altitude = t.has_altitude;
    out.speed_kts = t.speed_kts;
    out.has_speed = t.has_speed;
    out.heading_deg = t.heading_deg;
    out.has_heading = t.has_heading;
    out.vertical_rate_fpm = t.vertical_rate_fpm;
    out.has_vertical_rate = t.has_vertical_rate;
    out.latitude = t.latitude;
    out.longitude = t.longitude;
    out.has_position = t.has_position;
    out.messages = t.messages;
    out.age_ms = now - t.last_ms;
  }
  portEXIT_CRITICAL(&tracks_mux);
  snap.aircraft_count = count;

  snap.fm_signal_dbfs = orcsdr::fm::signal_dbfs();
  snap.fm_spectrum_bins = orcsdr::fm::kSpectrumBins;
  orcsdr::fm::copy_spectrum(snap.fm_spectrum, orcsdr::fm::kSpectrumBins);
  snap.pcm_underruns = orcsdr::fm::underruns();
  snap.pcm_overruns = orcsdr::fm::overruns();
  snap.pcm_available = static_cast<uint32_t>(orcsdr::fm::pcm_available());
  snap.pcm_sequence = orcsdr::fm::pcm_sequence();
  if (g_mode == Mode::Fm || g_mode == Mode::Wx) {
    snap.strongest_signal_dbfs = snap.fm_signal_dbfs;
  }

  orcsdr::web::publish_snapshot(snap);
}

bool initialize_rtl() {
  iq_free = xQueueCreate(kIqBlockCount, sizeof(uint8_t));
  iq_ready = xQueueCreate(kIqBlockCount, sizeof(uint8_t));
  if (!iq_free || !iq_ready) {
    set_status("iq queue fail");
    return false;
  }
  for (uint8_t i = 0; i < kIqBlockCount; ++i) {
    iq_blocks[i] = static_cast<uint8_t*>(
        heap_caps_malloc(kIqBlockBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!iq_blocks[i]) {
      iq_blocks[i] = static_cast<uint8_t*>(
          heap_caps_malloc(kIqBlockBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (!iq_blocks[i]) {
      set_status("iq alloc fail");
      return false;
    }
    (void)xQueueSend(iq_free, &i, 0);
  }

  if (xTaskCreatePinnedToCore(adsb_decoder_task, "adsb_decode", 6144, nullptr, 4, nullptr,
                              1) != pdPASS) {
    set_status("decode task fail");
    return false;
  }

  rtl_sdr_v4_esp_config_t cfg;
  rtl_sdr_v4_esp_config_default(&cfg);
  cfg.event_cb = on_rtl_event;
  cfg.transfer_bytes = 32768;
  cfg.transfer_count = 3;
  cfg.usb_task_core_id = 0;
  esp_err_t err = rtl_sdr_v4_esp_config_validate(&cfg);
  if (err != ESP_OK) {
    set_status("cfg invalid");
    return false;
  }
  err = rtl_sdr_v4_esp_install(&cfg, &g_rtl);
  if (err != ESP_OK) {
    Serial.printf("RTL_INSTALL %s\n", rtl_sdr_v4_esp_err_to_name(err));
    set_status("install failed");
    return false;
  }
  Serial.printf("RTL_INSTALL ok v%s caps=0x%08x\n", rtl_sdr_v4_esp_get_version_string(),
                static_cast<unsigned>(rtl_sdr_v4_esp_get_capabilities()));
  set_status("host waiting");
  return true;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println("ORCSDR_WAVESHARE boot branch=codex/ads-b-dashboard multi-mode");
  print_help();

  if (!orcsdr::adsb_rx::Decoder::self_check()) {
    Serial.println("RTL_ADSB_SELF_CHECK_FAIL");
  } else {
    Serial.println("RTL_ADSB_SELF_CHECK_OK");
  }

  const esp_err_t display_result = initialize_display();
  Serial.printf("DISPLAY_INIT result=%s\n", esp_err_to_name(display_result));
  if (display_result == ESP_OK) {
    set_status("display ready");
    const esp_err_t touch_result = initialize_touch();
    Serial.printf("TOUCH_INIT result=%s irq=%d cs=%d\n", esp_err_to_name(touch_result),
                  kTouchIrq, kTouchCs);
    draw_dashboard(true);
  } else {
    set_status("display failed");
  }

  if (!orcsdr::web::begin_network_and_http()) {
    Serial.println("ETH_INIT failed — web UI unavailable until link");
  }
  if (!orcsdr::fm::begin()) {
    Serial.println("FM_PCM_INIT failed");
  } else {
    Serial.println("FM_PCM_INIT ok rate=48000 ring=1s");
  }

  if (!initialize_rtl()) {
    draw_dashboard(true);
    return;
  }
  draw_dashboard(true);
  Serial.println("ORCSDR_WAVESHARE ready usb_host=lower_left_type_a_by_eth web=/");
}

void loop() {
  poll_serial();
  orcsdr::web::poll_network_and_http();

  const uint8_t web_mode = orcsdr::web::take_mode_request();
  if (web_mode == 1) request_mode(Mode::Adsb);
  else if (web_mode == 2) request_mode(Mode::Fm);
  else if (web_mode == 3) request_mode(Mode::Wx);

  const uint32_t web_freq = orcsdr::web::take_freq_request();
  if (web_freq != 0) {
    g_freq_hz = web_freq;
    if (g_mode != Mode::Fm && g_mode != Mode::Wx) {
      /* Tuning from FM UI implies radio mode. */
      request_mode(Mode::Fm);
      g_freq_hz = web_freq;
    } else if (g_streaming.load(std::memory_order_acquire) && g_rtl) {
      const esp_err_t err = rtl_sdr_v4_esp_retune_hz(g_rtl, g_freq_hz);
      Serial.printf("RTL_RETUNE %s frequency_hz=%u\n", rtl_sdr_v4_esp_err_to_name(err),
                    g_freq_hz);
      orcsdr::fm::reset();
    } else if (!g_streaming.load(std::memory_order_acquire)) {
      start_pending = true;
    }
    track_revision.fetch_add(1, std::memory_order_release);
  }

  const uint8_t stream_req = orcsdr::web::take_stream_request();
  if (stream_req == 1) start_pending = true;
  else if (stream_req == 2) stop_pending = true;

  static bool touch_was_down = false;
  uint16_t raw_x = 0;
  uint16_t raw_y = 0;
  const bool touch_down = read_touch(&raw_x, &raw_y);
  if (touch_down && !touch_was_down) {
    Serial.printf("TOUCH raw_x=%u raw_y=%u\n", raw_x, raw_y);
    const int next = (static_cast<int>(g_mode) + 1) % static_cast<int>(Mode::Count);
    request_mode(static_cast<Mode>(next));
  }
  touch_was_down = touch_down;

  if (stop_pending) {
    stop_pending = false;
    (void)stop_stream();
  }
  if (mode_change_pending) {
    mode_change_pending = false;
    (void)stop_stream();
    if (g_mode != Mode::Status && g_device_ready.load(std::memory_order_acquire)) {
      delay(100);
      (void)start_stream();
    }
    draw_dashboard(true);
  }
  if (start_pending) {
    start_pending = false;
    delay(100);
    (void)start_stream();
    draw_dashboard(true);
  }

  static uint32_t last_metrics_ms = 0;
  static uint32_t last_web_ms = 0;
  const uint32_t now = millis();
  if (now - last_web_ms >= 500) {
    last_web_ms = now;
    publish_web_snapshot();
  }
  if (now - last_metrics_ms >= 2000) {
    last_metrics_ms = now;
    expire_tracks(now);
    if (g_rtl && g_streaming.load(std::memory_order_acquire)) {
      rtl_sdr_v4_esp_metrics_t m{};
      if (rtl_sdr_v4_esp_get_metrics(g_rtl, &m) == ESP_OK) {
        g_effective_sps.store(m.effective_sps, std::memory_order_relaxed);
        const auto& st = adsb_decoder.stats();
        Serial.printf(
            "ORC_METRICS mode=%s sps=%u bytes=%llu drops=%u sig=%.1f msg=%u "
            "preambles=%u frames=%u df17=%u crc=%u mag=%u..%u heap=%u ip=%s\n",
            mode_name(g_mode), m.effective_sps,
            static_cast<unsigned long long>(g_iq_bytes.load()),
            g_iq_drops.load(), static_cast<double>(g_signal_dbfs.load()),
            total_messages.load(), st.preambles, st.frames, st.df17, st.crc_ok,
            st.magnitude_min == 0xffff ? 0 : st.magnitude_min, st.magnitude_max,
            ESP.getFreeHeap(), orcsdr::web::local_ip());
      }
      track_revision.fetch_add(1, std::memory_order_relaxed);
    } else if (!g_device_ready.load(std::memory_order_acquire)) {
      Serial.println("ORC_WAIT rtl_sdr_not_attached use_lower_left_usb_by_eth");
    }
  }

  draw_dashboard(false);
  delay(5);
}
