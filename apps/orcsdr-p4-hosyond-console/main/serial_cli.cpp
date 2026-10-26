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
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_timer.h"
#include "esp_clk_tree.h"
#include "esp_cpu.h"
#include "esp_private/esp_clk.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio_service.hpp"
#include "home_app.hpp"
#include "mbedtls/md.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "radio_service.hpp"
#include "screen_snap.hpp"
#include "orcsdr_restart.hpp"
#include "orcsdr_storage.hpp"
#include "sd_card.hpp"
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
  } else if (std::strncmp(args, "POWER ", 6) == 0) {
    wifi_app::set_power(args[6] == '1');
    std::printf("RTL_WIFI_POWER_OK power=%d\n", args[6] == '1');
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

void cmd_restart(char* args) {
  if (!authed()) {
    std::printf("RTL_RESTART_ERROR auth_required\n");
    return;
  }
  if (std::strcmp(args, "SOFT") == 0) {
    std::printf("RTL_RESTART_OK mode=soft\n");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
  }
  if (std::strcmp(args, "CLEAN") == 0) {
    std::printf("RTL_RESTART_OK mode=clean\n");
    orcsdr_restart_clean();
  }
  std::printf("RTL_RESTART_INVALID use SOFT | CLEAN\n");
}

void cmd_sd(char* args) {
  if (std::strcmp(args, "STATUS") == 0) {
    const sd_card::Status s = sd_card::status();
    std::printf("RTL_SD_STATUS attempted=%d mounted=%d total_mb=%llu\n", s.attempted, s.mounted, (unsigned long long)(s.total_bytes >> 20));
    return;
  }
  if (std::strcmp(args, "CHECK") == 0) {
    if (!authed()) {
      std::printf("RTL_SD_ERROR auth_required\n");
      return;
    }
    std::printf("RTL_SD_CHECK pass=%d\n", orcsdr::storage::run_file_semantics_check());
    return;
  }
  std::printf("RTL_SD_INVALID use STATUS | CHECK\n");
}

void cmd_cpu() {
  static TaskStatus_t before[48], after[48];
  uint32_t t1 = 0, t2 = 0;
  const UBaseType_t n1 = uxTaskGetSystemState(before, 48, &t1);
  vTaskDelay(pdMS_TO_TICKS(2000));
  const UBaseType_t n2 = uxTaskGetSystemState(after, 48, &t2);
  const uint32_t dt = t2 - t1;
  if (!dt) {
    std::printf("RTL_CPU_ERROR no_runtime_stats\n");
    return;
  }
  std::printf("RTL_CPU window_ms=2000 (percent of one core; two cores available)\n");
  for (UBaseType_t i = 0; i < n2; ++i) {
    uint32_t prev = 0;
    for (UBaseType_t j = 0; j < n1; ++j)
      if (before[j].xHandle == after[i].xHandle) prev = before[j].ulRunTimeCounter;
    const float pct = 100.0f * static_cast<float>(after[i].ulRunTimeCounter - prev) / static_cast<float>(dt);
    if (pct >= 0.5f) std::printf("RTL_CPU_TASK %-14s core=%d prio=%u %.1f%%\n", after[i].pcTaskName,
                                 xTaskGetCoreID(after[i].xHandle) == tskNO_AFFINITY ? -1 : int(xTaskGetCoreID(after[i].xHandle)), unsigned(after[i].uxCurrentPriority), double(pct));
  }
}

void cmd_bench() {
  constexpr size_t kBytes = 16384;
  auto* a = static_cast<uint8_t*>(heap_caps_malloc(kBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  auto* b = static_cast<uint8_t*>(heap_caps_malloc(kBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  auto* c = static_cast<uint8_t*>(heap_caps_malloc(kBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!a || !b || !c) {
    std::printf("RTL_BENCH_ERROR alloc\n");
    heap_caps_free(a);
    heap_caps_free(b);
    heap_caps_free(c);
    return;
  }
  std::memset(a, 1, kBytes);
  std::memset(b, 2, kBytes);
  auto time = [&](uint8_t* dst, const uint8_t* src) {
    const int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < 20; ++i) std::memcpy(dst, src, kBytes);
    return static_cast<double>(esp_timer_get_time() - t0) / 20.0;
  };
  const double psram_to_psram = time(b, a), psram_to_internal = time(c, a), internal_to_psram = time(a, c);
  float sum = 0;
  const int64_t t0 = esp_timer_get_time();
  for (int i = 0; i < 20; ++i)
    for (size_t k = 0; k < kBytes; ++k) sum += static_cast<float>(a[k]);
  const double convert = static_cast<double>(esp_timer_get_time() - t0) / 20.0;
  {
    uint32_t i0 = 0, i1 = 0;
    asm volatile("csrr %0, minstret" : "=r"(i0));
    const uint32_t c0 = esp_cpu_get_cycle_count();
    uint32_t v = 1;
    for (int i = 0; i < 2000000; ++i) {
      v = v * 1664525u + 1013904223u;
      asm volatile("" : "+r"(v));
    }
    const uint32_t c1 = esp_cpu_get_cycle_count();
    asm volatile("csrr %0, minstret" : "=r"(i1));
    std::printf("RTL_BENCH integer: 2M dependent mul-adds %u cycles (%.1f cycles/iter) v=%u, instructions retired %u (%.1f per iter, loop itself needs ~4)\n", unsigned(c1 - c0), double(c1 - c0) / 2e6, unsigned(v), unsigned(i1 - i0), double(i1 - i0) / 2e6);
  }
  {
    const uint32_t c0 = esp_cpu_get_cycle_count();
    const int64_t u0 = esp_timer_get_time();
    float r = 1.0f;
    for (int i = 0; i < 2000000; ++i) {
      r = r * 1.0000001f + 0.5f;
      asm volatile("" : "+f"(r));
    }
    const uint32_t c1 = esp_cpu_get_cycle_count();
    const int64_t u1 = esp_timer_get_time();
    std::printf("RTL_BENCH clock: cpu_freq=%d Hz, register loop 2M iterations %lld us, %u cycles (%.1f cycles/iter)\n",
                esp_clk_cpu_freq(), (long long)(u1 - u0), unsigned(c1 - c0), double(c1 - c0) / 2e6);
  }
  volatile float x = 1.0f;
  const int64_t tc0 = esp_timer_get_time();
  for (int i = 0; i < 1000000; ++i) x = x * 1.0000001f + 0.5f;
  std::printf("RTL_BENCH compute: 1M dependent float mul-adds %lld us (a 360 MHz core needs ~11000 us)\n", (long long)(esp_timer_get_time() - tc0));
    std::printf("RTL_BENCH 16KB: psram->psram %.0f us, psram->internal %.0f us, internal->psram %.0f us, psram byte->float loop %.0f us (sum %.0f)\n",
              psram_to_psram, psram_to_internal, internal_to_psram, convert, double(sum));
  heap_caps_free(a);
  heap_caps_free(b);
  heap_caps_free(c);
}

// ---- sampling profiler: a high-priority timer interrupt records the interrupted program counter ----
namespace prof {
constexpr int kSlots = 512;
uint32_t g_pc[kSlots];
uint32_t g_count[kSlots];
uint32_t g_total = 0;
uint32_t g_dropped = 0;

bool IRAM_ATTR on_alarm(gptimer_handle_t, const gptimer_alarm_event_data_t*, void*) {
  uint32_t pc;
  asm volatile("csrr %0, mepc" : "=r"(pc));
  pc &= ~1u;
  ++g_total;
  uint32_t h = (pc >> 1) * 2654435761u >> 23;  // 9 bits
  for (int probe = 0; probe < 8; ++probe) {
    const uint32_t i = (h + probe) & (kSlots - 1);
    if (g_count[i] == 0) {
      g_pc[i] = pc;
      g_count[i] = 1;
      return false;
    }
    if (g_pc[i] == pc) {
      ++g_count[i];
      return false;
    }
  }
  ++g_dropped;
  return false;
}

struct Args {
  int core;
  int millis;
  bool done;
};

void task(void* arg) {
  Args* a = static_cast<Args*>(arg);
  std::memset(g_pc, 0, sizeof(g_pc));
  std::memset(g_count, 0, sizeof(g_count));
  g_total = g_dropped = 0;
  gptimer_handle_t t = nullptr;
  gptimer_config_t cfg = {};
  cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  cfg.direction = GPTIMER_COUNT_UP;
  cfg.resolution_hz = 1000000;
  cfg.intr_priority = 3;
  gptimer_new_timer(&cfg, &t);
  gptimer_event_callbacks_t cbs = {};
  cbs.on_alarm = on_alarm;
  gptimer_register_event_callbacks(t, &cbs, nullptr);
  gptimer_alarm_config_t al = {};
  al.alarm_count = 53;  // about 19 kHz
  al.reload_count = 0;
  al.flags.auto_reload_on_alarm = true;
  gptimer_set_alarm_action(t, &al);
  gptimer_enable(t);
  gptimer_start(t);
  vTaskDelay(pdMS_TO_TICKS(a->millis));
  gptimer_stop(t);
  gptimer_disable(t);
  gptimer_del_timer(t);
  a->done = true;
  vTaskDelete(nullptr);
}
}  // namespace prof

void cmd_prof(char* args) {
  int core = args && *args ? std::atoi(args) : 0;
  static prof::Args a;
  a = {core == 1 ? 1 : 0, 1500, false};
  xTaskCreatePinnedToCore(prof::task, "prof", 4096, &a, 20, nullptr, a.core);
  while (!a.done) vTaskDelay(pdMS_TO_TICKS(50));
  std::printf("RTL_PROF core=%d samples=%u slots_full_drops=%u\n", a.core, unsigned(prof::g_total), unsigned(prof::g_dropped));
  // Top 14 program counters by sample count.
  for (int rank = 0; rank < prof::kSlots; ++rank) {
    int best = -1;
    for (int i = 0; i < prof::kSlots; ++i)
      if (prof::g_count[i] && (best < 0 || prof::g_count[i] > prof::g_count[best])) best = i;
    if (best < 0) break;
    std::printf("RTL_PROF_PC 0x%08x %u (%.1f%%)\n", unsigned(prof::g_pc[best]), unsigned(prof::g_count[best]),
                100.0 * prof::g_count[best] / std::max<uint32_t>(1, prof::g_total));
    prof::g_count[best] = 0;
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
    std::printf("RTL_AUDIO_STATUS running=%d muted=%d volume=%u blocks=%u dropped=%u underruns=%u fill=%.2f ppm=%.0f demod_us=%u write_us=%u resample_us=%u\n",
                a.running, a.muted, unsigned(a.volume), unsigned(a.blocks), unsigned(a.dropped_blocks),
                unsigned(a.underruns), double(a.buffer_fill), double(a.resample_ppm), unsigned(a.demod_us), unsigned(a.write_us), unsigned(a.resample_us));
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
  if (std::strncmp(line, "RTL_RESTART ", 12) == 0) return cmd_restart(line + 12);
  if (std::strncmp(line, "RTL_SPAN ", 9) == 0) {
    if (!authed()) {
      std::printf("RTL_SPAN_ERROR auth_required\n");
      return;
    }
    home_app::request_span(static_cast<uint32_t>(std::strtoul(line + 9, nullptr, 10)));
    std::printf("RTL_SPAN_OK\n");
    return;
  }
  if (std::strcmp(line, "RTL_CPU") == 0) return cmd_cpu();
  if (std::strncmp(line, "RTL_PROF", 8) == 0) return cmd_prof(line[8] ? line + 9 : line + 8);
  if (std::strcmp(line, "RTL_BENCH") == 0) return cmd_bench();
  if (std::strcmp(line, "RTL_INTR") == 0) {
    esp_intr_dump(stdout);
    return;
  }
  if (std::strncmp(line, "RTL_SD ", 7) == 0) return cmd_sd(line + 7);
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
