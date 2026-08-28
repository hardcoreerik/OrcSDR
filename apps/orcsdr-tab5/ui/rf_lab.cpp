#include "rf_lab.hpp"

#include "rf_analysis.hpp"

#include <M5Unified.h>
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_rtl_sdr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace orcsdr::rf_lab {
namespace {

constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kGrid = 0x2945;
constexpr uint16_t kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kRed = 0xf800;
constexpr int kPlotX = 22, kPlotY = 96, kPlotW = 830, kPlotH = 530;
constexpr int kSpectrumH = 260, kWaterfallY = 372, kWaterfallH = 242;
constexpr int kSideX = 870, kSideW = 388;
constexpr int kFooterY = 650, kFooterH = 56;
constexpr uint32_t kBiasHoldMs = 2000;
constexpr size_t kActionCapacity = 16;
constexpr size_t kMaxReadings = 3600;
constexpr size_t kScreenPixels = 1280u * 720u;

std::atomic<bool> g_active{false};
Runtime g_runtime{};
Runtime g_initial{};
portMUX_TYPE g_runtime_mux = portMUX_INITIALIZER_UNLOCKED;
Page g_page = Page::live;
uint8_t g_origin_screen = 0, g_origin_tab = 0;
bool g_keep_settings = false;
bool g_dirty = true;
uint32_t g_last_draw_ms = 0;
uint32_t g_last_revision = 0;
rf_analysis::Snapshot g_snapshot{};
M5Canvas g_waterfall(&M5.Display);
bool g_waterfall_ready = false;
Action g_actions[kActionCapacity]{};
size_t g_action_read = 0, g_action_write = 0;
bool g_pressed = false;
int32_t g_press_x = 0, g_press_y = 0;
uint32_t g_press_ms = 0;
bool g_bias_ack = false;
bool g_bias_hold_fired = false;
char g_message[96]{};
uint32_t g_message_until_ms = 0;
uint32_t g_bias_cli_ack_until_ms = 0;
storage::FileSystem* g_filesystem = nullptr;

struct Reference {
  bool configured = false;
  bool calibrated = false;
  uint32_t frequency_hz = 0;
  float source_dbm = 0;
  float path_loss_db = 0;
  float tolerance_db = 0;
  float dbm_offset = 0;
  Runtime calibration_runtime{};
  char note[64]{};
};

struct Reading {
  uint32_t elapsed_ms = 0;
  uint32_t frequency_hz = 0;
  uint32_t effective_sps = 0;
  float peak_dbfs = -160;
  float peak_offset_hz = 0;
  float noise_dbfs = -160;
  float snr_db = 0;
  float channel_power_dbfs = -160;
  float occupied_bandwidth_hz = 0;
  float clipping_percent = 0;
  float dc_i = 0, dc_q = 0, iq_imbalance_db = 0;
  bool source_available = false;
  bool marker = false;
  char note[48]{};
};

struct SaveJob {
  Reading* readings = nullptr;
  size_t count = 0;
  uint16_t* screen = nullptr;
  Runtime runtime{};
  Reference reference{};
  uint32_t started_ms = 0;
  uint32_t stopped_ms = 0;
  char id[32]{};
  char result[32]{};
};

Reference g_reference{};
Reading* g_readings = nullptr;
size_t g_reading_count = 0;
bool g_run_active = false;
bool g_run_paused = false;
uint32_t g_run_started_ms = 0;
uint32_t g_run_duration_ms = 0;
uint32_t g_last_reading_ms = 0;
char g_pending_marker[48]{};
SaveJob g_save_job{};
std::atomic<bool> g_save_pending{false};
std::atomic<bool> g_save_busy{false};
std::atomic<bool> g_save_ok{false};
std::atomic<bool> g_save_finished{false};
TaskHandle_t g_save_task = nullptr;
char g_recent_ids[8][32]{};
char g_recent_results[8][32]{};
size_t g_recent_count = 0;
uint32_t g_session_sequence = 0;
bool g_last_source_available = false;
bool g_source_state_known = false;
uint32_t g_loss_revision = 0;
char g_recipe[32]{"NONE"};
char g_recipe_status[64]{"IDLE"};
uint32_t g_recipe_usb_overruns = 0;
uint32_t g_recipe_consumer_drops = 0;

bool inside(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void text(const char* value, int x, int y, uint16_t color = TFT_WHITE,
          uint8_t size = 2, textdatum_t datum = middle_left) {
  M5.Display.setFont(nullptr);
  M5.Display.setTextDatum(datum);
  M5.Display.setTextColor(color, kBg);
  M5.Display.setTextSize(size);
  M5.Display.drawString(value, x, y);
}

void panel(int x, int y, int w, int h, uint16_t color = kCyan) {
  M5.Display.fillRoundRect(x, y, w, h, 9, kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 9, color);
}

void message(const char* value) {
  strlcpy(g_message, value, sizeof(g_message));
  g_message_until_ms = millis() + 3500;
  g_dirty = true;
}

bool queue(ActionKind kind, int32_t value) {
  const size_t next = (g_action_write + 1) % kActionCapacity;
  if (next == g_action_read) return false;
  g_actions[g_action_write] = {kind, value};
  g_action_write = next;
  return true;
}

bool safe_note(const char* value) {
  if (!value || strlen(value) >= sizeof(g_pending_marker)) return false;
  for (const char* p = value; *p; ++p)
    if (*p < 32 || *p > 126 || *p == '"' || *p == '\\' || *p == ',') return false;
  return true;
}

bool write_hashed(File& file, mbedtls_sha256_context& sha, const void* data,
                  size_t bytes) {
  return file.write(static_cast<const uint8_t*>(data), bytes) == bytes &&
         mbedtls_sha256_update(&sha, static_cast<const uint8_t*>(data), bytes) == 0;
}

void hex_string(const uint8_t* bytes, size_t count, char* output, size_t capacity) {
  if (!bytes || !output || capacity < count * 2 + 1) return;
  for (size_t i = 0; i < count; ++i) snprintf(output + i * 2, 3, "%02x", bytes[i]);
}

bool begin_part(const char* path, char* part, size_t part_size, File* file) {
  if (!g_filesystem || !path || !part || !file ||
      snprintf(part, part_size, "%s.part", path) >= static_cast<int>(part_size)) return false;
  g_filesystem->remove(part);
  *file = g_filesystem->open(part, FILE_WRITE, true);
  if (!*file) *file = g_filesystem->open(part, FILE_WRITE);
  return static_cast<bool>(*file);
}

bool finish_part(const char* part, const char* path, File* file) {
  if (!file || !*file) return false;
  file->flush();
  file->close();
  g_filesystem->remove(path);
  if (g_filesystem->rename(part, path)) return true;
  g_filesystem->remove(part);
  return false;
}

bool write_csv(const SaveJob& job, const char* directory, uint8_t hash[32]) {
  char path[112], part[120];
  snprintf(path, sizeof(path), "%s/readings.csv", directory);
  File file;
  if (!begin_part(path, part, sizeof(part), &file)) return false;
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  bool ok = mbedtls_sha256_starts(&sha, 0) == 0;
  constexpr char header[] =
      "elapsed_ms,frequency_hz,effective_sps,peak_dbfs,peak_offset_hz,noise_dbfs,snr_db,channel_power_dbfs,occupied_bandwidth_hz,clipping_percent,dc_i,dc_q,iq_imbalance_db,source,marker,note\n";
  ok = ok && write_hashed(file, sha, header, sizeof(header) - 1);
  char line[384];
  for (size_t i = 0; ok && i < job.count; ++i) {
    const Reading& r = job.readings[i];
    const int length = snprintf(
        line, sizeof(line),
        "%lu,%lu,%lu,%.3f,%.1f,%.3f,%.3f,%.3f,%.1f,%.4f,%.5f,%.5f,%.3f,%d,%d,\"%s\"\n",
        static_cast<unsigned long>(r.elapsed_ms),
        static_cast<unsigned long>(r.frequency_hz),
        static_cast<unsigned long>(r.effective_sps), static_cast<double>(r.peak_dbfs),
        static_cast<double>(r.peak_offset_hz), static_cast<double>(r.noise_dbfs),
        static_cast<double>(r.snr_db), static_cast<double>(r.channel_power_dbfs),
        static_cast<double>(r.occupied_bandwidth_hz),
        static_cast<double>(r.clipping_percent), static_cast<double>(r.dc_i),
        static_cast<double>(r.dc_q), static_cast<double>(r.iq_imbalance_db),
        r.source_available, r.marker, r.note);
    ok = length > 0 && length < static_cast<int>(sizeof(line)) &&
         write_hashed(file, sha, line, static_cast<size_t>(length));
  }
  ok = ok && mbedtls_sha256_finish(&sha, hash) == 0;
  mbedtls_sha256_free(&sha);
  if (!ok) {
    file.close(); g_filesystem->remove(part); return false;
  }
  return finish_part(part, path, &file);
}

constexpr uint16_t unswap565(uint16_t value) {
  return static_cast<uint16_t>((value << 8) | (value >> 8));
}

void put_u16(uint8_t* out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value); out[1] = static_cast<uint8_t>(value >> 8);
}

void put_u32(uint8_t* out, uint32_t value) {
  for (int i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>(value >> (i * 8));
}

bool write_bmp(const SaveJob& job, const char* directory, uint8_t hash[32]) {
  if (!job.screen) return false;
  char path[112], part[120];
  snprintf(path, sizeof(path), "%s/screen.bmp", directory);
  File file;
  if (!begin_part(path, part, sizeof(part), &file)) return false;
  constexpr uint32_t header_size = 54;
  constexpr uint32_t image_bytes = 1280u * 720u * 3u;
  uint8_t header[header_size]{};
  header[0] = 'B'; header[1] = 'M';
  put_u32(header + 2, header_size + image_bytes); put_u32(header + 10, header_size);
  put_u32(header + 14, 40); put_u32(header + 18, 1280); put_u32(header + 22, 720);
  put_u16(header + 26, 1); put_u16(header + 28, 24); put_u32(header + 34, image_bytes);
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  bool ok = mbedtls_sha256_starts(&sha, 0) == 0 &&
            write_hashed(file, sha, header, sizeof(header));
  uint8_t row[1280 * 3];
  for (int y = 719; ok && y >= 0; --y) {
    const uint16_t* source = job.screen + static_cast<size_t>(y) * 1280;
    for (size_t x = 0; x < 1280; ++x) {
      const uint16_t pixel = unswap565(source[x]);
      row[x * 3] = static_cast<uint8_t>((pixel & 0x1f) * 255 / 31);
      row[x * 3 + 1] = static_cast<uint8_t>(((pixel >> 5) & 0x3f) * 255 / 63);
      row[x * 3 + 2] = static_cast<uint8_t>(((pixel >> 11) & 0x1f) * 255 / 31);
    }
    ok = write_hashed(file, sha, row, sizeof(row));
  }
  ok = ok && mbedtls_sha256_finish(&sha, hash) == 0;
  mbedtls_sha256_free(&sha);
  if (!ok) { file.close(); g_filesystem->remove(part); return false; }
  return finish_part(part, path, &file);
}

bool write_json(const SaveJob& job, const char* directory, const uint8_t csv_hash[32],
                const uint8_t bmp_hash[32]) {
  char path[112], part[120], csv_hex[65]{}, bmp_hex[65]{};
  snprintf(path, sizeof(path), "%s/session.json", directory);
  hex_string(csv_hash, 32, csv_hex, sizeof(csv_hex));
  hex_string(bmp_hash, 32, bmp_hex, sizeof(bmp_hex));
  File file;
  if (!begin_part(path, part, sizeof(part), &file)) return false;
  const esp_app_desc_t* app = esp_app_get_description();
  const size_t written = file.printf(
      "{\n  \"schema_version\":1,\n  \"id\":\"%s\",\n  \"result\":\"%s\",\n"
      "  \"firmware\":\"%s\",\n  \"driver\":\"%s\",\n  \"device\":\"%s\",\n"
      "  \"started_ms\":%lu,\n  \"stopped_ms\":%lu,\n  \"readings\":%u,\n"
      "  \"configuration\":{\"frequency_hz\":%lu,\"sample_rate_sps\":%lu,\"ppm\":%d,\"gain_mode\":\"%s\",\"gain_tenth_db\":%d,\"rtl_agc\":%s,\"bias_tee\":%s},\n"
      "  \"capabilities\":%lu,\n  \"reference\":{\"configured\":%s,\"frequency_hz\":%lu,\"source_dbm\":%.3f,\"path_loss_db\":%.3f,\"tolerance_db\":%.3f,\"calibrated\":%s,\"dbm_offset\":%.3f,\"note\":\"%s\"},\n"
      "  \"artifacts\":{\"readings.csv\":\"%s\",\"screen.bmp\":\"%s\"}\n}\n",
      job.id, job.result, app ? app->version : "unknown", job.runtime.driver_version,
      job.runtime.device, static_cast<unsigned long>(job.started_ms),
      static_cast<unsigned long>(job.stopped_ms), static_cast<unsigned>(job.count),
      static_cast<unsigned long>(job.runtime.frequency_hz),
      static_cast<unsigned long>(job.runtime.sample_rate_sps), job.runtime.ppm,
      job.runtime.gain_mode == GainMode::automatic ? "AUTO" : "MANUAL",
      job.runtime.gain_tenth_db, job.runtime.rtl_agc ? "true" : "false",
      job.runtime.bias_tee ? "true" : "false",
      static_cast<unsigned long>(job.runtime.capabilities),
      job.reference.configured ? "true" : "false",
      static_cast<unsigned long>(job.reference.frequency_hz),
      static_cast<double>(job.reference.source_dbm),
      static_cast<double>(job.reference.path_loss_db),
      static_cast<double>(job.reference.tolerance_db),
      job.reference.calibrated ? "true" : "false",
      static_cast<double>(job.reference.dbm_offset), job.reference.note, csv_hex, bmp_hex);
  if (written == 0) { file.close(); g_filesystem->remove(part); return false; }
  return finish_part(part, path, &file);
}

void save_worker(void*) {
  while (true) {
    if (!g_save_pending.exchange(false, std::memory_order_acq_rel)) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    g_save_busy.store(true, std::memory_order_release);
    bool ok = g_filesystem && g_save_job.readings && g_save_job.screen;
    char directory[96]{};
    if (ok) {
      g_filesystem->mkdir("/orcsdr");
      g_filesystem->mkdir("/orcsdr/rf_lab");
      snprintf(directory, sizeof(directory), "/orcsdr/rf_lab/%s", g_save_job.id);
      ok = g_filesystem->mkdir(directory) || g_filesystem->exists(directory);
    }
    uint8_t csv_hash[32]{}, bmp_hash[32]{};
    if (ok) ok = write_csv(g_save_job, directory, csv_hash);
    if (ok) ok = write_bmp(g_save_job, directory, bmp_hash);
    if (ok) ok = write_json(g_save_job, directory, csv_hash, bmp_hash);
    heap_caps_free(g_save_job.readings);
    heap_caps_free(g_save_job.screen);
    g_save_job = {};
    g_save_ok.store(ok, std::memory_order_release);
    g_save_finished.store(true, std::memory_order_release);
    g_save_busy.store(false, std::memory_order_release);
  }
}

Runtime runtime() {
  Runtime copy{};
  portENTER_CRITICAL(&g_runtime_mux);
  copy = g_runtime;
  portEXIT_CRITICAL(&g_runtime_mux);
  return copy;
}

bool same_calibration_state(const Runtime& a, const Runtime& b) {
  return a.frequency_hz == b.frequency_hz &&
         a.sample_rate_sps == b.sample_rate_sps && a.ppm == b.ppm &&
         a.gain_tenth_db == b.gain_tenth_db && a.gain_mode == b.gain_mode &&
         a.rtl_agc == b.rtl_agc;
}

void invalidate_calibration_if_needed(const Runtime& current) {
  if (g_reference.calibrated &&
      !same_calibration_state(current, g_reference.calibration_runtime))
    g_reference.calibrated = false;
}

Reading make_reading(uint32_t now_ms, const char* note = nullptr, bool marker = false) {
  Reading reading{};
  const Runtime r = runtime();
  reading.elapsed_ms = g_run_started_ms ? now_ms - g_run_started_ms : 0;
  reading.frequency_hz = r.frequency_hz;
  reading.effective_sps = r.effective_sps;
  reading.peak_dbfs = g_snapshot.strongest;
  reading.peak_offset_hz = g_snapshot.strongest_offset_hz;
  reading.noise_dbfs = g_snapshot.noise;
  reading.snr_db = g_snapshot.snr_db;
  reading.channel_power_dbfs = g_snapshot.channel_power_dbfs;
  reading.occupied_bandwidth_hz = static_cast<float>(g_snapshot.occupied_bandwidth_hz);
  reading.clipping_percent = g_snapshot.clipping_percent;
  reading.dc_i = g_snapshot.dc_i;
  reading.dc_q = g_snapshot.dc_q;
  reading.iq_imbalance_db = g_snapshot.iq_imbalance_db;
  reading.source_available = r.source_available;
  reading.marker = marker;
  if (note) strlcpy(reading.note, note, sizeof(reading.note));
  return reading;
}

bool start_run(uint32_t seconds, const char* label = "RUN") {
  if (!g_filesystem || g_run_active || g_save_busy.load(std::memory_order_acquire) ||
      g_save_pending.load(std::memory_order_acquire)) return false;
  g_readings = static_cast<Reading*>(heap_caps_calloc(
      kMaxReadings, sizeof(Reading), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_readings) return false;
  g_reading_count = 0;
  g_run_active = true;
  g_run_paused = false;
  g_run_started_ms = millis();
  g_run_duration_ms = seconds ? seconds * 1000u : 0;
  g_last_reading_ms = 0;
  strlcpy(g_pending_marker, label, sizeof(g_pending_marker));
  g_dirty = true;
  return true;
}

void cancel_run() {
  heap_caps_free(g_readings);
  g_readings = nullptr;
  g_reading_count = 0;
  g_run_active = false;
  g_run_paused = false;
  g_run_duration_ms = 0;
  strlcpy(g_recipe, "NONE", sizeof(g_recipe));
  strlcpy(g_recipe_status, "CANCELLED", sizeof(g_recipe_status));
  g_dirty = true;
}

bool capture_screen(uint16_t** output) {
  if (!output) return false;
  auto* pixels = static_cast<uint16_t*>(heap_caps_malloc(
      kScreenPixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!pixels) return false;
  constexpr int rows = 8;
  for (int y = 0; y < 720; y += rows)
    M5.Display.readRect(0, y, 1280, std::min(rows, 720 - y),
                        pixels + static_cast<size_t>(y) * 1280);
  *output = pixels;
  return true;
}

bool finish_run(const char* result) {
  if (!g_run_active || !g_readings || g_save_busy.load(std::memory_order_acquire) ||
      g_save_pending.load(std::memory_order_acquire)) return false;
  if (g_reading_count == 0) g_readings[g_reading_count++] = make_reading(millis());
  uint16_t* screen = nullptr;
  if (!capture_screen(&screen)) return false;
  SaveJob job{};
  job.readings = g_readings;
  job.count = g_reading_count;
  job.screen = screen;
  job.runtime = runtime();
  job.reference = g_reference;
  job.started_ms = g_run_started_ms;
  job.stopped_ms = millis();
  char candidate_path[96];
  do {
    snprintf(job.id, sizeof(job.id), "boot-%010lu-%03lu",
             static_cast<unsigned long>(job.stopped_ms),
             static_cast<unsigned long>(++g_session_sequence));
    snprintf(candidate_path, sizeof(candidate_path), "/orcsdr/rf_lab/%s", job.id);
  } while (g_filesystem->exists(candidate_path));
  strlcpy(job.result, result ? result : "RECORDED", sizeof(job.result));
  g_save_job = job;
  g_readings = nullptr;
  g_reading_count = 0;
  g_run_active = false;
  g_run_paused = false;
  if (g_recent_count < std::size(g_recent_ids)) ++g_recent_count;
  for (size_t i = g_recent_count - 1; i > 0; --i) {
    strlcpy(g_recent_ids[i], g_recent_ids[i - 1], sizeof(g_recent_ids[i]));
    strlcpy(g_recent_results[i], g_recent_results[i - 1], sizeof(g_recent_results[i]));
  }
  strlcpy(g_recent_ids[0], job.id, sizeof(g_recent_ids[0]));
  strlcpy(g_recent_results[0], job.result, sizeof(g_recent_results[0]));
  g_save_pending.store(true, std::memory_order_release);
  g_dirty = true;
  return true;
}

bool snapshot_session(const char* note) {
  if (!start_run(0, "SNAPSHOT")) return false;
  g_readings[g_reading_count++] = make_reading(millis(), note, note && *note);
  return finish_run(runtime().source_available ? "RECORDED" : "INCONCLUSIVE_SOURCE_LOST");
}

bool snapshot_result(const char* note, const char* result) {
  if (!start_run(0, "RECIPE")) return false;
  g_readings[g_reading_count++] = make_reading(millis(), note, true);
  return finish_run(result);
}

void complete_timed_run() {
  const Runtime r = runtime();
  const bool transport = strcmp(g_recipe, "transport") == 0;
  const bool pass = r.source_available && r.usb_overruns == g_recipe_usb_overruns &&
                    r.consumer_drops == g_recipe_consumer_drops;
  const char* result = g_run_paused ? "INCONCLUSIVE_SOURCE_LOST" :
                       (transport ? (pass ? "PASS" : "FAIL_HEALTH_DELTA") : "RECORDED");
  if (finish_run(result) && transport) {
    strlcpy(g_recipe_status, result, sizeof(g_recipe_status));
    strlcpy(g_recipe, "NONE", sizeof(g_recipe));
  }
}

bool has_cap(uint32_t capability) {
  return (runtime().capabilities & capability) != 0;
}

const char* page_name(Page value) {
  switch (value) {
    case Page::live: return "LIVE";
    case Page::controls: return "CONTROLS";
    case Page::measurements: return "MEASUREMENTS";
    case Page::records: return "RECORDS";
  }
  return "LIVE";
}

void draw_header() {
  M5.Display.fillRect(0, 0, 1280, 80, kBg);
  panel(18, 14, 104, 52, kCyan);
  text("BACK", 70, 40, kCyan, 2, middle_center);
  text("RF LAB", 148, 39, kGreen, 4);
  text("LIVE RECEIVER TEST BENCH", 360, 42, kCyan, 2);
  const Runtime r = runtime();
  if (r.bias_tee) {
    M5.Display.fillRoundRect(1010, 14, 246, 52, 8, kRed);
    text("BIAS ON", 1133, 40, TFT_WHITE, 3, middle_center);
  } else {
    panel(1080, 14, 176, 52, r.source_available ? kGreen : TFT_ORANGE);
    text(r.source_available ? "SOURCE LIVE" : "NO SOURCE", 1168, 40,
         r.source_available ? kGreen : TFT_ORANGE, 2, middle_center);
  }
}

void draw_footer() {
  const int width = 304;
  for (int i = 0; i < 4; ++i) {
    const int x = 20 + i * 312;
    const auto candidate = static_cast<Page>(i);
    panel(x, kFooterY, width, kFooterH, candidate == g_page ? kGreen : kCyan);
    text(page_name(candidate), x + width / 2, kFooterY + kFooterH / 2,
         candidate == g_page ? kGreen : TFT_WHITE, 2, middle_center);
  }
}

void draw_static_live() {
  panel(kPlotX, kPlotY, kPlotW, kPlotH);
  M5.Display.drawRect(kPlotX + 10, kPlotY + 12, kPlotW - 20, kSpectrumH, kGrid);
  text("SPECTRUM", kPlotX + 22, kPlotY + 28, kCyan, 1);
  M5.Display.drawRect(kPlotX + 10, kWaterfallY, kPlotW - 20, kWaterfallH, kGrid);
  text("WATERFALL", kPlotX + 22, kWaterfallY + 16, kCyan, 1);
  panel(kSideX, kPlotY, kSideW, kPlotH);
}

void draw_button(int x, int y, int w, const char* label, uint16_t color = kCyan) {
  panel(x, y, w, 48, color);
  text(label, x + w / 2, y + 24, color, 2, middle_center);
}

void draw_static_controls() {
  panel(22, 96, 1236, 530);
  text("Driver controls report accepted requests and software shadows.", 44, 122,
       TFT_ORANGE, 2);
  const Runtime r = runtime();
  char line[160];
  snprintf(line, sizeof(line), "Driver %s  Device %.36s  Health %.20s  Passport %s",
           r.driver_version, r.device, r.health,
           (r.capabilities & ESP_RTL_SDR_CAP_PASSPORT) ? "SUPPORTED" : "UNAVAILABLE");
  text(line, 48, 148, kMuted, 1);
  snprintf(line, sizeof(line), "Frequency  %.6f MHz", r.frequency_hz / 1.0e6);
  text(line, 48, 178, TFT_WHITE, 2);
  draw_button(700, 154, 120, "-100k", has_cap(ESP_RTL_SDR_CAP_RETUNE) ? kCyan : kMuted);
  draw_button(832, 154, 120, "+100k", has_cap(ESP_RTL_SDR_CAP_RETUNE) ? kCyan : kMuted);
  snprintf(line, sizeof(line), "Sample rate  %.3f MSPS", r.sample_rate_sps / 1.0e6);
  text(line, 48, 238, TFT_WHITE, 2);
  draw_button(700, 214, 252, "NEXT RATE",
              has_cap(ESP_RTL_SDR_CAP_CONTINUOUS_RATE) ? kCyan : kMuted);
  snprintf(line, sizeof(line), "PPM correction  %+d", r.ppm);
  text(line, 48, 298, TFT_WHITE, 2);
  draw_button(700, 274, 120, "-1", has_cap(ESP_RTL_SDR_CAP_FREQ_CORRECTION) ? kCyan : kMuted);
  draw_button(832, 274, 120, "+1", has_cap(ESP_RTL_SDR_CAP_FREQ_CORRECTION) ? kCyan : kMuted);
  snprintf(line, sizeof(line), "Tuner gain  %s  %.1f dB",
           r.gain_mode == GainMode::automatic ? "AUTO" : "MANUAL",
           r.gain_tenth_db / 10.0);
  text(line, 48, 358, TFT_WHITE, 2);
  draw_button(700, 334, 120, r.gain_mode == GainMode::automatic ? "MANUAL" : "AUTO",
              has_cap(r.gain_mode == GainMode::automatic ? ESP_RTL_SDR_CAP_GAIN
                                                         : ESP_RTL_SDR_CAP_GAIN_AUTO)
                  ? kCyan : kMuted);
  draw_button(832, 334, 120, "+ GAIN", has_cap(ESP_RTL_SDR_CAP_GAIN) ? kCyan : kMuted);
  text(r.rtl_agc ? "RTL digital AGC  ON" : "RTL digital AGC  OFF", 48, 418,
       r.rtl_agc ? kGreen : TFT_WHITE, 2);
  draw_button(700, 394, 252, r.rtl_agc ? "TURN OFF" : "TURN ON",
              has_cap(ESP_RTL_SDR_CAP_RTL_AGC) ? kCyan : kMuted);
  text(r.bias_tee ? "Bias tee  ON (software shadow)" : "Bias tee  OFF", 48, 478,
       r.bias_tee ? kRed : TFT_WHITE, 2);
  draw_button(700, 454, 252, r.bias_tee ? "BIAS OFF" : "BIAS ON",
              has_cap(ESP_RTL_SDR_CAP_BIAS_TEE) ? (r.bias_tee ? kRed : TFT_ORANGE) : kMuted);
  text(g_keep_settings ? "Exit behavior: KEEP SETTINGS" : "Exit behavior: RESTORE ENTRY STATE",
       48, 538, g_keep_settings ? kGreen : kCyan, 2);
  draw_button(700, 514, 252, g_keep_settings ? "RESTORE ON EXIT" : "KEEP SETTINGS");
  text("Automatic hardware: V4 triplexer, HF upconverter and analog filters.", 48, 594,
       kMuted, 1);
  text("Unavailable: tuner bandwidth. Unsafe: raw EP0, EEPROM, test mode, bias GPIO.",
       640, 594, kMuted, 1);
  text("Not applicable on V4: direct sampling and R828D offset tuning.", 48, 616,
       kMuted, 1);
}

void draw_static_measurements() {
  panel(22, 96, 1236, 530);
  text("MEASUREMENT SESSION", 48, 128, kCyan, 3);
  text("Snapshots and timed 5 Hz runs use the live shared analyzer.", 48, 174,
       TFT_WHITE, 2);
  draw_button(48, 214, 250, "SNAPSHOT");
  draw_button(316, 214, 250, "RUN 10 SEC");
  draw_button(584, 214, 250, "ADD MARKER");
  draw_button(852, 214, 250, "STOP");
  char line[112];
  if (g_run_active) {
    snprintf(line, sizeof(line), "Run %s  readings=%u%s",
             g_run_duration_ms ? "TIMED" : "MANUAL",
             static_cast<unsigned>(g_reading_count), g_run_paused ? "  PAUSED: SOURCE LOST" : "");
    text(line, 48, 304, g_run_paused ? TFT_ORANGE : kGreen, 2);
  } else {
    text(g_save_busy.load(std::memory_order_acquire) ? "Saving session to SD..." :
         (!g_save_finished.load(std::memory_order_acquire) ? "No active run" :
          (g_save_ok.load(std::memory_order_acquire) ? "Last save complete" : "Last save FAILED")),
         48, 304, g_save_busy.load(std::memory_order_acquire) ? kCyan : TFT_WHITE, 2);
  }
  if (g_reference.configured) {
    snprintf(line, sizeof(line), "Reference %.6f MHz  %.1f dBm  path %.1f dB%s",
             g_reference.frequency_hz / 1.0e6, static_cast<double>(g_reference.source_dbm),
             static_cast<double>(g_reference.path_loss_db),
             g_reference.calibrated ? "  ESTIMATED dBm ENABLED" : "");
    text(line, 48, 344, g_reference.calibrated ? kGreen : kCyan, 2);
  } else {
    text("Reference source: not configured; measurements are dBFS.", 48, 344,
         TFT_ORANGE, 2);
  }
  text("Guided recipes are available through RTL_LAB RECIPE LIST.", 48, 410,
       kCyan, 2);
}

void draw_static_records() {
  panel(22, 96, 1236, 530);
  text("RF LAB RECORDS", 48, 128, kCyan, 3);
  text(g_filesystem ? "SD storage ready" : "SD storage unavailable", 48, 180,
       g_filesystem ? kGreen : TFT_ORANGE, 2);
  text("Completed sessions: /orcsdr/rf_lab/<session>/", 48, 232, TFT_WHITE, 2);
  text("Use RTL_LAB RECORDS LIST or the existing SD export commands.", 48, 276,
       kMuted, 2);
  int y = 340;
  for (size_t i = 0; i < g_recent_count; ++i) {
    char line[80];
    snprintf(line, sizeof(line), "%.31s  %.31s", g_recent_ids[i], g_recent_results[i]);
    text(line, 48, y, i == 0 ? kGreen : TFT_WHITE, 2);
    y += 42;
  }
}

void draw_page() {
  M5.Display.fillScreen(kBg);
  draw_header();
  if (g_page == Page::live) draw_static_live();
  else if (g_page == Page::controls) draw_static_controls();
  else if (g_page == Page::measurements) draw_static_measurements();
  else draw_static_records();
  draw_footer();
  g_dirty = false;
}

uint16_t waterfall_color(float db, float floor) {
  const float level = std::clamp((db - floor) / 65.0f, 0.0f, 1.0f);
  if (level < 0.25f) return M5.Display.color565(0, 0, static_cast<uint8_t>(level * 600));
  if (level < 0.5f) return M5.Display.color565(0, static_cast<uint8_t>((level - .25f) * 900), 255);
  if (level < 0.75f) return M5.Display.color565(static_cast<uint8_t>((level - .5f) * 900), 255,
                                                static_cast<uint8_t>((.75f - level) * 900));
  return M5.Display.color565(255, static_cast<uint8_t>((1.0f - level) * 900), 0);
}

void draw_live_dynamic() {
  if (!g_snapshot.bins) return;
  constexpr int x0 = kPlotX + 11, y0 = kPlotY + 13, width = kPlotW - 22;
  M5.Display.fillRect(x0, y0, width, kSpectrumH - 2, kBg);
  for (int i = 1; i < 8; ++i) M5.Display.drawFastVLine(x0 + i * width / 8, y0, kSpectrumH - 2, kGrid);
  for (int i = 1; i < 5; ++i) M5.Display.drawFastHLine(x0, y0 + i * (kSpectrumH - 2) / 5, width, kGrid);
  const float floor = std::clamp(g_snapshot.noise - 15.0f, -140.0f, -45.0f);
  int px = x0, py = y0 + kSpectrumH - 3;
  for (int x = 0; x < width; ++x) {
    const size_t bin = std::min<size_t>(g_snapshot.bins - 1,
                                        static_cast<size_t>(x) * g_snapshot.bins / width);
    const int y = y0 + kSpectrumH - 3 - static_cast<int>(
        std::clamp((g_snapshot.live[bin] - floor) / 75.0f, 0.0f, 1.0f) * (kSpectrumH - 18));
    if (x) M5.Display.drawLine(px, py, x0 + x, y, kCyan);
    px = x0 + x; py = y;
  }
  if (g_waterfall_ready) {
    g_waterfall.setScrollRect(0, 0, width, kWaterfallH - 2, kBg);
    g_waterfall.scroll(0, -2);
    for (int x = 0; x < width; ++x) {
      const size_t bin = std::min<size_t>(g_snapshot.bins - 1,
                                          static_cast<size_t>(x) * g_snapshot.bins / width);
      g_waterfall.drawFastVLine(x, kWaterfallH - 3, 2,
                               waterfall_color(g_snapshot.live[bin], floor));
    }
    g_waterfall.pushSprite(x0, kWaterfallY + 1);
  }

  M5.Display.fillRect(kSideX + 10, kPlotY + 10, kSideW - 20, kPlotH - 20, kPanel);
  char line[80];
  int y = kPlotY + 38;
  auto metric = [&](const char* label, const char* value, uint16_t color = TFT_WHITE) {
    text(label, kSideX + 22, y, kMuted, 1);
    text(value, kSideX + kSideW - 22, y, color, 2, middle_right);
    y += 48;
  };
  snprintf(line, sizeof(line), "%.6f MHz", g_snapshot.strongest_frequency_hz / 1.0e6);
  metric("PEAK FREQUENCY", line, kGreen);
  snprintf(line, sizeof(line), "%.1f dBFS", g_snapshot.strongest); metric("PEAK", line);
  snprintf(line, sizeof(line), "%.1f dBFS", g_snapshot.noise); metric("NOISE FLOOR", line);
  snprintf(line, sizeof(line), "%.1f dB", g_snapshot.snr_db); metric("SNR", line, kCyan);
  if (g_reference.calibrated) {
    snprintf(line, sizeof(line), "~%.1f dBm", g_snapshot.channel_power_dbfs + g_reference.dbm_offset);
    metric("REFERENCE-EST. POWER", line, kGreen);
  } else {
    snprintf(line, sizeof(line), "%.1f dBFS", g_snapshot.channel_power_dbfs);
    metric("CHANNEL POWER", line);
  }
  snprintf(line, sizeof(line), "%.1f kHz", g_snapshot.occupied_bandwidth_hz / 1000.0); metric("99% OBW", line);
  snprintf(line, sizeof(line), "%+.0f Hz", g_snapshot.strongest_offset_hz); metric("FREQUENCY ERROR", line);
  snprintf(line, sizeof(line), "%.2f %%", g_snapshot.clipping_percent); metric("CLIPPING", line,
         g_snapshot.clipping_percent > 0.1f ? kRed : kGreen);
  snprintf(line, sizeof(line), "I %+.3f  Q %+.3f", g_snapshot.dc_i, g_snapshot.dc_q); metric("DC OFFSET", line);
  snprintf(line, sizeof(line), "%+.2f dB", g_snapshot.iq_imbalance_db); metric("IQ BALANCE", line);
}

void queue_restore() {
  if (g_keep_settings) return;
  queue(ActionKind::ppm, g_initial.ppm);
  queue(ActionKind::sample_rate_sps, static_cast<int32_t>(g_initial.sample_rate_sps));
  queue(ActionKind::tune_hz, static_cast<int32_t>(g_initial.frequency_hz));
  queue(ActionKind::gain_mode, g_initial.gain_mode == GainMode::automatic ? 0 : 1);
  if (g_initial.gain_mode == GainMode::manual)
    queue(ActionKind::gain_tenth_db, g_initial.gain_tenth_db);
  queue(ActionKind::rtl_agc, g_initial.rtl_agc ? 1 : 0);
  queue(ActionKind::bias_tee, g_initial.bias_tee ? 1 : 0);
}

void request_close() {
  if (g_run_active && !finish_run("CANCELLED_EXIT")) cancel_run();
  queue_restore();
  queue(ActionKind::close, 0);
}

uint32_t next_rate(uint32_t current) {
  constexpr uint32_t rates[] = {256000, 960000, 1024000, 1800000, 2048000, 2400000, 2560000};
  for (uint32_t rate : rates)
    if (rate > current) return rate;
  return rates[0];
}

int next_gain(const Runtime& r) {
  if (!r.gain_count) return r.gain_tenth_db;
  for (uint8_t i = 0; i < r.gain_count; ++i)
    if (r.gain_ladder[i] > r.gain_tenth_db) return r.gain_ladder[i];
  return r.gain_ladder[0];
}

bool valid_gain(const Runtime& r, int value) {
  for (uint8_t i = 0; i < r.gain_count; ++i)
    if (r.gain_ladder[i] == value) return true;
  return false;
}

void handle_tap(int32_t x, int32_t y) {
  if (inside(x, y, 18, 14, 104, 52)) { request_close(); return; }
  if (y >= kFooterY) {
    const int slot = static_cast<int>(std::clamp<int32_t>((x - 20) / 312, 0, 3));
    g_page = static_cast<Page>(slot);
    g_dirty = true;
    return;
  }
  if (g_page == Page::measurements) {
    if (inside(x, y, 48, 214, 250, 48)) {
      message(snapshot_session(nullptr) ? "Snapshot queued for SD" : "Snapshot unavailable");
    } else if (inside(x, y, 316, 214, 250, 48)) {
      message(start_run(10) ? "10 second run started" : "Run unavailable");
    } else if (inside(x, y, 584, 214, 250, 48) && g_run_active &&
               g_reading_count < kMaxReadings) {
      g_readings[g_reading_count++] = make_reading(millis(), "TOUCH MARKER", true);
      message("Marker recorded");
    } else if (inside(x, y, 852, 214, 250, 48)) {
      message(finish_run(g_run_paused ? "INCONCLUSIVE_SOURCE_LOST" : "RECORDED")
                  ? "Run queued for SD" : "No active run");
    }
    g_dirty = true;
    return;
  }
  if (g_page != Page::controls) return;
  const Runtime r = runtime();
  if (inside(x, y, 700, 154, 120, 48) && has_cap(ESP_RTL_SDR_CAP_RETUNE)) queue(ActionKind::tune_hz, std::max<int32_t>(500000, r.frequency_hz - 100000));
  else if (inside(x, y, 832, 154, 120, 48) && has_cap(ESP_RTL_SDR_CAP_RETUNE)) queue(ActionKind::tune_hz, std::min<int32_t>(1766000000, r.frequency_hz + 100000));
  else if (inside(x, y, 700, 214, 252, 48) && has_cap(ESP_RTL_SDR_CAP_CONTINUOUS_RATE)) queue(ActionKind::sample_rate_sps, next_rate(r.sample_rate_sps));
  else if (inside(x, y, 700, 274, 120, 48) && has_cap(ESP_RTL_SDR_CAP_FREQ_CORRECTION)) queue(ActionKind::ppm, std::max(-200, r.ppm - 1));
  else if (inside(x, y, 832, 274, 120, 48) && has_cap(ESP_RTL_SDR_CAP_FREQ_CORRECTION)) queue(ActionKind::ppm, std::min(200, r.ppm + 1));
  else if (inside(x, y, 700, 334, 120, 48) &&
           has_cap(r.gain_mode == GainMode::automatic ? ESP_RTL_SDR_CAP_GAIN
                                                      : ESP_RTL_SDR_CAP_GAIN_AUTO)) queue(ActionKind::gain_mode, r.gain_mode == GainMode::automatic ? 1 : 0);
  else if (inside(x, y, 832, 334, 120, 48) && has_cap(ESP_RTL_SDR_CAP_GAIN))
    queue(ActionKind::gain_tenth_db, next_gain(r));
  else if (inside(x, y, 700, 394, 252, 48) && has_cap(ESP_RTL_SDR_CAP_RTL_AGC)) queue(ActionKind::rtl_agc, r.rtl_agc ? 0 : 1);
  else if (inside(x, y, 700, 454, 252, 48) && r.bias_tee && has_cap(ESP_RTL_SDR_CAP_BIAS_TEE)) queue(ActionKind::bias_tee, 0);
  else if (inside(x, y, 700, 454, 252, 48) && !r.bias_tee && !g_bias_ack) {
    g_bias_ack = true;
    message("Disconnect DC-short loads, then HOLD BIAS ON for 2 seconds");
  } else if (inside(x, y, 700, 514, 252, 48)) {
    g_keep_settings = !g_keep_settings;
    g_dirty = true;
  }
}

bool parse_page(const char* value, Page* output) {
  if (!value || !output) return false;
  if (strcasecmp(value, "LIVE") == 0) *output = Page::live;
  else if (strcasecmp(value, "CONTROLS") == 0) *output = Page::controls;
  else if (strcasecmp(value, "MEASUREMENTS") == 0) *output = Page::measurements;
  else if (strcasecmp(value, "RECORDS") == 0) *output = Page::records;
  else return false;
  return true;
}

}  // namespace

bool initialize(storage::FileSystem* filesystem) {
  g_filesystem = filesystem;
  if (!rf_analysis::initialize()) return false;
  if (!g_save_task &&
      xTaskCreatePinnedToCoreWithCaps(save_worker, "rf_lab_save", 12288, nullptr, 1,
                                     &g_save_task, 1,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS)
    return false;
  return true;
}

void set_runtime(const Runtime& value) {
  const Runtime previous = runtime();
  const bool changed = previous.frequency_hz != value.frequency_hz ||
                       previous.sample_rate_sps != value.sample_rate_sps ||
                       previous.capabilities != value.capabilities ||
                       previous.ppm != value.ppm ||
                       previous.gain_tenth_db != value.gain_tenth_db ||
                       previous.gain_mode != value.gain_mode ||
                       previous.rtl_agc != value.rtl_agc ||
                       previous.bias_tee != value.bias_tee ||
                       previous.source_available != value.source_available ||
                       strcmp(previous.health, value.health) != 0;
  portENTER_CRITICAL(&g_runtime_mux);
  g_runtime = value;
  portEXIT_CRITICAL(&g_runtime_mux);
  invalidate_calibration_if_needed(value);
  if (active() && (previous.frequency_hz != value.frequency_hz ||
                   previous.sample_rate_sps != value.sample_rate_sps)) {
    rf_analysis::Config config{};
    config.center_hz = value.frequency_hz;
    config.span_hz = value.sample_rate_sps ? value.sample_rate_sps : 960000;
    config.sample_rate_sps = config.span_hz;
    config.measurement_bandwidth_hz = config.span_hz;
    config.fft_size = 2048;
    config.interval_ms = 33;
    rf_analysis::set_config(config);
  }
  if (changed && g_page == Page::controls) g_dirty = true;
}

bool enter(uint8_t origin_screen_value, uint8_t origin_tab_value) {
  if (!g_waterfall_ready) {
    g_waterfall.setColorDepth(16);
    g_waterfall_ready = g_waterfall.createSprite(kPlotW - 22, kWaterfallH - 2) != nullptr;
    if (!g_waterfall_ready) return false;
    g_waterfall.fillScreen(kBg);
  }
  g_origin_screen = origin_screen_value;
  g_origin_tab = origin_tab_value;
  g_initial = runtime();
  g_keep_settings = false;
  g_bias_ack = false;
  g_page = Page::live;
  g_last_revision = 0;
  g_source_state_known = false;
  g_loss_revision = 0;
  g_active.store(true, std::memory_order_release);
  rf_analysis::Config config{};
  config.center_hz = g_initial.frequency_hz;
  config.span_hz = g_initial.sample_rate_sps ? g_initial.sample_rate_sps : 960000;
  config.sample_rate_sps = config.span_hz;
  config.measurement_bandwidth_hz = config.span_hz;
  config.fft_size = 2048;
  config.interval_ms = 33;
  rf_analysis::set_config(config);
  rf_analysis::set_enabled(true);
  g_dirty = true;
  draw_page();
  return true;
}

void leave() {
  g_active.store(false, std::memory_order_release);
  rf_analysis::set_enabled(false);
  if (g_waterfall_ready) {
    g_waterfall.deleteSprite();
    g_waterfall_ready = false;
  }
}

bool active() { return g_active.load(std::memory_order_acquire); }
uint8_t origin_screen() { return g_origin_screen; }
uint8_t origin_tab() { return g_origin_tab; }
Page page() { return g_page; }

void service_ui(uint32_t now_ms) {
  static bool last_save_busy = false;
  if (!active()) return;
  const Runtime r = runtime();
  const bool save_busy = g_save_busy.load(std::memory_order_acquire) ||
                         g_save_pending.load(std::memory_order_acquire);
  if (save_busy != last_save_busy) {
    last_save_busy = save_busy;
    if (g_page == Page::measurements || g_page == Page::records) g_dirty = true;
  }
  if (!g_source_state_known || r.source_available != g_last_source_available) {
    g_source_state_known = true;
    g_last_source_available = r.source_available;
    if (!r.source_available) g_loss_revision = g_snapshot.revision;
    draw_header();
    if (!r.source_available && g_page == Page::live) {
      M5.Display.fillRect(kPlotX + 80, kPlotY + 235, kPlotW - 160, 72, 0x2104);
      text("SOURCE LOST - PAUSED", kPlotX + kPlotW / 2, kPlotY + 271,
           TFT_ORANGE, 3, middle_center);
    }
  }
  const bool stable_source = r.source_available && g_snapshot.source_stable &&
                             g_snapshot.revision > g_loss_revision;
  const bool paused = g_run_active && !stable_source;
  if (paused != g_run_paused) {
    g_run_paused = paused;
    if (paused && g_readings && g_reading_count < kMaxReadings)
      g_readings[g_reading_count++] = make_reading(now_ms, "SOURCE LOST", true);
    g_dirty = true;
  }
  if (g_run_active && !g_run_paused && now_ms - g_last_reading_ms >= 200 &&
      g_reading_count < kMaxReadings) {
    g_last_reading_ms = now_ms;
    g_readings[g_reading_count++] = make_reading(now_ms,
        g_pending_marker[0] ? g_pending_marker : nullptr, g_pending_marker[0]);
    g_pending_marker[0] = '\0';
  }
  if (g_run_active && g_reading_count >= kMaxReadings) {
    finish_run("RECORDED_LIMIT");
    if (g_page == Page::measurements) g_dirty = true;
  }
  if (g_run_active && g_run_duration_ms && now_ms - g_run_started_ms >= g_run_duration_ms) {
    complete_timed_run();
    if (g_page == Page::measurements) g_dirty = true;
  }
  if (g_dirty) draw_page();
  if (now_ms - g_last_draw_ms >= 100 && rf_analysis::copy_snapshot(&g_snapshot) &&
      g_snapshot.revision != g_last_revision) {
    g_last_draw_ms = now_ms;
    g_last_revision = g_snapshot.revision;
    if (g_page == Page::live) {
      draw_live_dynamic();
      if (!r.source_available) {
        M5.Display.fillRect(kPlotX + 80, kPlotY + 235, kPlotW - 160, 72, 0x2104);
        text("SOURCE LOST - PAUSED", kPlotX + kPlotW / 2, kPlotY + 271,
             TFT_ORANGE, 3, middle_center);
      }
    }
  }
  if (g_message_until_ms && static_cast<int32_t>(g_message_until_ms - now_ms) > 0) {
    M5.Display.fillRoundRect(260, 598, 760, 42, 7, kPanel);
    M5.Display.drawRoundRect(260, 598, 760, 42, 7, TFT_ORANGE);
    text(g_message, 640, 619, TFT_ORANGE, 2, middle_center);
  }
}

void handle_touch(int32_t x, int32_t y, bool pressed, uint32_t now_ms) {
  if (!active()) return;
  const bool bias_button = g_page == Page::controls && inside(x, y, 700, 454, 252, 48);
  const Runtime r = runtime();
  if (pressed && !g_pressed) {
    g_pressed = true; g_press_x = x; g_press_y = y; g_press_ms = now_ms;
    g_bias_hold_fired = false;
    return;
  }
  if (pressed && g_pressed && bias_button && g_bias_ack && !r.bias_tee &&
      !g_bias_hold_fired && now_ms - g_press_ms >= kBiasHoldMs) {
    g_bias_hold_fired = true;
    g_bias_ack = false;
    queue(ActionKind::bias_tee, 1);
    message("BIAS ON request accepted by OrcSDR queue");
    return;
  }
  if (pressed || !g_pressed) return;
  const bool tap = std::abs(x - g_press_x) < 18 && std::abs(y - g_press_y) < 18 &&
                   now_ms - g_press_ms < 700 && !g_bias_hold_fired;
  g_pressed = false;
  if (tap) handle_tap(x, y);
}

bool take_action(Action* action) {
  if (!action || g_action_read == g_action_write) return false;
  *action = g_actions[g_action_read];
  g_action_read = (g_action_read + 1) % kActionCapacity;
  return true;
}

bool process_command(const char* command, char* response, size_t response_size) {
  if (!command || !response || !response_size) return false;
  if (strcmp(command, "RTL_LAB OPEN") == 0) {
    strlcpy(response, "RTL_LAB_OPEN_REQUEST", response_size); return true;
  }
  if (strcmp(command, "RTL_LAB CLOSE") == 0) {
    request_close(); strlcpy(response, "RTL_LAB_OK close=queued", response_size); return true;
  }
  if (strcmp(command, "RTL_LAB STATUS") == 0) {
    const Runtime r = runtime();
    snprintf(response, response_size,
             "RTL_LAB_STATUS active=%d page=%s source=%d frequency_hz=%lu sample_rate_sps=%lu ppm=%d mode=%s gain_tenth_db=%d rtl_agc=%d bias=%d keep=%d",
             active(), page_name(g_page), r.source_available,
             static_cast<unsigned long>(r.frequency_hz),
             static_cast<unsigned long>(r.sample_rate_sps), r.ppm,
             r.gain_mode == GainMode::automatic ? "AUTO" : "MANUAL",
             r.gain_tenth_db, r.rtl_agc, r.bias_tee, g_keep_settings);
    return true;
  }
  if (strncmp(command, "RTL_LAB PAGE ", 13) == 0) {
    Page next{};
    if (!parse_page(command + 13, &next)) {
      strlcpy(response, "RTL_LAB_ERROR page", response_size); return true;
    }
    g_page = next; g_dirty = true;
    snprintf(response, response_size, "RTL_LAB_OK page=%s", page_name(next)); return true;
  }
  if (strcmp(command, "RTL_LAB SELF_CHECK") == 0) {
    snprintf(response, response_size, "RTL_LAB_SELF_CHECK pass=%d", self_check()); return true;
  }
  if (strcmp(command, "RTL_LAB ACTION bias_ack") == 0) {
    g_bias_cli_ack_until_ms = millis() + 30000;
    strlcpy(response, "RTL_LAB_OK bias_ack=30s warning=disconnect_dc_short_loads",
            response_size);
    return true;
  }
  if (strcmp(command, "RTL_LAB GET frequency_hz") == 0 ||
      strcmp(command, "RTL_LAB GET sample_rate_sps") == 0 ||
      strcmp(command, "RTL_LAB GET ppm") == 0 ||
      strcmp(command, "RTL_LAB GET gain_tenth_db") == 0 ||
      strcmp(command, "RTL_LAB GET gain_mode") == 0 ||
      strcmp(command, "RTL_LAB GET rtl_agc") == 0 ||
      strcmp(command, "RTL_LAB GET bias_tee") == 0) {
    const Runtime r = runtime();
    const char* id = command + 12;
    long value = 0;
    if (strcmp(id, "frequency_hz") == 0) value = r.frequency_hz;
    else if (strcmp(id, "sample_rate_sps") == 0) value = r.sample_rate_sps;
    else if (strcmp(id, "ppm") == 0) value = r.ppm;
    else if (strcmp(id, "gain_tenth_db") == 0) value = r.gain_tenth_db;
    else if (strcmp(id, "gain_mode") == 0) value = r.gain_mode == GainMode::manual;
    else if (strcmp(id, "rtl_agc") == 0) value = r.rtl_agc;
    else value = r.bias_tee;
    snprintf(response, response_size, "RTL_LAB_VALUE id=%s value=%ld", id, value);
    return true;
  }
  if (strcmp(command, "RTL_LAB ACTION calibrate") == 0) {
    if (!g_reference.configured || !runtime().source_available ||
        !g_snapshot.source_stable) {
      strlcpy(response, "RTL_LAB_ERROR reference_or_source_unavailable", response_size);
      return true;
    }
    g_reference.dbm_offset = g_reference.source_dbm - g_reference.path_loss_db -
                             g_snapshot.channel_power_dbfs;
    g_reference.calibrated = true;
    g_reference.calibration_runtime = runtime();
    strlcpy(response, "RTL_LAB_OK calibration=reference_estimated", response_size);
    g_dirty = true;
    return true;
  }
  if (strcmp(command, "RTL_LAB REFERENCE STATUS") == 0) {
    snprintf(response, response_size,
             "RTL_LAB_REFERENCE configured=%d frequency_hz=%lu source_dbm=%.2f path_loss_db=%.2f tolerance_db=%.2f calibrated=%d",
             g_reference.configured, static_cast<unsigned long>(g_reference.frequency_hz),
             static_cast<double>(g_reference.source_dbm),
             static_cast<double>(g_reference.path_loss_db),
             static_cast<double>(g_reference.tolerance_db), g_reference.calibrated);
    return true;
  }
  if (strcmp(command, "RTL_LAB REFERENCE CLEAR") == 0) {
    g_reference = {}; g_dirty = true;
    strlcpy(response, "RTL_LAB_OK reference=cleared", response_size); return true;
  }
  unsigned long reference_hz = 0;
  float source_dbm = 0, path_loss_db = 0, tolerance_db = 0;
  char note[48]{};
  const int reference_fields = sscanf(command, "RTL_LAB REFERENCE SET %lu %f %f %f %47[^\n]",
                                      &reference_hz, &source_dbm, &path_loss_db,
                                      &tolerance_db, note);
  if (reference_fields >= 4) {
    if (reference_hz < 500000 || reference_hz > 1766000000UL ||
        source_dbm > 20 || source_dbm < -180 || path_loss_db < 0 ||
        path_loss_db > 200 || tolerance_db < 0 || tolerance_db > 100 ||
        (reference_fields == 5 && !safe_note(note))) {
      strlcpy(response, "RTL_LAB_ERROR reference", response_size); return true;
    }
    g_reference = {};
    g_reference.configured = true;
    g_reference.frequency_hz = static_cast<uint32_t>(reference_hz);
    g_reference.source_dbm = source_dbm;
    g_reference.path_loss_db = path_loss_db;
    g_reference.tolerance_db = tolerance_db;
    if (reference_fields == 5) strlcpy(g_reference.note, note, sizeof(g_reference.note));
    g_dirty = true;
    strlcpy(response, "RTL_LAB_OK reference=configured calibration=invalid", response_size);
    return true;
  }
  if (strncmp(command, "RTL_LAB SNAPSHOT", 16) == 0) {
    const char* note_value = command[16] == ' ' ? command + 17 : nullptr;
    if (note_value && !safe_note(note_value)) {
      strlcpy(response, "RTL_LAB_ERROR note", response_size); return true;
    }
    snprintf(response, response_size, "RTL_LAB_%s snapshot=%s",
             snapshot_session(note_value) ? "OK" : "ERROR",
             g_save_pending.load(std::memory_order_acquire) ? "queued" : "unavailable");
    return true;
  }
  unsigned long seconds = 0;
  if (sscanf(command, "RTL_LAB RUN START %lu", &seconds) == 1 ||
      strcmp(command, "RTL_LAB RUN START") == 0) {
    if (seconds > kMaxReadings / 5) {
      strlcpy(response, "RTL_LAB_ERROR duration", response_size); return true;
    }
    snprintf(response, response_size, "RTL_LAB_%s run=%s",
             start_run(static_cast<uint32_t>(seconds)) ? "OK" : "ERROR",
             g_run_active ? "started" : "unavailable");
    return true;
  }
  if (strncmp(command, "RTL_LAB RUN MARK", 16) == 0) {
    const char* mark = command[16] == ' ' ? command + 17 : "CLI MARKER";
    if (!g_run_active || g_reading_count >= kMaxReadings || !safe_note(mark)) {
      strlcpy(response, "RTL_LAB_ERROR marker", response_size); return true;
    }
    g_readings[g_reading_count++] = make_reading(millis(), mark, true);
    strlcpy(response, "RTL_LAB_OK marker=recorded", response_size); return true;
  }
  if (strcmp(command, "RTL_LAB RUN STOP") == 0) {
    const bool ok = finish_run(g_run_paused ? "INCONCLUSIVE_SOURCE_LOST" : "RECORDED");
    snprintf(response, response_size, "RTL_LAB_%s run=%s", ok ? "OK" : "ERROR",
             ok ? "queued" : "inactive"); return true;
  }
  if (strcmp(command, "RTL_LAB RECIPE LIST") == 0) {
    strlcpy(response,
        "RTL_LAB_RECIPES identity transport rate_passport gain_quick gain_full tuner_auto rtl_agc ppm hf_path source_reconnect bias_off_on_off",
        response_size); return true;
  }
  if (strcmp(command, "RTL_LAB RECIPE STATUS") == 0) {
    snprintf(response, response_size, "RTL_LAB_RECIPE id=%s status=%s", g_recipe,
             g_recipe_status); return true;
  }
  if (strcmp(command, "RTL_LAB RECIPE CANCEL") == 0) {
    if (g_run_active) cancel_run();
    else { strlcpy(g_recipe, "NONE", sizeof(g_recipe));
           strlcpy(g_recipe_status, "CANCELLED", sizeof(g_recipe_status)); }
    strlcpy(response, "RTL_LAB_OK recipe=cancelled", response_size); return true;
  }
  char recipe[32]{};
  if (sscanf(command, "RTL_LAB RECIPE START %31s", recipe) == 1) {
    if (strcmp(recipe, "identity") == 0) {
      const Runtime r = runtime();
      const bool pass = r.driver_version[0] && r.capabilities;
      const bool ok = snapshot_result("IDENTITY CAPABILITY INVENTORY", pass ? "PASS" : "FAIL");
      strlcpy(g_recipe_status, ok ? (pass ? "PASS" : "FAIL") : "BUSY", sizeof(g_recipe_status));
      snprintf(response, response_size, "RTL_LAB_%s recipe=identity status=%s",
               ok ? "OK" : "ERROR", g_recipe_status); return true;
    }
    if (strcmp(recipe, "transport") == 0) {
      const Runtime r = runtime();
      if (!start_run(10, "TRANSPORT BASELINE")) {
        strlcpy(response, "RTL_LAB_ERROR recipe=busy_or_no_sd", response_size); return true;
      }
      strlcpy(g_recipe, recipe, sizeof(g_recipe));
      strlcpy(g_recipe_status, "RUNNING_10_SECONDS", sizeof(g_recipe_status));
      g_recipe_usb_overruns = r.usb_overruns;
      g_recipe_consumer_drops = r.consumer_drops;
      strlcpy(response, "RTL_LAB_OK recipe=transport status=running", response_size);
      return true;
    }
    constexpr const char* carrier_recipes[] = {
        "gain_quick", "gain_full", "tuner_auto", "rtl_agc", "ppm", "hf_path"};
    bool carrier_recipe = false;
    for (const char* candidate : carrier_recipes)
      carrier_recipe |= strcmp(recipe, candidate) == 0;
    const bool known_guided = carrier_recipe || strcmp(recipe, "rate_passport") == 0 ||
        strcmp(recipe, "source_reconnect") == 0 ||
        strcmp(recipe, "bias_off_on_off") == 0;
    if (!known_guided) {
      strlcpy(response, "RTL_LAB_ERROR recipe=unknown", response_size); return true;
    }
    const char* reason = carrier_recipe && !g_reference.configured
        ? "INCONCLUSIVE_REFERENCE_UNAVAILABLE"
        : "INCONCLUSIVE_MANUAL_STEP_REQUIRED";
    snapshot_result(recipe, reason);
    strlcpy(g_recipe_status, reason, sizeof(g_recipe_status));
    snprintf(response, response_size, "RTL_LAB_OK recipe=%s status=%s", recipe, reason);
    return true;
  }
  if (strcmp(command, "RTL_LAB RECORDS LIST") == 0) {
    snprintf(response, response_size, "RTL_LAB_RECORDS count=%u newest=%s result=%s",
             static_cast<unsigned>(g_recent_count), g_recent_count ? g_recent_ids[0] : "none",
             g_recent_count ? g_recent_results[0] : "none"); return true;
  }
  char record_id[32]{};
  if (sscanf(command, "RTL_LAB RECORDS SHOW %31s", record_id) == 1) {
    for (size_t i = 0; i < g_recent_count; ++i) {
      if (strcmp(record_id, g_recent_ids[i]) == 0) {
        snprintf(response, response_size, "RTL_LAB_RECORD id=%s result=%s path=/orcsdr/rf_lab/%s",
                 record_id, g_recent_results[i], record_id); return true;
      }
    }
    strlcpy(response, "RTL_LAB_ERROR record=not_found", response_size); return true;
  }
  char id[40]{};
  long value = 0;
  if (sscanf(command, "RTL_LAB SET %39s %ld", id, &value) == 2) {
    ActionKind kind = ActionKind::none;
    if (strcmp(id, "frequency_hz") == 0 && value >= 500000 && value <= 1766000000 &&
        has_cap(ESP_RTL_SDR_CAP_RETUNE)) kind = ActionKind::tune_hz;
    else if (strcmp(id, "sample_rate_sps") == 0 &&
             ((value >= 225001 && value <= 300000) ||
              (value >= 900000 && value <= 3200000)) &&
             has_cap(ESP_RTL_SDR_CAP_CONTINUOUS_RATE)) kind = ActionKind::sample_rate_sps;
    else if (strcmp(id, "ppm") == 0 && value >= -200 && value <= 200 &&
             has_cap(ESP_RTL_SDR_CAP_FREQ_CORRECTION)) kind = ActionKind::ppm;
    else if (strcmp(id, "gain_tenth_db") == 0 && valid_gain(runtime(), value) &&
             has_cap(ESP_RTL_SDR_CAP_GAIN)) kind = ActionKind::gain_tenth_db;
    else if (strcmp(id, "gain_mode") == 0 && (value == 0 || value == 1) &&
             has_cap(value ? ESP_RTL_SDR_CAP_GAIN : ESP_RTL_SDR_CAP_GAIN_AUTO)) kind = ActionKind::gain_mode;
    else if (strcmp(id, "rtl_agc") == 0 && (value == 0 || value == 1) &&
             has_cap(ESP_RTL_SDR_CAP_RTL_AGC)) kind = ActionKind::rtl_agc;
    else if (strcmp(id, "bias_tee") == 0 && value == 0 &&
             has_cap(ESP_RTL_SDR_CAP_BIAS_TEE)) kind = ActionKind::bias_tee;
    else if (strcmp(id, "bias_tee") == 0 && value == 1 &&
             has_cap(ESP_RTL_SDR_CAP_BIAS_TEE) &&
             static_cast<int32_t>(g_bias_cli_ack_until_ms - millis()) > 0) {
      kind = ActionKind::bias_tee;
      g_bias_cli_ack_until_ms = 0;
    }
    if (kind == ActionKind::none) {
      strlcpy(response, "RTL_LAB_ERROR invalid_or_unsafe", response_size); return true;
    }
    queue(kind, static_cast<int32_t>(value));
    snprintf(response, response_size, "RTL_LAB_OK id=%s queued=1", id); return true;
  }
  return false;
}

bool self_check() {
  if (next_rate(256000) != 960000 || next_rate(2560000) != 256000 ||
      !inside(20, kFooterY, 20, kFooterY, 304, kFooterH) ||
      !safe_note("bounded note") || safe_note("comma,is_not_csv_safe") ||
      safe_note("quote\"is_not_json_safe")) return false;
  Page parsed{};
  Runtime a{}, b{};
  a.frequency_hz = b.frequency_hz = 100000000;
  a.sample_rate_sps = b.sample_rate_sps = 960000;
  a.gain_mode = b.gain_mode = GainMode::manual;
  a.gain_tenth_db = b.gain_tenth_db = 297;
  a.gain_count = 3;
  a.gain_ladder[0] = 0; a.gain_ladder[1] = 297; a.gain_ladder[2] = 496;
  if (next_gain(a) != 496) return false;
  if (!same_calibration_state(a, b)) return false;
  b.ppm = 1;
  return !same_calibration_state(a, b) && parse_page("measurements", &parsed) &&
         parsed == Page::measurements && kPlotY + kPlotH < kFooterY &&
         sizeof(g_pending_marker) == 48 &&
         rf_analysis::self_check();
}

}  // namespace orcsdr::rf_lab
