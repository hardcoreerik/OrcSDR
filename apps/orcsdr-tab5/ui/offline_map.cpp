#include "offline_map.hpp"

#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "map_packs.hpp"
#include "map_sources.hpp"
#include "map_view_math.hpp"
#include "orcmap/esp_idf/file_byte_source.hpp"
#include "orcmap/experimental/mvt_classify.hpp"
#include "orcmap/feature.hpp"
#include "orcmap/m5gfx/display_target.hpp"
#include "orcmap/mvt_stream.hpp"
#include "orcmap/pmtiles.hpp"
#include "orcmap/renderer.hpp"
#include "orcmap/style.hpp"
#include "orcmap/viewport.hpp"

namespace orcsdr::offline_map {
namespace {

constexpr char kTag[] = "offline_map";

// One rendered map is kept, keyed by everything that changes it, so a screen that redraws often (the LoRa map) only
// pushes the finished picture.
M5Canvas g_cache(&M5.Display);
struct CacheKey {
  float lat = 0, lon = 0, range = 0, radius = 0;
  int width = 0, height = 0;
  uint32_t generation = 0;
  bool operator==(const CacheKey& other) const {
    return lat == other.lat && lon == other.lon && range == other.range && radius == other.radius &&
           width == other.width && height == other.height && generation == other.generation;
  }
} g_key;
bool g_cache_valid = false;
char g_label[64] = "";
char g_credit[64] = "";

float radius_of(const View& view) {
  return view.radius_px > 0.0f ? view.radius_px : std::min(view.width, view.height) / 2.0f;
}

struct RenderContext {
  const orcmap::TilePlacement* placement = nullptr;
  const orcmap::Viewport* viewport = nullptr;
  const orcmap::MapStyle* style = nullptr;
  orcmap::RenderTarget* target = nullptr;
};

bool IncludeBasemapLayer(const char* name, size_t name_len, void* ctx) {
  return orcmap::experimental::IncludeNoTextBasemapLayer(name, name_len, ctx);
}

// One feature at a time: the streaming decoder never materialises a whole tile.
bool DrawFeature(const orcmap::Feature& feature, void* ctx) {
  RenderContext& render = *static_cast<RenderContext*>(ctx);
  orcmap::Feature shaped = feature;
  orcmap::FeatureKind kind{};
  if (orcmap::experimental::TryClassifyFeature(shaped, &kind)) {
    shaped.kind = kind;
    shaped.kind_assigned = true;
  }
  orcmap::RenderFeatureAt(shaped, *render.placement, *render.viewport, *render.style, render.target);
  return true;
}

// An open archive and the source it reads. Closed when it goes out of scope.
struct Layer {
  std::unique_ptr<orcmap::ByteSource> source;
  std::unique_ptr<orcmap::PmTilesReader> reader;
  uint8_t min_zoom = 0;
  uint8_t max_zoom = 0;
  bool open = false;

  bool open_embedded() {
    source = std::make_unique<orcsdr::map_sources::EmbeddedWorldSource>();
    return finish();
  }
  bool open_file(const char* path) {
    source = std::make_unique<orcmap::esp_idf::FileByteSource>(path);
    return finish();
  }

 private:
  bool finish() {
    if (!source->Valid()) return false;
    reader = std::make_unique<orcmap::PmTilesReader>(source.get());
    if (!reader->Open()) return false;
    min_zoom = reader->Header().min_zoom;
    max_zoom = reader->Header().max_zoom;
    open = true;
    return true;
  }
};

// Draws every visible tile of one archive at the zoom plan that reproduces the wanted scale.
void draw_layer(Layer& layer, double fractional_zoom, const View& view, orcmap::MvtStreamScratch* scratch,
                orcmap::RenderTarget* target, const orcmap::MapStyle& style) {
  const orcsdr::map_view::ZoomPlan plan = orcsdr::map_view::plan_zoom(fractional_zoom, layer.min_zoom, layer.max_zoom);
  orcmap::Viewport viewport;
  viewport.center_lat_deg = view.center_lat;
  viewport.center_lon_deg = view.center_lon;
  viewport.zoom = plan.zoom;
  viewport.width_px = view.width;
  viewport.height_px = view.height;
  viewport.tile_size_px = plan.tile_size_px;
  std::vector<orcmap::TilePlacement> placements;
  orcmap::EnumerateVisibleTilePlacements(viewport, &placements);
  for (const orcmap::TilePlacement& placement : placements) {
    if (!layer.reader->TileExists(placement.tile.z, placement.tile.x, placement.tile.y)) continue;
    RenderContext render;
    render.placement = &placement;
    render.viewport = &viewport;
    render.style = &style;
    render.target = target;
    orcmap::MvtStreamOptions options;
    options.include_layer = &IncludeBasemapLayer;
    options.feature_sink = &DrawFeature;
    options.feature_sink_ctx = &render;
    (void)layer.reader->StreamTile(placement.tile.z, placement.tile.x, placement.tile.y, options, scratch);
  }
}

bool contains(const map_packs::PackInfo& pack, float lat, float lon) {
  return lat >= pack.min_lat && lat <= pack.max_lat && lon >= pack.min_lon && lon <= pack.max_lon;
}

// Renders the view into the cache canvas (origin 0,0, view-sized). Returns the number of archives drawn.
int render(const View& view) {
  orcmap::m5gfx_adapter::DisplayTarget target(g_cache);
  const orcmap::MapStyle& style = orcmap::styles::OrcSdrDark();
  const double mpp = orcsdr::map_view::metres_per_pixel(view.range_nm, radius_of(view));
  const double zoom = orcsdr::map_view::fractional_zoom(view.center_lat, mpp);

  orcmap::Viewport background;
  background.zoom = 0;
  background.width_px = view.width;
  background.height_px = view.height;
  orcmap::ClearMapBackground(background, style, &target);

  orcmap::MvtStreamScratch scratch;
  orcmap::ReserveMvtStreamScratch(&scratch, 64u * 1024u);
  int drawn = 0;
  g_credit[0] = '\0';

  // Base: the deepest world pack on the card, else the world map built into the firmware.
  Layer base;
  char world_path[128]{};
  uint8_t world_zoom = 0;
  bool sd_world = map_packs::best_world_archive(world_path, sizeof(world_path), &world_zoom) &&
                  base.open_file(world_path);
  if (!sd_world && !base.open_embedded()) {
    g_label[0] = '\0';
    return 0;
  }
  draw_layer(base, zoom, view, &scratch, &target, style);
  ++drawn;
  std::snprintf(g_label, sizeof(g_label), "WORLD z%u", static_cast<unsigned>(base.max_zoom));
  std::snprintf(g_credit, sizeof(g_credit), "Natural Earth");

  // Detail: any pack on the card that covers the centre and goes deeper than the base, drawn on top of it.
  map_packs::PackInfo packs[map_packs::kInfoMax];
  const size_t count = map_packs::valid_packs(packs, map_packs::kInfoMax);
  int detail = 0;
  for (size_t i = 0; i < count; ++i) {
    const map_packs::PackInfo& pack = packs[i];
    if (pack.max_zoom <= base.max_zoom || !contains(pack, view.center_lat, view.center_lon)) continue;
    // A detail pack only helps once the view is as deep as the base map goes; wider than that the world map is the
    // right picture, and drawing the pack's coarse low zooms over it would only add cost.
    if (zoom < static_cast<double>(base.max_zoom)) continue;
    if (sd_world && std::strcmp(pack.path, world_path) == 0) continue;
    Layer layer;
    if (!layer.open_file(pack.path)) continue;
    draw_layer(layer, zoom, view, &scratch, &target, style);
    ++detail;
    ++drawn;
    if (pack.credit[0]) std::snprintf(g_credit, sizeof(g_credit), "%s", pack.credit);
  }
  if (detail > 0) {
    const size_t used = std::strlen(g_label);
    std::snprintf(g_label + used, sizeof(g_label) - used, " + %d DETAIL", detail);
  }
  return drawn;
}

}  // namespace

bool load(orcsdr::storage::FileSystem*) {
  map_packs::scan_if_needed();
  return available();
}

bool available() {
  size_t size = 0;
  if (orcsdr::map_sources::embedded_world(&size) != nullptr && size > 0) return true;
  char path[8];
  return map_packs::best_world_archive(path, sizeof(path), nullptr);
}

const char* source_label() {
  if (g_label[0] != '\0') return g_label;
  return available() ? "WORLD" : "NONE";
}

void project_unclipped(const View& view, float latitude, float longitude, int* x, int* y) {
  double east = 0, north = 0;
  orcsdr::map_view::offset_nm(view.center_lat, view.center_lon, latitude, longitude, &east, &north);
  const double per_nm = view.range_nm > 0.0f ? radius_of(view) / view.range_nm : 1.0;
  *x = view.x + view.width / 2 + static_cast<int>(std::lround(east * per_nm));
  *y = view.y + view.height / 2 - static_cast<int>(std::lround(north * per_nm));
}

bool project(const View& view, float latitude, float longitude, int* x, int* y) {
  if (x == nullptr || y == nullptr || view.range_nm <= 0.0f) return false;
  project_unclipped(view, latitude, longitude, x, y);
  return *x >= view.x && *x < view.x + view.width && *y >= view.y && *y < view.y + view.height;
}

void draw_base(lgfx::v1::LovyanGFX& display, const View& view, uint16_t, uint16_t road_color,
               uint16_t airport_color, uint16_t border_color) {
  display.drawRect(view.x, view.y, view.width, view.height, border_color);
  if (!available() || view.width <= 8 || view.height <= 8 || view.range_nm <= 0.0f) return;

  CacheKey key;
  key.lat = view.center_lat;
  key.lon = view.center_lon;
  key.range = view.range_nm;
  key.radius = radius_of(view);
  key.width = view.width;
  key.height = view.height;
  key.generation = map_packs::generation();
  if (!g_cache_valid || !(key == g_key) || g_cache.width() != view.width || g_cache.height() != view.height) {
    g_cache.deleteSprite();
    g_cache.setPsram(true);
    g_cache.setColorDepth(16);
    if (g_cache.createSprite(view.width, view.height) == nullptr) {
      g_cache_valid = false;
      ESP_LOGW(kTag, "map canvas %dx%d unavailable", view.width, view.height);
      return;
    }
    const int64_t started = esp_timer_get_time();
    const int drawn = render(view);
    ESP_LOGI(kTag, "map rendered archives=%d ms=%lld label=%s", drawn,
             static_cast<long long>((esp_timer_get_time() - started) / 1000), g_label);
    g_key = key;
    g_cache_valid = drawn > 0;
  }
  if (!g_cache_valid) return;
  g_cache.pushSprite(&display, view.x, view.y);
  display.setTextDatum(bottom_left);
  display.setTextSize(1);
  display.setTextColor(road_color);
  display.drawString(g_credit, view.x + 3, view.y + view.height - 2);
  (void)airport_color;
}

bool draw_shifted(lgfx::v1::LovyanGFX& display, const View& view, int dx, int dy) {
  if (!g_cache_valid || g_cache.width() != view.width || g_cache.height() != view.height) return false;
  display.setClipRect(view.x, view.y, view.width, view.height);
  g_cache.pushSprite(&display, view.x + dx, view.y + dy);
  display.clearClipRect();
  return true;
}

void draw_base(const View& view, uint16_t water_color, uint16_t road_color,
               uint16_t airport_color, uint16_t border_color) {
  draw_base(M5.Display, view, water_color, road_color, airport_color, border_color);
}

bool self_check() {
  View view{44.0f, -123.0f, 25.0f, 0, 0, 400, 400};
  int x = 0, y = 0;
  const bool centre = project(view, 44.0f, -123.0f, &x, &y) && x == 200 && y == 200;
  const bool outside = !project(view, 0.0f, 0.0f, &x, &y);
  // 25 NM is the radius: a point 25 NM north is 200 px above the centre.
  const bool north = project(view, 44.0f + 25.0f / 60.0f, -123.0f, &x, &y) && x == 200 && y == 0;
  return centre && outside && north && orcsdr::map_view::self_check();
}

}  // namespace orcsdr::offline_map
