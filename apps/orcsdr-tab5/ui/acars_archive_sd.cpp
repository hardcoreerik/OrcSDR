#include "acars_archive_sd.hpp"

#include <cstdio>
#include <cstring>
#include <new>

#include <esp_heap_caps.h>

#include "orcsdr_storage.hpp"

namespace orcsdr::acars_archive_sd {
namespace {

using acars_archive::DirEntry;

bool ready(bool mount) {
  if (orcsdr::storage::mounted()) return true;
  return mount && orcsdr::storage::mount_tab5_sd();
}

bool make_dir(const char* path) {
  auto& fs = orcsdr::storage::filesystem();
  if (fs.exists(path)) return true;
  return fs.mkdir(path) || fs.exists(path);
}

// A handle is a heap-allocated File; the archive code only ever holds it between open and close.
void* open_file(const char* path, bool append) {
  File* file = new (std::nothrow) File(orcsdr::storage::filesystem().open(path, append ? FILE_APPEND : FILE_READ, append));
  if (!file) return nullptr;
  if (!*file) {
    delete file;
    return nullptr;
  }
  return file;
}

size_t write_file(void* handle, const void* data, size_t size) {
  return handle ? static_cast<File*>(handle)->write(static_cast<const uint8_t*>(data), size) : 0;
}

size_t read_file(void* handle, void* out, size_t size) {
  return handle ? static_cast<File*>(handle)->read(static_cast<uint8_t*>(out), size) : 0;
}

bool seek_file(void* handle, uint32_t position) { return handle && static_cast<File*>(handle)->seek(position); }

uint32_t size_of(void* handle) { return handle ? static_cast<uint32_t>(static_cast<File*>(handle)->size()) : 0; }

void close_file(void* handle) {
  if (!handle) return;
  File* file = static_cast<File*>(handle);
  file->flush();
  file->close();
  delete file;
}

size_t list_dir(const char* dir, DirEntry* out, size_t capacity) {
  File root = orcsdr::storage::filesystem().open(dir, FILE_READ);
  if (!root || !root.isDirectory()) return 0;
  size_t n = 0;
  for (File entry = root.openNextFile(); entry && n < capacity; entry = root.openNextFile()) {
    if (entry.isDirectory()) continue;
    const char* name = entry.name();
    const char* slash = name ? std::strrchr(name, '/') : nullptr;     // some layers return the full path
    std::snprintf(out[n].name, sizeof(out[n].name), "%s", slash ? slash + 1 : (name ? name : ""));
    out[n].size = static_cast<uint32_t>(entry.size());
    ++n;
  }
  return n;
}

bool remove_file(const char* path) { return orcsdr::storage::filesystem().remove(path); }

}  // namespace

const acars_archive::FileOps& file_ops() {
  static const acars_archive::FileOps ops = [] {
    acars_archive::FileOps o;
    o.ready = ready;
    o.make_dir = make_dir;
    o.open = open_file;
    o.write = write_file;
    o.read = read_file;
    o.seek = seek_file;
    o.file_size = size_of;
    o.close = close_file;
    o.list = list_dir;
    o.remove = remove_file;
    return o;
  }();
  return ops;
}

namespace {
// "/orcsdr/acars-selftest/<name>", bounded copy (the compiler cannot prove a printf of a file name fits).
void scratch_path(char* out, size_t capacity, const char* name) {
  static const char kDir[] = "/orcsdr/acars-selftest/";
  size_t used = 0;
  for (const char* p = kDir; *p && used + 1 < capacity; ++p) out[used++] = *p;
  for (const char* p = name; *p && used + 1 < capacity; ++p) out[used++] = *p;
  out[used] = 0;
}
}  // namespace

bool self_test(char* out, size_t size) {
  using namespace acars_archive;
  constexpr char kScratch[] = "/orcsdr/acars-selftest";
  constexpr unsigned kRecords = 12;
  const FileOps& ops = file_ops();
  void* memory = heap_caps_malloc(2 * sizeof(Store) + 2 * sizeof(Logger) + sizeof(acars_ui::MessageRecord),
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) {
    std::snprintf(out, size, "RTL_ACARS_ARCHIVETEST result=FAIL reason=no_memory");
    return false;
  }
  uint8_t* base = static_cast<uint8_t*>(memory);
  Store* writer_store = new (base) Store();
  Logger* writer = new (base + sizeof(Store)) Logger(writer_store, kScratch);
  Store* reader_store = new (base + sizeof(Store) + sizeof(Logger)) Store();
  Logger* reader = new (base + 2 * sizeof(Store) + sizeof(Logger)) Logger(reader_store, kScratch);
  acars_ui::MessageRecord* record = new (base + 2 * sizeof(Store) + 2 * sizeof(Logger)) acars_ui::MessageRecord();
  // Clean any leftovers from an interrupted earlier run.
  writer->begin(ops, 0);
  {
    DirEntry entries[16];
    const size_t n = ops.list(kScratch, entries, 16);
    for (size_t i = 0; i < n; ++i) {
      char path[96];
      scratch_path(path, sizeof(path), entries[i].name);
      ops.remove(path);
    }
  }
  const bool ready = writer->active();
  for (unsigned i = 0; ready && i < kRecords; ++i) {
    acars_ui::MessageRecord& m = *record;
    m = acars_ui::MessageRecord{};
    std::snprintf(m.tail, sizeof(m.tail), "N%05u", i);
    std::snprintf(m.flight, sizeof(m.flight), "TEST%02u", i);
    std::snprintf(m.label, sizeof(m.label), "H%u", i % 3);
    m.mode = '2'; m.acknowledgement = '\x15'; m.block_id = static_cast<char>('0' + i % 10); m.end_marker = 0x03;
    m.frequency_hz = 131550000u; m.time_valid = true; m.utc_epoch = 1760000000u + i; m.uptime_ms = 1000u * i;
    m.text_length = static_cast<uint16_t>(std::snprintf(m.text, sizeof(m.text), "ARCHIVE SELFTEST RECORD %u \"quoted\" \\ slash", i));
    m.has_text = true;
    m.raw_bytes = static_cast<uint16_t>(m.text_length + 16);
    acars_ui::sanitize_preview(m.preview, sizeof(m.preview), m.text, m.text_length);
    writer->log(m, 0);
  }
  writer->end(0);
  reader->begin(ops, 0);
  reader->load_history();
  bool match = ready && reader->stats().loaded == kRecords && reader_store->count() == kRecords;
  const acars_ui::MessageRecord* rows[1];
  size_t total = 0;
  if (match && reader_store->query({}, 0, rows, 1, &total) == 1) {
    char expected[64];
    std::snprintf(expected, sizeof(expected), "ARCHIVE SELFTEST RECORD %u \"quoted\" \\ slash", kRecords - 1);
    match = std::strlen(expected) == rows[0]->text_length && std::memcmp(expected, rows[0]->text, rows[0]->text_length) == 0 &&
            std::strcmp(rows[0]->tail, "N00011") == 0 && rows[0]->utc_epoch == 1760000000u + kRecords - 1;
  } else {
    match = false;
  }
  const LogStats ws = writer->stats();
  const LogStats rs = reader->stats();
  {   // remove the scratch files so nothing is left behind
    DirEntry entries[16];
    const size_t n = ops.list(kScratch, entries, 16);
    for (size_t i = 0; i < n; ++i) {
      char path[96];
      scratch_path(path, sizeof(path), entries[i].name);
      ops.remove(path);
    }
  }
  reader->~Logger(); reader_store->~Store(); writer->~Logger(); writer_store->~Store();
  heap_caps_free(memory);
  std::snprintf(out, size, "RTL_ACARS_ARCHIVETEST sd=%d written=%u dropped=%u loaded=%u skipped=%u result=%s",
                ready ? 1 : 0, static_cast<unsigned>(ws.written), static_cast<unsigned>(ws.dropped),
                static_cast<unsigned>(rs.loaded), static_cast<unsigned>(rs.skipped_lines), match ? "PASS" : "FAIL");
  return match;
}

}  // namespace orcsdr::acars_archive_sd
