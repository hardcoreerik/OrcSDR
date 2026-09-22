#pragma once

#include "shortwave_model.hpp"
#include "orcsdr_storage.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::shortwave {

enum class StorageStatus : uint8_t { unavailable, ready, read_failed, write_failed };

constexpr char kLibraryRoot[] = "/OrcSDR/shortwave";
constexpr char kMemoriesPath[] = "/OrcSDR/shortwave/memories.csv";
constexpr char kLogbookPath[] = "/OrcSDR/shortwave/logbook.csv";
constexpr char kExportRoot[] = "/OrcSDR/exports";

struct LibraryState {
  MemoryTable memories;
  LogTable logs;
  StorageStatus status = StorageStatus::unavailable;
  size_t invalid_rows = 0;
};

bool encode_memory_csv(const Memory& memory, char* output, size_t capacity);
bool decode_memory_csv(const char* input, Memory* memory);
bool encode_log_csv(const LogEntry& entry, char* output, size_t capacity);
bool decode_log_csv(const char* input, LogEntry* entry);
bool encode_adif(const LogEntry& entry, char* output, size_t capacity);
bool load_library(storage::FileSystem& fs, LibraryState* state,
                  char* error, size_t error_capacity);
bool save_memories(storage::FileSystem& fs, const MemoryTable& memories,
                   char* error, size_t error_capacity);
bool save_logs(storage::FileSystem& fs, const LogTable& logs,
               char* error, size_t error_capacity);
bool export_logs(storage::FileSystem& fs, const LogTable& logs,
                 char* error, size_t error_capacity);
bool library_self_check();

}  // namespace orcsdr::shortwave
