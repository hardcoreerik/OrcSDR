// Web build: OrcSDR's storage layer sits on the Tab5 SD card. Here files live in memory and the page copies
// them to and from localStorage (m5_fs_export / m5_fs_import in web_main.cpp), so the shortwave memories and
// logbook, and their CSV/ADIF exports, behave as on the device. Same class and method names as the original.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace orcsdr::storage {

class File {
 public:
  File() = default;
  explicit operator bool() const { return static_cast<bool>(state_); }
  size_t read(uint8_t* data, size_t size);
  size_t read(char* data, size_t size) { return read(reinterpret_cast<uint8_t*>(data), size); }
  size_t readBytes(char* data, size_t size) { return read(data, size); }
  size_t readBytesUntil(char delimiter, char* data, size_t size);
  size_t write(const uint8_t* data, size_t size);
  size_t printf(const char* format, ...);
  size_t print(const char* value);
  bool available() const;
  size_t size() const;
  size_t position() const;
  bool seek(size_t position);
  bool flush();
  bool sync() { return flush(); }
  bool close();
  bool isDirectory() const { return false; }
  const char* name() const;
  uint64_t getLastWrite() const { return 0; }
  File openNextFile() { return File(); }

 private:
  struct State;
  explicit File(std::shared_ptr<State> state) : state_(std::move(state)) {}
  std::shared_ptr<State> state_;
  friend class FileSystem;
};

class FileSystem {
 public:
  File open(const char* path, const char* mode = "r", bool create = false) const;
  bool exists(const char* path) const;
  bool mkdir(const char* path) const;
  bool remove(const char* path) const;
  bool rename(const char* from, const char* to) const;
};

}  // namespace orcsdr::storage

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"
using File = orcsdr::storage::File;

namespace orcsdr::storage {

FileSystem& filesystem();
// The device mounts an SD card on demand; a page has none, so code that asks for one is told so (Dead Air then reports its recorder as failed).
bool mounted();
// Whole-store snapshot for the page: "path:hex" lines. Import replaces the store.
unsigned revision();
}  // namespace orcsdr::storage
