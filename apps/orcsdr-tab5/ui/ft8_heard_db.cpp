#include "ft8_heard_db.hpp"

#include <cstring>

namespace orcsdr::ft8 {

// The serialised layout is fixed by offsets in serialise/deserialise below: callsign 0..11, grid 12..19, three u32 at 20/24/28, band mask 32..33, mode mask 34,
// two SNR bytes 35/36, flags 37, CRC-16 38..39. Any change to these constants must change the offsets (and the journal version).
static_assert(kHeardCallLen == 12 && kHeardGridLen == 8, "callsign and grid fields are 12 and 8 bytes in the journal record");
static_assert(kHeardRecordBytes == 40 && kHeardHeaderBytes == 16, "journal record and header sizes");
static_assert(sizeof(HeardEntry) <= 48, "entry size feeds the PSRAM budget in main.cpp");

namespace {

uint32_t fnv1a(const char* s) {
  uint32_t h = 2166136261u;
  for (; *s; ++s) {
    h ^= static_cast<uint8_t>(*s);
    h *= 16777619u;
  }
  return h;
}

uint16_t crc16(const uint8_t* data, size_t length) {   // CCITT-FALSE, only to catch torn or damaged journal records
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int b = 0; b < 8; ++b) crc = (crc & 0x8000u) ? static_cast<uint16_t>((crc << 1) ^ 0x1021u) : static_cast<uint16_t>(crc << 1);
  }
  return crc;
}

void put32(uint8_t* p, uint32_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
  p[2] = (v >> 16) & 0xFF;
  p[3] = (v >> 24) & 0xFF;
}
uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }

void serialise(const HeardEntry& e, uint8_t out[kHeardRecordBytes]) {
  std::memset(out, 0, kHeardRecordBytes);
  std::memcpy(out, e.callsign, kHeardCallLen);
  std::memcpy(out + 12, e.grid, kHeardGridLen);
  put32(out + 20, e.first_utc);
  put32(out + 24, e.last_utc);
  put32(out + 28, e.count);
  out[32] = e.band_mask & 0xFF;
  out[33] = e.band_mask >> 8;
  out[34] = e.mode_mask;
  out[35] = static_cast<uint8_t>(e.best_snr);
  out[36] = static_cast<uint8_t>(e.last_snr);
  out[37] = e.flags & static_cast<uint8_t>(~heard_flag_dirty);
  const uint16_t crc = crc16(out, 38);
  out[38] = crc & 0xFF;
  out[39] = crc >> 8;
}

bool deserialise(const uint8_t in[kHeardRecordBytes], HeardEntry* e) {
  const uint16_t crc = static_cast<uint16_t>(in[38] | (in[39] << 8));
  if (crc16(in, 38) != crc) return false;
  HeardEntry out{};
  std::memcpy(out.callsign, in, kHeardCallLen);
  std::memcpy(out.grid, in + 12, kHeardGridLen);
  out.callsign[kHeardCallLen - 1] = '\0';
  out.grid[kHeardGridLen - 1] = '\0';
  if (!HeardDb::valid_callsign(out.callsign)) return false;
  out.first_utc = get32(in + 20);
  out.last_utc = get32(in + 24);
  out.count = get32(in + 28);
  out.band_mask = static_cast<uint16_t>(in[32] | (in[33] << 8));
  out.mode_mask = in[34];
  out.best_snr = static_cast<int8_t>(in[35]);
  out.last_snr = static_cast<int8_t>(in[36]);
  out.flags = in[37] & static_cast<uint8_t>(~heard_flag_dirty);
  *e = out;
  return true;
}

// Folds a journal record into an entry that already exists. Each journal record is a cumulative snapshot of one station, so replaying them in
// file order and keeping the last would be enough, but merging makes replay independent of order (a duplicated or older record after a newer one,
// two journals concatenated) and can only add information: earliest first-heard, latest last-heard, the larger count, the union of bands and
// modes, the best SNR, and the grid and last SNR from whichever record is newer.
void merge_entry(HeardEntry& e, const HeardEntry& n) {
  const bool n_newer = n.last_utc >= e.last_utc;
  if (n.first_utc != 0 && (e.first_utc == 0 || n.first_utc < e.first_utc)) e.first_utc = n.first_utc;
  if (n.last_utc > e.last_utc) e.last_utc = n.last_utc;
  if (n.count > e.count) e.count = n.count;
  e.band_mask = static_cast<uint16_t>(e.band_mask | n.band_mask);
  e.mode_mask = static_cast<uint8_t>(e.mode_mask | n.mode_mask);
  if (n.flags & heard_flag_snr_known) {
    if (!(e.flags & heard_flag_snr_known) || n.best_snr > e.best_snr) e.best_snr = n.best_snr;
    if (n_newer || !(e.flags & heard_flag_snr_known)) e.last_snr = n.last_snr;
    e.flags = static_cast<uint8_t>(e.flags | heard_flag_snr_known);
  }
  if (n.grid[0] && (n_newer || !e.grid[0])) std::memcpy(e.grid, n.grid, kHeardGridLen);
  e.flags = static_cast<uint8_t>(e.flags | (n.flags & heard_flag_worked));
}

const uint8_t kMagic[8] = {'O', 'R', 'C', 'H', 'E', 'A', 'R', 'D'};

}  // namespace

bool HeardDb::valid_callsign(const char* c) {
  if (c == nullptr) return false;
  size_t n = 0;
  bool digit = false, letter = false;
  for (; c[n]; ++n) {
    const char ch = c[n];
    const bool ok = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || ch == '/';
    if (!ok || n >= kHeardCallLen - 1) return false;
    digit = digit || (ch >= '0' && ch <= '9');
    letter = letter || (ch >= 'A' && ch <= 'Z');
  }
  return n >= 3 && digit && letter;
}

bool HeardDb::begin(void* memory, size_t bytes) {
  if (memory == nullptr) return false;
  size_t cap = 1;
  while (cap * 2 * sizeof(HeardEntry) <= bytes) cap *= 2;
  if (cap * sizeof(HeardEntry) > bytes || cap < 16) return false;
  entries_ = static_cast<HeardEntry*>(memory);
  capacity_ = cap;
  for (size_t i = 0; i < cap; ++i) entries_[i] = HeardEntry{};
  size_ = dropped_ = rejected_ = drain_cursor_ = 0;
  return true;
}

size_t HeardDb::find_slot(const char* callsign, bool* found) const {
  size_t i = fnv1a(callsign) & (capacity_ - 1);
  for (size_t probes = 0; probes < capacity_; ++probes, i = (i + 1) & (capacity_ - 1)) {
    if (!entries_[i].callsign[0]) {
      *found = false;
      return i;
    }
    if (std::strcmp(entries_[i].callsign, callsign) == 0) {
      *found = true;
      return i;
    }
  }
  *found = false;
  return capacity_;   // full
}

const HeardEntry* HeardDb::find(const char* callsign) const {
  if (entries_ == nullptr || !valid_callsign(callsign)) return nullptr;
  bool found = false;
  const size_t i = find_slot(callsign, &found);
  return found ? &entries_[i] : nullptr;
}

HeardUpdate HeardDb::observe(const HeardObservation& o) {
  HeardUpdate update;
  if (entries_ == nullptr || !valid_callsign(o.callsign)) return update;
  bool found = false;
  const size_t i = find_slot(o.callsign, &found);
  // Keep the table at most 3/4 full so probing stays short; a full table drops new stations but keeps updating known ones.
  if (!found && (i >= capacity_ || size_ * 4 >= capacity_ * 3)) {
    ++dropped_;
    return update;
  }
  HeardEntry& e = entries_[i];
  if (!found) {
    e = HeardEntry{};
    std::strncpy(e.callsign, o.callsign, kHeardCallLen - 1);
    e.first_utc = o.utc;
    ++size_;
    update.before = HeardClass::first_time;
  } else {
    update.before = (e.flags & heard_flag_worked) ? HeardClass::worked : HeardClass::heard_before;
  }
  const uint16_t band_bit = static_cast<uint16_t>(1u << (o.band & 15u));
  const uint8_t mode_bit = static_cast<uint8_t>(1u << (o.mode & 7u));
  update.new_band = (e.band_mask & band_bit) == 0;
  update.new_mode = (e.mode_mask & mode_bit) == 0;
  e.band_mask |= band_bit;
  e.mode_mask |= mode_bit;
  if (o.utc != 0 && (e.first_utc == 0 || o.utc < e.first_utc)) e.first_utc = o.utc;
  if (o.utc > e.last_utc) e.last_utc = o.utc;
  if (e.count < 0xFFFFFFFFu) ++e.count;
  if (o.grid != nullptr && o.grid[0]) std::strncpy(e.grid, o.grid, kHeardGridLen - 1);
  if (o.snr_known) {
    if (!(e.flags & heard_flag_snr_known) || o.snr > e.best_snr) e.best_snr = o.snr;
    e.last_snr = o.snr;
    e.flags |= heard_flag_snr_known;
  }
  e.flags |= heard_flag_dirty;
  update.stored = true;
  update.entry = &e;
  return update;
}

bool HeardDb::mark_worked(const char* callsign) {
  if (entries_ == nullptr || !valid_callsign(callsign)) return false;
  bool found = false;
  const size_t i = find_slot(callsign, &found);
  if (!found) return false;
  entries_[i].flags |= heard_flag_worked | heard_flag_dirty;
  return true;
}

size_t HeardDb::drain_dirty(uint8_t* out, size_t max_records) {
  if (entries_ == nullptr || out == nullptr) return 0;
  size_t written = 0;
  for (size_t scanned = 0; scanned < capacity_ && written < max_records; ++scanned) {
    HeardEntry& e = entries_[drain_cursor_];
    drain_cursor_ = (drain_cursor_ + 1) & (capacity_ - 1);
    if (!e.callsign[0] || !(e.flags & heard_flag_dirty)) continue;
    serialise(e, out + written * kHeardRecordBytes);
    e.flags = static_cast<uint8_t>(e.flags & ~heard_flag_dirty);
    ++written;
  }
  return written;
}

void HeardDb::journal_header(uint8_t out[kHeardHeaderBytes]) {
  std::memset(out, 0, kHeardHeaderBytes);
  std::memcpy(out, kMagic, 8);
  out[8] = 1;   // version
}

size_t HeardDb::load(const uint8_t* data, size_t length) {
  if (entries_ == nullptr || data == nullptr) return 0;
  size_t pos = 0;
  if (length >= kHeardHeaderBytes && std::memcmp(data, kMagic, 8) == 0) {
    if (data[8] != 1) return 0;   // unknown version: do not guess
    pos = kHeardHeaderBytes;
  }
  size_t applied = 0;
  for (; pos + kHeardRecordBytes <= length; pos += kHeardRecordBytes) {
    HeardEntry loaded;
    if (!deserialise(data + pos, &loaded)) {
      ++rejected_;
      continue;
    }
    bool found = false;
    const size_t i = find_slot(loaded.callsign, &found);
    if (!found && (i >= capacity_ || size_ * 4 >= capacity_ * 3)) {
      ++dropped_;
      continue;
    }
    if (!found) {
      ++size_;
      entries_[i] = loaded;   // first record for this callsign; not dirty (it came from the journal)
    } else {
      merge_entry(entries_[i], loaded);
    }
    ++applied;
  }
  return applied;
}

size_t HeardDb::requeue(const uint8_t* records, size_t record_count) {
  if (entries_ == nullptr || records == nullptr) return 0;
  size_t marked = 0;
  for (size_t r = 0; r < record_count; ++r) {
    char callsign[kHeardCallLen];
    std::memcpy(callsign, records + r * kHeardRecordBytes, kHeardCallLen);
    callsign[kHeardCallLen - 1] = '\0';
    if (!valid_callsign(callsign)) continue;
    bool found = false;
    const size_t i = find_slot(callsign, &found);
    if (!found) continue;
    entries_[i].flags = static_cast<uint8_t>(entries_[i].flags | heard_flag_dirty);
    ++marked;
  }
  return marked;
}

HeardDb::ReplayResult HeardDb::replay(ReadFn read, void* context, uint8_t* scratch, size_t scratch_bytes) {
  ReplayResult result;
  if (entries_ == nullptr || read == nullptr || scratch == nullptr || scratch_bytes < kHeardRecordBytes) return result;
  uint8_t header[kHeardHeaderBytes];
  size_t header_got = 0;
  while (header_got < sizeof(header)) {
    const size_t n = read(context, header + header_got, sizeof(header) - header_got);
    if (n == 0) return result;   // shorter than a header
    header_got += n;
  }
  uint8_t expected[kHeardHeaderBytes];
  journal_header(expected);
  if (std::memcmp(header, expected, sizeof(header)) != 0) return result;
  result.recognised = true;
  size_t held = 0;
  for (;;) {
    const size_t got = read(context, scratch + held, scratch_bytes - held);
    if (got == 0) break;
    held += got;
    const size_t whole = held - held % kHeardRecordBytes;
    result.applied += load(scratch, whole);
    held -= whole;
    if (held > 0) std::memmove(scratch, scratch + whole, held);
  }
  return result;
}

bool heard_db_self_check() { return sizeof(HeardEntry) <= 48 && kHeardRecordBytes == 40; }

}  // namespace orcsdr::ft8
