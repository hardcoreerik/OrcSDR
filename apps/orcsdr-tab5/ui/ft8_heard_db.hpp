#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::ft8 {

// Persistent "stations heard" table, built only from this device's own decodes (no network, no external data).
//
// One entry per callsign: first/last heard (UTC seconds), times heard, the bands and modes it was heard on, best and last SNR, last grid, and
// whether a contact was logged. The table lives in caller-provided memory (PSRAM on the Tab5) as an open-addressing hash, so there is no
// heap use. Persistence is an append-only journal of fixed 40-byte records: after a change the entry is serialised and appended; loading
// replays the journal and the last record for a callsign wins. A corrupt record (bad CRC) is skipped, never trusted. The daily decode CSVs
// remain the source of truth, so the journal can always be rebuilt.
//
// Known limit (accepted): the 16-byte header (magic + version) has no CRC of its own. A flipped bit in the version byte makes the whole file read as an unknown
// version: nothing is loaded and nothing is appended, but the file is left untouched (never rewritten), so the history is recoverable by hand or by rebuilding
// from the daily CSV logs. Guessing at the version instead would risk loading records laid out differently.
constexpr size_t kHeardCallLen = 12;   // up to 11 characters + NUL
constexpr size_t kHeardGridLen = 8;
constexpr size_t kHeardRecordBytes = 40;
constexpr size_t kHeardHeaderBytes = 16;

enum HeardFlags : uint8_t {
  heard_flag_worked = 1u << 0,      // a contact with this station has been logged
  heard_flag_snr_known = 1u << 1,   // best_snr / last_snr are real measurements
  heard_flag_dirty = 1u << 7,       // in memory only: changed since the last journal write (never serialised)
};

struct HeardEntry {
  char callsign[kHeardCallLen]{};
  char grid[kHeardGridLen]{};
  uint32_t first_utc = 0;
  uint32_t last_utc = 0;
  uint32_t count = 0;
  uint16_t band_mask = 0;   // bit n = band index n
  uint8_t mode_mask = 0;    // bit n = DigitalMode n
  int8_t best_snr = 0;
  int8_t last_snr = 0;
  uint8_t flags = 0;
};

struct HeardObservation {
  const char* callsign = nullptr;
  const char* grid = nullptr;   // may be empty
  uint32_t utc = 0;
  uint8_t band = 0;             // band index, 0..15
  uint8_t mode = 0;             // DigitalMode value, 0..7
  int8_t snr = 0;
  bool snr_known = false;
};

enum class HeardClass : uint8_t { first_time, heard_before, worked };

struct HeardUpdate {
  bool stored = false;           // false when the callsign was invalid or the table is full
  HeardClass before = HeardClass::first_time;   // what the device knew BEFORE this observation (for the colour code)
  bool new_band = false;         // first time on this band
  bool new_mode = false;
  const HeardEntry* entry = nullptr;   // the entry after the update
};

class HeardDb {
 public:
  // `memory` must stay valid for the object's life. Capacity is memory / sizeof(HeardEntry), rounded down to a power of two.
  bool begin(void* memory, size_t bytes);
  size_t capacity() const { return capacity_; }
  size_t size() const { return size_; }
  size_t dropped() const { return dropped_; }   // observations lost because the table was full

  HeardUpdate observe(const HeardObservation& observation);
  const HeardEntry* find(const char* callsign) const;
  bool mark_worked(const char* callsign);

  // Serialises up to `max` changed entries (40 bytes each) and clears their dirty bit. Returns how many.
  size_t drain_dirty(uint8_t* out, size_t max_records);
  static void journal_header(uint8_t out[kHeardHeaderBytes]);
  // Marks the entries inside serialised records dirty again. drain_dirty clears the mark as it copies a record out, so a caller whose write then failed
  // (or was short) hands the same records back here and they go out with the next drain instead of waiting for the station to be heard again.
  size_t requeue(const uint8_t* records, size_t record_count);
  // Replays journal bytes (header optional on the first call). Returns the records applied; bad records are counted in `rejected()`.
  size_t load(const uint8_t* data, size_t length);
  size_t rejected() const { return rejected_; }

  // Replays a whole journal from a reader that may return FEWER bytes than asked at any time (a file read can stop at a sector boundary without being at the
  // end). The header is read to completion, then records are applied whole; the tail of a partial record is carried to the front of `scratch` so the next
  // read continues exactly where the file left off. `scratch` must hold at least one record. A reader returning 0 means the end.
  using ReadFn = size_t (*)(void* context, uint8_t* out, size_t max_bytes);
  struct ReplayResult {
    size_t applied = 0;
    bool recognised = false;   // false: the header is missing, not ours, or a newer version (nothing was applied; do not append to this file)
  };
  ReplayResult replay(ReadFn read, void* context, uint8_t* scratch, size_t scratch_bytes);

  // Walks entries in hash order; `index` runs 0..capacity()-1. Returns nullptr for an empty slot.
  const HeardEntry* slot(size_t index) const { return index < capacity_ && entries_[index].callsign[0] ? &entries_[index] : nullptr; }

  static bool valid_callsign(const char* callsign);

 private:
  size_t find_slot(const char* callsign, bool* found) const;
  HeardEntry* entries_ = nullptr;
  size_t capacity_ = 0;
  size_t size_ = 0;
  size_t dropped_ = 0;
  size_t rejected_ = 0;
  size_t drain_cursor_ = 0;
};

bool heard_db_self_check();

}  // namespace orcsdr::ft8
