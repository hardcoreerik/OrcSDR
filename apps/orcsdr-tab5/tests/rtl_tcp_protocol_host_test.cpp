#include "rtl_tcp_protocol.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace orcsdr::rtl_tcp;

static int g_failures = 0;
#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
      ++g_failures;                                                      \
    }                                                                    \
  } while (0)

static std::vector<uint8_t> greeting_bytes(uint32_t tuner, uint32_t gains) {
  return {'R', 'T', 'L', '0',
          static_cast<uint8_t>(tuner >> 24), static_cast<uint8_t>(tuner >> 16),
          static_cast<uint8_t>(tuner >> 8), static_cast<uint8_t>(tuner),
          static_cast<uint8_t>(gains >> 24), static_cast<uint8_t>(gains >> 16),
          static_cast<uint8_t>(gains >> 8), static_cast<uint8_t>(gains)};
}

static uint32_t g_rng = 12345;
static uint32_t next_rand() {
  g_rng = g_rng * 1664525u + 1013904223u;
  return g_rng >> 8;
}

static void test_command_encoding() {
  uint8_t out[kCommandBytes];
  encode_command(Command::set_frequency, 100100000u, out);  // 100.1 MHz
  const uint8_t freq[5] = {0x01, 0x05, 0xF7, 0x67, 0xA0};
  CHECK(std::memcmp(out, freq, 5) == 0);
  encode_command(Command::set_sample_rate, 2400000u, out);
  const uint8_t rate[5] = {0x02, 0x00, 0x24, 0x9F, 0x00};
  CHECK(std::memcmp(out, rate, 5) == 0);
  encode_command(Command::set_freq_correction, parameter_signed(-5), out);
  const uint8_t ppm[5] = {0x05, 0xFF, 0xFF, 0xFF, 0xFB};
  CHECK(std::memcmp(out, ppm, 5) == 0);
  CHECK(parameter_if_gain(1, -10) == 0x0001FFF6u);
  encode_command(Command::set_bias_tee, 1, out);
  const uint8_t bias[5] = {0x0E, 0, 0, 0, 1};
  CHECK(std::memcmp(out, bias, 5) == 0);
  CHECK(static_cast<uint8_t>(Command::set_gain_by_index) == 0x0D);
}

static void test_greeting_valid_all_splits() {
  const auto stream = greeting_bytes(5, 29);
  // Trailing IQ bytes after the greeting must be left unconsumed.
  std::vector<uint8_t> full = stream;
  full.insert(full.end(), {0x80, 0x7F, 0x81, 0x7E});
  for (size_t split = 0; split <= stream.size(); ++split) {
    GreetingParser parser;
    size_t used_a = 0, used_b = 0;
    parser.feed(full.data(), split, &used_a);
    CHECK(used_a == split);
    const auto status = parser.feed(full.data() + used_a, full.size() - used_a, &used_b);
    CHECK(status == GreetingStatus::ok);
    CHECK(used_a + used_b == kGreetingBytes);
    CHECK(parser.greeting().tuner_type_raw == 5);
    CHECK(parser.greeting().gain_count == 29);
  }
  GreetingParser bytewise;
  size_t used = 0;
  for (size_t i = 0; i < stream.size(); ++i) {
    CHECK(bytewise.status() == GreetingStatus::need_more);
    bytewise.feed(&stream[i], 1, &used);
    CHECK(used == 1);
  }
  CHECK(bytewise.status() == GreetingStatus::ok);
  CHECK(std::strcmp(tuner_type_name(bytewise.greeting().tuner_type_raw), "R820T/R820T2") == 0);
}

static void test_greeting_rejects() {
  size_t used = 0;
  {
    GreetingParser parser;  // an HTTP server answering on the wrong port
    const uint8_t http[] = {'H', 'T', 'T', 'P', '/', '1'};
    CHECK(parser.feed(http, sizeof(http), &used) == GreetingStatus::bad_magic);
    CHECK(used == 0);  // refused on the very first byte
  }
  {
    GreetingParser parser;
    const uint8_t near_miss[] = {'R', 'T', 'L', 'x'};
    CHECK(parser.feed(near_miss, sizeof(near_miss), &used) == GreetingStatus::bad_magic);
    CHECK(used == 3);
  }
  {
    GreetingParser parser;
    const auto implausible = greeting_bytes(5, 0x1000);
    CHECK(parser.feed(implausible.data(), implausible.size(), &used) == GreetingStatus::bad_field);
  }
  {
    GreetingParser parser;  // the connection dropped part way through the greeting
    const auto stream = greeting_bytes(5, 29);
    CHECK(parser.feed(stream.data(), 7, &used) == GreetingStatus::need_more);
    CHECK(used == 7);
    CHECK(parser.status() == GreetingStatus::need_more);  // caller treats EOF here as failure
    parser.reset();  // a reconnect starts a fresh greeting
    CHECK(parser.feed(stream.data(), stream.size(), &used) == GreetingStatus::ok);
  }
  {
    GreetingParser parser;
    CHECK(parser.feed(nullptr, 0, &used) == GreetingStatus::need_more);
    CHECK(used == 0);
    CHECK(std::strcmp(tuner_type_name(99), "unknown") == 0);
  }
}

static void test_iq_order_across_random_splits() {
  std::vector<uint8_t> source(100001);  // odd on purpose
  for (size_t i = 0; i < source.size(); ++i)
    source[i] = static_cast<uint8_t>((i % 2 == 0 ? 0x10 : 0xC0) + (i / 2) % 13);
  for (int round = 0; round < 50; ++round) {
    IqPairCarry carry;
    std::vector<uint8_t> out;
    size_t pos = 0;
    uint8_t slot[64];
    while (pos < source.size()) {
      const size_t offset = carry.prepare(slot);
      size_t want = 1 + next_rand() % (sizeof(slot) - offset - 1);
      if (want > source.size() - pos) want = source.size() - pos;
      std::memcpy(slot + offset, source.data() + pos, want);
      pos += want;
      const size_t usable = carry.commit(slot, offset, want);
      CHECK(usable % 2 == 0);
      out.insert(out.end(), slot, slot + usable);
    }
    CHECK(out.size() == source.size() - 1);  // the one odd tail byte is still held
    CHECK(carry.pending());
    CHECK(std::memcmp(out.data(), source.data(), out.size()) == 0);
    for (size_t i = 0; i < out.size(); i += 2) CHECK(out[i] < 0x80 && out[i + 1] >= 0xC0);
  }
}

static void test_reconnect_drops_carried_byte() {
  IqPairCarry carry;
  uint8_t slot[8] = {1, 2, 3};
  CHECK(carry.commit(slot, 0, 3) == 2);
  CHECK(carry.pending());
  carry.reset();  // new connection: the old byte must not lead the new stream
  CHECK(!carry.pending());
  CHECK(carry.prepare(slot) == 0);
}

static void test_timeout_keeps_carried_byte() {
  IqPairCarry carry;
  uint8_t slot[8] = {0xAA, 0xBB, 0xCC};
  CHECK(carry.commit(slot, 0, 3) == 2);  // 0xCC carried
  uint8_t other[8];
  CHECK(carry.prepare(other) == 1 && other[0] == 0xCC);
  // The read timed out and the slot was released: nothing committed.
  uint8_t next[8];
  CHECK(carry.prepare(next) == 1 && next[0] == 0xCC);  // still held, not lost
  next[1] = 0xDD;
  CHECK(carry.commit(next, 1, 1) == 2);  // pair CC DD, stays in order
  CHECK(!carry.pending());
}

static void test_host_validation() {
  CHECK(valid_host("192.168.1.50"));
  CHECK(valid_host("harbour-sdr.local"));
  CHECK(!valid_host(nullptr));
  CHECK(!valid_host(""));
  CHECK(!valid_host("host name"));
  CHECK(!valid_host("http://example.com"));
  CHECK(!valid_host("-bad"));
  CHECK(!valid_host(".bad"));
  CHECK(!valid_host("a;rm"));
  char long_host[80];
  std::memset(long_host, 'a', sizeof(long_host));
  long_host[63] = '\0';
  CHECK(valid_host(long_host));
  long_host[63] = 'a';
  long_host[64] = '\0';  // 64 characters is one too many
  CHECK(!valid_host(long_host));
}

static void test_endpoint_parsing() {
  char host[64];
  uint16_t port = 0;
  CHECK(parse_endpoint("192.168.1.50", host, sizeof(host), &port) && port == 1234 &&
        std::strcmp(host, "192.168.1.50") == 0);
  CHECK(parse_endpoint("harbour-sdr.local:5000", host, sizeof(host), &port) && port == 5000 &&
        std::strcmp(host, "harbour-sdr.local") == 0);
  CHECK(parse_endpoint("10.0.0.2:65535", host, sizeof(host), &port) && port == 65535);
  CHECK(!parse_endpoint("10.0.0.2:0", host, sizeof(host), &port));
  CHECK(!parse_endpoint("10.0.0.2:65536", host, sizeof(host), &port));
  CHECK(!parse_endpoint("10.0.0.2:", host, sizeof(host), &port));
  CHECK(!parse_endpoint("10.0.0.2:12a4", host, sizeof(host), &port));
  CHECK(!parse_endpoint(":1234", host, sizeof(host), &port));
  CHECK(!parse_endpoint("", host, sizeof(host), &port));
  CHECK(!parse_endpoint("bad host", host, sizeof(host), &port));
  CHECK(!parse_endpoint("http://x:80", host, sizeof(host), &port));
  CHECK(!parse_endpoint(nullptr, host, sizeof(host), &port));
  CHECK(!parse_endpoint("192.168.1.50", host, 5, &port));  // does not fit the buffer
}

static void test_server_side_framing() {
  uint8_t hello[kGreetingBytes];
  encode_greeting(5, 29, hello);
  GreetingParser parser;
  size_t used = 0;
  CHECK(parser.feed(hello, sizeof(hello), &used) == GreetingStatus::ok);  // a client accepts it
  CHECK(parser.greeting().tuner_type_raw == 5 && parser.greeting().gain_count == 29);

  // Two commands arriving as 3 + 4 + 3 bytes still frame as two commands.
  const uint8_t stream[10] = {0x01, 0x05, 0xF7, 0x67, 0xA0, 0x04, 0x00, 0x00, 0x00, 0xC8};
  CommandParser commands;
  RawCommand out;
  std::vector<RawCommand> seen;
  for (size_t i = 0; i < sizeof(stream); ++i)
    if (commands.push(stream[i], &out)) seen.push_back(out);
  CHECK(seen.size() == 2);
  CHECK(seen[0].id == 0x01 && seen[0].parameter == 100100000u);
  CHECK(seen[1].id == 0x04 && seen[1].parameter == 200u);
  commands.reset();  // a new connection starts mid-nothing
  CHECK(!commands.push(0x01, &out));
}

static void test_backoff_is_bounded() {
  BackoffPolicy policy;
  const uint32_t expect[] = {500, 1000, 2000, 4000, 8000, 15000, 15000, 15000};
  uint32_t delay = 0;
  for (uint32_t i = 0; i < 8; ++i) {
    CHECK(reconnect_delay_ms(policy, i, &delay));
    CHECK(delay == expect[i]);
  }
  CHECK(!reconnect_delay_ms(policy, 8, &delay));
  CHECK(!reconnect_delay_ms(policy, 1000000, &delay));
  BackoffPolicy huge{1, 0xFFFFFFFFu, 100};
  CHECK(reconnect_delay_ms(huge, 99, &delay));  // no overflow while doubling
}

static void test_rate_adapter() {
  using C = RateAdapter::Change;
  auto none = [](C c) { return c == C::none; };
  // Recorded on the real link (Tab5 on Wi-Fi, Pi on Wi-Fi), link ratio every few seconds:
  // 1.44 MS/s (one hour, every minute): the server dropped about 11% of the samples and nothing stepped down at the old 85% threshold.
  const uint8_t lossy_144[] = {91, 85, 82, 87, 90, 85, 89, 89, 89, 91, 88, 88, 80, 90, 87, 87, 80, 34, 89, 91, 90, 91, 88, 92, 89, 87, 89, 84, 87, 86};
  // 0.96 MS/s (five minutes, every 5 s): acceptable, noisy around 94.
  const uint8_t fine_96[] = {95, 95, 93, 92, 94, 95, 94, 95, 94, 95, 93, 93, 92, 96, 94, 95, 96, 93, 96, 92, 96, 94, 93, 94, 95, 96, 94, 93, 94, 92,
                             95, 92, 92, 93, 97, 94, 93, 79, 95, 95, 93, 92, 97, 93, 95, 91, 96, 95, 95, 95, 94, 95, 94, 94, 95, 95, 95, 97};
  // 0.24 MS/s: clean.
  const uint8_t clean_24[] = {101, 99, 99, 99, 86, 100, 100, 99, 99, 100, 100, 99, 99, 101};

  RateAdapter a;
  CHECK(a.rate(960000) == 960000);
  // An unmeasured or healthy link never steps, and not-measured clears the window.
  for (int i = 0; i < 20; ++i) CHECK(none(a.note(0, 960000, 240000)));
  for (int i = 0; i < 20; ++i) CHECK(none(a.note(100, 960000, 240000)));
  // A collapsing link steps down within three readings (here the mean of the window drops below 91% on the second;
  // the three-readings-below-70% fast path covers a link whose window is not yet full).
  CHECK(none(a.note(50, 960000, 240000)));
  CHECK(a.note(50, 960000, 240000) == C::down);
  CHECK(a.rate(960000) == 240000);                    // 480 kS/s is not a rate an RTL2832 can run
  for (int i = 0; i < 50; ++i) CHECK(none(a.note(10, 960000, 240000)));  // ladder ends at 240k
  CHECK(a.rate(960000) == 240000);
  a.reset();
  CHECK(a.rate(960000) == 960000);
  // A steady shortfall needs a full window: seven 88% readings do nothing, the eighth steps down.
  RateAdapter w;
  for (int i = 0; i < 7; ++i) CHECK(none(w.note(88, 1440000, 240000)));
  CHECK(w.note(88, 1440000, 240000) == C::down);
  CHECK(w.rate(1440000) == 960000);
  // One bad reading in eight does not trigger a step; an alternating 97 / 80 link (mean 88.5) does.
  RateAdapter x;
  for (int i = 0; i < 40; ++i) CHECK(none(x.note(i % 8 == 3 ? 80 : 98, 1440000, 240000)));
  RateAdapter y;
  bool stepped = false;
  for (int i = 0; i < 16; ++i) stepped = (y.note(i % 2 ? 97 : 80, 1440000, 240000) == C::down) || stepped;
  CHECK(stepped);
  // The consumer's floor stops the descent early.
  RateAdapter b;
  for (int i = 0; i < 20; ++i) CHECK(none(b.note(30, 960000, 500000)));  // 240k is below the floor
  CHECK(b.rate(960000) == 960000);
  // The 1.44 MS/s ladder only uses rates a real dongle supports and whole multiples of 240 kS/s.
  RateAdapter c;
  CHECK(c.rate(1440000) == 1440000);
  const uint32_t expect[] = {960000, 240000};
  for (uint32_t want : expect) {
    int guard = 0;
    while (c.note(40, 1440000, 240000) != C::down && ++guard < 20) {}
    CHECK(c.rate(1440000) == want);
    CHECK(want % 240000 == 0);
    CHECK((want >= 225001 && want <= 300000) || (want >= 900001 && want <= 3200000));
  }
  for (int i = 0; i < 20; ++i) CHECK(none(c.note(40, 1440000, 240000)));  // nothing below the bottom rung
  // A wanted rate off the ladder (ADS-B, lab overrides) is left alone until it steps.
  RateAdapter d;
  CHECK(d.rate(2048000) == 2048000);

  // The recorded series. The lossy 1.44 MS/s link steps down; the acceptable 0.96 MS/s link never does.
  RateAdapter r144;
  bool r144_down = false;
  for (uint8_t v : lossy_144) {
    if (r144.note(v, 1440000, 240000) == C::down) { r144_down = true; break; }   // the series belongs to 1.44 MS/s: stop at the first step
  }
  CHECK(r144_down && r144.rate(1440000) == 960000);
  RateAdapter r96;
  r96.step = 1;                                        // running at 0.96 MS/s after a step down from 1.44
  bool r96_moved = false;
  for (uint8_t v : fine_96) r96_moved = (r96.note(v, 1440000, 240000) != C::none) || r96_moved;
  CHECK(!r96_moved && r96.rate(1440000) == 960000);   // no step down, and no probe up: its mean (94) is not clearly healthy
  RateAdapter r24;
  r24.step = 2;                                        // running at 0.24 MS/s; a clean link earns a probe up after a healthy minute
  bool r24_up = false;
  for (int i = 0; i < 200 && !r24_up; ++i) r24_up = r24.note(clean_24[i % 14], 1440000, 240000) == C::up;
  CHECK(r24_up && r24.rate(1440000) == 960000);

  // Automatic recovery: a clean window plus 30 healthy readings steps up one rung at a time.
  RateAdapter e;
  e.step = 1;
  for (int i = 0; i < 7; ++i) CHECK(none(e.note(100, 1440000, 240000)));   // fill the window
  for (int i = 0; i < 29; ++i) CHECK(none(e.note(100, 1440000, 240000)));
  CHECK(e.note(100, 1440000, 240000) == C::up);        // the 30th healthy reading
  CHECK(e.rate(1440000) == 1440000);
  for (int i = 0; i < 100; ++i) CHECK(none(e.note(100, 1440000, 240000)));  // nothing above the top
  // Readings in the grey zone (mean 91-96) neither step down nor count toward stepping up.
  RateAdapter f;
  f.step = 2;                                          // at 0.24 MS/s on the 1.44 MS/s ladder
  for (int i = 0; i < 300; ++i) CHECK(none(f.note(94, 1440000, 240000)));
  CHECK(f.rate(1440000) == 240000);
  // A probe that fails doubles the wait before the next one, then settles.
  RateAdapter g;
  g.step = 2;                                          // at 0.24 MS/s on the 1.44 MS/s ladder
  auto climb = [&](int healthy_needed) {
    for (int i = 0; i < 7; ++i) g.note(99, 1440000, 240000);                // fill the window
    for (int i = 0; i < healthy_needed - 1; ++i) CHECK(none(g.note(99, 1440000, 240000)));
    return g.note(99, 1440000, 240000);
  };
  CHECK(climb(30) == C::up);                           // probe 960k
  CHECK(g.rate(1440000) == 960000);
  bool failed = false;
  for (int i = 0; i < 8 && !failed; ++i) failed = g.note(80, 1440000, 240000) == C::down;
  CHECK(failed && g.rate(1440000) == 240000);          // probe failed, back to 240k
  CHECK(climb(60) == C::up);                           // the wait is now 60
}

static void test_nominal_gain_steps() {
  int g[40]{};
  CHECK(nominal_gain_steps(5, 29, g, 40) == 29);        // R820T
  CHECK(g[0] == 0 && g[28] == 496);
  for (int i = 1; i < 29; ++i) CHECK(g[i] > g[i - 1]);  // strictly increasing
  CHECK(nominal_gain_steps(6, 29, nullptr, 0) == 29);   // R828D: size query
  CHECK(nominal_gain_steps(1, 14, g, 40) == 14);        // E4000
  CHECK(g[0] == -10 && g[13] == 420);
  CHECK(nominal_gain_steps(5, 28, g, 40) == 0);         // a variant with a different count gets no guessed table
  CHECK(nominal_gain_steps(3, 23, g, 40) == 0);         // FC0013: unknown
  CHECK(nominal_gain_steps(0, 1, g, 40) == 0);
  CHECK(nominal_gain_steps(5, 29, g, 4) == 4 && g[3] == 27);  // a short buffer is filled, not overrun
}

int main() {
  test_nominal_gain_steps();
  test_rate_adapter();
  test_command_encoding();
  test_greeting_valid_all_splits();
  test_greeting_rejects();
  test_iq_order_across_random_splits();
  test_reconnect_drops_carried_byte();
  test_timeout_keeps_carried_byte();
  test_host_validation();
  test_endpoint_parsing();
  test_server_side_framing();
  test_backoff_is_bounded();
  std::printf("RTL_TCP_PROTOCOL_HOST_TEST failures=%d pass=%d\n", g_failures, g_failures == 0);
  return g_failures == 0 ? 0 : 1;
}
