#pragma once
// ACARS archive: every CRC-verified block the receiver decodes is appended to a JSONL file on the SD card, and the newest
// blocks are read back into a bounded in-memory store that the dashboard's ARCHIVE tab browses.
//
//   Model::service() --live record--> Logger::log() --> line buffer --> SD file (flushed every few seconds)
//                                         \--> Store::add()  (what the ARCHIVE tab shows)
//   at screen open:  Logger::load_history() --> parses the newest lines of the newest files --> Store
//
// Files: /orcsdr/acars/YYYYMMDD.jsonl (UTC date) or /orcsdr/acars/undated.jsonl when the clock is not valid. One JSON object
// per line, flat, keys in a fixed order; the text is stored as hex (exact) plus a printable preview (greppable). Nothing is
// uploaded or sent anywhere. Pure C++ apart from the file operations, which the firmware supplies (FileOps), so the format, the
// logger and the store are host-tested with an in-memory file system.
#include <cstddef>
#include <cstdint>

#include "acars_dashboard_model.hpp"

namespace orcsdr::acars_archive {

constexpr size_t kStoreCapacity = 300;            // records browsable in the ARCHIVE tab (newest kept)
constexpr size_t kLineMax = 1100;                 // longest line the writer produces (and the reader accepts)
constexpr size_t kBufferBytes = 4096;             // pending lines before a flush
constexpr uint32_t kFlushAgeMs = 20000;           // a pending line is written within this long
constexpr size_t kMaxFiles = 30;                  // dated files kept (oldest removed first)
constexpr uint32_t kUndatedMaxBytes = 2u * 1024u * 1024u;   // undated.jsonl is restarted when it grows past this
constexpr size_t kHistoryReadBytes = 320 * 1024;  // newest bytes of the log parsed at open
constexpr uint32_t kSeqBase = 0x80000000u;        // archive seq values never collide with the live ring's

// ---- line format --------------------------------------------------------------------------------------------------------
// Returns the line length including the trailing '\n', or 0 when `capacity` is too small.
size_t format_line(const acars_ui::MessageRecord& record, char* out, size_t capacity);
// Parses one line (with or without trailing newline). False for anything that is not a complete, valid record: truncated lines
// from a power cut, other event lines, garbage. Never reads past `length`.
bool parse_line(const char* line, size_t length, acars_ui::MessageRecord* out);
// File name for a record: "YYYYMMDD" from the UTC time, or "undated".
void day_key(const acars_ui::MessageRecord& record, char* out, size_t capacity);

// ---- bounded store (what the ARCHIVE tab browses) -----------------------------------------------------------------------
class Store : public acars_ui::ArchiveSource {
 public:
  Store();
  void clear();
  // Adds a record (newest). Assigns the archive seq. Returns the stored copy.
  const acars_ui::MessageRecord& add(const acars_ui::MessageRecord& record);
  size_t count() const override { return held_; }
  size_t query(const acars_ui::ArchiveQuery& query, size_t offset, const acars_ui::MessageRecord** out, size_t capacity,
               size_t* total) const override;
  bool cycle_tail(char* tail_inout) const override;
  bool cycle_label(char* label_inout) const override;
  const acars_ui::MessageRecord* find(uint32_t seq) const override;
  uint32_t neighbor(uint32_t seq, bool newer) const override;
  const char* status() const override { return status_; }
  void set_status(const char* text);
 private:
  char status_[24] = "LOG OFF";
  const acars_ui::MessageRecord& at_newest(size_t k) const;   // k = 0 is the newest
  acars_ui::MessageRecord records_[kStoreCapacity];
  size_t head_ = 0;      // next slot to write
  size_t held_ = 0;
  uint32_t next_seq_ = kSeqBase;
};

// ---- logger -------------------------------------------------------------------------------------------------------------
struct DirEntry {
  char name[32]{};
  uint32_t size = 0;
};

// File operations supplied by the firmware (SD card) or a test (memory). All paths are absolute.
struct FileOps {
  bool (*ready)(bool mount) = nullptr;                                   // card present; mount it when asked
  bool (*make_dir)(const char* path) = nullptr;                          // creates one directory level; true if it exists after
  void* (*open)(const char* path, bool append) = nullptr;                // handle, or null
  size_t (*write)(void* handle, const void* data, size_t size) = nullptr;
  size_t (*read)(void* handle, void* out, size_t size) = nullptr;
  bool (*seek)(void* handle, uint32_t position) = nullptr;
  uint32_t (*file_size)(void* handle) = nullptr;
  void (*close)(void* handle) = nullptr;                                 // closing flushes
  size_t (*list)(const char* dir, DirEntry* out, size_t capacity) = nullptr;
  bool (*remove)(const char* path) = nullptr;
};

enum class LogState : uint8_t { off, no_card, ready, write_error };
const char* log_state_name(LogState state);

struct LogStats {
  LogState state = LogState::off;
  uint32_t written = 0;        // lines written to the card since start
  uint32_t pending = 0;        // lines waiting for a flush
  uint32_t dropped = 0;        // lines lost because the card refused writes
  uint32_t loaded = 0;         // records read back from the card at open
  uint32_t skipped_lines = 0;  // unparseable lines met while loading
  uint32_t files_removed = 0;  // old files deleted by the retention rule
};

class Logger {
 public:
  // `dir` is the folder the day files live in (the self-test uses a scratch folder).
  explicit Logger(Store* store, const char* dir = "/orcsdr/acars");
  void begin(const FileOps& ops, uint32_t now_ms);   // mounts the card if needed, creates directories, applies retention
  void end(uint32_t now_ms);                         // flushes what is pending
  // Called for every live verified block (UI task). Adds it to the store and queues its line.
  void log(const acars_ui::MessageRecord& record, uint32_t now_ms);
  // UI task, every tick: writes pending lines when old enough or when the buffer is filling.
  void service(uint32_t now_ms);
  // Reads the newest part of the newest files into the store (call once after begin()).
  void load_history();
  LogStats stats() const;
  bool active() const { return state_ == LogState::ready; }
 private:
  bool flush(uint32_t now_ms);
  void apply_retention();
  // Scratch buffers live here, not on the stack: the Logger sits in PSRAM, and the calling task has only a few KB of stack
  // (an on-device test overflowed it and the board reset through the watchdog).
  struct NameBuf { char text[32]; };
  struct Pick { char name[32]; uint32_t size; uint32_t take; };
  DirEntry entries_[64];
  NameBuf names_[64];
  Pick picks_[40];
  char line_[kLineMax + 2];
  char chunk_[1024];
  FileOps ops_{};
  char dir_[48];
  Store* store_;
  LogState state_ = LogState::off;
  char buffer_[kBufferBytes];
  size_t used_ = 0;
  uint32_t pending_lines_ = 0;
  uint32_t oldest_pending_ms_ = 0;
  char buffer_day_[12]{};
  LogStats stats_{};
};

}  // namespace orcsdr::acars_archive
