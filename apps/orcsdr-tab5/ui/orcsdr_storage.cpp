#include "orcsdr_storage.hpp"
#include "wifi_service.hpp"
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
#include <sdmmc_cmd.h>
#include <sd_pwr_ctrl_by_on_chip_ldo.h>
#include <driver/sdmmc_default_configs.h>
#include <driver/sdmmc_host.h>

namespace {
bool g_mounted = false;
sdmmc_card_t* g_card = nullptr;
sd_pwr_ctrl_handle_t g_sd_power = nullptr;
orcsdr::storage::FileSystem g_filesystem;

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
  ~State() { if (stream) fclose(stream); if (directory) closedir(directory); }
  FILE* stream = nullptr;
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
void File::flush() { if (state_ && state_->stream) fflush(state_->stream); }
void File::close() { state_.reset(); }
bool File::isDirectory() const { return state_ && state_->directory != nullptr; }
const char* File::name() const { return state_ ? state_->path.c_str() : ""; }
uint64_t File::getLastWrite() const { struct stat info{}; return state_ && stat(state_->path.c_str(), &info) == 0 ? static_cast<uint64_t>(info.st_mtime) : 0; }
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
    auto state = std::make_shared<File::State>(); state->directory = opendir(mounted.c_str()); state->path = mounted; return File{std::move(state)};
  }
  auto state = std::make_shared<File::State>();
  const bool writing = mode && mode[0] == 'w';
  state->stream = fopen(mounted.c_str(), writing ? "wb" : "rb");
  if (state->stream && writing) setvbuf(state->stream, nullptr, _IONBF, 0);
  state->path = mounted;
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
  if (!g_mounted) ESP_LOGE("orcsdr_storage", "SDMMC Slot0 mount failed: %s", esp_err_to_name(mount_result));
  return g_mounted;
}

bool mounted() { return g_mounted; }
FileSystem& filesystem() { return g_filesystem; }
uint64_t total_bytes() { return g_card ? static_cast<uint64_t>(g_card->csd.capacity) * g_card->csd.sector_size : 0; }
uint64_t used_bytes() { return 0; }

}  // namespace orcsdr::storage
