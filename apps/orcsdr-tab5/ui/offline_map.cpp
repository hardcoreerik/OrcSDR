#include "offline_map.hpp"

#include <M5Unified.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace orcsdr::offline_map {
namespace {

constexpr size_t kSegmentCapacity = 640;
constexpr size_t kLabelCapacity = 32;
enum class Kind : uint8_t { road, water, airport };
struct Segment { float lat1, lon1, lat2, lon2; Kind kind; };
struct Label { float lat, lon; char text[24]; };
EXT_RAM_BSS_ATTR Segment g_segments[kSegmentCapacity]{};
EXT_RAM_BSS_ATTR Label g_labels[kLabelCapacity]{};
size_t g_count = 0;
size_t g_label_count = 0;
bool g_available = false;

bool parse_line(const char* line, Segment* output) {
  if (!line || !output) return false;
  char kind = 0;
  Segment segment{};
  if (sscanf(line, "%c %f %f %f %f", &kind, &segment.lat1, &segment.lon1,
             &segment.lat2, &segment.lon2) != 5) return false;
  if (segment.lat1 < -90 || segment.lat1 > 90 || segment.lat2 < -90 || segment.lat2 > 90 ||
      segment.lon1 < -180 || segment.lon1 > 180 || segment.lon2 < -180 || segment.lon2 > 180)
    return false;
  if (kind == 'R') segment.kind = Kind::road;
  else if (kind == 'W') segment.kind = Kind::water;
  else if (kind == 'A') segment.kind = Kind::airport;
  else return false;
  *output = segment;
  return true;
}

bool parse_label(const char* line, Label* output) {
  if (!line || !output) return false;
  Label label{};
  if (sscanf(line, "L %f %f %23[^\n]", &label.lat, &label.lon, label.text) != 3 ||
      label.lat < -90 || label.lat > 90 || label.lon < -180 || label.lon > 180 || !label.text[0])
    return false;
  *output = label;
  return true;
}

void project_unclipped(const View& view, float latitude, float longitude, int* x, int* y) {
  constexpr float kNmPerDegree = 60.0f;
  const float lon_scale = std::fmax(0.1f, std::cos(view.center_lat * DEG_TO_RAD));
  const float east_nm = (longitude - view.center_lon) * lon_scale * kNmPerDegree;
  const float north_nm = (latitude - view.center_lat) * kNmPerDegree;
  *x = view.x + view.width / 2 + static_cast<int>(east_nm * (view.width / 2.0f) / view.range_nm);
  *y = view.y + view.height / 2 - static_cast<int>(north_nm * (view.height / 2.0f) / view.range_nm);
}

uint8_t clip_code(const View& view, int x, int y) {
  uint8_t code = 0;
  if (x < view.x) code |= 1;
  else if (x >= view.x + view.width) code |= 2;
  if (y < view.y) code |= 4;
  else if (y >= view.y + view.height) code |= 8;
  return code;
}

bool clip_line(const View& view, int* x1, int* y1, int* x2, int* y2) {
  if (!x1 || !y1 || !x2 || !y2) return false;
  const int left = view.x, right = view.x + view.width - 1;
  const int top = view.y, bottom = view.y + view.height - 1;
  while (true) {
    const uint8_t first = clip_code(view, *x1, *y1);
    const uint8_t second = clip_code(view, *x2, *y2);
    if (!(first | second)) return true;
    if (first & second) return false;
    const uint8_t outside = first ? first : second;
    int x = 0, y = 0;
    if (outside & 8) { y = bottom; x = *x1 + (*x2 - *x1) * (bottom - *y1) / (*y2 - *y1); }
    else if (outside & 4) { y = top; x = *x1 + (*x2 - *x1) * (top - *y1) / (*y2 - *y1); }
    else if (outside & 2) { x = right; y = *y1 + (*y2 - *y1) * (right - *x1) / (*x2 - *x1); }
    else { x = left; y = *y1 + (*y2 - *y1) * (left - *x1) / (*x2 - *x1); }
    if (outside == first) { *x1 = x; *y1 = y; }
    else { *x2 = x; *y2 = y; }
  }
}

}  // namespace

bool project(const View& view, float latitude, float longitude, int* x, int* y) {
  if (!x || !y || view.width <= 0 || view.height <= 0 || view.range_nm <= 0.0f) return false;
  project_unclipped(view, latitude, longitude, x, y);
  return *x >= view.x && *x < view.x + view.width && *y >= view.y && *y < view.y + view.height;
}

bool load(orcsdr::storage::FileSystem* filesystem) {
  g_count = 0;
  g_label_count = 0;
  g_available = false;
  if (!filesystem) return false;
  orcsdr::storage::File file = filesystem->open(kRuntimePath);
  if (!file) return false;
  char header[9]{};
  const bool header_ok = file.readBytesUntil('\n', header, sizeof(header)) == 7 &&
                         strcmp(header, "ORCMAP1") == 0;
  char line[96]{};
  while (header_ok && file.available()) {
    const size_t used = file.readBytesUntil('\n', line, sizeof(line) - 1);
    line[used] = '\0';
    if (line[0] == 'L' && g_label_count < kLabelCapacity) {
      Label label{};
      if (parse_label(line, &label)) g_labels[g_label_count++] = label;
    } else if (g_count < kSegmentCapacity) {
      Segment segment{};
      if (parse_line(line, &segment)) g_segments[g_count++] = segment;
    }
  }
  file.close();
  g_available = header_ok && g_count > 0;
  return g_available;
}

bool available() { return g_available; }

void draw_base(const View& view, uint16_t water_color, uint16_t road_color,
               uint16_t airport_color, uint16_t border_color) {
  M5.Display.drawRect(view.x, view.y, view.width, view.height, border_color);
  if (!g_available) return;
  for (size_t i = 0; i < g_count; ++i) {
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    project_unclipped(view, g_segments[i].lat1, g_segments[i].lon1, &x1, &y1);
    project_unclipped(view, g_segments[i].lat2, g_segments[i].lon2, &x2, &y2);
    if (!clip_line(view, &x1, &y1, &x2, &y2)) continue;
    const uint16_t color = g_segments[i].kind == Kind::water ? water_color :
                           g_segments[i].kind == Kind::airport ? airport_color : road_color;
    M5.Display.drawLine(x1, y1, x2, y2, color);
  }
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(airport_color);
  for (size_t i = 0; i < g_label_count; ++i) {
    int x = 0, y = 0;
    if (project(view, g_labels[i].lat, g_labels[i].lon, &x, &y))
      M5.Display.drawString(g_labels[i].text, x + 3, y);
  }
}

bool self_check() {
  View view{44.0f, -123.0f, 25.0f, 0, 0, 400, 400};
  int x = 0, y = 0;
  int x1 = -100, y1 = 200, x2 = 500, y2 = 200;
  return project(view, 44.0f, -123.0f, &x, &y) && x == 200 && y == 200 &&
         !project(view, 0.0f, 0.0f, &x, &y) &&
         clip_line(view, &x1, &y1, &x2, &y2) && x1 == 0 && x2 == 399;
}

}  // namespace orcsdr::offline_map
