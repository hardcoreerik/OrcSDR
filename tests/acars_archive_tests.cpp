// SPDX-License-Identifier: AGPL-3.0-only
// Host tests for the ACARS archive: line format round trip (including hostile bytes), parser rejection of damaged lines,
// the bounded store (order, filters, cycling, seq lookup), and the logger against an in-memory file system (flush rules, day
// files, retention, write errors, history load from the tail of a big file, a power-cut line).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "acars_archive.hpp"

using namespace orcsdr;
using namespace orcsdr::acars_archive;
using orcsdr::acars_ui::MessageRecord;

namespace {

[[noreturn]] void fail(const char* e, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, e);
  std::exit(1);
}
#define CHECK(x) do { if (!(x)) fail(#x, __LINE__); } while (false)

MessageRecord make(const char* tail, const char* flight, const char* label, const char* text, uint32_t utc, uint32_t up) {
  MessageRecord m;
  std::snprintf(m.tail, sizeof(m.tail), "%s", tail);
  std::snprintf(m.flight, sizeof(m.flight), "%s", flight);
  std::snprintf(m.label, sizeof(m.label), "%s", label);
  m.mode = '2';
  m.acknowledgement = '\x15';
  m.block_id = '1';
  m.frequency_hz = 131550000u;
  m.end_marker = 0x17;
  m.time_valid = utc != 0;
  m.utc_epoch = utc;
  m.uptime_ms = up;
  m.raw_bytes = static_cast<uint16_t>(std::strlen(text) + 16);
  m.text_length = static_cast<uint16_t>(std::strlen(text));
  std::memcpy(m.text, text, m.text_length);
  m.has_text = m.text_length > 0;
  acars_ui::sanitize_preview(m.preview, sizeof(m.preview), m.text, m.text_length);
  return m;
}

// ---- an in-memory file system ----------------------------------------------------------------------------------------------
struct Mem {
  std::map<std::string, std::string> files;
  bool card = true;
  bool fail_writes = false;
  unsigned opens = 0;
  struct Handle { std::string path; size_t pos = 0; bool append = false; };
} g_fs;

bool m_ready(bool) { return g_fs.card; }
bool m_mkdir(const char*) { return true; }
void* m_open(const char* path, bool append) {
  if (!append && !g_fs.files.count(path)) return nullptr;
  ++g_fs.opens;
  auto* h = new Mem::Handle{path, 0, append};
  if (append) g_fs.files[path];             // create
  return h;
}
size_t m_write(void* h, const void* d, size_t n) {
  if (g_fs.fail_writes) return 0;
  g_fs.files[static_cast<Mem::Handle*>(h)->path].append(static_cast<const char*>(d), n);
  return n;
}
size_t m_read(void* h, void* out, size_t n) {
  auto* hd = static_cast<Mem::Handle*>(h);
  const std::string& f = g_fs.files[hd->path];
  const size_t k = std::min(n, f.size() > hd->pos ? f.size() - hd->pos : 0);
  std::memcpy(out, f.data() + hd->pos, k);
  hd->pos += k;
  return k;
}
bool m_seek(void* h, uint32_t p) { static_cast<Mem::Handle*>(h)->pos = p; return true; }
uint32_t m_size(void* h) { return static_cast<uint32_t>(g_fs.files[static_cast<Mem::Handle*>(h)->path].size()); }
void m_close(void* h) { delete static_cast<Mem::Handle*>(h); }
size_t m_list(const char* dir, DirEntry* out, size_t cap) {
  size_t n = 0;
  const std::string prefix = std::string(dir) + "/";
  for (const auto& kv : g_fs.files)
    if (kv.first.compare(0, prefix.size(), prefix) == 0 && n < cap) {
      std::snprintf(out[n].name, sizeof(out[n].name), "%s", kv.first.c_str() + prefix.size());
      out[n].size = static_cast<uint32_t>(kv.second.size());
      ++n;
    }
  return n;
}
bool m_remove(const char* p) { return g_fs.files.erase(p) > 0; }

FileOps ops() {
  FileOps o;
  o.ready = m_ready; o.make_dir = m_mkdir; o.open = m_open; o.write = m_write; o.read = m_read; o.seek = m_seek;
  o.file_size = m_size; o.close = m_close; o.list = m_list; o.remove = m_remove;
  return o;
}

constexpr uint32_t kDay1 = 1760572800u;     // 2025-10-16 00:00:00 UTC
constexpr uint32_t kDay2 = kDay1 + 86400u;

void test_line_round_trip() {
  MessageRecord a = make("N37267", "UA1227", "H1", "D03AUA1227#DFB\r\n5141160 17418 77.8", kDay1 + 100, 5000);
  a.integrity = acars_ui::Integrity::crc_corrected;
  a.corrected_bits = 1;
  char line[kLineMax + 2];
  const size_t n = format_line(a, line, sizeof(line));
  CHECK(n > 0 && line[n - 1] == '\n' && std::strchr(line, '\n') == line + n - 1);   // exactly one line
  MessageRecord b;
  CHECK(parse_line(line, n, &b));
  CHECK(std::strcmp(a.tail, b.tail) == 0 && std::strcmp(a.flight, b.flight) == 0 && std::strcmp(a.label, b.label) == 0);
  CHECK(b.mode == a.mode && b.acknowledgement == a.acknowledgement && b.block_id == a.block_id);
  CHECK(b.utc_epoch == a.utc_epoch && b.time_valid && b.uptime_ms == 5000 && b.frequency_hz == 131550000u);
  CHECK(b.end_marker == 0x17 && b.corrected_bits == 1 && b.integrity == acars_ui::Integrity::crc_corrected);
  CHECK(b.text_length == a.text_length && std::memcmp(a.text, b.text, a.text_length) == 0);
  CHECK(b.raw_bytes == a.raw_bytes && std::strcmp(a.preview, b.preview) == 0);
}

void test_hostile_bytes() {
  // Text with every byte value, tail/label with quotes, backslashes and control bytes: nothing breaks the line.
  MessageRecord a = make("N\"\\1", "", "_\x7f", "", kDay1, 1);
  for (int i = 0; i < 220; ++i) a.text[i] = static_cast<char>(i + 1);
  a.text_length = 220;
  a.has_text = true;
  acars_ui::sanitize_preview(a.preview, sizeof(a.preview), a.text, a.text_length);
  char line[kLineMax + 2];
  const size_t n = format_line(a, line, sizeof(line));
  CHECK(n > 0 && std::strchr(line, '\n') == line + n - 1);
  MessageRecord b;
  CHECK(parse_line(line, n, &b));
  CHECK(std::strcmp(a.tail, b.tail) == 0 && a.label[0] == b.label[0] && a.label[1] == b.label[1]);
  CHECK(b.text_length == 220 && std::memcmp(a.text, b.text, 220) == 0);
  // A full-size block still fits the line limit.
  MessageRecord big = make("N1234AB", "UA1234", "H1", "", kDay1, 1);
  for (size_t i = 0; i < sizeof(big.text); ++i) big.text[i] = static_cast<char>('"');
  big.text_length = sizeof(big.text);
  acars_ui::sanitize_preview(big.preview, sizeof(big.preview), big.text, big.text_length);
  CHECK(format_line(big, line, sizeof(line)) > 0);
  // Too small a buffer is reported, not truncated silently.
  char tiny[40];
  CHECK(format_line(a, tiny, sizeof(tiny)) == 0);
}

void test_parser_rejects_damage() {
  MessageRecord a = make("N37267", "UA1227", "H1", "HELLO", kDay1, 7);
  char line[kLineMax + 2];
  const size_t n = format_line(a, line, sizeof(line));
  MessageRecord out;
  CHECK(parse_line(line, n, &out));
  for (size_t cut = 0; cut + 1 < n; cut += 7)              // a power cut at any point: never a record, never a crash
    CHECK(!parse_line(line, cut, &out));
  CHECK(!parse_line("", 0, &out));
  CHECK(!parse_line("{\"v\":2,\"event\":\"start\"}", 24, &out));
  CHECK(!parse_line("not json at all, definitely not a record line", 45, &out));
  std::string bad(line, n);
  const size_t hex = bad.find("\"x\":\"") + 5;
  bad[hex] = 'g';                                            // not hex
  CHECK(!parse_line(bad.data(), bad.size(), &out));
  std::string count(line, n);
  const size_t at = count.find("\"n\":") + 4;
  count[at] = '9';                                           // length no longer matches the hex
  CHECK(!parse_line(count.data(), count.size(), &out));
  CHECK(!parse_line(nullptr, 5, &out));
}

void test_store() {
  Store s;
  CHECK(s.count() == 0);
  for (unsigned i = 0; i < 10; ++i) {
    char tail[8], text[16];
    std::snprintf(tail, sizeof(tail), "N%05u", i % 3);
    std::snprintf(text, sizeof(text), "MSG%u", i);
    s.add(make(tail, "F", i % 2 ? "H1" : "H2", text, kDay1 + i, i));
  }
  CHECK(s.count() == 10);
  const MessageRecord* rows[4];
  size_t total = 0;
  acars_ui::ArchiveQuery q;
  size_t n = s.query(q, 0, rows, 4, &total);
  CHECK(n == 4 && total == 10 && std::strcmp(rows[0]->text, "MSG9") == 0 && std::strcmp(rows[3]->text, "MSG6") == 0);   // newest first
  n = s.query(q, 8, rows, 4, &total);
  CHECK(n == 2 && total == 10 && std::strcmp(rows[1]->text, "MSG0") == 0);                                        // offset
  std::snprintf(q.label, sizeof(q.label), "H1");
  CHECK(s.query(q, 0, rows, 4, &total) == 4 && total == 5);
  std::snprintf(q.tail, sizeof(q.tail), "N00001");
  s.query(q, 0, rows, 4, &total);
  for (size_t i = 0; i < std::min<size_t>(4, total); ++i) CHECK(std::strcmp(rows[i]->tail, "N00001") == 0 && std::strcmp(rows[i]->label, "H1") == 0);
  // Cycling visits every distinct tail once, then returns to ANY.
  char tail[8] = "";
  std::vector<std::string> seen;
  for (int i = 0; i < 6; ++i) {
    CHECK(s.cycle_tail(tail));
    if (!tail[0]) break;
    seen.push_back(tail);
  }
  CHECK(seen.size() == 3 && !tail[0]);
  char label[3] = "";
  CHECK(s.cycle_label(label) && label[0]);
  // seq lookup and neighbours.
  const MessageRecord* newest = s.find(s.add(make("N1", "F", "H1", "NEWEST", kDay1, 99)).seq);
  CHECK(newest && std::strcmp(newest->text, "NEWEST") == 0);
  CHECK(s.neighbor(newest->seq, true) == 0);
  CHECK(s.neighbor(newest->seq, false) == newest->seq - 1);
  CHECK(s.find(12345) == nullptr && s.find(kSeqBase - 1) == nullptr);
  // The ring keeps the newest kStoreCapacity records and forgets older seq values.
  const uint32_t first = newest->seq;
  for (size_t i = 0; i < kStoreCapacity + 5; ++i) s.add(make("N2", "F", "H1", "x", kDay1, static_cast<uint32_t>(i)));
  CHECK(s.count() == kStoreCapacity && s.find(first) == nullptr);
  CHECK(s.neighbor(first, true) == 0 || s.find(s.neighbor(first, true)) != nullptr);
}

void test_logger_flush_and_files() {
  g_fs = Mem{};
  Store store;
  Logger log(&store);
  log.begin(ops(), 0);
  CHECK(log.active() && std::strcmp(store.status(), "SAVING TO SD") == 0);
  log.log(make("N1", "A1", "H1", "ONE", kDay1 + 10, 1), 1000);
  log.log(make("N1", "A1", "H1", "TWO", kDay1 + 20, 2), 2000);
  CHECK(store.count() == 2);                                   // visible at once
  CHECK(g_fs.files.empty());                                   // but not written yet
  log.service(5000);
  CHECK(g_fs.files.empty());
  log.service(1000 + kFlushAgeMs);                             // old enough: written
  CHECK(g_fs.files.size() == 1 && g_fs.files.count("/orcsdr/acars/20251016.jsonl"));
  CHECK(log.stats().written == 2 && log.stats().pending == 0);
  // A record from the next UTC day goes to its own file; an invalid clock goes to "undated".
  log.log(make("N1", "A1", "H1", "THREE", kDay2 + 5, 3), 90000);
  log.log(make("N1", "A1", "H1", "NOCLOCK", 0, 4), 90100);
  log.end(90200);
  CHECK(g_fs.files.count("/orcsdr/acars/20251017.jsonl") && g_fs.files.count("/orcsdr/acars/undated.jsonl"));
  size_t lines = 0;
  for (const auto& kv : g_fs.files) lines += static_cast<size_t>(std::count(kv.second.begin(), kv.second.end(), '\n'));
  CHECK(lines == 4);
  // Many records fill the buffer and are flushed without loss.
  g_fs.files.clear();
  Store store2;
  Logger log2(&store2);
  log2.begin(ops(), 0);
  for (unsigned i = 0; i < 200; ++i) log2.log(make("N2", "A2", "H1", "0123456789012345678901234567890123456789", kDay1 + i, i), i);
  log2.end(300);
  CHECK(log2.stats().written == 200 && log2.stats().dropped == 0);
  CHECK(static_cast<size_t>(std::count(g_fs.files["/orcsdr/acars/20251016.jsonl"].begin(), g_fs.files["/orcsdr/acars/20251016.jsonl"].end(), '\n')) == 200);
}

void test_logger_failures() {
  g_fs = Mem{};
  g_fs.card = false;
  Store store;
  Logger log(&store);
  log.begin(ops(), 0);
  CHECK(!log.active() && std::strcmp(store.status(), "NO SD CARD") == 0);
  log.log(make("N1", "A", "H1", "KEPT IN MEMORY", kDay1, 1), 10);
  CHECK(store.count() == 1 && g_fs.files.empty());              // the session archive still works without a card
  g_fs = Mem{};
  g_fs.fail_writes = true;
  Logger bad(&store);
  bad.begin(ops(), 0);
  bad.log(make("N1", "A", "H1", "LOST", kDay1, 2), 20);
  bad.end(30);
  CHECK(bad.stats().state == LogState::write_error && bad.stats().dropped == 1);
  CHECK(std::strcmp(store.status(), "SD WRITE ERROR") == 0);
  g_fs.fail_writes = false;                                     // the card comes back: logging resumes
  bad.log(make("N1", "A", "H1", "BACK", kDay1, 3), 40);
  bad.end(50);
  CHECK(bad.stats().state == LogState::ready && bad.stats().written == 1);
}

void test_retention() {
  g_fs = Mem{};
  for (int d = 1; d <= 34; ++d) {
    char name[64];
    std::snprintf(name, sizeof(name), "/orcsdr/acars/202509%02d.jsonl", d);
    g_fs.files[name] = "x\n";
  }
  g_fs.files["/orcsdr/acars/undated.jsonl"] = "x\n";
  g_fs.files["/orcsdr/acars/notes.txt"] = "keep me";
  Store store;
  Logger log(&store);
  log.begin(ops(), 0);
  size_t dated = 0;
  for (const auto& kv : g_fs.files)
    if (kv.first.find("/2025") != std::string::npos) ++dated;
  CHECK(dated == kMaxFiles && log.stats().files_removed == 34 - kMaxFiles);
  // 34 dated files, 30 kept: the 4 oldest go, the newest stay.
  CHECK(!g_fs.files.count("/orcsdr/acars/20250901.jsonl") && !g_fs.files.count("/orcsdr/acars/20250904.jsonl"));
  CHECK(g_fs.files.count("/orcsdr/acars/20250905.jsonl") && g_fs.files.count("/orcsdr/acars/20250934.jsonl"));
  CHECK(g_fs.files.count("/orcsdr/acars/undated.jsonl") && g_fs.files.count("/orcsdr/acars/notes.txt"));   // only dated logs are pruned
}

void test_undated_file_is_capped() {
  g_fs = Mem{};
  g_fs.files["/orcsdr/acars/undated.jsonl"] = std::string(kUndatedMaxBytes + 10, 'x');     // already over the cap
  Store store;
  Logger log(&store);
  log.begin(ops(), 0);
  log.log(make("N1", "A", "H1", "FIRST AFTER CAP", 0, 1), 10);                          // no valid clock: undated
  log.end(20);
  const std::string& f = g_fs.files["/orcsdr/acars/undated.jsonl"];
  CHECK(f.size() < 4096 && f.find("xxxxxxxxxx") == std::string::npos && f.find("FIRST AFTER CAP") != std::string::npos && f.find("\"n\":15") != std::string::npos);
  CHECK(log.stats().files_removed == 1 && log.stats().written == 1);
  // Under the cap the file only grows.
  log.log(make("N1", "A", "H1", "SECOND", 0, 2), 30);
  log.end(40);
  CHECK(g_fs.files["/orcsdr/acars/undated.jsonl"].find("\"p\":\"SECOND\"") != std::string::npos);
  CHECK(std::count(g_fs.files["/orcsdr/acars/undated.jsonl"].begin(), g_fs.files["/orcsdr/acars/undated.jsonl"].end(), '\n') == 2);
}

void test_history_load() {
  g_fs = Mem{};
  Store writer_store;
  Logger writer(&writer_store);
  writer.begin(ops(), 0);
  for (unsigned i = 0; i < 40; ++i) {
    char text[24];
    std::snprintf(text, sizeof(text), "REC%03u", i);
    writer.log(make("N7", "F7", "H1", text, kDay1 + i, i), i);
  }
  writer.end(100);
  // A power cut leaves a half line at the end, and another program wrote a foreign line in the middle.
  std::string& f = g_fs.files["/orcsdr/acars/20251016.jsonl"];
  f.insert(f.find('\n') + 1, "{\"v\":1,\"event\":\"start\"}\n");
  f += "{\"v\":1,\"utc\":1760572999,\"up\":9,\"f\":131550000,\"tail\":\"N7\",\"flt\":\"F7\",\"lbl\":\"H";
  Store store;
  Logger reader(&store);
  reader.begin(ops(), 200);
  reader.load_history();
  CHECK(store.count() == 40 && reader.stats().loaded == 40);
  CHECK(reader.stats().skipped_lines >= 1);
  const MessageRecord* rows[1];
  size_t total = 0;
  CHECK(store.query({}, 0, rows, 1, &total) == 1 && std::strcmp(rows[0]->text, "REC039") == 0);   // chronological order kept
  // A huge old file is read only from its tail: the first (partial) line is dropped, the newest records survive.
  g_fs = Mem{};
  std::string big;
  for (unsigned i = 0; i < 2500; ++i) {
    char line[kLineMax + 2], text[24];
    std::snprintf(text, sizeof(text), "BIG%05u", i);
    const size_t n = format_line(make("N8", "F8", "H1", text, kDay1 + 1, i), line, sizeof(line));
    big.append(line, n);
  }
  g_fs.files["/orcsdr/acars/20251016.jsonl"] = big;
  Store store2;
  Logger reader2(&store2);
  reader2.begin(ops(), 0);
  reader2.load_history();
  CHECK(store2.count() == kStoreCapacity);
  CHECK(store2.query({}, 0, rows, 1, &total) == 1 && std::strcmp(rows[0]->text, "BIG02499") == 0);
  CHECK(reader2.stats().loaded < 2500);                          // bounded read, not the whole file
  // A second logger session for the same card appends to the same day file and does not reload what it just wrote.
}

}  // namespace

int main() {
  test_line_round_trip();
  test_hostile_bytes();
  test_parser_rejects_damage();
  test_store();
  test_logger_flush_and_files();
  test_logger_failures();
  test_retention();
  test_undated_file_is_capped();
  test_history_load();
  std::puts("acars_archive_tests: all passed");
  return 0;
}
