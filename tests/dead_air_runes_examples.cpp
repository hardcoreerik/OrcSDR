// Reproducible examples from the real canonical engine and rune module; no hardware.
#include <M5Unified.h>
#include "dead_air_dashboard.hpp"
#include "dead_air_veil26.hpp"
#include "screen_controller.hpp"
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>

namespace da = orcsdr::dead_air;
int main() {
  const uint32_t frequencies[] = {98700000, 162400000, 146520000};
  const double levels[] = {-31.7, -45.2, -12.5};
  std::cout << "[";
  for (unsigned i = 0; i < 3; ++i) {
    da::veil26::Engine engine; engine.reset("DEAD AIR");
    const auto frame = engine.feed_rf(frequencies[i], levels[i]);
    da::Snapshot snapshot;
    snapshot.revision = 1; snapshot.receiver_ready = snapshot.receiver_settled = true;
    snapshot.frequency_hz = frequencies[i];
    auto& v = snapshot.veil;
    v.enabled = true; v.alphabet = da::runes::Alphabet::elder; v.seed = engine.seed(); v.frames = 1;
    std::snprintf(v.intention, sizeof(v.intention), "%s", engine.intention());
    std::snprintf(v.dictionary, sizeof(v.dictionary), "%s", da::veil26::kDictionaryId);
    std::snprintf(v.letters, sizeof(v.letters), "%s", engine.letters());
    v.seq_a1z26 = engine.sequence_a1z26(); v.seq_pythagorean = engine.sequence_pythagorean();
    auto& f = v.live;
    f.valid = true; f.frequency_hz = frequencies[i]; f.level_dbfs = levels[i];
    f.monotonic_ms = 1000; f.freq_code = frame.codes.frequency; f.level_code = frame.codes.level;
    f.state_in = frame.state_in; f.state = frame.state; f.frame = frame.index;
    f.letter = frame.letter; f.letter_number = frame.letter_number;
    f.a1z26 = frame.word_a1z26; f.pythagorean = frame.word_pythagorean;
    std::snprintf(f.word, sizeof(f.word), "%s", frame.word);
    std::snprintf(f.source, sizeof(f.source), "rf");
    host_display::reset();
    orcsdr::screens::begin_transition(orcsdr::screens::Id::dead_air, 100);
    da::enter(snapshot); orcsdr::screens::finish_transition();
    da::handle_touch(386, 325, 1000);  // actual Overview -> VEIL-26 entry
    if (i) std::cout << ",";
    std::cout << "{\"frequency_hz\":" << frequencies[i] << ",\"level_dbfs\":" << levels[i]
              << ",\"intention\":\"DEAD AIR\",\"frame\":0,\"seed\":" << engine.seed()
              << ",\"state\":" << frame.state << ",\"freq_code\":" << frame.codes.frequency
              << ",\"level_code\":" << frame.codes.level << ",\"letter\":" << std::quoted(std::string(1, frame.letter))
              << ",\"word\":" << std::quoted(frame.word) << ",\"a1z26\":" << frame.word_a1z26
              << ",\"pyth\":" << frame.word_pythagorean << ",\"rune_index\":" << frame.state % 24
              << ",\"rune_name\":" << std::quoted(da::runes::glyph(da::runes::Alphabet::elder, frame.state % 24)->name)
              << ",\"primitives\":[";
    bool first = true;
    for (const auto& p : host_display::prims) {
      if (!first) std::cout << ",";
      first = false;
      std::cout << "[" << unsigned(p.kind) << "," << p.x << "," << p.y << "," << p.w << "," << p.h
                << "," << p.color << "," << std::quoted(p.str) << "]";
    }
    std::cout << "]}";
    da::leave();
  }
  std::cout << "]\n";
}
