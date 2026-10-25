// Storage interface used by the Tab5 modules (apps/orcsdr-tab5/ui/orcsdr_storage.hpp). The Tab5 backs it with
// its SD card; this target has no card mounted yet, so every call reports "not available" and the modules fall
// back to their built-in defaults.
#include "orcsdr_storage.hpp"

namespace orcsdr::storage {

struct File::State {};

File::operator bool() const { return false; }
size_t File::read(uint8_t*, size_t) { return 0; }
size_t File::read(char*, size_t) { return 0; }
size_t File::readBytes(char*, size_t) { return 0; }
size_t File::readBytesUntil(char, char*, size_t) { return 0; }
size_t File::write(const uint8_t*, size_t) { return 0; }
size_t File::printf(const char*, ...) { return 0; }
size_t File::print(const char*) { return 0; }
bool File::available() const { return false; }
size_t File::size() const { return 0; }
size_t File::position() const { return 0; }
bool File::seek(size_t) { return false; }
bool File::flush() { return false; }
bool File::sync() { return false; }
bool File::close() { return false; }
bool File::isDirectory() const { return false; }
const char* File::name() const { return ""; }
uint64_t File::getLastWrite() const { return 0; }
File File::openNextFile() { return File(); }

File FileSystem::open(const char*, const char*, bool) const { return File(); }
bool FileSystem::exists(const char*) const { return false; }
bool FileSystem::mkdir(const char*) const { return false; }
bool FileSystem::remove(const char*) const { return false; }
bool FileSystem::rename(const char*, const char*) const { return false; }

bool mount_tab5_sd() { return false; }
bool mounted() { return false; }
FileSystem& filesystem() {
  static FileSystem fs;
  return fs;
}
uint64_t total_bytes() { return 0; }
uint64_t used_bytes() { return 0; }

}  // namespace orcsdr::storage

namespace orcsdr::storage {
bool run_file_semantics_check() { return false; }
bool run_write_benchmark(uint32_t) { return false; }
}  // namespace orcsdr::storage
