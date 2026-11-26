// Host tests for the Digital Voice data log: the JSON line format (only reported fields), buffering, the writer hand-off, drops.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dv_event_log.hpp"

using namespace orcsdr::dv_event_log;
namespace {
int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

Entry identity_entry(uint32_t ms) {
  Entry e;
  e.utc = 1795500000; e.ms = ms; e.hz = 463400000; e.protocol = "dmr"; e.evidence = "identity";
  e.has_cc = true; e.cc = 1; e.has_slot = true; e.slot = 1; e.has_data_type = true; e.data_type = 1; e.flco = 0; e.feature_id = 0; e.service_options = 0;
  e.has_ids = true; e.source_id = 65535; e.destination_id = 16; e.group = true;
  e.info_valid = true;
  for (int i = 0; i < 12; ++i) e.info[i] = static_cast<uint8_t>(i * 17);
  return e;
}

void test_format() {
  char line[kMaxLine];
  const size_t n = format(identity_entry(123456), line, sizeof(line));
  CHECK(n > 0 && line[n - 1] == '\n');
  const std::string s(line, n);
  CHECK(s.find("\"utc\":1795500000,") != std::string::npos);
  CHECK(s.find("\"hz\":463400000") != std::string::npos);
  CHECK(s.find("\"p\":\"dmr\",\"ev\":\"identity\"") != std::string::npos);
  CHECK(s.find("\"cc\":1,\"slot\":1,\"dt\":1,") != std::string::npos);
  CHECK(s.find("\"src\":65535,\"dst\":16,\"grp\":1") != std::string::npos);
  CHECK(s.find("\"info\":\"00112233445566778899aabbccddeeff\"") == std::string::npos);   // 12 bytes: 0,17,34,...,187
  CHECK(s.find("\"info\":\"001122334455667788" "99aabb\"") != std::string::npos);
  CHECK(s.front() == '{' && s[s.size() - 2] == '}');
  // Unknown stays out: no utc, no cc, no ids, no info.
  Entry bare; bare.ms = 5; bare.hz = 152157500; bare.protocol = "dmr"; bare.evidence = "integrity";
  const size_t m = format(bare, line, sizeof(line));
  const std::string t(line, m);
  CHECK(t == "{\"ms\":5,\"hz\":152157500,\"p\":\"dmr\",\"ev\":\"integrity\"}\n");
  // NXDN carries RAN and RF channel, never a colour code or slot.
  Entry nx; nx.ms = 9; nx.hz = 463250000; nx.protocol = "nxdn48"; nx.evidence = "integrity"; nx.has_ran = true; nx.ran = 0; nx.has_rf_channel = true; nx.rf_channel = 3;
  const std::string u(line, format(nx, line, sizeof(line)));
  CHECK(u.find("\"ran\":0,\"rf\":3") != std::string::npos && u.find("\"cc\"") == std::string::npos && u.find("\"slot\"") == std::string::npos);
  CHECK(format(bare, line, 10) == 0);       // too small a buffer is refused, never truncated
}

void test_buffers_and_writer() {
  std::vector<char> a(4096), b(4096);
  Log log;
  CHECK(!log.add(identity_entry(1), 1));    // not started: nothing is kept
  log.begin(a.data(), b.data(), 4096, 1795500000);
  const char* data = nullptr; size_t len = 0; char name[72];
  CHECK(!log.ready(&data, &len, name, sizeof(name), 1000));      // empty: nothing to write
  for (uint32_t i = 0; i < 5; ++i) CHECK(log.add(identity_entry(1000 + i), 1000 + i));
  CHECK(log.lines() == 5);
  CHECK(!log.ready(&data, &len, name, sizeof(name), 5000));      // small and young: waits
  CHECK(log.ready(&data, &len, name, sizeof(name), 1000 + 61000)); // 60 s old: offered
  CHECK(std::string(name) == "/orcsdr/dv_log_1795500000_0000.jsonl");
  CHECK(len > 0 && data[len - 1] == '\n');
  const std::vector<char> first(data, data + len);
  // The writer refused (busy): nothing was committed, the same segment is offered again under the same name.
  CHECK(log.ready(&data, &len, name, sizeof(name), 1000 + 62000) && std::string(name) == "/orcsdr/dv_log_1795500000_0000.jsonl");
  log.saving();                                                   // the writer took it
  CHECK(!log.ready(&data, &len, name, sizeof(name), 100000));    // one segment at a time
  // Lines keep arriving into the other buffer while the first is being written; the first stays unchanged.
  for (uint32_t i = 0; i < 3; ++i) CHECK(log.add(identity_entry(40000 + i), 40000 + i));
  CHECK(std::vector<char>(a.data(), a.data() + first.size()) == first || std::vector<char>(b.data(), b.data() + first.size()) == first);
  log.release(true);
  CHECK(log.segments_saved() == 1 && log.bytes_saved() == first.size());
  CHECK(log.ready(&data, &len, name, sizeof(name), 40000 + 61000) && std::string(name) == "/orcsdr/dv_log_1795500000_0001.jsonl");
  CHECK(len > 0);
  log.saving(); log.release(false);
  CHECK(log.segments_failed() == 1);
  // A forced flush writes what is left even when it is small and young.
  CHECK(log.add(identity_entry(90000), 90000));
  CHECK(!log.ready(&data, &len, name, sizeof(name), 90001));
  CHECK(log.ready(&data, &len, name, sizeof(name), 90001, true));
  log.saving(); log.release(true);
}

void test_drops_when_both_full() {
  std::vector<char> a(2048), b(2048);
  Log log;
  log.begin(a.data(), b.data(), 2048, 0);
  uint32_t added = 0;
  for (uint32_t i = 0; i < 40; ++i) if (log.add(identity_entry(i + 1), i + 1)) ++added;
  CHECK(added > 6 && added < 40 && log.dropped() == 40 - added);   // both buffers fill, the rest is counted as dropped, never silently lost
  const char* data = nullptr; size_t len = 0; char name[72];
  CHECK(log.ready(&data, &len, name, sizeof(name), 100));
  CHECK(std::string(name).rfind("/orcsdr/dv_log_n", 0) == 0);     // no clock: named from the uptime
  log.saving();
  CHECK(!log.add(identity_entry(200), 200));                      // the other buffer is full too and the writer holds this one
  log.release(true);
  CHECK(log.add(identity_entry(300), 300));                       // space again
}
}  // namespace

int main() {
  test_format();
  test_buffers_and_writer();
  test_drops_when_both_full();
  if (g_fail) { std::printf("dv_event_log_tests: %d FAILED\n", g_fail); return 1; }
  std::puts("PASS dv_event_log_tests");
  return 0;
}
