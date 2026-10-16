#include "acars_archive.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace orcsdr::acars_archive {

using acars_ui::ArchiveQuery;
using acars_ui::MessageRecord;
namespace ui = orcsdr::acars_ui;

namespace {

constexpr char kDir[] = "/orcsdr/acars";
constexpr char kHex[] = "0123456789abcdef";

// ---- writing ------------------------------------------------------------------------------------------------------------
struct Writer {
  char* out;
  size_t capacity;
  size_t used = 0;
  bool overflow = false;
  void put(char c) {
    if (used + 1 < capacity) out[used++] = c;
    else overflow = true;
  }
  void raw(const char* s) { while (*s) put(*s++); }
  void number(uint32_t v) {
    char t[12];
    std::snprintf(t, sizeof(t), "%u", static_cast<unsigned>(v));
    raw(t);
  }
  void quoted(const char* s, size_t n) {
    put('"');
    for (size_t i = 0; i < n; ++i) {
      const unsigned char c = static_cast<unsigned char>(s[i]);
      if (c == '"' || c == '\\') { put('\\'); put(static_cast<char>(c)); }
      else if (c < 0x20 || c >= 0x7f) { raw("\\u00"); put(kHex[c >> 4]); put(kHex[c & 15]); }
      else put(static_cast<char>(c));
    }
    put('"');
  }
};

// ---- reading ------------------------------------------------------------------------------------------------------------
struct Reader {
  const char* p;
  const char* end;
  bool ok = true;
  bool lit(const char* s) {
    const size_t n = std::strlen(s);
    if (!ok || static_cast<size_t>(end - p) < n || std::memcmp(p, s, n) != 0) { ok = false; return false; }
    p += n;
    return true;
  }
  bool key(const char* name) {            // ,"name":
    char t[16];
    std::snprintf(t, sizeof(t), "\"%s\":", name);
    return lit(t);
  }
  bool number(uint32_t* v) {
    if (!ok || p >= end || *p < '0' || *p > '9') { ok = false; return false; }
    uint64_t acc = 0;
    while (p < end && *p >= '0' && *p <= '9') {
      acc = acc * 10 + static_cast<uint64_t>(*p - '0');
      if (acc > 0xffffffffull) { ok = false; return false; }
      ++p;
    }
    *v = static_cast<uint32_t>(acc);
    return true;
  }
  // Reads a JSON string into `out` (at most capacity-1 bytes, NUL terminated). Returns the decoded length, or -1.
  int string(char* out, size_t capacity) {
    if (!ok || p >= end || *p != '"') { ok = false; return -1; }
    ++p;
    size_t n = 0;
    while (p < end && *p != '"') {
      unsigned char c = static_cast<unsigned char>(*p++);
      if (c == '\\') {
        if (p >= end) { ok = false; return -1; }
        const char e = *p++;
        if (e == '"' || e == '\\') c = static_cast<unsigned char>(e);
        else if (e == 'u') {
          if (end - p < 4 || p[0] != '0' || p[1] != '0') { ok = false; return -1; }
          auto hv = [](char h) { return h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1; };
          const int hi = hv(p[2]), lo = hv(p[3]);
          if (hi < 0 || lo < 0) { ok = false; return -1; }
          c = static_cast<unsigned char>(hi * 16 + lo);
          p += 4;
        } else { ok = false; return -1; }
      }
      if (n + 1 >= capacity) { ok = false; return -1; }
      out[n++] = static_cast<char>(c);
    }
    if (p >= end) { ok = false; return -1; }   // unterminated: a truncated line
    ++p;
    out[n] = '\0';
    return static_cast<int>(n);
  }
};

int hex_value(char c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

void copy_name(char* dst, size_t capacity, const char* src) {     // bounded copy of a file name, always NUL terminated
  const size_t n = strnlen(src, capacity - 1);
  std::memcpy(dst, src, n);
  dst[n] = '\0';
}

void join(char* out, size_t cap, const char* a, const char* b) {     // a + '/' + b, bounded, always NUL terminated
  size_t used = 0;
  for (const char* p = a; *p && used + 1 < cap; ++p) out[used++] = *p;
  if (used + 1 < cap) out[used++] = '/';
  for (const char* p = b; *p && used + 1 < cap; ++p) out[used++] = *p;
  out[used] = 0;
}

bool is_dated_name(const char* name) {
  if (std::strlen(name) != 14 || std::strcmp(name + 8, ".jsonl") != 0) return false;
  for (int i = 0; i < 8; ++i) if (name[i] < '0' || name[i] > '9') return false;
  return true;
}

}  // namespace

// ---- line format --------------------------------------------------------------------------------------------------------
size_t format_line(const MessageRecord& m, char* out, size_t capacity) {
  Writer w{out, capacity};
  w.raw("{\"v\":1,\"utc\":");
  w.number(m.time_valid ? m.utc_epoch : 0u);
  w.raw(",\"up\":"); w.number(m.uptime_ms);
  w.raw(",\"f\":"); w.number(m.frequency_hz);
  w.raw(",\"tail\":"); w.quoted(m.tail, strnlen(m.tail, sizeof(m.tail)));
  w.raw(",\"flt\":"); w.quoted(m.flight, strnlen(m.flight, sizeof(m.flight)));
  w.raw(",\"lbl\":"); w.quoted(m.label, strnlen(m.label, sizeof(m.label)));
  w.raw(",\"mode\":"); w.quoted(&m.mode, 1);
  w.raw(",\"ack\":"); w.quoted(&m.acknowledgement, 1);
  w.raw(",\"blk\":"); w.quoted(&m.block_id, 1);
  w.raw(",\"end\":"); w.number(m.end_marker);
  w.raw(",\"cor\":"); w.number(m.corrected_bits);
  w.raw(",\"fix\":"); w.number(m.integrity == ui::Integrity::crc_corrected ? 1u : 0u);
  w.raw(",\"raw\":"); w.number(m.raw_bytes);
  const size_t n = std::min<size_t>(m.text_length, sizeof(m.text));
  w.raw(",\"n\":"); w.number(static_cast<uint32_t>(n));
  w.raw(",\"x\":\"");
  for (size_t i = 0; i < n; ++i) {
    const unsigned char c = static_cast<unsigned char>(m.text[i]);
    w.put(kHex[c >> 4]);
    w.put(kHex[c & 15]);
  }
  w.raw("\",\"p\":"); w.quoted(m.preview, strnlen(m.preview, sizeof(m.preview)));
  w.raw("}\n");
  if (w.overflow) return 0;
  out[w.used] = '\0';
  return w.used;
}

bool parse_line(const char* line, size_t length, MessageRecord* out) {
  if (!line || !out) return false;
  while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) --length;
  if (length < 20 || length > kLineMax) return false;
  Reader r{line, line + length};
  MessageRecord m{};
  uint32_t v = 0, utc = 0, up = 0, f = 0, end_marker = 0, cor = 0, fix = 0, raw = 0, n = 0;
  char tmp[8], hex[2 * sizeof(m.text) + 2], preview[ui::kPreviewChars + 8];
  r.lit("{\"v\":"); r.number(&v);
  if (!r.ok || v != 1) return false;
  r.lit(",\"utc\":"); r.number(&utc);
  r.lit(",\"up\":"); r.number(&up);
  r.lit(",\"f\":"); r.number(&f);
  r.lit(",\"tail\":"); if (r.string(m.tail, sizeof(m.tail)) < 0) return false;
  r.lit(",\"flt\":"); if (r.string(m.flight, sizeof(m.flight)) < 0) return false;
  r.lit(",\"lbl\":"); if (r.string(m.label, sizeof(m.label)) < 0) return false;
  r.lit(",\"mode\":"); if (r.string(tmp, sizeof(tmp)) != 1) return false; m.mode = tmp[0];
  r.lit(",\"ack\":"); if (r.string(tmp, sizeof(tmp)) != 1) return false; m.acknowledgement = tmp[0];
  r.lit(",\"blk\":"); if (r.string(tmp, sizeof(tmp)) != 1) return false; m.block_id = tmp[0];
  r.lit(",\"end\":"); r.number(&end_marker);
  r.lit(",\"cor\":"); r.number(&cor);
  r.lit(",\"fix\":"); r.number(&fix);
  r.lit(",\"raw\":"); r.number(&raw);
  r.lit(",\"n\":"); r.number(&n);
  r.lit(",\"x\":");
  const int hex_len = r.string(hex, sizeof(hex));
  if (!r.ok || hex_len < 0) return false;
  r.lit(",\"p\":"); if (r.string(preview, sizeof(preview)) < 0) return false;
  r.lit("}");
  if (!r.ok || r.p != r.end) return false;
  if (n > sizeof(m.text) || static_cast<size_t>(hex_len) != 2 * static_cast<size_t>(n) || end_marker > 255 || cor > 255 || raw > 65535)
    return false;
  for (uint32_t i = 0; i < n; ++i) {
    const int hi = hex_value(hex[2 * i]), lo = hex_value(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    m.text[i] = static_cast<char>(hi * 16 + lo);
  }
  m.text_length = static_cast<uint16_t>(n);
  m.has_text = n > 0;
  m.time_valid = utc != 0;
  m.utc_epoch = utc;
  m.uptime_ms = up;
  m.frequency_hz = f;
  m.end_marker = static_cast<uint8_t>(end_marker);
  m.corrected_bits = static_cast<uint8_t>(cor);
  m.integrity = fix ? ui::Integrity::crc_corrected : ui::Integrity::crc_verified;
  m.raw_bytes = static_cast<uint16_t>(raw);
  m.source = ui::SourceKind::live;
  m.channel_index = -1;
  ui::sanitize_preview(m.preview, sizeof(m.preview), m.text, m.text_length);
  *out = m;
  return true;
}

void day_key(const MessageRecord& m, char* out, size_t capacity) {
  if (!m.time_valid || m.utc_epoch == 0) { std::snprintf(out, capacity, "undated"); return; }
  const time_t t = static_cast<time_t>(m.utc_epoch);
  struct tm parts{};
#if defined(_WIN32)
  gmtime_s(&parts, &t);
#else
  gmtime_r(&t, &parts);
#endif
  std::snprintf(out, capacity, "%04d%02d%02d", parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday);
}

// ---- store --------------------------------------------------------------------------------------------------------------
Store::Store() { clear(); }

void Store::clear() {
  head_ = 0;
  held_ = 0;
  next_seq_ = kSeqBase;
}

const MessageRecord& Store::add(const MessageRecord& record) {
  MessageRecord& slot = records_[head_];
  slot = record;
  slot.seq = next_seq_++;
  head_ = (head_ + 1) % kStoreCapacity;
  if (held_ < kStoreCapacity) ++held_;
  return slot;
}

void Store::set_status(const char* text) { std::snprintf(status_, sizeof(status_), "%s", text ? text : ""); }

const MessageRecord& Store::at_newest(size_t k) const {
  return records_[(head_ + kStoreCapacity - 1 - k) % kStoreCapacity];
}

size_t Store::query(const ArchiveQuery& q, size_t offset, const MessageRecord** out, size_t capacity, size_t* total) const {
  if (total) *total = 0;
  if (!out) return 0;
  size_t matched = 0, written = 0;
  for (size_t k = 0; k < held_; ++k) {
    const MessageRecord& m = at_newest(k);
    if (q.tail[0] && std::strcmp(q.tail, m.tail) != 0) continue;
    if (q.label[0] && std::strcmp(q.label, m.label) != 0) continue;
    if (matched++ < offset) continue;
    if (written < capacity) out[written++] = &m;
  }
  if (total) *total = matched;
  return written;
}

bool Store::cycle_tail(char* tail) const {
  if (!tail) return false;
  constexpr size_t kMax = 48;
  char seen[kMax][8];
  size_t n = 0;
  for (size_t k = held_; k-- > 0 && n < kMax;) {            // oldest first, order of first appearance
    const MessageRecord& m = at_newest(k);
    if (!m.tail[0]) continue;
    bool dup = false;
    for (size_t i = 0; i < n && !dup; ++i) dup = std::strcmp(seen[i], m.tail) == 0;
    if (!dup) std::snprintf(seen[n++], 8, "%s", m.tail);
  }
  if (n == 0) { tail[0] = '\0'; return false; }
  if (!tail[0]) { std::snprintf(tail, 8, "%s", seen[0]); return true; }
  for (size_t i = 0; i < n; ++i)
    if (std::strcmp(seen[i], tail) == 0) {
      if (i + 1 < n) std::snprintf(tail, 8, "%s", seen[i + 1]);
      else tail[0] = '\0';                                   // after the last: back to ANY
      return true;
    }
  std::snprintf(tail, 8, "%s", seen[0]);
  return true;
}

bool Store::cycle_label(char* label) const {
  if (!label) return false;
  constexpr size_t kMax = 32;
  char seen[kMax][3];
  size_t n = 0;
  for (size_t k = held_; k-- > 0 && n < kMax;) {
    const MessageRecord& m = at_newest(k);
    bool dup = false;
    for (size_t i = 0; i < n && !dup; ++i) dup = std::strcmp(seen[i], m.label) == 0;
    if (!dup) std::snprintf(seen[n++], 3, "%s", m.label);
  }
  if (n == 0) { label[0] = '\0'; return false; }
  if (!label[0]) { std::snprintf(label, 3, "%s", seen[0]); return true; }
  for (size_t i = 0; i < n; ++i)
    if (std::strcmp(seen[i], label) == 0) {
      if (i + 1 < n) std::snprintf(label, 3, "%s", seen[i + 1]);
      else label[0] = '\0';
      return true;
    }
  std::snprintf(label, 3, "%s", seen[0]);
  return true;
}

const MessageRecord* Store::find(uint32_t seq) const {
  if (held_ == 0 || seq < kSeqBase || seq >= next_seq_) return nullptr;
  const uint32_t back = next_seq_ - 1 - seq;               // 0 = newest
  if (back >= held_) return nullptr;                       // overwritten
  return &at_newest(back);
}

uint32_t Store::neighbor(uint32_t seq, bool newer) const {
  if (held_ == 0) return 0;
  const uint32_t oldest = next_seq_ - static_cast<uint32_t>(held_);
  if (newer) return seq + 1 < next_seq_ && seq + 1 >= oldest ? seq + 1 : 0;
  return seq > oldest && seq - 1 < next_seq_ ? seq - 1 : 0;
}

// ---- logger -------------------------------------------------------------------------------------------------------------
const char* log_state_name(LogState s) {
  switch (s) {
    case LogState::ready: return "SAVING TO SD";
    case LogState::no_card: return "NO SD CARD";
    case LogState::write_error: return "SD WRITE ERROR";
    default: return "LOG OFF";
  }
}

Logger::Logger(Store* store, const char* dir) : store_(store) { copy_name(dir_, sizeof(dir_), dir); }

void Logger::begin(const FileOps& ops, uint32_t now_ms) {
  ops_ = ops;
  used_ = 0;
  pending_lines_ = 0;
  buffer_day_[0] = '\0';
  stats_ = LogStats{};
  oldest_pending_ms_ = now_ms;
  if (!ops_.ready || !ops_.open || !ops_.write || !ops_.close || !ops_.list || !ops_.make_dir) { state_ = LogState::off; }
  else if (!ops_.ready(true)) { state_ = LogState::no_card; }
  else if (!ops_.make_dir("/orcsdr") || !ops_.make_dir(dir_)) { state_ = LogState::write_error; }
  else { state_ = LogState::ready; apply_retention(); }
  if (store_) store_->set_status(log_state_name(state_));
}

void Logger::end(uint32_t now_ms) {
  if (state_ == LogState::ready || state_ == LogState::write_error) flush(now_ms);
}

void Logger::apply_retention() {
  const size_t n = ops_.list(dir_, entries_, 64);
  size_t dated = 0;
  for (size_t i = 0; i < n; ++i)
    if (is_dated_name(entries_[i].name)) copy_name(names_[dated++].text, sizeof(NameBuf::text), entries_[i].name);
  std::sort(names_, names_ + dated, [](const NameBuf& a, const NameBuf& b) { return std::strcmp(a.text, b.text) < 0; });
  for (size_t i = 0; dated - i > kMaxFiles; ++i) {
    char path[64];
    join(path, sizeof(path), dir_, names_[i].text);
    if (ops_.remove && ops_.remove(path)) ++stats_.files_removed;
  }
}

void Logger::log(const MessageRecord& record, uint32_t now_ms) {
  if (store_) store_->add(record);
  if (state_ != LogState::ready && state_ != LogState::write_error) return;   // after an error keep trying: the card may return
  char* line = line_;
  const size_t n = format_line(record, line, kLineMax + 1);
  if (n == 0) return;
  char day[12];
  day_key(record, day, sizeof(day));
  if (used_ > 0 && std::strcmp(day, buffer_day_) != 0) flush(now_ms);     // lines of one buffer share one file
  if (used_ + n > kBufferBytes && !flush(now_ms)) { ++stats_.dropped; return; }
  if (used_ == 0) { std::snprintf(buffer_day_, sizeof(buffer_day_), "%s", day); oldest_pending_ms_ = now_ms; }
  std::memcpy(buffer_ + used_, line, n);
  used_ += n;
  ++pending_lines_;
}

void Logger::service(uint32_t now_ms) {
  if ((state_ != LogState::ready && state_ != LogState::write_error) || used_ == 0) return;
  if (used_ >= kBufferBytes / 2 || now_ms - oldest_pending_ms_ >= kFlushAgeMs) flush(now_ms);
}

bool Logger::flush(uint32_t now_ms) {
  (void)now_ms;
  if (used_ == 0) return true;
  char path[64];
  char name[24];
  std::snprintf(name, sizeof(name), "%s.jsonl", buffer_day_);
  join(path, sizeof(path), dir_, name);
  if (std::strcmp(buffer_day_, "undated") == 0 && ops_.file_size && ops_.remove) {
    // Blocks without a valid clock all go to one file that retention does not cover: cap it, so an invalid clock cannot fill
    // the card. Past the cap the file starts again (the oldest undated blocks are dropped, like the dated files' rule).
    void* probe = ops_.open(path, false);
    if (probe) {
      const uint32_t size = ops_.file_size(probe);
      ops_.close(probe);
      if (size > kUndatedMaxBytes) { ops_.remove(path); ++stats_.files_removed; }
    }
  }
  void* h = ops_.open(path, true);
  bool ok = h != nullptr;
  if (ok) {
    ok = ops_.write(h, buffer_, used_) == used_;
    ops_.close(h);
  }
  if (ok) {
    stats_.written += pending_lines_;
    if (state_ == LogState::write_error) state_ = LogState::ready;
  } else {
    stats_.dropped += pending_lines_;
    state_ = LogState::write_error;
  }
  used_ = 0;
  pending_lines_ = 0;
  if (store_) store_->set_status(log_state_name(state_));
  return ok;
}

void Logger::load_history() {
  if (!store_ || state_ != LogState::ready || !ops_.read || !ops_.seek || !ops_.file_size) return;
  Pick* picks = picks_;
  const size_t n = ops_.list(dir_, entries_, 64);
  size_t count = 0;
  for (size_t i = 0; i < n && count < 40; ++i) {
    if (!is_dated_name(entries_[i].name) && std::strcmp(entries_[i].name, "undated.jsonl") != 0) continue;
    copy_name(picks[count].name, sizeof(picks[count].name), entries_[i].name);
    picks[count].size = entries_[i].size;
    picks[count].take = 0;
    ++count;
  }
  // Undated sessions are the oldest we can place; dated files sort by name.
  std::sort(picks, picks + count, [](const Pick& a, const Pick& b) {
    const bool au = std::strcmp(a.name, "undated.jsonl") == 0, bu = std::strcmp(b.name, "undated.jsonl") == 0;
    if (au != bu) return au;
    return std::strcmp(a.name, b.name) < 0;
  });
  size_t remaining = kHistoryReadBytes;
  size_t first = count;
  for (size_t i = count; i-- > 0 && remaining > 0;) {
    picks[i].take = static_cast<uint32_t>(std::min<size_t>(picks[i].size, remaining));
    remaining -= picks[i].take;
    first = i;
  }
  for (size_t i = first; i < count; ++i) {
    if (picks[i].take == 0) continue;
    char path[64];
    join(path, sizeof(path), dir_, picks[i].name);
    void* h = ops_.open(path, false);
    if (!h) continue;
    const uint32_t size = ops_.file_size(h);
    const uint32_t start = size > picks[i].take ? size - picks[i].take : 0;
    if (start > 0) ops_.seek(h, start);
    char* line = line_;
    size_t len = 0;
    bool skipping = start > 0;           // a read that starts mid-file begins inside a line: drop up to the first newline
    bool overlong = false;
    char* chunk = chunk_;
    size_t got;
    while ((got = ops_.read(h, chunk, sizeof(chunk_))) > 0) {
      for (size_t k = 0; k < got; ++k) {
        const char c = chunk[k];
        if (c == '\n') {
          if (skipping) { skipping = false; }
          else if (!overlong && len > 0) {
            MessageRecord m;
            if (parse_line(line, len, &m)) { store_->add(m); ++stats_.loaded; }
            else ++stats_.skipped_lines;
          }
          len = 0;
          overlong = false;
        } else if (!skipping) {
          if (len < kLineMax) line[len++] = c;
          else overlong = true;
        }
      }
    }
    ops_.close(h);
  }
}

LogStats Logger::stats() const {
  LogStats s = stats_;
  s.state = state_;
  s.pending = pending_lines_;
  return s;
}

}  // namespace orcsdr::acars_archive
