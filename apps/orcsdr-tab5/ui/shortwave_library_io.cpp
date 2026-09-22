#include "shortwave_library.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace orcsdr::shortwave {
namespace {

void set_error(char* error, size_t capacity, const char* message) {
  if (error && capacity) std::snprintf(error, capacity, "%s", message);
}

bool recover_backup(storage::FileSystem& fs, const char* path, char* error,
                    size_t error_capacity) {
  char backup[160];
  std::snprintf(backup, sizeof(backup), "%s.bak", path);
  if (fs.exists(path) || !fs.exists(backup)) return true;
  if (fs.rename(backup, path)) return true;
  set_error(error, error_capacity, "cannot restore Shortwave library backup");
  return false;
}

void remove_backup(storage::FileSystem& fs, const char* path) {
  char backup[160];
  std::snprintf(backup, sizeof(backup), "%s.bak", path);
  if (fs.exists(backup)) fs.remove(backup);
}

enum class ReadResult : uint8_t { record, eof, error };

ReadResult read_record(File& file, char* output, size_t capacity) {
  if (!output || capacity < 2) return ReadResult::error;
  size_t used = 0;
  bool quoted = false;
  bool read_any = false;
  while (file.available()) {
    char value = '\0';
    if (file.read(&value, 1) != 1) return ReadResult::error;
    read_any = true;
    if (used + 1 >= capacity) return ReadResult::error;
    output[used++] = value;
    if (value == '\"') {
      if (!quoted) {
        quoted = true;
      } else if (file.available()) {
        char next = '\0';
        if (file.read(&next, 1) != 1) return ReadResult::error;
        if (used + 1 >= capacity) return ReadResult::error;
        output[used++] = next;
        if (next != '\"') {
          quoted = false;
          if (next == '\n') break;
        }
      } else {
        quoted = false;
      }
    } else if (value == '\n' && !quoted) {
      break;
    }
  }
  while (used && (output[used - 1] == '\r' || output[used - 1] == '\n')) --used;
  output[used] = '\0';
  return read_any ? ReadResult::record : ReadResult::eof;
}

template <typename Table, typename Record>
bool load_table(storage::FileSystem& fs, const char* path, Table* table,
                bool (*decode)(const char*, Record*), size_t* invalid_rows,
                char* error, size_t error_capacity) {
  const std::unique_ptr<Table> loaded(new (std::nothrow) Table);
  if (!loaded) {
    set_error(error, error_capacity, "cannot allocate Shortwave library table");
    return false;
  }
  size_t invalid = 0;
  if (!fs.exists(path)) {
    *table = *loaded;
    return true;
  }
  File file = fs.open(path, FILE_READ);
  if (!file) {
    set_error(error, error_capacity, "cannot open Shortwave library");
    return false;
  }
  char record[2048];
  for (;;) {
    const ReadResult read = read_record(file, record, sizeof(record));
    if (read == ReadResult::eof) break;
    if (read == ReadResult::error) {
      file.close();
      set_error(error, error_capacity, "Shortwave library record is too large");
      return false;
    }
    if (record[0] == '#') continue;
    Record value{};
    if (!decode(record, &value) || loaded->upsert(value) != RecordResult::ok)
      ++invalid;
  }
  file.close();
  *table = *loaded;
  *invalid_rows += invalid;
  return true;
}

template <>
bool load_table<LogTable, LogEntry>(storage::FileSystem& fs, const char* path,
                                    LogTable* table,
                                    bool (*decode)(const char*, LogEntry*),
                                    size_t* invalid_rows, char* error,
                                    size_t error_capacity) {
  const std::unique_ptr<LogTable> loaded(new (std::nothrow) LogTable);
  if (!loaded) {
    set_error(error, error_capacity, "cannot allocate Shortwave logbook table");
    return false;
  }
  size_t invalid = 0;
  if (!fs.exists(path)) {
    *table = *loaded;
    return true;
  }
  File file = fs.open(path, FILE_READ);
  if (!file) {
    set_error(error, error_capacity, "cannot open Shortwave logbook");
    return false;
  }
  char record[2048];
  for (;;) {
    const ReadResult read = read_record(file, record, sizeof(record));
    if (read == ReadResult::eof) break;
    if (read == ReadResult::error) {
      file.close();
      set_error(error, error_capacity, "Shortwave logbook record is too large");
      return false;
    }
    if (record[0] == '#') continue;
    LogEntry value{};
    if (!decode(record, &value) || loaded->append(value) != RecordResult::ok)
      ++invalid;
  }
  file.close();
  *table = *loaded;
  *invalid_rows += invalid;
  return true;
}

template <typename Table, typename Record>
bool load_table_with_backup(storage::FileSystem& fs, const char* path,
                            Table* table, bool (*decode)(const char*, Record*),
                            size_t* invalid_rows, char* error,
                            size_t error_capacity) {
  if (!recover_backup(fs, path, error, error_capacity)) return false;
  if (load_table(fs, path, table, decode, invalid_rows, error, error_capacity)) {
    remove_backup(fs, path);
    return true;
  }

  char backup[160];
  std::snprintf(backup, sizeof(backup), "%s.bak", path);
  if (!fs.exists(backup)) return false;
  const std::unique_ptr<Table> recovered(new (std::nothrow) Table);
  if (!recovered) {
    set_error(error, error_capacity, "cannot allocate Shortwave backup table");
    return false;
  }
  size_t recovered_invalid = 0;
  if (!load_table(fs, backup, recovered.get(), decode, &recovered_invalid, error,
                  error_capacity))
    return false;
  if (fs.exists(path) && !fs.remove(path)) {
    set_error(error, error_capacity, "cannot replace invalid Shortwave library");
    return false;
  }
  if (!fs.rename(backup, path)) {
    set_error(error, error_capacity, "cannot restore Shortwave library backup");
    return false;
  }
  *table = *recovered;
  *invalid_rows += recovered_invalid;
  return true;
}

template <typename Table, typename Record>
bool write_table(storage::FileSystem& fs, const char* path, const char* header,
                 const Table& table,
                 bool (*encode)(const Record&, char*, size_t),
                 bool (*decode)(const char*, Record*), char* error,
                 size_t error_capacity) {
  char temporary[160], backup[160];
  std::snprintf(temporary, sizeof(temporary), "%s.part", path);
  std::snprintf(backup, sizeof(backup), "%s.bak", path);
  fs.mkdir("/OrcSDR");
  fs.mkdir(kLibraryRoot);
  fs.remove(temporary);
  File file = fs.open(temporary, FILE_WRITE, true);
  if (!file) {
    set_error(error, error_capacity, "cannot create Shortwave library");
    return false;
  }
  if (!file.print(header)) {
    file.close();
    fs.remove(temporary);
    set_error(error, error_capacity, "cannot write Shortwave library header");
    return false;
  }
  char record[2048];
  for (size_t i = 0; i < table.size(); ++i) {
    const Record* value = table.at(i);
    if (!value || !encode(*value, record, sizeof(record)) ||
        file.print(record) == 0 || file.print("\r\n") == 0) {
      file.close();
      fs.remove(temporary);
      set_error(error, error_capacity, "cannot write Shortwave library");
      return false;
    }
  }
  const bool flushed = file.flush();
  const bool closed = file.close();
  if (!flushed || !closed) {
    fs.remove(temporary);
    set_error(error, error_capacity, "cannot flush Shortwave library");
    return false;
  }

  File verify = fs.open(temporary, FILE_READ);
  if (!verify) {
    fs.remove(temporary);
    set_error(error, error_capacity, "cannot verify Shortwave library");
    return false;
  }
  size_t verified = 0;
  for (;;) {
    const ReadResult read = read_record(verify, record, sizeof(record));
    if (read == ReadResult::eof) break;
    if (read == ReadResult::error) {
      verify.close();
      fs.remove(temporary);
      set_error(error, error_capacity, "Shortwave library verification read failed");
      return false;
    }
    if (record[0] == '#') continue;
    Record value{};
    if (!decode(record, &value)) {
      verify.close();
      fs.remove(temporary);
      set_error(error, error_capacity, "Shortwave library verification failed");
      return false;
    }
    ++verified;
  }
  if (!verify.close() || verified != table.size()) {
    fs.remove(temporary);
    set_error(error, error_capacity, "Shortwave library record count mismatch");
    return false;
  }

  fs.remove(backup);
  const bool had_target = fs.exists(path);
  if (had_target && !fs.rename(path, backup)) {
    fs.remove(temporary);
    set_error(error, error_capacity, "cannot back up Shortwave library");
    return false;
  }
  if (!fs.rename(temporary, path)) {
    if (had_target) fs.rename(backup, path);
    fs.remove(temporary);
    set_error(error, error_capacity, "cannot replace Shortwave library");
    return false;
  }
  if (had_target) fs.remove(backup);
  return true;
}

bool write_exports(storage::FileSystem& fs, const LogTable& logs, char* error,
                   size_t error_capacity) {
  fs.mkdir("/OrcSDR");
  fs.mkdir(kExportRoot);
  const uint64_t stamp = logs.size() ? logs.at(logs.size() - 1)->timestamp_utc : 0;
  char csv_path[160], adi_path[160];
  std::snprintf(csv_path, sizeof(csv_path),
                "/OrcSDR/exports/shortwave-logbook-%llu.csv",
                static_cast<unsigned long long>(stamp));
  std::snprintf(adi_path, sizeof(adi_path),
                "/OrcSDR/exports/shortwave-logbook-%llu.adi",
                static_cast<unsigned long long>(stamp));
  File csv = fs.open(csv_path, FILE_WRITE, true);
  File adi = fs.open(adi_path, FILE_WRITE, true);
  if (!csv || !adi) {
    csv.close(); adi.close();
    set_error(error, error_capacity, "cannot create Shortwave exports");
    return false;
  }
  csv.print("# OrcSDR Shortwave logbook CSV v1\r\n");
  char record[2048];
  for (size_t i = 0; i < logs.size(); ++i) {
    const LogEntry* entry = logs.at(i);
    if (!entry || !encode_log_csv(*entry, record, sizeof(record)) ||
        !csv.print(record) || !csv.print("\r\n") ||
        !encode_adif(*entry, record, sizeof(record)) || !adi.print(record)) {
      csv.close(); adi.close();
      set_error(error, error_capacity, "cannot write Shortwave exports");
      return false;
    }
  }
  const bool csv_flushed = csv.flush();
  const bool adi_flushed = adi.flush();
  const bool csv_closed = csv.close();
  const bool adi_closed = adi.close();
  const bool flushed = csv_flushed && adi_flushed;
  const bool closed = csv_closed && adi_closed;
  if (flushed && closed) return true;
  set_error(error, error_capacity, "cannot flush Shortwave exports");
  return false;
}

}  // namespace

bool load_library(storage::FileSystem& fs, LibraryState* state, char* error,
                  size_t error_capacity) {
  if (!state) return false;
  const std::unique_ptr<MemoryTable> memories(new (std::nothrow) MemoryTable);
  const std::unique_ptr<LogTable> logs(new (std::nothrow) LogTable);
  if (!memories || !logs) {
    set_error(error, error_capacity, "cannot allocate Shortwave library state");
    state->status = StorageStatus::read_failed;
    return false;
  }
  *memories = state->memories;
  *logs = state->logs;
  size_t invalid_rows = 0;
  if (!load_table_with_backup(fs, kMemoriesPath, memories.get(), decode_memory_csv,
                              &invalid_rows, error, error_capacity) ||
      !load_table_with_backup(fs, kLogbookPath, logs.get(), decode_log_csv,
                              &invalid_rows, error, error_capacity)) {
    state->status = StorageStatus::read_failed;
    return false;
  }
  state->memories = *memories;
  state->logs = *logs;
  state->invalid_rows = invalid_rows;
  state->status = StorageStatus::ready;
  return true;
}

bool save_memories(storage::FileSystem& fs, const MemoryTable& memories,
                   char* error, size_t error_capacity) {
  return write_table(fs, kMemoriesPath, "# OrcSDR Shortwave memories CSV v1\r\n",
                     memories, encode_memory_csv, decode_memory_csv, error,
                     error_capacity);
}

bool save_logs(storage::FileSystem& fs, const LogTable& logs, char* error,
               size_t error_capacity) {
  return write_table(fs, kLogbookPath, "# OrcSDR Shortwave logbook CSV v1\r\n",
                     logs, encode_log_csv, decode_log_csv, error, error_capacity);
}

bool export_logs(storage::FileSystem& fs, const LogTable& logs, char* error,
                 size_t error_capacity) {
  return write_exports(fs, logs, error, error_capacity);
}

}  // namespace orcsdr::shortwave
