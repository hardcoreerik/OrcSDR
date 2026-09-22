#include "orcsdr_storage.hpp"
#include "sd_benchmark_plan.hpp"
#include "wifi_service.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include <esp_vfs_fat.h>
#include <esp_cache.h>
#include <esp_private/esp_cache_private.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdmmc_cmd.h>
#include <sd_pwr_ctrl_by_on_chip_ldo.h>
#include <driver/sdmmc_default_configs.h>
#include <driver/sdmmc_host.h>

namespace {
constexpr size_t kWriteBufferBytes = 32 * 1024;
bool g_mounted = false;
sdmmc_card_t* g_card = nullptr;
sd_pwr_ctrl_handle_t g_sd_power = nullptr;
orcsdr::storage::FileSystem g_filesystem;
bool g_report_next_write_buffer = false;

std::string mounted_path(const char* path) {
  if (!path || strncmp(path, "/sd/", 4) == 0 || strcmp(path, "/sd") == 0) return path ? path : "";
  return std::string("/sd") + path;
}

// ESP-Hosted initializes SDMMC for the C6 on Slot 1.  The Tab5 card is Slot 0
// on the same controller, so do not reinitialize or deinitialize it here.
esp_err_t sdmmc_host_init_already_running() { return ESP_OK; }
esp_err_t sdmmc_host_deinit_already_running() { return ESP_OK; }

}

namespace orcsdr::storage {

struct File::State {
  ~State() {
    if (stream) fclose(stream);
    if (write_buffer) heap_caps_free(write_buffer);
    if (directory) closedir(directory);
  }
  FILE* stream = nullptr;
  uint8_t* write_buffer = nullptr;
  bool write_buffer_configured = false;
  DIR* directory = nullptr;
  std::string path;
};

File::File(std::shared_ptr<State> state) : state_(std::move(state)) {}
File::operator bool() const { return state_ && (state_->stream || state_->directory); }
size_t File::read(uint8_t* data, size_t size) { return !state_ || !state_->stream ? 0 : fread(data, 1, size, state_->stream); }
size_t File::read(char* data, size_t size) { return read(reinterpret_cast<uint8_t*>(data), size); }
size_t File::readBytes(char* data, size_t size) { return read(data, size); }
size_t File::readBytesUntil(char delimiter, char* data, size_t size) {
  if (!state_ || !state_->stream || data == nullptr || size == 0) return 0;
  size_t count = 0;
  for (; count < size; ++count) { const int c = fgetc(state_->stream); if (c == EOF || c == delimiter) break; data[count] = static_cast<char>(c); }
  return count;
}
size_t File::write(const uint8_t* data, size_t size) { return !state_ || !state_->stream ? 0 : fwrite(data, 1, size, state_->stream); }
size_t File::printf(const char* format, ...) { if (!state_ || !state_->stream) return 0; va_list args; va_start(args, format); const int result = vfprintf(state_->stream, format, args); va_end(args); return result > 0 ? static_cast<size_t>(result) : 0; }
size_t File::print(const char* value) { return value ? write(reinterpret_cast<const uint8_t*>(value), strlen(value)) : 0; }
bool File::available() const { if (!state_ || !state_->stream) return false; const long here = ftell(state_->stream); fseek(state_->stream, 0, SEEK_END); const long end = ftell(state_->stream); fseek(state_->stream, here, SEEK_SET); return here < end; }
size_t File::size() const { if (!state_ || !state_->stream) return 0; const long here = ftell(state_->stream); fseek(state_->stream, 0, SEEK_END); const long end = ftell(state_->stream); fseek(state_->stream, here, SEEK_SET); return end > 0 ? static_cast<size_t>(end) : 0; }
size_t File::position() const { return !state_ || !state_->stream ? 0 : static_cast<size_t>(ftell(state_->stream)); }
bool File::seek(size_t position) { return state_ && state_->stream && fseek(state_->stream, static_cast<long>(position), SEEK_SET) == 0; }
bool File::flush() { return !state_ || !state_->stream || fflush(state_->stream) == 0; }
bool File::close() {
  if (!state_) return true;
  bool ok = true;
  if (state_->stream) {
    ok = fclose(state_->stream) == 0;
    state_->stream = nullptr;
  }
  if (state_->write_buffer) {
    heap_caps_free(state_->write_buffer);
    state_->write_buffer = nullptr;
  }
  if (state_->directory) {
    ok = closedir(state_->directory) == 0 && ok;
    state_->directory = nullptr;
  }
  state_.reset();
  return ok;
}
bool File::isDirectory() const { return state_ && state_->directory != nullptr; }
const char* File::name() const { return state_ ? state_->path.c_str() : ""; }
uint64_t File::getLastWrite() const { struct stat info{}; const std::string path = state_ ? mounted_path(state_->path.c_str()) : ""; return state_ && stat(path.c_str(), &info) == 0 ? static_cast<uint64_t>(info.st_mtime) : 0; }
File File::openNextFile() {
  if (!state_ || !state_->directory) return {};
  while (dirent* entry = readdir(state_->directory)) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    std::string path = state_->path + (state_->path.back() == '/' ? "" : "/") + entry->d_name;
    return FileSystem{}.open(path.c_str(), FILE_READ);
  }
  return {};
}

File FileSystem::open(const char* path, const char* mode, bool) const {
  if (!path || !orcsdr::storage::mounted()) return {};
  const std::string mounted = mounted_path(path);
  struct stat info{};
  if (stat(mounted.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) {
    auto state = std::make_shared<File::State>(); state->directory = opendir(mounted.c_str()); state->path = path; return File{std::move(state)};
  }
  auto state = std::make_shared<File::State>();
  const bool writing = mode && mode[0] == 'w';
  state->stream = fopen(mounted.c_str(), writing ? "wb" : "rb");
  if (state->stream && writing) {
    state->write_buffer = static_cast<uint8_t*>(heap_caps_malloc(
        kWriteBufferBytes,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_CACHE_ALIGNED));
    if (state->write_buffer) {
      state->write_buffer_configured =
          setvbuf(state->stream, reinterpret_cast<char*>(state->write_buffer), _IOFBF,
                  kWriteBufferBytes) == 0;
      if (!state->write_buffer_configured) {
        heap_caps_free(state->write_buffer);
        state->write_buffer = nullptr;
      }
    }
  }
  if (writing && g_report_next_write_buffer) {
    g_report_next_write_buffer = false;
    size_t alignment = 0;
    const bool alignment_ok =
        esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &alignment) == ESP_OK && alignment > 0;
    printf("RTL_SD_BUFFER bytes=%u alignment=%u ptr_aligned=%d size_aligned=%d "
           "psram=%d setvbuf=%d\n",
           static_cast<unsigned>(kWriteBufferBytes), static_cast<unsigned>(alignment),
           state->write_buffer && alignment_ok &&
                   reinterpret_cast<uintptr_t>(state->write_buffer) % alignment == 0,
           alignment_ok && kWriteBufferBytes % alignment == 0,
           state->write_buffer && esp_ptr_external_ram(state->write_buffer),
           state->write_buffer_configured ? 1 : 0);
  }
  state->path = path;
  return File{std::move(state)};
}
bool FileSystem::exists(const char* path) const { const std::string mounted = mounted_path(path); struct stat info{}; return path && stat(mounted.c_str(), &info) == 0; }
bool FileSystem::mkdir(const char* path) const { const std::string mounted = mounted_path(path); return path && (::mkdir(mounted.c_str(), 0775) == 0 || errno == EEXIST); }
bool FileSystem::remove(const char* path) const { const std::string mounted = mounted_path(path); return path && unlink(mounted.c_str()) == 0; }
bool FileSystem::rename(const char* from, const char* to) const { const std::string source = mounted_path(from); const std::string destination = mounted_path(to); return from && to && ::rename(source.c_str(), destination.c_str()) == 0; }

}  // namespace orcsdr::storage

namespace orcsdr::storage {

bool mount_tab5_sd() {
  if (g_mounted) return true;
  esp_vfs_fat_sdmmc_mount_config_t mount = {.format_if_mount_failed = false, .max_files = 8, .allocation_unit_size = 16 * 1024};
  // M5Stack's Tab5 reference uses native SDMMC Slot 0 for the card. ESP-Hosted
  // owns Slot 1 for the C6, so both devices use their intended slots.
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_0;
  host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
  if (orcsdr::wifi::hosted_transport_ready()) {
    host.init = &sdmmc_host_init_already_running;
    host.deinit = &sdmmc_host_deinit_already_running;
  }
  if (!g_sd_power) {
    const sd_pwr_ctrl_ldo_config_t ldo = {.ldo_chan_id = 4};
    const esp_err_t power_result = sd_pwr_ctrl_new_on_chip_ldo(&ldo, &g_sd_power);
    if (power_result != ESP_OK) {
      ESP_LOGE("orcsdr_storage", "SD LDO4 init failed: %s", esp_err_to_name(power_result));
      return false;
    }
  }
  host.pwr_ctrl_handle = g_sd_power;
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 4;
  slot.clk = GPIO_NUM_43;
  slot.cmd = GPIO_NUM_44;
  slot.d0 = GPIO_NUM_39;
  slot.d1 = GPIO_NUM_40;
  slot.d2 = GPIO_NUM_41;
  slot.d3 = GPIO_NUM_42;
  const esp_err_t mount_result = esp_vfs_fat_sdmmc_mount("/sd", &host, &slot, &mount, &g_card);
  g_mounted = mount_result == ESP_OK;
  if (g_mounted) sdmmc_card_print_info(stdout, g_card);
  else ESP_LOGE("orcsdr_storage", "SDMMC Slot0 mount failed: %s", esp_err_to_name(mount_result));
  return g_mounted;
}

bool mounted() { return g_mounted; }
FileSystem& filesystem() { return g_filesystem; }
uint64_t total_bytes() { return g_card ? static_cast<uint64_t>(g_card->csd.capacity) * g_card->csd.sector_size : 0; }
uint64_t used_bytes() { return 0; }

bool run_file_semantics_check() {
  if (!mount_tab5_sd() || !g_filesystem.mkdir("/orcsdr")) return false;
  constexpr const char* kPath = "/orcsdr/sdcheck.bin";
  constexpr const char* kRenamed = "/orcsdr/sdcheck.renamed";
  constexpr uint8_t kPayload[] = "OrcSDR storage semantics check";
  if (g_filesystem.exists(kPath)) (void)g_filesystem.remove(kPath);
  if (g_filesystem.exists(kRenamed)) (void)g_filesystem.remove(kRenamed);

  File file = g_filesystem.open(kPath, FILE_WRITE, true);
  const bool create_ok = static_cast<bool>(file);
  const bool write_ok = create_ok && file.write(kPayload, sizeof(kPayload)) == sizeof(kPayload);
  const bool flush_ok = write_ok && file.flush();
  const bool write_close_ok = create_ok && file.close();

  uint8_t actual[sizeof(kPayload)]{};
  File reader = write_close_ok ? g_filesystem.open(kPath, FILE_READ) : File{};
  const bool read_ok = reader && reader.read(actual, sizeof(actual)) == sizeof(actual) &&
                       memcmp(actual, kPayload, sizeof(kPayload)) == 0;
  const bool close_ok = write_close_ok && reader.close();
  const bool rename_ok = read_ok && close_ok && g_filesystem.rename(kPath, kRenamed);
  const bool remove_ok = rename_ok && g_filesystem.remove(kRenamed);
  const bool pass = create_ok && write_ok && flush_ok && close_ok && read_ok && rename_ok &&
                    remove_ok;
  if (g_filesystem.exists(kPath)) (void)g_filesystem.remove(kPath);
  if (g_filesystem.exists(kRenamed)) (void)g_filesystem.remove(kRenamed);
  printf("RTL_SD_SELF_CHECK_RESULT create=%d write=%d flush=%d close=%d read=%d "
         "rename=%d remove=%d pass=%d\n",
         create_ok, write_ok, flush_ok, close_ok, read_ok, rename_ok, remove_ok, pass);
  fflush(stdout);
  return pass;
}

bool run_write_benchmark(uint32_t file_mib) {
  if (file_mib < 4 || file_mib > 64) {
    printf("RTL_SD_BENCH_ERROR reason=file_size\n");
    fflush(stdout);
    return false;
  }
  if (!mount_tab5_sd()) {
    printf("RTL_SD_BENCH_ERROR reason=sd_mount\n");
    fflush(stdout);
    return false;
  }
  constexpr const char* kDirectory = "/orcsdr";
  constexpr const char* kPath = "/orcsdr/sdbench.bin";
  constexpr size_t kMaxChunk = 128 * 1024;
  const uint64_t total_bytes = static_cast<uint64_t>(file_mib) * 1024u * 1024u;
  if (!g_filesystem.mkdir(kDirectory)) {
    printf("RTL_SD_BENCH_ERROR reason=mkdir\n");
    fflush(stdout);
    return false;
  }
  size_t cache_alignment = 0;
  if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &cache_alignment) != ESP_OK ||
      cache_alignment == 0) {
    printf("RTL_SD_BENCH_ERROR reason=cache_alignment\n");
    fflush(stdout);
    return false;
  }
  uint8_t* buffer = static_cast<uint8_t*>(heap_caps_malloc(
      kMaxChunk, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_CACHE_ALIGNED));
  if (!buffer) {
    printf("RTL_SD_BENCH_ERROR reason=psram_buffer bytes=%u\n",
           static_cast<unsigned>(kMaxChunk));
    fflush(stdout);
    return false;
  }
  for (size_t i = 0; i < kMaxChunk; ++i)
    buffer[i] = static_cast<uint8_t>((i * 131u + 17u) & 0xffu);

  printf("RTL_SD_BENCH_BEGIN file_mib=%lu bytes=%llu path=%s buffering=production "
         "cache_alignment=%u address_aligned=%d\n",
         static_cast<unsigned long>(file_mib),
         static_cast<unsigned long long>(total_bytes), kPath,
         static_cast<unsigned>(cache_alignment),
         reinterpret_cast<uintptr_t>(buffer) % cache_alignment == 0 ? 1 : 0);
  sdmmc_card_print_info(stdout, g_card);
  bool pass = true;
  g_report_next_write_buffer = true;
  for (const size_t chunk : benchmark::kChunkBytes) {
    double loop_sum = 0.0;
    double durable_sum = 0.0;
    const size_t runs = benchmark::repetitions(chunk);
    for (size_t iteration = 1; iteration <= runs; ++iteration) {
      File file = g_filesystem.open(kPath, FILE_WRITE, true);
      uint64_t written = 0;
      const int64_t started_us = esp_timer_get_time();
      while (file && written < total_bytes) {
        const size_t request = static_cast<size_t>(
            std::min<uint64_t>(chunk, total_bytes - written));
        const size_t wrote = file.write(buffer, request);
        if (wrote != request) {
          printf("RTL_SD_BENCH_ERROR chunk=%u iteration=%u wrote=%u expected=%u\n",
                 static_cast<unsigned>(chunk), static_cast<unsigned>(iteration),
                 static_cast<unsigned>(wrote), static_cast<unsigned>(request));
          pass = false;
          break;
        }
        written += wrote;
        if ((written & ((1u << 20) - 1u)) == 0) vTaskDelay(1);
      }
      const uint64_t loop_us = static_cast<uint64_t>(esp_timer_get_time() - started_us);
      const bool flush_ok = file.flush();
      const bool close_ok = file.close();
      const uint64_t durable_us = static_cast<uint64_t>(esp_timer_get_time() - started_us);
      const bool remove_ok = !g_filesystem.exists(kPath) || g_filesystem.remove(kPath);
      const double loop_rate = benchmark::mib_per_second(written, loop_us);
      const double durable_rate = benchmark::mib_per_second(written, durable_us);
      printf("RTL_SD_BENCH_RUN chunk=%u iteration=%u bytes=%llu loop_us=%llu "
             "durable_us=%llu loop_mib_s=%.3f durable_mib_s=%.3f "
             "flush=%d close=%d remove=%d pass=%d\n",
             static_cast<unsigned>(chunk), static_cast<unsigned>(iteration),
             static_cast<unsigned long long>(written),
             static_cast<unsigned long long>(loop_us),
             static_cast<unsigned long long>(durable_us), loop_rate, durable_rate,
             flush_ok ? 1 : 0, close_ok ? 1 : 0, remove_ok ? 1 : 0,
             pass && written == total_bytes && flush_ok && close_ok && remove_ok ? 1 : 0);
      fflush(stdout);
      if (!pass || written != total_bytes || !flush_ok || !close_ok || !remove_ok) {
        pass = false;
        break;
      }
      loop_sum += loop_rate;
      durable_sum += durable_rate;
    }
    if (!pass) break;
    printf("RTL_SD_BENCH_SUMMARY chunk=%u runs=%u loop_avg_mib_s=%.3f "
           "durable_avg_mib_s=%.3f\n",
           static_cast<unsigned>(chunk), static_cast<unsigned>(runs),
           loop_sum / runs, durable_sum / runs);
    fflush(stdout);
  }
  heap_caps_free(buffer);
  if (g_filesystem.exists(kPath)) (void)g_filesystem.remove(kPath);
  printf("RTL_SD_BENCH_DONE pass=%d\n", pass ? 1 : 0);
  fflush(stdout);
  return pass;
}

}  // namespace orcsdr::storage
