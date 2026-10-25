#include "serial_cli.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "band_profile.hpp"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rtl_sdr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio_service.hpp"
#include "home_app.hpp"
#include "mbedtls/md.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "radio_service.hpp"
#include "screen_snap.hpp"
#include "wifi_app.hpp"

namespace {
constexpr char TAG[] = "SERIAL_CLI";
constexpr auto kPort = static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM);
constexpr int64_t kSessionTimeoutUs = 10 * 1000 * 1000;  // an authenticated session ends after 10 s without a command
constexpr uint32_t kTuneMinHz = 24000000, kTuneMaxHz = 1766000000;

orcsdr::gfx::ScaledDisplay* g_display = nullptr;
uint8_t g_pairing_key[32];
bool g_paired = false;
bool g_authenticated = false;
int64_t g_last_activity_us = 0;

bool decode_hex(const char* text, uint8_t* out, size_t bytes) {
  if (std::strlen(text) != bytes * 2) return false;
  for (size_t i = 0; i < bytes; ++i) {
    char pair[3] = {text[i * 2], text[i * 2 + 1], 0};
    char* end = nullptr;
    const long v = std::strtol(pair, &end, 16);
    if (end != pair + 2 || v < 0) return false;
    out[i] = static_cast<uint8_t>(v);
  }
  return true;
}

void print_hex(const uint8_t* data, size_t length) {
  for (size_t i = 0; i < length; ++i) std::printf("%02x", data[i]);
}

bool hmac(const char* label, const uint8_t* nonce, uint8_t out[32]) {
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  uint8_t input[6 + 16];
  const size_t label_length = std::strlen(label);
  std::memcpy(input, label, label_length);
  std::memcpy(input + label_length, nonce, 16);
  return info && mbedtls_md_hmac(info, g_pairing_key, sizeof(g_pairing_key), input, label_length + 16, out) == 0;
}

void load_pairing() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
    return;
  }
  nvs_handle_t handle;
  if (nvs_open("orcsdr", NVS_READONLY, &handle) != ESP_OK) return;
  size_t length = sizeof(g_pairing_key);
  g_paired = nvs_get_blob(handle, "pair_key", g_pairing_key, &length) == ESP_OK && length == sizeof(g_pairing_key);
  nvs_close(handle);
}

void store_pairing() {
  nvs_handle_t handle;
  if (nvs_open("orcsdr", NVS_READWRITE, &handle) != ESP_OK) return;
  nvs_set_blob(handle, "pair_key", g_pairing_key, sizeof(g_pairing_key));
  nvs_commit(handle);
  nvs_close(handle);
}

bool authed() {
  if (g_authenticated && esp_timer_get_time() - g_last_activity_us > kSessionTimeoutUs) g_authenticated = false;
  return g_authenticated;
}
void touch_session() { g_last_activity_us = esp_timer_get_time(); }

const char* band_name(uint32_t hz) { return orcsdr::band_profile::resolve(orcsdr::band_profile::Region::us, hz).name; }

// ---- command handlers ---------------------------------------------------------------------------

void cmd_pair(const char* hex) {
  uint8_t candidate[32];
  if (!decode_hex(hex, candidate, sizeof(candidate))) {
    std::printf("PAIR_INVALID\n");
    return;
  }
  if (!g_paired) {
    std::memcpy(g_pairing_key, candidate, sizeof(g_pairing_key));
    store_pairing();
    g_paired = true;
  }
  uint8_t difference = 0;
  for (size_t i = 0; i < sizeof(g_pairing_key); ++i) difference |= g_pairing_key[i] ^ candidate[i];
  std::printf(difference == 0 ? "PAIR_OK\n" : "PAIR_LOCKED\n");
}

void cmd_auth(char* args) {
  uint8_t nonce[16], host_proof[32], expected[32], signature[32];
  char* proof_hex = std::strchr(args, ' ');
  if (!proof_hex) {
    std::printf("AUTH_INVALID\n");
    return;
  }
  *proof_hex++ = '\0';
  if (!decode_hex(args, nonce, sizeof(nonce)) || !decode_hex(proof_hex, host_proof, sizeof(host_proof))) {
    std::printf("AUTH_INVALID\n");
    return;
  }
  if (!hmac("host", nonce, expected)) {
    std::printf("AUTH_ERROR\n");
    return;
  }
  uint8_t difference = 0;
  for (size_t i = 0; i < sizeof(host_proof); ++i) difference |= host_proof[i] ^ expected[i];
  if (difference != 0) {
    std::printf("AUTH_DENIED\n");
    return;
  }
  if (!hmac("device", nonce, signature)) {
    std::printf("AUTH_ERROR\n");
    return;
  }
  std::printf("AUTH_OK ");
  print_hex(signature, sizeof(signature));
  std::printf("\n");
  g_authenticated = true;
  touch_session();
}

void cmd_help() {
  std::printf("RTL_HELP OrcSDR P4 console: subset of docs/API_SERIAL_CLI.md. Unknown commands stay silent.\n");
  std::printf("PAIR <key> / AUTH <nonce> <proof> / PING        - pairing and session (auth commands need AUTH)\n");
  std::printf("RTL_STATUS                                      - receiver state\n");
  std::printf("RTL_FREQ | RTL_FREQ <HZ>                         - query / hot retune (auth)\n");
  std::printf("RTL_TUNE <FM|AM|WX|CB|P25|LORA|BROWSE> <HZ>      - retune (auth)\n");
  std::printf("RTL_SIGNAL                                      - signal level snapshot\n");
  std::printf("RTL_VOLUME | RTL_VOLUME <0-32>                   - query / set (auth)\n");
  std::printf("RTL_UI STATUS | RTL_UI OPEN <HOME> | RTL_UI TAP <x 0-1279> <y 0-719>  - UI (OPEN/TAP need auth)\n");
  std::printf("RTL_SCREEN_STATUS / RTL_MEM STATUS               - diagnostics\n");
  std::printf("FT8 SNAP                                        - screenshot as PNG over serial (auth)\n");
}

void cmd_status() {
  const auto rs = orcsdr::radio::snapshot();
  std::printf("RTL_SDR_STATUS connected=%d streaming=%d effective_sps=%u frequency_hz=%u driver=rtl_sdr\n",
              rs.receiver_state >= 2 ? 1 : 0, rs.receiver_state == 2 ? 1 : 0, unsigned(rs.effective_sps),
              unsigned(rs.applied_hz));
}

void cmd_freq(const char* arg) {
  const home_app::Status st = home_app::status();
  if (!arg) {
    std::printf("RTL_FREQ_STATUS band=%s frequency_hz=%u mode=%s\n", st.band, unsigned(st.frequency_hz), st.mode);
    return;
  }
  if (!authed()) {
    std::printf("RTL_FREQ_ERROR auth_required\n");
    return;
  }
  char* end = nullptr;
  const unsigned long hz = std::strtoul(arg, &end, 10);
  if (end == arg || *end != '\0' || hz < kTuneMinHz || hz > kTuneMaxHz) {
    std::printf("RTL_FREQ_INVALID use RTL_FREQ <%u-%u>\n", unsigned(kTuneMinHz), unsigned(kTuneMaxHz));
    return;
  }
  home_app::request_frequency(static_cast<uint32_t>(hz));
  std::printf("RTL_FREQ_OK band=%s frequency_hz=%lu\n", band_name(static_cast<uint32_t>(hz)), hz);
}

void cmd_tune(const char* args) {
  if (!authed()) {
    std::printf("RTL_TUNE_ERROR auth_required\n");
    return;
  }
  char band[16] = {};
  unsigned long hz = 0;
  static const char* const kBands[] = {"FM", "AM", "WX", "CB", "P25", "LORA", "BROWSE"};
  bool known = false;
  if (std::sscanf(args, "%15s %lu", band, &hz) == 2)
    for (const char* b : kBands) known |= std::strcmp(b, band) == 0;
  if (!known || hz < kTuneMinHz || hz > kTuneMaxHz) {
    std::printf("RTL_TUNE_INVALID use RTL_TUNE <FM|AM|WX|CB|P25|LORA|BROWSE> <HZ>\n");
    return;
  }
  home_app::request_frequency(static_cast<uint32_t>(hz));
  std::printf("RTL_TUNE_OK band=%s frequency_hz=%lu\n", band, hz);
}

void cmd_signal() {
  const home_app::Status st = home_app::status();
  std::printf("RTL_SIGNAL_STATUS band=%s frequency_hz=%u signal_dbfs=%.1f\n", st.band, unsigned(st.frequency_hz),
              static_cast<double>(orcsdr::radio::level_dbfs()));
}

void cmd_volume(const char* arg) {
  if (!arg) {
    std::printf("RTL_VOLUME_STATUS volume=%u\n", unsigned(home_app::status().volume * 32 / 255));
    return;
  }
  if (!authed()) {
    std::printf("RTL_VOLUME_ERROR auth_required\n");
    return;
  }
  char* end = nullptr;
  const long v = std::strtol(arg, &end, 10);
  if (end == arg || *end != '\0' || v < 0 || v > 32) {
    std::printf("RTL_VOLUME_INVALID\n");
    return;
  }
  home_app::request_volume(static_cast<uint8_t>(v * 255 / 32));
  std::printf("RTL_VOLUME_OK volume=%ld\n", v);
}

void cmd_wifi(char* args) {
  if (std::strcmp(args, "STATUS") == 0) {
    const wifi_app::Status w = wifi_app::status();
    std::printf("RTL_WIFI_STATUS started=%d link_failed=%d connected=%d connecting=%d ssid=%s ip=%s rssi=%d c6=%s stage=%s code=%d saved=%d\n",
                w.started, w.link_failed, w.connected, w.connecting, w.ssid, w.ip, int(w.rssi), w.c6_version, w.failure_stage,
                w.failure_code, w.saved);
    return;
  }
  if (std::strcmp(args, "RESULTS") == 0) {
    orcsdr::wifi::ScanResult r[16];
    const int n = wifi_app::scan_results(r, 16);
    std::printf("RTL_WIFI_RESULTS count=%d scanning=%d\n", n, wifi_app::scanning());
    for (int i = 0; i < n; ++i)
      std::printf("RTL_WIFI_AP ssid=\"%s\" rssi=%d channel=%d security=%s\n", r[i].ssid, int(r[i].rssi), int(r[i].channel), r[i].security);
    return;
  }
  if (!authed()) {
    std::printf("RTL_WIFI_ERROR auth_required\n");
    return;
  }
  if (std::strcmp(args, "SCAN") == 0) {
    std::printf(wifi_app::request_scan() ? "RTL_WIFI_SCAN_OK\n" : "RTL_WIFI_ERROR not_started\n");
  } else if (std::strcmp(args, "DISCONNECT") == 0) {
    wifi_app::disconnect();
    std::printf("RTL_WIFI_DISCONNECT_OK\n");
  } else if (std::strcmp(args, "C6_UPDATE") == 0) {
    std::printf(orcsdr::wifi::begin_c6_update() ? "RTL_WIFI_C6_UPDATE_STARTED\n" : "RTL_WIFI_C6_UPDATE_NOT_READY\n");
  } else if (std::strcmp(args, "FORGET") == 0) {
    wifi_app::forget();
    std::printf("RTL_WIFI_FORGET_OK\n");
  } else if (std::strncmp(args, "CONNECT ", 8) == 0) {
    char* ssid = args + 8;
    char* pass = std::strchr(ssid, ' ');
    if (pass) *pass++ = '\0';
    if (wifi_app::connect(ssid, pass ? pass : "", true)) std::printf("RTL_WIFI_CONNECT_OK ssid=\"%s\"\n", ssid);
    else std::printf("RTL_WIFI_ERROR not_started\n");
  } else {
    std::printf("RTL_WIFI_INVALID use STATUS | SCAN | RESULTS | CONNECT <ssid> [password] | DISCONNECT | FORGET\n");
  }
}

void cmd_usb_guard(char* args) {
  if (std::strcmp(args, "STATUS") == 0) {
    std::printf("RTL_USB_GUARD_STATUS safe_mode=%d\n", esp_rtl_sdr_usb_safe_mode_active());
    return;
  }
  if (std::strcmp(args, "RESET") == 0) {
    if (!authed()) {
      std::printf("RTL_USB_GUARD_ERROR auth_required\n");
      return;
    }
    std::printf("RTL_USB_GUARD_RESET %s; restarting\n", esp_err_to_name(esp_rtl_sdr_usb_fault_guard_reset()));
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
  }
}

void cmd_audio(char* args) {
  if (std::strcmp(args, "STATUS") == 0) {
    const auto a = audio_service::status();
    std::printf("RTL_AUDIO_STATUS running=%d muted=%d volume=%u blocks=%u dropped=%u underruns=%u fill=%.2f ppm=%.0f demod_us=%u\n",
                a.running, a.muted, unsigned(a.volume), unsigned(a.blocks), unsigned(a.dropped_blocks),
                unsigned(a.underruns), double(a.buffer_fill), double(a.resample_ppm), unsigned(a.demod_us));
    return;
  }
  if (std::strncmp(args, "TONE ", 5) == 0) {
    if (!authed()) {
      std::printf("RTL_AUDIO_ERROR auth_required\n");
      return;
    }
    unsigned hz = 0, ms = 0;
    if (std::sscanf(args + 5, "%u %u", &hz, &ms) != 2 || !audio_service::play_tone(hz, ms)) {
      std::printf("RTL_AUDIO_INVALID use RTL_AUDIO TONE <50-8000 Hz> <1-5000 ms>\n");
      return;
    }
    std::printf("RTL_AUDIO_TONE_OK hz=%u ms=%u\n", hz, ms);
    return;
  }
  std::printf("RTL_AUDIO_INVALID use RTL_AUDIO STATUS | RTL_AUDIO TONE <hz> <ms>\n");
}

void cmd_ui(char* args) {
  if (std::strcmp(args, "STATUS") == 0) {
    const home_app::Status st = home_app::status();
    std::printf("RTL_UI_STATUS screen=%s band=%s frequency_hz=%u home_font=1 graphics=1 radio_ui=1 route=m5gfx_scaled\n",
                home_app::screen_name(), st.band, unsigned(st.frequency_hz));
    return;
  }
  if (std::strncmp(args, "TAP ", 4) == 0) {
    if (!authed()) {
      std::printf("RTL_UI_TAP_ERROR auth_required\n");
      return;
    }
    int x = 0, y = 0;
    if (std::sscanf(args + 4, "%d %d", &x, &y) != 2 || x < 0 || y < 0 || x >= 1280 || y >= 720) {
      std::printf("RTL_UI_TAP_INVALID use RTL_UI TAP <x 0-1279> <y 0-719>\n");
      return;
    }
    home_app::inject_tap(x, y);
    std::printf("RTL_UI_TAP_OK x=%d y=%d\n", x, y);
    return;
  }
  if (std::strncmp(args, "OPEN ", 5) == 0) {
    if (!authed()) {
      std::printf("RTL_UI_OPEN_ERROR auth_required\n");
      return;
    }
    const char* name = args + 5;
    static const char* const kValid[] = {"HOME", "FM", "AM", "SHORTWAVE", "P25", "ADSB", "LORA", "RF_LAB", "WIFI_ANALYSIS",
                                         "FT8", "SETTINGS", "WEATHER", "CB", "POCSAG", "AIRBAND", "ACARS", "DEAD_AIR",
                                         "MARINE", "SATELLITE", "UTILITIES"};
    bool valid = false;
    for (const char* v : kValid) valid |= std::strcmp(v, name) == 0;
    if (!valid) {
      std::printf("RTL_UI_OPEN_INVALID use HOME|FM|AM|SHORTWAVE|P25|ADSB|LORA|RF_LAB|WIFI_ANALYSIS|FT8|SETTINGS|WEATHER|CB|POCSAG|AIRBAND|ACARS|DEAD_AIR|MARINE|SATELLITE|UTILITIES\n");
      return;
    }
    if (!home_app::open_screen(name)) {
      std::printf("RTL_UI_OPEN_ERROR not_available target=%s (not ported to this target yet)\n", name);
      return;
    }
    std::printf("RTL_UI_OPEN_OK target=%s\n", name);
  }
}

void cmd_mem() {
  std::printf("RTL_MEM psram_free=%u psram_largest=%u internal_free=%u internal_largest=%u\n",
              unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)), unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),
              unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)), unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
  std::printf("RTL_MEM_DMA dma_free=%u dma_largest=%u\n", unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)),
              unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)));
}

void handle_line(char* line) {
  // Trim trailing whitespace.
  size_t n = std::strlen(line);
  while (n && (line[n - 1] == ' ' || line[n - 1] == '\r' || line[n - 1] == '\n')) line[--n] = '\0';
  if (!n) return;
  if (authed()) touch_session();

  if (std::strncmp(line, "PAIR ", 5) == 0) return cmd_pair(line + 5);
  if (std::strncmp(line, "AUTH ", 5) == 0 && g_paired) return cmd_auth(line + 5);
  if (std::strcmp(line, "PING") == 0) {
    if (authed()) touch_session();
    return;
  }
  if (std::strcmp(line, "RTL_HELP") == 0) return cmd_help();
  if (std::strcmp(line, "RTL_STATUS") == 0) return cmd_status();
  if (std::strcmp(line, "RTL_FREQ") == 0) return cmd_freq(nullptr);
  if (std::strncmp(line, "RTL_FREQ ", 9) == 0) return cmd_freq(line + 9);
  if (std::strncmp(line, "RTL_TUNE ", 9) == 0) return cmd_tune(line + 9);
  if (std::strcmp(line, "RTL_SIGNAL") == 0) return cmd_signal();
  if (std::strcmp(line, "RTL_VOLUME") == 0) return cmd_volume(nullptr);
  if (std::strncmp(line, "RTL_VOLUME ", 11) == 0) return cmd_volume(line + 11);
  if (std::strncmp(line, "RTL_WIFI ", 9) == 0) return cmd_wifi(line + 9);
  if (std::strncmp(line, "RTL_USB_GUARD ", 14) == 0) return cmd_usb_guard(line + 14);
  if (std::strncmp(line, "RTL_AUDIO ", 10) == 0) return cmd_audio(line + 10);
  if (std::strncmp(line, "RTL_UI ", 7) == 0) return cmd_ui(line + 7);
  if (std::strcmp(line, "RTL_SCREEN_STATUS") == 0) {
    std::printf("RTL_SCREEN_STATUS active=%s return=%s transitions=0 rejected_draws=0 visible_updates=%u last_transition_ms=0\n",
                home_app::screen_name(), home_app::screen_name(), unsigned(home_app::status().visible_updates));
    return;
  }
  if (std::strcmp(line, "RTL_MEM STATUS") == 0) return cmd_mem();
  if (std::strcmp(line, "FT8 SNAP") == 0) {
    if (!authed()) {
      std::printf("ORC_FT8_ERROR auth_required\n");
      return;
    }
    screen_snap_stream(g_display);
    return;
  }
  // Unknown commands are silent, as documented for the Tab5.
}

void task(void*) {
  char line[256];
  size_t length = 0;
  for (;;) {
    uint8_t c = 0;
    if (uart_read_bytes(kPort, &c, 1, pdMS_TO_TICKS(100)) != 1) continue;
    if (c == '\n' || c == '\r') {
      if (!length) continue;
      line[length] = '\0';
      length = 0;
      handle_line(line);
    } else if (length < sizeof(line) - 1) {
      line[length++] = static_cast<char>(c);
    } else {
      length = 0;
    }
  }
}
}  // namespace

bool serial_cli_start(orcsdr::gfx::ScaledDisplay* display) {
  g_display = display;
  load_pairing();
  if (!uart_is_driver_installed(kPort) && uart_driver_install(kPort, 2048, 0, 0, nullptr, 0) != ESP_OK) return false;
  return xTaskCreate(task, "serial_cli", 8192, nullptr, 3, nullptr) == pdPASS;
}
