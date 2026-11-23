#include "dead_air_runes.hpp"
#include <cstring>

namespace orcsdr::dead_air::runes {
namespace {
// Original simplified strokes; conventional names, no meaning or interpretation.
constexpr Glyph kElder[] = {
  {"fehu", 3, {{0,0,0,16},{0,6,8,0},{0,11,8,5}}},
  {"uruz", 3, {{0,0,0,16},{0,0,8,4},{8,4,8,16}}},
  {"thurisaz", 4, {{0,0,0,16},{0,3,8,7},{8,7,0,11},{0,3,0,11}}},
  {"ansuz", 3, {{0,0,0,16},{0,0,8,6},{0,6,8,12}}},
  {"raido", 5, {{0,0,0,16},{0,0,8,4},{8,4,0,8},{0,8,8,16},{0,0,0,8}}},
  {"kenaz", 2, {{8,0,0,8},{0,8,8,16}}},
  {"gebo", 2, {{0,0,8,16},{8,0,0,16}}},
  {"wunjo", 3, {{0,0,0,16},{0,0,8,4},{8,4,0,8}}},
  {"hagalaz", 3, {{0,0,0,16},{8,0,8,16},{0,4,8,12}}},
  {"nauthiz", 2, {{4,0,4,16},{0,4,8,12}}},
  {"isa", 1, {{4,0,4,16}}},
  {"jera", 4, {{0,0,4,4},{4,4,0,8},{8,8,4,12},{4,12,8,16}}},
  {"eihwaz", 3, {{4,0,4,16},{4,0,8,4},{4,16,0,12}}},
  {"perthro", 5, {{0,0,0,16},{0,0,4,4},{4,4,8,0},{0,16,4,12},{4,12,8,16}}},
  {"algiz", 3, {{4,0,4,16},{0,0,4,6},{8,0,4,6}}},
  {"sowilo", 3, {{8,0,0,6},{0,6,8,10},{8,10,0,16}}},
  {"tiwaz", 3, {{4,0,4,16},{4,0,0,5},{4,0,8,5}}},
  {"berkano", 5, {{0,0,0,16},{0,0,8,4},{8,4,0,8},{0,8,8,12},{8,12,0,16}}},
  {"ehwaz", 4, {{0,0,0,16},{8,0,8,16},{0,0,4,8},{4,8,8,0}}},
  {"mannaz", 4, {{0,0,0,16},{8,0,8,16},{0,0,8,8},{8,0,0,8}}},
  {"laguz", 2, {{0,0,0,16},{0,0,8,6}}},
  {"ingwaz", 4, {{4,0,8,8},{8,8,4,16},{4,16,0,8},{0,8,4,0}}},
  {"dagaz", 4, {{0,0,0,16},{8,0,8,16},{0,0,8,16},{8,0,0,16}}},
  {"othala", 6, {{4,0,8,6},{8,6,4,12},{4,12,0,6},{0,6,4,0},{4,12,8,16},{4,12,0,16}}},
};
constexpr AlphabetData kAlphabets[] = {
  {"latin", "LATIN", 0, nullptr},
  {"elder-futhark-24", "ELDER FUTHARK", 24, kElder},
};
}  // namespace
const AlphabetData& alphabet(Alphabet id) {
  const unsigned i = static_cast<unsigned>(id);
  return kAlphabets[i < sizeof(kAlphabets) / sizeof(kAlphabets[0]) ? i : 0];
}
const Glyph* glyph(Alphabet id, unsigned i) {
  const auto& a = alphabet(id);
  return i < a.size ? &a.glyphs[i] : nullptr;
}
int index(Alphabet id, uint32_t state) {
  const auto& a = alphabet(id);
  return a.size ? static_cast<int>(state % a.size) : -1;
}
bool parse(const char* token, Alphabet* out) {
  if (!token || !out) return false;
  if (std::strcmp(token, "LATIN") == 0) *out = Alphabet::latin;
  else if (std::strcmp(token, "ELDER") == 0) *out = Alphabet::elder;
  else return false;
  return true;
}
void set_alphabet(Alphabet requested, Alphabet& selected, bool enabled, bool& config_pending) {
  if (requested == selected) return;
  selected = requested;
  config_pending = enabled;
}
}  // namespace orcsdr::dead_air::runes
