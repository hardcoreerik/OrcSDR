#include "offline_map.hpp"

#include <M5Unified.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace orcsdr::offline_map {
namespace {

constexpr size_t kSegmentCapacity = 640;
enum class Kind : uint8_t { road, water, airport };
struct Segment { float lat1, lon1, lat2, lon2; Kind kind; };
Segment g_segments[kSegmentCapacity]{};
size_t g_count = 0;
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

}  // namespace

bool project(const View& view, float latitude, float longitude, int* x, int* y) {
  if (!x || !y || view.width <= 0 || view.height <= 0 || view.range_nm <= 0.0f) return false;
  constexpr float kNmPerDegree = 60.0f;
  const float lon_scale = std::fmax(0.1f, std::cos(view.center_lat * DEG_TO_RAD));
  const float east_nm = (longitude - view.center_lon) * lon_scale * kNmPerDegree;
  const float north_nm = (latitude - view.center_lat) * kNmPerDegree;
  *x = view.x + view.width / 2 + static_cast<int>(east_nm * (view.width / 2.0f) / view.range_nm);
  *y = view.y + view.height / 2 - static_cast<int>(north_nm * (view.height / 2.0f) / view.range_nm);
  return *x >= view.x && *x < view.x + view.width && *y >= view.y && *y < view.y + view.height;
}

bool load(fs::FS* filesystem) {
  g_count = 0;
  g_available = false;
  if (!filesystem) return false;
  File file = filesystem->open(kRuntimePath, FILE_READ);
  if (!file) return false;
  char header[9]{};
  const bool header_ok = file.readBytesUntil('\n', header, sizeof(header)) == 7 &&
                         strcmp(header, "ORCMAP1") == 0;
  char line[96]{};
  while (header_ok && file.available() && g_count < kSegmentCapacity) {
    const size_t used = file.readBytesUntil('\n', line, sizeof(line) - 1);
    line[used] = '\0';
    Segment segment{};
    if (parse_line(line, &segment)) g_segments[g_count++] = segment;
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
    const bool first_visible = project(view, g_segments[i].lat1, g_segments[i].lon1, &x1, &y1);
    const bool second_visible = project(view, g_segments[i].lat2, g_segments[i].lon2, &x2, &y2);
    if (!first_visible || !second_visible) continue;
    const uint16_t color = g_segments[i].kind == Kind::water ? water_color :
                           g_segments[i].kind == Kind::airport ? airport_color : road_color;
    M5.Display.drawLine(x1, y1, x2, y2, color);
  }
}

bool self_check() {
  View view{44.0f, -123.0f, 25.0f, 0, 0, 400, 400};
  int x = 0, y = 0;
  return project(view, 44.0f, -123.0f, &x, &y) && x == 200 && y == 200 &&
         !project(view, 0.0f, 0.0f, &x, &y);
}

}  // namespace orcsdr::offline_map
