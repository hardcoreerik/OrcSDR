#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace orcsdr::storage {

enum class EntryStatus {missing,file,directory,error};

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
  bool flush();
  bool sync();  // Flush stdio and commit filesystem data/metadata to the card.
  bool truncate(size_t size); // Shrink only; caller must own the removed suffix.
  bool close();
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
  EntryStatus status(const char* path) const; // Distinguish absence from SD I/O failure.
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

struct SdHardware {
  int slot=0,clk=43,cmd=44,d0=39,d1=40,d2=41,d3=42;
  int power_ldo_channel=4; // -1: board adapter already powers the slot
};
// Configure before mounting, from the selected board; legacy Tab5 defaults remain.
// Reject configuration changes while a card is mounted.
bool configure_sd_hardware(const SdHardware& hardware);
bool mount_tab5_sd();
bool mounted();
FileSystem& filesystem();
// Filesystem capacity, not raw card sectors. Unmounted/query failures return 0.
uint64_t total_bytes();
uint64_t used_bytes();
uint64_t free_bytes();
bool query_free_bytes(uint64_t* available); // False distinguishes unknown capacity from a full card.
bool run_file_semantics_check();
bool run_write_benchmark(uint32_t file_mib = 32);

}  // namespace orcsdr::storage
