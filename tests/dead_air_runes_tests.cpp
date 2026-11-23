#include "dead_air_runes.hpp"
#include "dead_air_veil26_log.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

namespace r = orcsdr::dead_air::runes;
namespace v = orcsdr::dead_air::veil26;

int main() {
  // Independently checked by tests/dead_air_runes_vectors.py (plain integer divmod).
  const uint32_t states[] = {0, 7, 23, 24, 0xffffffffu, 0xabeb47eeu};
  const int expected[] = {0, 7, 23, 0, 15, 22};
  const auto& alphabet = r::alphabet(r::Alphabet::elder);
  assert(alphabet.size == 24);
  for (size_t i = 0; i < 6; ++i) {
    assert(r::index(r::Alphabet::elder, states[i]) == expected[i]);
    v::RecordInput in;
    in.frame.state = states[i]; in.frame.word = "TEST";
    in.alphabet = r::Alphabet::elder;
    char line[v::kLineBytes];
    assert(v::format_record(in, line, sizeof(line)));
    // Read the serialized state, not the original frame, to recompute.
    const std::string record(line);
    const size_t at = record.find("\"state\":\"");
    assert(at != std::string::npos);
    const auto saved = static_cast<uint32_t>(std::stoul(record.substr(at + 9, 8), nullptr, 16));
    char want[80];
    std::snprintf(want, sizeof(want), "\"rune_index\":%d,\"rune_name\":\"%s\"",
                  r::index(r::Alphabet::elder, saved), r::glyph(r::Alphabet::elder, expected[i])->name);
    assert(record.find(want) != std::string::npos);
    assert(record.find("\"rune_alphabet\":\"elder-futhark-24\"") != std::string::npos);
  }
  for (unsigned i = 0; i < alphabet.size; ++i) {
    const auto* glyph = r::glyph(r::Alphabet::elder, i);
    assert(glyph && glyph->count);
    for (unsigned j = 0; j < glyph->count; ++j) {
      const auto& s = glyph->segments[j];
      assert(s.x0 <= 8 && s.x1 <= 8 && s.y0 <= 16 && s.y1 <= 16);
      assert(s.x0 != s.x1 || s.y0 != s.y1);
    }
    for (unsigned j = 0; j < i; ++j) {
      const auto* other = r::glyph(r::Alphabet::elder, j);
      assert(glyph->count != other->count ||
             std::memcmp(glyph->segments, other->segments, glyph->count * sizeof(r::Segment)) != 0);
    }
    for (int height : {1, 16, 37, 80}) for (int stroke : {1, 2, 5}) {
      int lines = 0;
      r::draw(*glyph, height, stroke, [&](int x0, int y0, int x1, int y1) {
        assert(x0 >= 0 && x1 >= 0 && y0 >= 0 && y1 >= 0);
        assert(x0 < (height + 1) / 2 && x1 < (height + 1) / 2 && y0 < height && y1 < height);
        ++lines;
      });
      assert(lines);
    }
  }
  assert(!r::glyph(r::Alphabet::latin, 0));
  assert(!r::glyph(r::Alphabet::elder, 24));
  assert(r::index(r::Alphabet::latin, 7) == -1);
  r::Alphabet selected = r::Alphabet::latin;
  assert(r::parse("ELDER", &selected) && selected == r::Alphabet::elder);
  assert(!r::parse("YOUNGER", &selected) && selected == r::Alphabet::elder);
  assert(!r::parse("ELDER extra", &selected));
  bool pending = false;
  r::set_alphabet(r::Alphabet::latin, selected, false, pending);
  assert(selected == r::Alphabet::latin && !pending);
  v::Engine e, control; e.reset("DEAD AIR"); control.reset("DEAD AIR");
  for (int i = 0; i < 30; ++i) {
    const auto f = e.feed_rf(98700000, -31.7);
    const uint32_t state = e.state(), seed = e.seed(), count = e.frames();
    for (auto a : {r::Alphabet::elder, r::Alphabet::latin}) {
      pending = false;
      r::set_alphabet(a, selected, true, pending);
      assert(selected == a && pending);
      pending = false;
      r::set_alphabet(a, selected, true, pending);
      assert(!pending);  // selecting the current alphabet queues no extra config
    }
    assert(e.state() == state && e.seed() == seed && e.frames() == count);
    const auto reference = control.feed_rf(98700000, -31.7);
    assert(f.state == reference.state && f.letter == reference.letter && f.word_index == reference.word_index);
    assert(f.word_a1z26 == reference.word_a1z26 && f.word_pythagorean == reference.word_pythagorean);
  }
  char config[v::kLineBytes];
  assert(v::format_config("DEAD AIR", 7, 0, 0, config, sizeof(config), r::Alphabet::elder));
  assert(std::strstr(config, "\"rune_alphabet\":\"elder-futhark-24\""));
  v::RecordInput latin; latin.frame.word = "TEST";
  char latin_line[v::kLineBytes];
  assert(v::format_record(latin, latin_line, sizeof(latin_line)));
  assert(std::strstr(latin_line, "\"rune_alphabet\":\"latin\",\"rune_index\":null,\"rune_name\":null"));
  // Worst escaped record with the longest rune name, including queue-size boundary.
  v::RecordInput worst; worst.frame.word = "TEMPERATURE"; worst.frame.state = 2;
  worst.intention = "\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"";
  worst.alphabet = r::Alphabet::elder; worst.event_kind = "TEMPERATURE CHANGE";
  worst.seed = worst.monotonic_ms = worst.unix_time = worst.frequency_hz = worst.config_revision =
      worst.event_sequence = worst.frame.index = worst.frame.state_in = UINT32_MAX;
  worst.frame.codes.frequency = 4294967; worst.frame.codes.level = 1600;
  worst.level_dbfs = -159.999;
  char line[v::kLineBytes]; assert(v::format_record(worst, line, sizeof(line)));
  std::puts("Dead Air runes host tests PASS");
}
