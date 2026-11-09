// OrcSDR P4 Core implementation of the shared map service interface (apps/orcsdr-tab5/ui/map_service.hpp).
//
// Why this is not the Tab5's map_service.cpp: the Tab5 service renders into an M5GFX canvas and pushes it to the panel. On a P4
// board every screen is drawn in the Tab5's 1280x720 design units and scaled to the panel by the scaled front end (orcsdr_gfx), and
// the map callers hand over the panel-resolution target. So this service renders each frame at design size with the same OrcMaps
// pipeline and renderer, scales it to the panel while copying, and tells the front end which pixels changed.
//
// State of this file (honest): it is synchronous. A render happens on the UI task when the camera changes and the frame is then
// cached, so panning blocks the UI for the render time (not yet measured on hardware). The Tab5's background worker, preview and
// download pipeline are not ported; downloads are therefore inert here. Map data is one PMTiles archive read from SD at kArchive;
// without it every map shows "NO BASEMAP - COORDINATES ONLY", as on a unit with no map pack.
#include "map_service.hpp"

#include <M5GFX.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

#include "orcmap/esp_idf/file_byte_source.hpp"
#include "orcmap/m5gfx/display_target.hpp"

// Answered by map_service_p4_display.cpp, which sees the scaled front end; this file sees the real M5GFX types (the two cannot share
// a translation unit because both define M5Canvas).
namespace orcsdr::p4_display {
int panel_width();
void mark_dirty(int x0, int y0, int x1, int y1);
}  // namespace orcsdr::p4_display

namespace orcsdr::maps {
namespace {
constexpr int kDesignWidth = 1280;
constexpr const char* kArchive = "/sd/orcmaps/basemap.pmtiles";
const char* const kProfiles[] = {"openmaptiles-3.16", "protomaps-v4", "orcmaps-overview-1"};

bool g_begun = false, g_installed = false, g_have_frame = false;
uint32_t g_revision = 1, g_frame_revision = 0;
std::unique_ptr<orcmap::esp_idf::FileByteSource> g_source;
std::unique_ptr<Pipeline> g_pipeline;
lgfx::LGFX_Sprite g_canvas;
orcmap::Viewport g_drawn{};
FrameInfo g_info;

void copy(char* out, size_t size, const char* text) { std::snprintf(out, size, "%s", text ? text : ""); }

bool open_archive() {
  g_pipeline.reset();
  g_source.reset(new (std::nothrow) orcmap::esp_idf::FileByteSource(kArchive));
  if (!g_source || !g_source->Valid()) { g_source.reset(); return false; }
  for (const char* profile : kProfiles) {
    g_pipeline.reset(new (std::nothrow) Pipeline(*g_source));
    if (g_pipeline && g_pipeline->open(profile)) return true;
  }
  g_pipeline.reset();
  g_source.reset();
  return false;
}
bool same_view(const orcmap::Viewport& a, const orcmap::Viewport& b) {
  return a.center_lat_deg == b.center_lat_deg && a.center_lon_deg == b.center_lon_deg && a.zoom == b.zoom &&
         a.tile_size_px == b.tile_size_px && a.width_px == b.width_px && a.height_px == b.height_px;
}
// Copies the design-size frame into the panel-resolution target, nearest neighbour, and marks the changed rectangle.
void blit_scaled(lgfx::v1::LovyanGFX& target, int x, int y, int width, int height) {
  const float scale = static_cast<float>(p4_display::panel_width()) / kDesignWidth;
  const int dx0 = static_cast<int>(x * scale), dy0 = static_cast<int>(y * scale);
  const int dw = static_cast<int>(width * scale), dh = static_cast<int>(height * scale);
  if (dw <= 0 || dh <= 0) return;
  const auto* source = static_cast<const uint16_t*>(g_canvas.getBuffer());
  if (!source) return;
  std::unique_ptr<uint16_t[]> row(new (std::nothrow) uint16_t[dw]);
  if (!row) return;
  for (int j = 0; j < dh; ++j) {
    const int sy = static_cast<int>(j / scale) < height ? static_cast<int>(j / scale) : height - 1;
    const uint16_t* source_row = source + static_cast<size_t>(sy) * width;
    for (int i = 0; i < dw; ++i) {
      const int sx = static_cast<int>(i / scale);
      row[i] = source_row[sx < width ? sx : width - 1];
    }
    target.pushImage(dx0, dy0 + j, dw, 1, row.get());
  }
  p4_display::mark_dirty(dx0, dy0, dx0 + dw, dy0 + dh);
}
}  // namespace

bool begin(storage::FileSystem*) {
  g_begun = true;
  rescan();
  return true;
}
void service_downloads() {}
bool running() { return g_begun; }
bool snapshot(MapSnapshot* out) {
  if (!out) return false;
  *out = MapSnapshot{};
  out->loaded = true;
  out->revision = g_revision;
  if (g_installed) {
    auto& pack = out->packs[out->count++];
    pack.kind = Kind::pmtiles;
    copy(pack.identity, sizeof(pack.identity), "sd-basemap");
    copy(pack.title, sizeof(pack.title), "Basemap on SD card");
    copy(pack.status, sizeof(pack.status), "LOADED; HASH UNCHECKED");
    pack.usable = true;
    if (g_source) pack.bytes = g_source->Size();
  }
  return true;
}
void rescan() {
  g_have_frame = false;
  g_installed = open_archive();
  invalidate();
}
void cancel() {}
void invalidate() { ++g_revision; }
uint32_t revision() { return g_revision; }
bool installed() { return g_installed; }

Frame draw(const orcmap::Viewport& view, lgfx::v1::LovyanGFX& target, int x, int y, FrameInfo* info, const char*, bool) {
  FrameInfo none;
  none.state = Frame::unavailable;
  if (!g_installed || !g_pipeline || view.width_px <= 0 || view.height_px <= 0 || view.width_px > 1280 || view.height_px > 720) {
    if (info) *info = none;
    return Frame::unavailable;
  }
  if (!g_have_frame || !same_view(view, g_drawn) || g_canvas.width() != view.width_px || g_canvas.height() != view.height_px) {
    g_canvas.deleteSprite();
    g_canvas.setPsram(true);
    g_canvas.setColorDepth(16);
    if (!g_canvas.createSprite(view.width_px, view.height_px)) { if (info) *info = none; return Frame::unavailable; }
    orcmap::Viewport source = view;
    bool ok = pack_source_view(view, g_pipeline->header().min_zoom, g_pipeline->header().max_zoom, &source);
    try {
      if (ok) {
        orcmap::m5gfx_adapter::DisplayTarget render_target(g_canvas);
        ok = g_pipeline->render(source, render_target, nullptr, nullptr, view.zoom);
      }
    } catch (const std::bad_alloc&) { ok = false; }
    g_info = FrameInfo{};
    g_info.stats = g_pipeline->stats();
    g_info.camera_zoom = view.zoom;
    g_info.source_zoom = source.zoom;
    g_info.state = ok ? Frame::ready : Frame::unavailable;
    copy(g_info.status, sizeof(g_info.status), ok ? "LOCAL MAP" : "MAP UNAVAILABLE");
    g_info.revision = ++g_frame_revision;
    g_drawn = view;
    g_have_frame = true;
  }
  if (g_info.state == Frame::ready) blit_scaled(target, x, y, view.width_px, view.height_px);
  if (info) *info = g_info;
  return g_info.state;
}
bool frame_info(FrameInfo* out) {
  if (!out || !g_have_frame) return false;
  *out = g_info;
  return true;
}
void report(void (*write)(const char*)) {
  if (write) write(g_installed ? "ORC_MAP P4 archive loaded\n" : "ORC_MAP P4 archive absent\n");
}
}  // namespace orcsdr::maps

// The shared dashboards redraw the Tab5's map-download notice through this call. Downloads run only on the Tab5 for now, so a P4
// board has no notice to redraw.
namespace orcsdr::map_download_notice {
void invalidate() {}
}  // namespace orcsdr::map_download_notice
