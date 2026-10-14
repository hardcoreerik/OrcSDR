#pragma once
// Read-only export of the newest finished SSTV picture for the web console's GET /api/sstv and GET /api/sstv/preview.
//
// Two parts. The first is pure: the 64-pixel-wide preview downscale, the 24-bit BMP encoder and the JSON formatter (no httpd,
// no display, host-tested). The second is a double-buffered snapshot: the UI task renders a snapshot into the back buffer and
// flips an index under a short mutex; the httpd task only copies the front buffer out under the same mutex and then sends. The
// HTTP side never touches the receiver's gallery or any picture pixels.
#include <cstddef>
#include <cstdint>

namespace orcsdr::sstv_export {

constexpr uint16_t kPreviewWidth = 64;
constexpr uint16_t kPreviewMaxHeight = 64;
constexpr size_t kBmpHeaderBytes = 54;
constexpr size_t kMaxBmpBytes = kBmpHeaderBytes + static_cast<size_t>(kPreviewWidth) * kPreviewMaxHeight * 3;   // 64 * 64 * 3 + 54
constexpr size_t kJsonCapacity = 768;

// clamp(round(decoded_rows * 64 / width), 1, 64); 0 for a picture with no width or no decoded rows.
uint16_t preview_height(uint16_t width, uint16_t decoded_rows);

// 64-pixel-wide, 24-bit, bottom-up BMP of the first decoded_rows rows of an RGB565 picture (a box filter over the source cells
// that each preview pixel covers; rows at and beyond decoded_rows are never read). Returns the byte count, or 0 when there is no
// picture or the buffer is too small. The preview height is returned through preview_height_out.
size_t encode_preview_bmp(const uint16_t* pixels, uint16_t width, uint16_t height, uint16_t decoded_rows, uint8_t* out,
                          size_t capacity, uint16_t* preview_height_out);

struct Meta {
  uint32_t serial = 0;
  uint32_t session_id = 0;
  uint32_t frequency_hz = 0;
  uint32_t utc_epoch = 0;          // 0 when the clock was not verified
  uint16_t width = 0, height = 0, rows = 0;
  uint16_t preview_height = 0;
  uint16_t sync_locked = 0, sync_missed = 0;
  uint8_t vis_code = 0;
  bool partial = false;
  bool start_unknown = false;
  char mode[24]{};
  char identification[40]{};
  char end[24]{};
  char filename[64]{};             // the SD name once the save has finished, otherwise empty
};

// {"none":true} for a null meta, otherwise the full object. Strings are escaped the way /api/status escapes them. Returns the
// length written (always NUL-terminated), or 0 when the buffer is too small.
size_t format_json(const Meta* meta, char* out, size_t capacity);

// ---- publisher ----
void init();                       // once, before use (creates the mutex); safe to call again
bool ready();
uint8_t* back_bmp();               // UI task: the buffer to encode into (kMaxBmpBytes); null before init()
bool publish(const Meta& meta, size_t bmp_bytes);   // UI task: the back buffer now holds a finished BMP of bmp_bytes
void publish_empty();              // UI task: the gallery is empty
bool published_capture(uint32_t* serial, char* filename, size_t filename_size);   // UI task: what is published now (false = empty)

// ---- httpd side: copies out under the mutex; never touches the gallery ----
size_t read_json(char* out, size_t capacity);      // always something: {"none":true} before anything is published
size_t read_bmp(uint8_t* out, size_t capacity);    // 0 when there is no capture

bool self_check();

}  // namespace orcsdr::sstv_export
