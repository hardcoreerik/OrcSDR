#pragma once

#include "orcsdr_storage.hpp"
#include <M5GFX.h>

#include <cstddef>
#include <cstdint>

namespace orcsdr::ui_capture {

struct Result {
  bool ok = false;
  size_t bytes = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t sha256[32]{};
  const char* error = nullptr;
};

bool valid_slug(const char* slug);
Result save_bmp(M5GFX& display, orcsdr::storage::FileSystem& filesystem, const char* slug);

}  // namespace orcsdr::ui_capture
