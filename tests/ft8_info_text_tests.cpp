// The (i) popup text: every topic exists, every line fits the popup width, nothing is empty, and the facts that the UI also states elsewhere agree.
#include "ft8_info_text.hpp"

#include <cstdio>
#include <cstring>
#include <string>

using namespace orcsdr::ft8::info;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

int main() {
  size_t total_lines = 0;
  for (unsigned char t = 0; t < static_cast<unsigned char>(Topic::count); ++t) {
    const Page p = page(static_cast<Topic>(t));
    CHECK(p.title != nullptr && p.title[0] != '\0');
    CHECK(p.lines != nullptr && p.line_count >= 6 && p.line_count <= 22);
    bool has_text = false;
    for (size_t i = 0; i < p.line_count; ++i) {
      CHECK(p.lines[i] != nullptr);
      const size_t length = std::strlen(p.lines[i]);
      if (length > kMaxLineChars) std::fprintf(stderr, "too long (%zu): %s\n", length, p.lines[i]);
      CHECK(length <= kMaxLineChars);
      for (const char* c = p.lines[i]; *c; ++c) CHECK(static_cast<unsigned char>(*c) >= 32 && static_cast<unsigned char>(*c) < 127);   // plain ASCII: the panel font has no other glyphs
      has_text = has_text || length > 0;
    }
    CHECK(has_text);
    total_lines += p.line_count;
  }
  CHECK(page(Topic::count).lines == nullptr);

  // Facts repeated from the Setup rows must match them (15 s FT8/JS8, 7.5 s FT4, the two LDPC codes and CRC widths).
  const auto joined = [](Topic t) {
    std::string s;
    const Page p = page(t);
    for (size_t i = 0; i < p.line_count; ++i) { s += p.lines[i]; s += ' '; }
    return s;
  };
  CHECK(joined(Topic::setup_ft8).find("LDPC(174,91)") != std::string::npos && joined(Topic::setup_ft8).find("15 second") != std::string::npos);
  CHECK(joined(Topic::setup_ft4).find("LDPC(174,91)") != std::string::npos && joined(Topic::setup_ft4).find("7.5 second") != std::string::npos);
  CHECK(joined(Topic::setup_js8).find("LDPC(174,87)") != std::string::npos && joined(Topic::setup_js8).find("12-bit CRC") != std::string::npos);
  CHECK(joined(Topic::setup_js8).find("NORMAL works today") != std::string::npos);
  CHECK(joined(Topic::hunter).find("FT4 and JS8 hunts are not available") != std::string::npos);
  CHECK(joined(Topic::hunter).find("6 seconds") != std::string::npos);
  CHECK(joined(Topic::setup_ft4).find("105 symbols including ramps") != std::string::npos);

  if (failures == 0) std::printf("ft8_info_text_tests OK (%zu lines)\n", total_lines);
  return failures == 0 ? 0 : 1;
}
