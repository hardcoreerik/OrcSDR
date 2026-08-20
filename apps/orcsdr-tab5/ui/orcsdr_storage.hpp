#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace orcsdr::storage {

class File {
 public:
  File() = default;
  explicit operator bool() const;
  size_t read(uint8_t* data, size_t size);
  size_t read(char* data, size_t size);
  size_t readBytes(char* data, size_t size);
  size_t readBytesUntil(char delimiter, char* data, size_t size);
  size_t write(const uint8_t* data, size_t size);
  size_t printf(const char* format, ...);
  size_t print(const char* value);
  bool available() const;
  size_t size() const;
  size_t position() const;
  bool seek(size_t position);
  void flush();
  void close();
  bool isDirectory() const;
  const char* name() const;
  uint64_t getLastWrite() const;
  File openNextFile();

 private:
  struct State;
  explicit File(std::shared_ptr<State> state);
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

bool mount_tab5_sd();
bool mounted();
FileSystem& filesystem();
uint64_t total_bytes();
uint64_t used_bytes();

}  // namespace orcsdr::storage
