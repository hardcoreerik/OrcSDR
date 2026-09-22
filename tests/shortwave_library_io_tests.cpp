#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "shortwave_library.hpp"

namespace {
std::string test_root;
bool fail_flush = false;
bool truncate_part_on_close = false;

std::string host_path(const char* path) { return test_root + (path ? path : ""); }
}

namespace orcsdr::storage {

struct File::State {
  ~State() { if (stream) std::fclose(stream); }
  FILE* stream = nullptr;
  std::string path;
};

File::File(std::shared_ptr<State> state) : state_(std::move(state)) {}
File::operator bool() const { return state_ && state_->stream; }
size_t File::read(uint8_t* data, size_t size) {
  return !state_ || !state_->stream ? 0 : std::fread(data, 1, size, state_->stream);
}
size_t File::read(char* data, size_t size) {
  return read(reinterpret_cast<uint8_t*>(data), size);
}
size_t File::readBytes(char* data, size_t size) { return read(data, size); }
size_t File::readBytesUntil(char delimiter, char* data, size_t size) {
  size_t count = 0;
  while (count < size && state_ && state_->stream) {
    const int value = std::fgetc(state_->stream);
    if (value == EOF || value == delimiter) break;
    data[count++] = static_cast<char>(value);
  }
  return count;
}
size_t File::write(const uint8_t* data, size_t size) {
  return !state_ || !state_->stream ? 0 : std::fwrite(data, 1, size, state_->stream);
}
size_t File::printf(const char* format, ...) {
  if (!state_ || !state_->stream) return 0;
  va_list args;
  va_start(args, format);
  const int result = std::vfprintf(state_->stream, format, args);
  va_end(args);
  return result > 0 ? static_cast<size_t>(result) : 0;
}
size_t File::print(const char* value) {
  return value ? write(reinterpret_cast<const uint8_t*>(value), std::strlen(value)) : 0;
}
bool File::available() const {
  if (!state_ || !state_->stream) return false;
  const long current = std::ftell(state_->stream);
  std::fseek(state_->stream, 0, SEEK_END);
  const long end = std::ftell(state_->stream);
  std::fseek(state_->stream, current, SEEK_SET);
  return current < end;
}
size_t File::size() const { return 0; }
size_t File::position() const {
  return !state_ || !state_->stream ? 0 : static_cast<size_t>(std::ftell(state_->stream));
}
bool File::seek(size_t position) {
  return state_ && state_->stream &&
         std::fseek(state_->stream, static_cast<long>(position), SEEK_SET) == 0;
}
bool File::flush() { return state_ && state_->stream && !fail_flush && std::fflush(state_->stream) == 0; }
bool File::close() {
  if (!state_) return true;
  const std::string path = state_->path;
  const bool ok = !state_->stream || std::fclose(state_->stream) == 0;
  state_->stream = nullptr;
  state_.reset();
  if (truncate_part_on_close && path.size() >= 5 &&
      path.compare(path.size() - 5, 5, ".part") == 0) {
    truncate_part_on_close = false;
    std::ifstream input(host_path(path.c_str()).c_str(), std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(input)), {});
    const size_t first = contents.find('\n');
    const size_t second = first == std::string::npos ? first : contents.find('\n', first + 1);
    if (second != std::string::npos)
      std::filesystem::resize_file(host_path(path.c_str()), second + 1);
  }
  return ok;
}
bool File::isDirectory() const { return false; }
const char* File::name() const { return state_ ? state_->path.c_str() : ""; }
uint64_t File::getLastWrite() const { return 0; }
File File::openNextFile() { return {}; }

File FileSystem::open(const char* path, const char* mode, bool) const {
  if (!path) return {};
  auto state = std::make_shared<File::State>();
  state->path = path;
  state->stream = std::fopen(host_path(path).c_str(), mode && mode[0] == 'w' ? "wb" : "rb");
  return File{std::move(state)};
}
bool FileSystem::exists(const char* path) const {
  return path && std::filesystem::exists(host_path(path));
}
bool FileSystem::mkdir(const char* path) const {
  return path && (exists(path) || std::filesystem::create_directories(host_path(path)));
}
bool FileSystem::remove(const char* path) const {
  return path && std::filesystem::remove(host_path(path));
}
bool FileSystem::rename(const char* from, const char* to) const {
  std::error_code error;
  std::filesystem::rename(host_path(from), host_path(to), error);
  return !error;
}
}

namespace {
[[noreturn]] void fail(const char* expression, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}
#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)

orcsdr::shortwave::Memory memory(uint32_t frequency, const char* label) {
  orcsdr::shortwave::Memory value{};
  value.frequency_hz = frequency;
  value.bandwidth_hz = 6000;
  std::strcpy(value.mode, "AM");
  std::strcpy(value.station, label);
  return value;
}
}

int main() {
  char root[] = "/tmp/orcsdr-shortwave-XXXXXX";
  CHECK(mkdtemp(root) != nullptr);
  test_root = root;
  using namespace orcsdr::shortwave;
  orcsdr::storage::FileSystem fs;
  char error[128]{};

  MemoryTable memories;
  CHECK(memories.upsert(memory(5850000, "WRMI")) == RecordResult::ok);
  CHECK(save_memories(fs, memories, error, sizeof(error)));
  CHECK(fs.exists(kMemoriesPath));
  CHECK(!fs.exists("/OrcSDR/shortwave/memories.csv.bak"));

  CHECK(fs.rename(kMemoriesPath, "/OrcSDR/shortwave/memories.csv.bak"));
  LibraryState restored{};
  CHECK(load_library(fs, &restored, error, sizeof(error)));
  CHECK(restored.memories.size() == 1);
  CHECK(fs.exists(kMemoriesPath));
  CHECK(!fs.exists("/OrcSDR/shortwave/memories.csv.bak"));

  fail_flush = true;
  CHECK(!save_memories(fs, memories, error, sizeof(error)));
  fail_flush = false;
  CHECK(fs.exists(kMemoriesPath));

  CHECK(memories.upsert(memory(9385000, "Test two")) == RecordResult::ok);
  truncate_part_on_close = true;
  CHECK(!save_memories(fs, memories, error, sizeof(error)));
  CHECK(fs.exists(kMemoriesPath));

  LogTable logs;
  LogEntry entry{};
  entry.timestamp_utc = 1789440123;
  entry.frequency_hz = 5850000;
  entry.bandwidth_hz = 6000;
  entry.signal_dbfs = -42.0f;
  std::strcpy(entry.mode, "AM");
  CHECK(logs.append(entry) == RecordResult::ok);
  CHECK(export_logs(fs, logs, error, sizeof(error)));
  CHECK(fs.exists("/OrcSDR/exports/shortwave-logbook-1789440123.csv"));
  CHECK(fs.exists("/OrcSDR/exports/shortwave-logbook-1789440123.adi"));

  MemoryTable corrupt_source;
  CHECK(corrupt_source.upsert(memory(9385000, "Corrupt source")) == RecordResult::ok);
  CHECK(save_memories(fs, corrupt_source, error, sizeof(error)));
  std::ofstream oversized(host_path(kMemoriesPath), std::ios::app | std::ios::binary);
  CHECK(oversized.good());
  oversized << '"';
  const std::string payload(2200, 'x');
  oversized << payload;
  oversized.close();
  LibraryState rejected{};
  CHECK(rejected.memories.upsert(memory(10000000, "Keep existing")) == RecordResult::ok);
  CHECK(!load_library(fs, &rejected, error, sizeof(error)));
  CHECK(rejected.memories.size() == 1);
  CHECK(rejected.memories.at(0)->frequency_hz == 10000000);

  CHECK(save_memories(fs, memories, error, sizeof(error)));
  std::filesystem::copy_file(host_path(kMemoriesPath),
                             host_path("/OrcSDR/shortwave/memories.csv.bak"),
                             std::filesystem::copy_options::overwrite_existing);
  std::ofstream corrupt_primary(host_path(kMemoriesPath),
                                std::ios::trunc | std::ios::binary);
  CHECK(corrupt_primary.good());
  corrupt_primary << '"' << payload;
  corrupt_primary.close();
  LibraryState recovered{};
  CHECK(load_library(fs, &recovered, error, sizeof(error)));
  CHECK(recovered.memories.size() == memories.size());
  CHECK(recovered.memories.at(0)->frequency_hz == 5850000);
  CHECK(fs.exists(kMemoriesPath));
  CHECK(!fs.exists("/OrcSDR/shortwave/memories.csv.bak"));

  std::filesystem::remove_all(test_root);
  std::puts("shortwave_library_io_tests: PASS");
  return 0;
}
