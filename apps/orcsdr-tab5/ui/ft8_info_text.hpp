#pragma once

#include <cstddef>

namespace orcsdr::ft8::info {

// Plain-language "about this page" text for the FT8 dashboard's (i) popup. Each topic is a list of lines of at most 68 characters; the
// dashboard pages them 11 lines at a time. Pure data (no drawing), so it is checked by a host test for length and content.
constexpr size_t kMaxLineChars = 68;

enum class Topic : unsigned char { live, decodes, map, hunter, heard, setup_ft8, setup_ft4, setup_js8, count };

struct Page {
  const char* title;
  const char* const* lines;
  size_t line_count;
};

Page page(Topic topic);

}  // namespace orcsdr::ft8::info
