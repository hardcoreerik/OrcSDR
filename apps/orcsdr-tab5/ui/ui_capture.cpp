#include "ui_capture.hpp"

#include <mbedtls/sha256.h>

#include <cstdlib>
#include <cstring>

namespace orcsdr::ui_capture {
namespace {

constexpr char kDirectory[] = "/orcsdr/screenshots";
constexpr size_t kRowsPerRead = 8;

constexpr uint16_t from_swap565(uint16_t value) {
  return static_cast<uint16_t>((value << 8) | (value >> 8));
}
static_assert(from_swap565(0xff05) == 0x05ff);

void put_u16(uint8_t* out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8);
}

void put_u32(uint8_t* out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8);
  out[2] = static_cast<uint8_t>(value >> 16);
  out[3] = static_cast<uint8_t>(value >> 24);
}

bool write_hashed(File& file, mbedtls_sha256_context& sha, const void* data,
                  size_t size) {
  if (file.write(static_cast<const uint8_t*>(data), size) != size) return false;
  return mbedtls_sha256_update(&sha, static_cast<const uint8_t*>(data), size) == 0;
}

}  // namespace

bool valid_slug(const char* slug) {
  if (!slug || !*slug || strlen(slug) > 63) return false;
  for (const char* p = slug; *p; ++p) {
    const bool valid = (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
                       *p == '-' || *p == '_';
    if (!valid) return false;
  }
  return true;
}

Result save_bmp(M5GFX& display, fs::FS& filesystem, const char* slug) {
  Result result{};
  result.width = static_cast<uint16_t>(display.width());
  result.height = static_cast<uint16_t>(display.height());
  if (!valid_slug(slug)) {
    result.error = "invalid_slug";
    return result;
  }
  if (result.width != 1280 || result.height != 720) {
    result.error = "unexpected_dimensions";
    return result;
  }

  filesystem.mkdir("/orcsdr");
  filesystem.mkdir(kDirectory);
  char path[112];
  snprintf(path, sizeof(path), "%s/%s.bmp", kDirectory, slug);
  filesystem.remove(path);
  File file = filesystem.open(path, FILE_WRITE, true);
  if (!file) file = filesystem.open(path, FILE_WRITE);
  if (!file) {
    snprintf(path, sizeof(path), "/orcsdr/%s.bmp", slug);
    filesystem.remove(path);
    file = filesystem.open(path, FILE_WRITE, true);
    if (!file) file = filesystem.open(path, FILE_WRITE);
  }
  if (!file) {
    result.error = "open_failed";
    return result;
  }

  const size_t pixel_count = static_cast<size_t>(result.width) * kRowsPerRead;
  auto* pixels = static_cast<uint16_t*>(malloc(pixel_count * sizeof(uint16_t)));
  auto* bgr = static_cast<uint8_t*>(malloc(static_cast<size_t>(result.width) * 3));
  if (!pixels || !bgr) {
    free(pixels);
    free(bgr);
    file.close();
    filesystem.remove(path);
    result.error = "allocation_failed";
    return result;
  }

  constexpr size_t kHeaderSize = 54;
  const uint32_t image_bytes = static_cast<uint32_t>(result.width) * result.height * 3u;
  uint8_t header[kHeaderSize]{};
  header[0] = 'B';
  header[1] = 'M';
  put_u32(header + 2, kHeaderSize + image_bytes);
  put_u32(header + 10, kHeaderSize);
  put_u32(header + 14, 40);
  put_u32(header + 18, result.width);
  put_u32(header + 22, result.height);
  put_u16(header + 26, 1);
  put_u16(header + 28, 24);
  put_u32(header + 34, image_bytes);

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  bool ok = mbedtls_sha256_starts(&sha, 0) == 0 &&
            write_hashed(file, sha, header, sizeof(header));
  for (int bottom = result.height; ok && bottom > 0; bottom -= kRowsPerRead) {
    const int rows = bottom < static_cast<int>(kRowsPerRead) ? bottom : kRowsPerRead;
    const int top = bottom - rows;
    display.readRect(0, top, result.width, rows, pixels);
    for (int row = rows - 1; ok && row >= 0; --row) {
      const uint16_t* source = pixels + static_cast<size_t>(row) * result.width;
      for (size_t x = 0; x < result.width; ++x) {
        const uint16_t rgb565 = from_swap565(source[x]);
        bgr[x * 3] = static_cast<uint8_t>((rgb565 & 0x1f) * 255 / 31);
        bgr[x * 3 + 1] = static_cast<uint8_t>(((rgb565 >> 5) & 0x3f) * 255 / 63);
        bgr[x * 3 + 2] = static_cast<uint8_t>(((rgb565 >> 11) & 0x1f) * 255 / 31);
      }
      ok = write_hashed(file, sha, bgr, static_cast<size_t>(result.width) * 3);
    }
  }
  if (ok) ok = mbedtls_sha256_finish(&sha, result.sha256) == 0;
  mbedtls_sha256_free(&sha);
  free(pixels);
  free(bgr);
  file.flush();
  result.bytes = file.size();
  file.close();
  result.ok = ok && result.bytes == kHeaderSize + image_bytes;
  if (!result.ok) {
    filesystem.remove(path);
    result.error = ok ? "size_mismatch" : "write_failed";
  }
  return result;
}

}  // namespace orcsdr::ui_capture
