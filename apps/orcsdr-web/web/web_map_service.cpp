// Browser implementation of the map service interface (apps/orcsdr-tab5/ui/map_service.hpp).
// The device runs a worker task that renders into a private PSRAM sprite; a page has one thread, so this renders synchronously
// in draw() and caches the finished frame until the camera changes. It uses the same OrcMaps pipeline and renderer as the device.
// Map data: one PMTiles archive in the page's virtual filesystem at kArchive (nothing is bundled and nothing is downloaded here;
// without the file every map shows "NO BASEMAP - COORDINATES ONLY", exactly as a unit with no map pack does).
#include "map_service.hpp"

#include <M5GFX.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

#include "orcmap/esp_idf/file_byte_source.hpp"
#include "orcmap/m5gfx/display_target.hpp"

namespace orcsdr::maps {
namespace {
constexpr const char* kArchive = "/orcmaps/browser.pmtiles";
const char* const kProfiles[] = {"openmaptiles-3.16", "protomaps-v4", "orcmaps-overview-1"};

bool g_begun = false, g_installed = false;
uint32_t g_revision = 1, g_frame_revision = 0;
std::unique_ptr<orcmap::esp_idf::FileByteSource> g_source;
std::unique_ptr<Pipeline> g_pipeline;
M5Canvas g_canvas(nullptr);
orcmap::Viewport g_drawn{};
bool g_have_frame = false;
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
    copy(pack.identity, sizeof(pack.identity), "browser-archive");
    copy(pack.title, sizeof(pack.title), "Map file loaded in this page");
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
  FrameInfo local;
  local.state = Frame::unavailable;
  if (!g_installed || !g_pipeline || view.width_px <= 0 || view.height_px <= 0 || view.width_px > 1280 || view.height_px > 720) {
    if (info) *info = local;
    return Frame::unavailable;
  }
  if (!g_have_frame || !same_view(view, g_drawn) || g_canvas.width() != view.width_px || g_canvas.height() != view.height_px) {
    g_canvas.deleteSprite();
    g_canvas.setColorDepth(16);
    if (!g_canvas.createSprite(view.width_px, view.height_px)) { if (info) *info = local; return Frame::unavailable; }
    orcmap::Viewport source = view;
    bool ok = pack_source_view(view, g_pipeline->header().min_zoom, g_pipeline->header().max_zoom, &source);
    if (ok) {
      orcmap::m5gfx_adapter::DisplayTarget render_target(g_canvas);
      ok = g_pipeline->render(source, render_target, nullptr, nullptr, view.zoom);
    }
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
  if (g_info.state == Frame::ready) g_canvas.pushSprite(&target, x, y);
  if (info) *info = g_info;
  return g_info.state;
}
bool frame_info(FrameInfo* out) {
  if (!out || !g_have_frame) return false;
  *out = g_info;
  return true;
}
void report(void (*write)(const char*)) {
  if (write) write(g_installed ? "ORC_MAP browser archive loaded\n" : "ORC_MAP browser archive absent\n");
}
}  // namespace orcsdr::maps

#include <emscripten.h>
// Called by the page after it has written a basemap into the virtual filesystem (app.js, MapSync).
extern "C" EMSCRIPTEN_KEEPALIVE void m5_maps_rescan() { orcsdr::maps::rescan(); }
