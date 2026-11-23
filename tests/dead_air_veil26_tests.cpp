// Host tests for the VEIL-26 engine: SHA-256 and the seed, Python-compatible rounding, both quantization conventions, the
// 32-bit recurrence (including negative codes), gematria, the dictionary mapping, gaps, and repeatability.
//
// The golden values come from an independent Python implementation of the documented recurrence (hashlib, unbounded ints,
// round-half-even). They prove the C++ matches the documented arithmetic; they are NOT outputs of the original prototype,
// which was not available, and they use the placeholder dictionary.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "dead_air_veil26.hpp"

using namespace orcsdr::dead_air::veil26;

#define CHECK(cond)                                                              \
  do {                                                                           \
    if (!(cond)) {                                                               \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
      std::exit(1);                                                              \
    }                                                                            \
  } while (0)

namespace {

void test_sha256_and_seed() {
  uint8_t d[32];
  sha256(reinterpret_cast<const uint8_t*>("abc"), 3, d);
  const uint8_t want[32] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
                            0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
  CHECK(std::memcmp(d, want, 32) == 0);
  // The padding boundaries: 55, 56 and 64 byte messages exercise one block, a forced second block and exactly full blocks.
  uint8_t msg[200];
  std::memset(msg, 'a', sizeof(msg));
  sha256(msg, 55, d);
  CHECK(d[0] == 0x9f && d[1] == 0x43 && d[2] == 0x90 && d[3] == 0xf8);  // 9f4390f8..., from hashlib
  sha256(msg, 56, d);
  CHECK(d[0] == 0xb3 && d[1] == 0x54 && d[2] == 0x39 && d[3] == 0xa4);  // b35439a4...
  sha256(msg, 64, d);
  CHECK(d[0] == 0xff && d[1] == 0xe0 && d[2] == 0x54 && d[3] == 0xfe);  // ffe054fe...
  sha256(nullptr, 0, d);
  CHECK(d[0] == 0xe3 && d[1] == 0xb0 && d[2] == 0xc4 && d[3] == 0x42);  // the empty message
  CHECK(seed_state("") == 0x8f6059bbu);
  CHECK(seed_state("hello") == 0x71c7425du);
  CHECK(seed_state("listen closely") == 0x227ad9c6u);
  CHECK(seed_state(nullptr) == seed_state(""));
  CHECK(seed_state("hello") != seed_state("Hello"));
}

void test_rounding() {
  CHECK(py_round(0.5) == 0 && py_round(1.5) == 2 && py_round(2.5) == 2 && py_round(3.5) == 4);  // halves go to even
  CHECK(py_round(-0.5) == 0 && py_round(-1.5) == -2 && py_round(-2.5) == -2);
  CHECK(py_round(1283.0000000000002) == 1283 && py_round(-1234.4) == -1234);
  CHECK(py_round(NAN) == 0 && py_round(INFINITY) == 0 && py_round(-INFINITY) == 0);  // a non-finite input is 0, never UB
  CHECK(py_round(1e300) == INT64_MAX && py_round(-1e300) == INT64_MIN);
}

void test_quantization() {
  Codes c = quantize_log(162400000.0, -31.7);  // above 1 MHz: kilohertz
  CHECK(c.frequency == 162400 && c.level == 1283);
  c = quantize_log(121500000.0, -15.5);
  CHECK(c.frequency == 121500 && c.level == 1445);
  c = quantize_log(7830.0, -80.0);  // 1 kHz..1 MHz: whole hertz
  CHECK(c.frequency == 7830 && c.level == 800);
  c = quantize_log(1.0e6, -60.0);   // exactly 1 MHz is still "through 1 MHz": hertz
  CHECK(c.frequency == 1000000 && c.level == 1000);
  c = quantize_log(1.0e3, -60.0);   // exactly 1 kHz: hertz
  CHECK(c.frequency == 1000);
  c = quantize_log(500.0, -100.25);  // below 1 kHz: centihertz
  CHECK(c.frequency == 50000 && c.level == 598);
  c = quantize_log(0.025, -160.0);   // 2.5 centihertz: half goes to even
  CHECK(c.frequency == 2 && c.level == 0);
  c = quantize_log(100.0, -170.0);   // below the -160 dB offset: a negative level code, kept as is
  CHECK(c.frequency == 10000 && c.level == -100);
  c = quantize_spectral(440.5, 0.1234565);  // the spectral convention is separate: Hz * 100 and amplitude * 10000
  CHECK(c.frequency == 44050 && c.level == 1235);
}

void test_recurrence_golden() {
  Engine e;
  e.reset("hello");
  CHECK(e.seed() == 0x71c7425du && e.state() == e.seed() && e.frames() == 0);
  struct Row { uint32_t hz; double db; uint32_t state; uint8_t number; char letter; uint8_t word; const char* name; uint32_t a1, py; };
  const Row rows[] = {
      {162400000, -31.7, 0xabeb47eeu, 25, 'Y', 23, "XENON", 72, 27},
      {162400000, -31.5, 0x28108fdau, 21, 'U', 15, "PRISM", 75, 30},
      {121500000, -15.5, 0x2ad00939u, 12, 'L', 19, "THRESHOLD", 109, 46},
      {7830, -80.0, 0x84dea3b8u, 19, 'S', 31, "FLINT", 61, 25},
  };
  uint32_t previous = e.state();
  for (size_t i = 0; i < 4; ++i) {
    const Frame f = e.feed_rf(rows[i].hz, rows[i].db);
    CHECK(f.index == i && f.state_in == previous);  // the first frame uses frame_index 0
    CHECK(f.state == rows[i].state && e.state() == rows[i].state);
    CHECK(f.letter_number == rows[i].number && f.letter == rows[i].letter);
    CHECK(f.word_index == rows[i].word && std::strcmp(f.word, rows[i].name) == 0);
    CHECK(f.word_a1z26 == rows[i].a1 && f.word_pythagorean == rows[i].py);
    previous = f.state;
  }
  CHECK(e.frames() == 4);
  CHECK(std::strcmp(e.letters(), "YULS") == 0 && e.letter_count() == 4);
  CHECK(e.sequence_a1z26() == 25 + 21 + 12 + 19);
  // A negative level code wraps like a Python integer taken mod 2^32.
  CHECK(step_state(0x12345678u, Codes{12345, -170}, 7) == 0xa7587ce2u);
}

void test_gematria() {
  CHECK(gematria_a1z26("VEIL") == 48 && gematria_pythagorean("VEIL") == 21);
  CHECK(gematria_a1z26("Dead Air") == 42 && gematria_pythagorean("Dead Air") == 33);  // case and spaces ignored
  CHECK(gematria_a1z26("xyz") == 75 && gematria_pythagorean("xyz") == 21);
  CHECK(gematria_a1z26("") == 0 && gematria_a1z26(nullptr) == 0 && gematria_a1z26("1234 !?") == 0);
  CHECK(gematria_pythagorean("J") == 1 && gematria_pythagorean("S") == 1 && gematria_pythagorean("I") == 9 &&
        gematria_pythagorean("R") == 9 && gematria_pythagorean("Z") == 8);
  CHECK(digital_root(0) == 0 && digital_root(9) == 9 && digital_root(10) == 1 && digital_root(48) == 3 && digital_root(999) == 9);
}

void test_dictionary() {
  for (size_t i = 0; i < kWordCount; ++i) {
    CHECK(kDictionary[i] && kDictionary[i][0]);
    for (size_t j = i + 1; j < kWordCount; ++j) CHECK(std::strcmp(kDictionary[i], kDictionary[j]) != 0);
  }
  CHECK(std::strcmp(kDictionaryId, "placeholder-0") == 0);  // results made with this table must say so
  // Every state maps to a letter and a word inside the tables.
  Engine e;
  e.reset("sweep");
  for (uint32_t i = 0; i < 5000; ++i) {
    const Frame f = e.feed(Codes{static_cast<int64_t>(i) * 7919, static_cast<int64_t>(i % 1500)});
    CHECK(f.letter_number >= 1 && f.letter_number <= 26 && f.letter >= 'A' && f.letter <= 'Z');
    CHECK(f.word_index < kWordCount && f.word == kDictionary[f.word_index]);
  }
}

void test_gaps_and_tail() {
  Engine e;
  e.reset("gap");
  e.note_gap();  // a gap before any letter shows nothing
  CHECK(e.letter_count() == 0 && e.gaps() == 1);
  e.feed(Codes{1, 1});
  const uint32_t before = e.state();
  e.note_gap();
  e.note_gap();  // consecutive gaps show one break but are both counted
  CHECK(e.state() == before);  // a gap never changes the state: the stream stays reproducible
  CHECK(e.letters()[1] == '|' && e.letter_count() == 2 && e.gaps() == 3);
  e.feed(Codes{2, 2});
  CHECK(e.letter_count() == 3 && e.letters()[1] == '|');
  CHECK(e.sequence_a1z26() == gematria_a1z26(e.letters()));  // breaks are ignored by the sum
  for (int i = 0; i < 200; ++i) { e.feed(Codes{i, i}); if (i % 5 == 0) e.note_gap(); }
  CHECK(e.letter_count() <= kLetterTail && std::strlen(e.letters()) == e.letter_count());
  CHECK(e.frames() == 202);
}

void test_repeatability_and_intention() {
  Engine a, b, c;
  a.reset("same");
  b.reset("same");
  c.reset("different");
  for (int i = 0; i < 300; ++i) {
    const uint32_t hz = 88000000u + static_cast<uint32_t>(i) * 25000u;
    const double db = -50.0 + 0.1 * (i % 90);
    const Frame fa = a.feed_rf(hz, db), fb = b.feed_rf(hz, db);
    CHECK(fa.state == fb.state && fa.letter == fb.letter && fa.word_index == fb.word_index);
    c.feed_rf(hz, db);
  }
  CHECK(a.state() == b.state() && std::strcmp(a.letters(), b.letters()) == 0);
  CHECK(a.state() != c.state());  // the intention matters
  a.reset("same");                // resetting restarts the identical stream
  CHECK(a.frames() == 0 && a.state() == b.seed() && a.letter_count() == 0 && a.gaps() == 0);
  Engine d;
  d.reset("this intention is much longer than the limit allows");
  CHECK(std::strlen(d.intention()) == kMaxIntention);
  CHECK(d.seed() == seed_state(d.intention()));  // the seed is of what is stored, so a record can be checked
}

Observation obs(uint32_t t, bool valid = true, bool settled = true, uint32_t hz = 162400000, uint32_t rev = 1, double db = -31.7) {
  Observation o;
  o.now_ms = t;
  o.valid = valid;
  o.settled = settled;
  o.frequency_hz = hz;
  o.config_revision = rev;
  o.level_dbfs = db;
  return o;
}

void test_stream_pacing_and_gaps() {
  Stream s;
  s.restart("hello");
  Frame f;
  bool log_due = false;
  // Twenty observations a second become one frame a second; the first frame is logged, the next only after 30 s.
  CHECK(s.observe(obs(1000), &f, &log_due) == Stream::Result::frame && log_due && f.index == 0);
  CHECK(f.state == 0xabeb47eeu);  // the same golden first frame as the engine test
  for (uint32_t t = 1050; t < 2000; t += 50) CHECK(s.observe(obs(t), &f, &log_due) == Stream::Result::waiting && !log_due);
  CHECK(s.observe(obs(2000), &f, &log_due) == Stream::Result::frame && !log_due && f.index == 1);
  uint32_t frames = 2;
  uint32_t logged = 1;
  for (uint32_t t = 2050; t <= 62000; t += 50) {
    if (s.observe(obs(t), &f, &log_due) == Stream::Result::frame) { ++frames; logged += log_due ? 1 : 0; }
  }
  CHECK(frames == 62 && s.frames() == 62);  // 1000 ms .. 62000 ms, one a second
  CHECK(logged == 3);                       // at 1 s, 31 s and 61 s
  CHECK(s.gaps() == 0 && s.skipped() == 0);
  // A retune with usable observations on both sides: the revision change is a break.
  CHECK(s.observe(obs(63000, true, true, 98700000, 2), &f, &log_due) == Stream::Result::frame);
  CHECK(s.gaps() == 1 && std::strchr(s.letters(), '|') != nullptr);
  // Unusable observations are skipped and counted, make no frame, and add one break however long they last.
  const uint32_t state = s.state(), skipped = s.skipped();
  CHECK(s.observe(obs(64000, false), &f, nullptr) == Stream::Result::skipped);
  CHECK(s.observe(obs(65000, true, false), &f, nullptr) == Stream::Result::skipped);          // still settling
  CHECK(s.observe(obs(66000, true, true, 0), &f, nullptr) == Stream::Result::skipped);        // no tuned frequency
  CHECK(s.observe(obs(67000, true, true, 98700000, 2, NAN), &f, nullptr) == Stream::Result::skipped);
  CHECK(s.state() == state && s.skipped() == skipped + 4 && s.gaps() == 2);
  CHECK(s.observe(obs(68000, true, true, 98700000, 3), &f, &log_due) == Stream::Result::frame);  // back: a frame again
  CHECK(s.gaps() == 2);  // coming back from a gap does not add a second break
}

void test_stream_events_and_restart() {
  Stream s;
  s.restart("hello");
  Frame f;
  CHECK(s.observe_event(162400000, -31.7, &f) && f.index == 0 && f.state == 0xabeb47eeu);
  CHECK(!s.observe_event(0, -31.7, &f) && !s.observe_event(162400000, NAN, &f) && s.skipped() == 2);
  CHECK(s.frames() == 1);  // refused events made no frame
  s.restart("hello");      // restarting replays the identical stream and clears the counters
  CHECK(s.frames() == 0 && s.skipped() == 0 && s.gaps() == 0 && s.letter_count() == 0);
  CHECK(s.observe_event(162400000, -31.7, &f) && f.state == 0xabeb47eeu);
  // Restarting from the stream's own intention() must keep it (the app does this when switching analysis back on).
  s.restart(s.intention());
  CHECK(std::strcmp(s.intention(), "hello") == 0 && s.seed() == 0x71c7425du);
}

}  // namespace

int main() {
  test_sha256_and_seed();
  test_rounding();
  test_quantization();
  test_recurrence_golden();
  test_gematria();
  test_dictionary();
  test_gaps_and_tail();
  test_repeatability_and_intention();
  test_stream_pacing_and_gaps();
  test_stream_events_and_restart();
  std::printf("dead_air_veil26_tests: PASS\n");
  return 0;
}
