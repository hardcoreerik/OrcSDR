#pragma once
#include <algorithm>
#include <cstdint>

namespace orcsdr::dead_air::runes {
enum class Alphabet : uint8_t { latin, elder };
struct Segment { uint8_t x0, y0, x1, y1; };
struct Glyph { const char* name; uint8_t count; Segment segments[6]; };
struct AlphabetData { const char* id; const char* label; uint8_t size; const Glyph* glyphs; };
const AlphabetData& alphabet(Alphabet id);
const Glyph* glyph(Alphabet id, unsigned index);
int index(Alphabet id, uint32_t state);  // -1 for Latin (no rune)
bool parse(const char* token, Alphabet* out);  // exact serial tokens; unchanged on failure
// Shared UI/serial selection: deliberately has no access to the canonical engine or stored frames.
void set_alphabet(Alphabet requested, Alphabet& selected, bool enabled, bool& config_pending);

// Integer grid 8x16. The square brush is offset inward, including at tiny sizes.
// Caller adds the box origin; emitted endpoints fit width=(height+1)/2, height.
template <typename Line>
void draw(const Glyph& glyph, int height, int stroke, Line line) {
  if (height <= 0 || stroke <= 0) return;
  const int width = (height + 1) / 2;
  stroke = std::min(stroke, width);
  for (unsigned i = 0; i < glyph.count; ++i) {
    const auto& s = glyph.segments[i];
    for (int dx = 0; dx < stroke; ++dx) for (int dy = 0; dy < stroke; ++dy)
      line(s.x0 * (width - stroke) / 8 + dx, s.y0 * (height - stroke) / 16 + dy,
           s.x1 * (width - stroke) / 8 + dx, s.y1 * (height - stroke) / 16 + dy);
  }
}
}  // namespace orcsdr::dead_air::runes
