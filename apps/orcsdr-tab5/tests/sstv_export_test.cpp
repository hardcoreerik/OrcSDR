// Host test for the read-only SSTV export: the 64-pixel preview BMP, the JSON, and the double-buffered snapshot the web console
// reads. No hardware, dongle or SD card.
#include "sstv_export.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace orcsdr::sstv_export;

namespace {
void require(bool pass, const char* why) {
  if (!pass) {
    std::fprintf(stderr, "SSTV FAIL: %s\n", why);
    std::exit(1);
  }
}
unsigned u16(const uint8_t* p) { return p[0] | (p[1] << 8); }
unsigned u32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<unsigned>(p[3]) << 24); }
constexpr uint16_t kRed = 0xF800, kGreen = 0x07E0, kBlue = 0x001F;
// The expansion the archive writer uses: B = (c & 31) << 3, G = ((c >> 5) & 63) << 2, R = ((c >> 11) & 31) << 3.
void expected(uint16_t c, unsigned* r, unsigned* g, unsigned* b) {
  *b = (c & 31u) << 3;
  *g = ((c >> 5) & 63u) << 2;
  *r = ((c >> 11) & 31u) << 3;
}
const uint8_t* pixel(const std::vector<uint8_t>& bmp, unsigned x, unsigned y_from_top, unsigned height) {
  const unsigned stored_row = height - 1 - y_from_top;   // bottom-up
  return bmp.data() + kBmpHeaderBytes + stored_row * 192u + x * 3u;
}
}  // namespace

int main() {
  std::vector<uint8_t> bmp(kMaxBmpBytes);
  uint16_t ph = 0;

  // ---- empty input produces no bitmap
  require(encode_preview_bmp(nullptr, 320, 240, 240, bmp.data(), bmp.size(), &ph) == 0 && ph == 0, "export: no picture, no bitmap");
  std::vector<uint16_t> solid(320 * 240, kRed);
  require(encode_preview_bmp(solid.data(), 320, 240, 0, bmp.data(), bmp.size(), &ph) == 0, "export: no decoded rows, no bitmap");
  require(encode_preview_bmp(solid.data(), 0, 240, 240, bmp.data(), bmp.size(), &ph) == 0, "export: zero width, no bitmap");
  require(encode_preview_bmp(solid.data(), 320, 240, 240, bmp.data(), 100, &ph) == 0, "export: a buffer that is too small is refused");

  // ---- a 320x240 solid colour becomes a 64x48 24-bit BMP whose centre pixel is the expanded colour
  for (uint16_t colour : {kRed, kGreen, kBlue, static_cast<uint16_t>(0x8410)}) {
    std::fill(solid.begin(), solid.end(), colour);
    const size_t n = encode_preview_bmp(solid.data(), 320, 240, 240, bmp.data(), bmp.size(), &ph);
    require(n == kBmpHeaderBytes + 192u * 48u && ph == 48, "export: 320x240 gives a 64x48 bitmap");
    require(n < 13 * 1024, "export: under 13 KB");
    unsigned r, g, b;
    expected(colour, &r, &g, &b);
    const uint8_t* centre = pixel(bmp, 32, 24, 48);
    require(centre[0] == b && centre[1] == g && centre[2] == r, "export: the centre pixel is the expanded colour (stored B, G, R)");
  }

  // ---- header: BM, 54-byte offset and 40-byte DIB, 24-bit, width 64, height = the preview height
  std::fill(solid.begin(), solid.end(), kRed);
  const size_t n = encode_preview_bmp(solid.data(), 320, 240, 240, bmp.data(), bmp.size(), &ph);
  require(bmp[0] == 'B' && bmp[1] == 'M', "export: BM signature");
  require(u32(&bmp[2]) == n && u32(&bmp[10]) == 54 && u32(&bmp[14]) == 40, "export: file size, 54-byte data offset, 40-byte DIB");
  require(u32(&bmp[18]) == 64 && u32(&bmp[22]) == ph && u16(&bmp[26]) == 1 && u16(&bmp[28]) == 24 && u32(&bmp[30]) == 0,
          "export: width 64, positive (bottom-up) height, 24 bits, uncompressed");
  require(u32(&bmp[34]) == 192u * ph, "export: stride 192, no row padding");

  // ---- decoded_rows of 120 does not read rows 120..239 (poisoned with green; the preview must be all red and 24 rows tall)
  std::fill(solid.begin(), solid.begin() + 320 * 120, kRed);
  std::fill(solid.begin() + 320 * 120, solid.end(), kGreen);
  const size_t half = encode_preview_bmp(solid.data(), 320, 240, 120, bmp.data(), bmp.size(), &ph);
  require(ph == 24 && half == kBmpHeaderBytes + 192u * 24u, "export: a half-decoded picture is half as tall");
  bool saw_green = false;
  for (size_t i = kBmpHeaderBytes; i < half; i += 3)
    if (bmp[i + 1] != 0) saw_green = true;   // the G channel of a pure red picture is 0
  require(!saw_green, "export: rows beyond decoded_rows are never read");

  // ---- bottom-up orientation: red on top, blue below -> the first stored row is blue, the last is red
  std::fill(solid.begin(), solid.begin() + 320 * 120, kRed);
  std::fill(solid.begin() + 320 * 120, solid.end(), kBlue);
  encode_preview_bmp(solid.data(), 320, 240, 240, bmp.data(), bmp.size(), &ph);
  require(bmp[kBmpHeaderBytes + 0] == 0xF8 && bmp[kBmpHeaderBytes + 2] == 0, "export: the first stored row is the bottom (blue)");
  require(bmp[kBmpHeaderBytes + 192u * (ph - 1) + 2] == 0xF8, "export: the last stored row is the top (red)");

  // ---- preview height arithmetic
  require(preview_height(320, 240) == 48 && preview_height(320, 1) == 1 && preview_height(320, 0) == 0, "export: preview heights");
  require(preview_height(160, 240) == 64 && preview_height(640, 496) == 50 && preview_height(100, 240) == 64, "export: clamped to 1..64");

  // ---- worst case fits the fixed buffers
  std::vector<uint16_t> tall(64 * 256, kGreen);
  const size_t worst = encode_preview_bmp(tall.data(), 64, 256, 256, bmp.data(), bmp.size(), &ph);
  require(ph == 64 && worst == kMaxBmpBytes && worst < 13 * 1024, "export: the largest bitmap is 64x64");

  // ---- JSON
  char json[kJsonCapacity];
  require(format_json(nullptr, json, sizeof(json)) > 0 && std::string(json) == "{\"none\":true}", "export: the empty state");
  Meta m;
  m.serial = 7; m.session_id = 9; m.frequency_hz = 145500000; m.utc_epoch = 0;
  m.width = 320; m.height = 240; m.rows = 240; m.preview_height = 48; m.sync_locked = 240; m.sync_missed = 0; m.vis_code = 8;
  std::snprintf(m.mode, sizeof(m.mode), "ROBOT 36");
  std::snprintf(m.identification, sizeof(m.identification), "VIS 8");
  std::snprintf(m.end, sizeof(m.end), "COMPLETE");
  std::snprintf(m.filename, sizeof(m.filename), "he said \"hi\"\\x.bmp");
  require(format_json(&m, json, sizeof(json)) > 0, "export: formats");
  const std::string text = json;
  require(text.find("\"none\":false") != std::string::npos && text.find("\"serial\":7") != std::string::npos &&
          text.find("\"frequency_hz\":145500000") != std::string::npos && text.find("\"mode\":\"ROBOT 36\"") != std::string::npos &&
          text.find("\"partial\":false") != std::string::npos && text.find("\"start_unknown\":false") != std::string::npos &&
          text.find("\"preview_width\":64") != std::string::npos && text.find("\"preview_height\":48") != std::string::npos &&
          text.find("\"identification\":\"VIS 8\"") != std::string::npos && text.find("\"end\":\"COMPLETE\"") != std::string::npos,
          "export: every field is present");
  require(text.find("he said \\\"hi\\\"\\\\x.bmp") != std::string::npos, "export: strings are escaped");
  {  // a name that fills the escape buffer must never leave a bare quote or backslash in the document
    Meta long_name = m;
    std::string rough(100, '\\');
    rough += std::string(40, '"');
    std::snprintf(long_name.filename, sizeof(long_name.filename), "%s", rough.c_str());
    char big[kJsonCapacity];
    require(format_json(&long_name, big, sizeof(big)) > 0, "export: a rough name still formats");
    const std::string doc = big;
    const size_t key = doc.find("\"filename\":\"");
    const size_t at = key + 12;
    const size_t end = doc.find("\",\"preview_width\"");
    require(key != std::string::npos && end != std::string::npos && end > at, "export: the filename field is closed");
    bool ok = true;
    for (size_t i = at; i < end; ++i) {
      if (doc[i] == '\\') { ++i; if (i >= end) ok = false; }          // every backslash escapes the next character
      else if (doc[i] == '"') ok = false;                                // no bare quote inside the string
    }
    require(ok, "export: escapes inside a truncated field are whole pairs");
  }
  char tiny[8];
  require(format_json(&m, tiny, sizeof(tiny)) == 0, "export: a buffer that is too small is refused");

  // ---- the published snapshot: nothing before anything is published, a capture after, empty again after publish_empty
  char out[kJsonCapacity];
  std::vector<uint8_t> got(kMaxBmpBytes);
  require(read_json(out, sizeof(out)) > 0 && std::string(out) == "{\"none\":true}", "export: before init the state is empty");
  require(read_bmp(got.data(), got.size()) == 0, "export: before init there is no preview");
  init();
  require(read_bmp(got.data(), got.size()) == 0, "export: after init nothing is published yet");
  std::fill(solid.begin(), solid.end(), kRed);
  uint8_t* back = back_bmp();
  require(back != nullptr, "export: a back buffer exists");
  const size_t bytes = encode_preview_bmp(solid.data(), 320, 240, 240, back, kMaxBmpBytes, &ph);
  require(publish(m, bytes), "export: publishes");
  require(read_json(out, sizeof(out)) > 0 && std::string(out).find("\"serial\":7") != std::string::npos, "export: the JSON is served");
  require(read_bmp(got.data(), got.size()) == bytes, "export: the preview is served");
  require(got[0] == 'B' && got[1] == 'M', "export: the served preview is a BMP");
  require(read_bmp(got.data(), 100) == 0, "export: a small reader buffer gets nothing, not a truncated BMP");
  uint32_t serial = 0;
  char name[64];
  require(published_capture(&serial, name, sizeof(name)) && serial == 7, "export: the UI side can see what is published");
  // A second capture goes into the other buffer and replaces the first as a whole.
  Meta m2 = m;
  m2.serial = 8;
  std::fill(solid.begin(), solid.end(), kBlue);
  back = back_bmp();
  const size_t bytes2 = encode_preview_bmp(solid.data(), 320, 240, 240, back, kMaxBmpBytes, &ph);
  require(publish(m2, bytes2), "export: publishes the second capture");
  require(read_bmp(got.data(), got.size()) == bytes2 && got[kBmpHeaderBytes] == 0xF8, "export: the preview is the second capture (blue)");
  read_json(out, sizeof(out));
  require(std::string(out).find("\"serial\":8") != std::string::npos, "export: the JSON follows the preview");
  publish_empty();
  read_json(out, sizeof(out));
  require(std::string(out) == "{\"none\":true}" && read_bmp(got.data(), got.size()) == 0 && !published_capture(nullptr, nullptr, 0),
          "export: the gallery being cleared publishes the empty state");
  require(!publish(m, 10), "export: a bitmap that cannot be real is refused");
  require(self_check(), "export: self check");
  std::puts("SSTV export: PASS");
  return 0;
}
