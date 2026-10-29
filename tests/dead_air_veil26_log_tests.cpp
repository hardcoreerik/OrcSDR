// Host tests for VEIL-26 interpretation records: the JSON line (fields, escaping, worst-case length), recomputing a result
// from its saved record alone, the bounded queue (overflow, wrap, oversize), and draining to a failing writer.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "dead_air_veil26_log.hpp"

using namespace orcsdr::dead_air::veil26;

#define CHECK(cond)                                                              \
  do {                                                                           \
    if (!(cond)) {                                                               \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
      std::exit(1);                                                              \
    }                                                                            \
  } while (0)

namespace {

// The value of "key": in a record line, as text (strings without their quotes).
std::string field(const std::string& line, const char* key) {
  const std::string k = std::string("\"") + key + "\":";
  const size_t at = line.find(k);
  if (at == std::string::npos) return "<missing>";
  size_t from = at + k.size();
  if (line[from] == '"') {
    ++from;
    size_t to = from;
    while (to < line.size() && !(line[to] == '"' && line[to - 1] != '\\')) ++to;
    return line.substr(from, to - from);
  }
  size_t to = from;
  while (to < line.size() && line[to] != ',' && line[to] != '}') ++to;
  return line.substr(from, to - from);
}

struct Capture : LineWriter {
  std::vector<std::string> lines;
  int fail_after = -1;
  bool append(const char* line, size_t length) override {
    if (fail_after >= 0 && static_cast<int>(lines.size()) >= fail_after) return false;
    lines.emplace_back(line, length);
    return true;
  }
};

RecordInput sample_input(Engine& e, uint32_t hz, double db) {
  RecordInput in;
  in.frame = e.feed_rf(hz, db);
  in.intention = e.intention();
  in.seed = e.seed();
  in.trigger = Trigger::event;
  in.event_sequence = 12;
  in.event_kind = "RF RISE";
  in.monotonic_ms = 123456;
  in.unix_time = 1793300000;
  in.frequency_hz = hz;
  in.level_dbfs = db;
  in.config_revision = 3;
  return in;
}

void test_record_fields_and_recompute() {
  Engine e;
  e.reset("hello");
  const RecordInput in = sample_input(e, 162400000, -31.7);
  char line[kLineBytes];
  const size_t n = format_record(in, line, sizeof(line));
  CHECK(n > 0 && line[n - 1] == '\n' && std::strlen(line) == n);
  const std::string s(line, n - 1);
  CHECK(field(s, "type") == "veil26" && field(s, "interpreted") == "true");  // never mistaken for a measured event
  CHECK(field(s, "alg") == "VEIL26-v1" && field(s, "dict") == "placeholder-0" && field(s, "quant") == "freq_log_v1");
  CHECK(field(s, "trigger") == "event" && field(s, "event_seq") == "12" && field(s, "event_kind") == "RF RISE");
  CHECK(field(s, "intention") == "hello" && field(s, "seed") == "71c7425d");
  CHECK(field(s, "frame") == "0" && field(s, "t_ms") == "123456" && field(s, "utc") == "1793300000");
  CHECK(field(s, "freq_hz") == "162400000" && field(s, "level_dbfs") == "-31.700" && field(s, "level_src") == "rf");
  CHECK(field(s, "freq_code") == "162400" && field(s, "level_code") == "1283");
  CHECK(field(s, "state_in") == "71c7425d" && field(s, "state") == "abeb47ee");
  CHECK(field(s, "letter") == "Y" && field(s, "word") == "XENON" && field(s, "a1z26") == "72" && field(s, "pyth") == "27");
  // A saved record is enough to recompute its symbols: no other state is needed.
  const Codes codes{std::atoll(field(s, "freq_code").c_str()), std::atoll(field(s, "level_code").c_str())};
  const uint32_t state_in = static_cast<uint32_t>(std::strtoul(field(s, "state_in").c_str(), nullptr, 16));
  const uint32_t again = step_state(state_in, codes, static_cast<uint32_t>(std::atoi(field(s, "frame").c_str())));
  CHECK(again == static_cast<uint32_t>(std::strtoul(field(s, "state").c_str(), nullptr, 16)));
  CHECK(quantize_log(static_cast<double>(std::atoll(field(s, "freq_hz").c_str())), std::atof(field(s, "level_dbfs").c_str())).level ==
        codes.level);
  // And the seed is recomputable from the intention that the record names.
  CHECK(seed_state(field(s, "intention").c_str()) == in.seed);
}

void test_unverified_clock_and_bad_values() {
  Engine e;
  e.reset("x");
  RecordInput in = sample_input(e, 98700000, -20.0);
  in.unix_time = 0;
  in.level_dbfs = NAN;  // never written as "nan", which is not JSON
  char line[kLineBytes];
  const size_t n = format_record(in, line, sizeof(line));
  CHECK(n > 0);
  const std::string s(line, n - 1);
  CHECK(field(s, "utc") == "null" && field(s, "level_dbfs") == "0.000");
  CHECK(s.find("nan") == std::string::npos && s.find("inf") == std::string::npos);
  char tiny[16];
  CHECK(format_record(in, tiny, sizeof(tiny)) == 0);  // too small: nothing partial is produced
  CHECK(format_record(in, nullptr, 0) == 0);
}

void test_escaping_and_worst_case_length() {
  Engine e;
  e.reset("q\"uo\\te\x01\xff\"\"\"\"\"\"\"\"\"\"\"\"\"");  // every character needs escaping or replacement
  RecordInput in = sample_input(e, 4294967295u, -159.999);
  in.event_kind = "TEMPERATURE CHANGE";
  in.monotonic_ms = 4294967295u;
  in.unix_time = 4294967295u;
  in.event_sequence = 4294967295u;
  in.config_revision = 4294967295u;
  char line[kLineBytes];
  const size_t n = format_record(in, line, sizeof(line));
  CHECK(n > 0 && n < kLineBytes);  // the largest plausible record still fits a queue slot
  const std::string s(line, n - 1);
  CHECK(s.find('\x01') == std::string::npos && s.find('\xff') == std::string::npos);
  CHECK(s.find("\\\"") != std::string::npos && s.find("\\\\") != std::string::npos);
  int quotes = 0;  // every unescaped quote is a structural one: an even count
  for (size_t i = 0; i < s.size(); ++i) quotes += s[i] == '"' && (i == 0 || s[i - 1] != '\\');
  CHECK(quotes % 2 == 0);
  char config[kLineBytes];
  const size_t cn = format_config(e.intention(), e.seed(), 4294967295u, 4294967295u, config, sizeof(config));
  CHECK(cn > 0 && cn < kLineBytes);
  const std::string c(config, cn - 1);
  CHECK(field(c, "type") == "veil26_config" && field(c, "dict_canonical") == "false" && field(c, "interpreted") == "true");
}

void test_summary() {
  Counters c;
  c.frames = 10; c.gaps = 2; c.skipped_invalid = 3; c.records_posted = 5; c.records_dropped = 1; c.records_written = 4; c.write_failures = 0;
  char line[kLineBytes];
  const size_t n = format_summary(c, line, sizeof(line));
  CHECK(n > 0);
  const std::string s(line, n - 1);
  CHECK(field(s, "type") == "veil26_summary" && field(s, "frames") == "10" && field(s, "records_dropped") == "1");
}

void test_queue_bounds_and_wrap() {
  static RecordQueue q;
  q.reset();
  CHECK(q.pending() == 0);
  char line[32];
  for (uint32_t i = 0; i < RecordQueue::kSlots; ++i) {
    const int n = std::snprintf(line, sizeof(line), "line %u\n", i);
    CHECK(q.post(line, static_cast<size_t>(n)));
  }
  CHECK(q.pending() == RecordQueue::kSlots && !q.post("overflow\n", 9) && q.dropped() == 1);  // full: dropped and counted
  char out[kLineBytes];
  size_t length = 0;
  CHECK(q.take(out, sizeof(out), &length) && std::string(out, length) == "line 0\n");
  CHECK(q.post("after\n", 6));  // a slot freed by the consumer is usable at once
  for (uint32_t i = 1; i < RecordQueue::kSlots; ++i) CHECK(q.take(out, sizeof(out), &length));
  CHECK(q.take(out, sizeof(out), &length) && std::string(out, length) == "after\n");
  CHECK(!q.take(out, sizeof(out), &length) && q.pending() == 0);
  for (int round = 0; round < 100; ++round) {  // many wraps of the ring
    CHECK(q.post("w\n", 2));
    CHECK(q.take(out, sizeof(out), &length) && length == 2);
  }
  CHECK(!q.post("", 0) && !q.post(nullptr, 3));  // nothing to store
  std::string big(kLineBytes + 1, 'x');
  CHECK(!q.post(big.data(), big.size()));         // too long for a slot
  CHECK(q.dropped() == 4);
  CHECK(q.posted() == RecordQueue::kSlots + 1 + 100);
}

void test_drain_and_failures() {
  static RecordQueue q;
  q.reset();
  Capture good;
  bool failed = false;
  for (int i = 0; i < 5; ++i) CHECK(q.post("a\n", 2));
  CHECK(drain(q, good, 3, &failed) == 3 && !failed && q.pending() == 2);  // bounded per call
  CHECK(drain(q, good, 10, &failed) == 2 && good.lines.size() == 5);
  CHECK(drain(q, good, 10, &failed) == 0);
  Capture bad;
  bad.fail_after = 1;
  for (int i = 0; i < 4; ++i) CHECK(q.post("b\n", 2));
  CHECK(drain(q, bad, 10, &failed) == 1 && failed);  // the writer failed: the caller is told, and counts it
  CHECK(q.pending() == 2);                           // the line that failed is gone; the rest wait for the caller's decision
}

}  // namespace

int main() {
  test_record_fields_and_recompute();
  test_unverified_clock_and_bad_values();
  test_escaping_and_worst_case_length();
  test_summary();
  test_queue_bounds_and_wrap();
  test_drain_and_failures();
  std::printf("dead_air_veil26_log_tests: PASS\n");
  return 0;
}
