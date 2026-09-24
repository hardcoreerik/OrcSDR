#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "p25_config.hpp"

namespace {
std::string test_root;

std::string host_path(const char* path) {
  return test_root + (path ? path : "");
}
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
  if (!state_ || !state_->stream) return 0;
  size_t count = 0;
  for (; count < size; ++count) {
    const int value = fgetc(state_->stream);
    if (value == EOF || value == delimiter) break;
    data[count] = static_cast<char>(value);
  }
  return count;
}
size_t File::write(const uint8_t* data, size_t size) { return !state_ || !state_->stream ? 0 : fwrite(data, 1, size, state_->stream); }
size_t File::printf(const char* format, ...) {
  if (!state_ || !state_->stream) return 0;
  va_list args;
  va_start(args, format);
  const int result = vfprintf(state_->stream, format, args);
  va_end(args);
  return result > 0 ? static_cast<size_t>(result) : 0;
}
size_t File::print(const char* value) { return value ? write(reinterpret_cast<const uint8_t*>(value), strlen(value)) : 0; }
bool File::available() const {
  if (!state_ || !state_->stream) return false;
  const long current = ftell(state_->stream);
  fseek(state_->stream, 0, SEEK_END);
  const long end = ftell(state_->stream);
  fseek(state_->stream, current, SEEK_SET);
  return current < end;
}
size_t File::size() const { return 0; }
size_t File::position() const { return !state_ || !state_->stream ? 0 : static_cast<size_t>(ftell(state_->stream)); }
bool File::seek(size_t position) { return state_ && state_->stream && fseek(state_->stream, static_cast<long>(position), SEEK_SET) == 0; }
bool File::flush() { return !state_ || !state_->stream || fflush(state_->stream) == 0; }
bool File::close() { state_.reset(); return true; }
bool File::isDirectory() const { return state_ && state_->directory; }
const char* File::name() const { return state_ ? state_->path.c_str() : ""; }
uint64_t File::getLastWrite() const { return 0; }
File File::openNextFile() {
  if (!state_ || !state_->directory) return {};
  while (dirent* entry = readdir(state_->directory)) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    return FileSystem{}.open((state_->path + "/" + entry->d_name).c_str());
  }
  return {};
}

File FileSystem::open(const char* path, const char* mode, bool) const {
  if (!path) return {};
  const std::string local = host_path(path);
  struct stat info{};
  auto state = std::make_shared<File::State>();
  state->path = path;
  if (stat(local.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
    state->directory = opendir(local.c_str());
  else
    state->stream = fopen(local.c_str(), mode && mode[0] == 'w' ? "wb" : "rb");
  return File{std::move(state)};
}
bool FileSystem::exists(const char* path) const { return path && std::filesystem::exists(host_path(path)); }
bool FileSystem::mkdir(const char* path) const {
  return path && (std::filesystem::exists(host_path(path)) ||
                  std::filesystem::create_directories(host_path(path)));
}
bool FileSystem::remove(const char* path) const { return path && std::filesystem::remove(host_path(path)); }
bool FileSystem::rename(const char* from, const char* to) const {
  if (!from || !to) return false;
  std::error_code error;
  std::filesystem::rename(host_path(from), host_path(to), error);
  return !error;
}

}  // namespace orcsdr::storage

namespace {

[[noreturn]] void fail(const char* expression, int line) {
  std::fprintf(stderr, "P25 CONFIG FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}

#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)

void write_text(const char* path, const char* text) {
  std::filesystem::create_directories(std::filesystem::path(host_path(path)).parent_path());
  FILE* file = fopen(host_path(path).c_str(), "wb");
  CHECK(file != nullptr);
  CHECK(fwrite(text, 1, strlen(text), file) == strlen(text));
  fclose(file);
}

struct Run {
  Run() {
    char root[] = "/tmp/orcsdr-p25-XXXXXX";
    CHECK(mkdtemp(root) != nullptr);
    test_root = root;
    using namespace orcsdr::p25config;
    orcsdr::storage::FileSystem fs;
    Config config{};
    StoreState state{};
    char error[64]{};

    defaults(&config);
    CHECK(config.control_channel_count == 0);
    CHECK(std::strcmp(config.system_name, "No P25 system configured") == 0);
    CHECK(!validate(config, error, sizeof(error)));
    CHECK(load_active(fs, &config, &state, error, sizeof(error)) == LoadResult::missing);

    constexpr char migrated[] =
        "version=1\nsystem_name=Portable Test\nwacn=781824\nsystem_id=101\n"
        "rfss=1\nsite=2\ncontrol_channel_hz=155000000\n"
        "control_channel_hz=851012500\nlast_control_channel_hz=851012500\n"
        "talkgroup=123,Dispatch\n";
    write_text(kLegacyPath, migrated);
    CHECK(load_active(fs, &config, &state, error, sizeof(error)) == LoadResult::ok);
    CHECK(state.count == 1 && std::strcmp(state.active_id, "legacy-import") == 0);
    CHECK(fs.exists(kLegacyPath));
    CHECK(fs.exists("/orcsdr/p25/legacy-import/profile.cfg"));
    CHECK(config.version == kSchemaVersion && config.control_channel_count == 2);

    write_text("/orcsdr/duplicate.cfg", migrated);
    CHECK(!import_profile(fs, "/orcsdr/duplicate.cfg", "p25_reserved", &state,
                          error, sizeof(error)));
    CHECK(std::strcmp(error, "p25_ ids are reserved for catalog packs") == 0);
    CHECK(!import_profile(fs, "/orcsdr/duplicate.cfg", "duplicate", &state,
                          error, sizeof(error)));
    CHECK(std::strcmp(error, "system identity already installed") == 0);
    CHECK(export_profile(fs, "legacy-import", "/orcsdr/exports/test.cfg",
                         error, sizeof(error)));
    CHECK(!export_profile(fs, "legacy-import", "/orcsdr/p25/escape.cfg",
                          error, sizeof(error)));
    CHECK(rename_profile(fs, "legacy-import", "Renamed", &state, error, sizeof(error)));
    CHECK(select(fs, "legacy-import", &config, &state, error, sizeof(error)));
    CHECK(std::strcmp(config.system_name, "Renamed") == 0);
    write_text("/orcsdr/p25/legacy-import/profile.cfg", "interrupted\n");
    CHECK(select(fs, "legacy-import", &config, &state, error, sizeof(error)));
    CHECK(std::strcmp(config.system_name, "Portable Test") == 0);
    CHECK(delete_profile(fs, "legacy-import", &state, error, sizeof(error)));
    CHECK(state.count == 0 && !fs.exists(kActivePath));

    constexpr char generic[] =
        "version=2\nsystem_name=Generic\ncontrol_channel_hz=851012500\n";
    write_text("/orcsdr/generic.cfg", generic);
    for (size_t i = 0; i < kMaxProfiles; ++i) {
      char id[32]{};
      snprintf(id, sizeof(id), "profile-%u", static_cast<unsigned>(i));
      CHECK(import_profile(fs, "/orcsdr/generic.cfg", id, &state, error, sizeof(error)));
    }
    CHECK(state.count == kMaxProfiles);
    CHECK(!import_profile(fs, "/orcsdr/generic.cfg", "profile-overflow", &state,
                          error, sizeof(error)));
    CHECK(std::strcmp(error, "profile limit reached") == 0);

    CHECK(!parse("version=2\nsystem_name=Bad\ncontrol_channel_hz=1\n",
                 &config, error, sizeof(error)));
    CHECK(!parse("version=2\nsystem_name=Bad\nunknown=value\n",
                 &config, error, sizeof(error)));
    CHECK(valid_profile_id("wacn-12345_sys-123"));
    CHECK(!valid_profile_id("../escape"));
    CHECK(self_check());
    std::filesystem::remove_all(test_root);
  }
} run;

}  // namespace
