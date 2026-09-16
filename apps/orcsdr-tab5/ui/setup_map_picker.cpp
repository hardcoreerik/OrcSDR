#include "setup_map_picker.hpp"

#include <M5Unified.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "orcmap/esp_idf/partition_byte_source.hpp"
#include "orcmap/experimental/mvt_classify.hpp"
#include "orcmap/feature.hpp"
#include "orcmap/location_picker.hpp"
#include "orcmap/m5gfx/display_target.hpp"
#include "orcmap/mvt_stream.hpp"
#include "orcmap/pmtiles.hpp"
#include "orcmap/renderer.hpp"
#include "orcmap/style.hpp"
#include "orcmap/viewport.hpp"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace orcsdr::setup_map_picker {
namespace {

constexpr char kTag[] = "setup_map";

// 1280x720. The map is the primary element; chrome is kept to a header strip
// and one row of large touch targets.
constexpr int kScreenW = 1280;
constexpr int kScreenH = 720;
constexpr int kHeaderH = 72;
constexpr int kFooterH = 92;
// The map viewport is the WHOLE panel and the header/footer are painted on
// top of it each frame. The renderer's target has no origin offset, so a
// short viewport would place the map under the header instead of below it.
// Drawing full-screen keeps the crosshair at the true viewport centre, which
// is what makes the selection exactly what sits under it. The cost is the
// ~164 px of map hidden behind chrome, which the Tab5 can afford.
constexpr int kMapTop = kHeaderH;
constexpr int kMapBottom = kScreenH - kFooterH;
constexpr int kCrossX = kScreenW / 2;
constexpr int kCrossY = kScreenH / 2;

// A drag under this many pixels is treated as a tap, not a pan.
constexpr int kDragThresholdPx = 12;

// When the camera opens on a known location, zoom in far enough that the
// place is distinguishable rather than a few pixels of continent. Bounded by
// the embedded pack's own maximum zoom.
constexpr uint8_t kInitialDetailZoom = 5;

// Buttons. Everything is at least 76 px tall, which is a comfortable target
// on a 5-inch panel.
constexpr int kBtnY = kScreenH - kFooterH + 8;
constexpr int kBtnH = 76;
struct Rect { int x, y, w, h; };
constexpr Rect kBack{16, kBtnY, 150, kBtnH};
constexpr Rect kZoomOut{182, kBtnY, 110, kBtnH};
constexpr Rect kZoomIn{302, kBtnY, 110, kBtnH};
constexpr Rect kSkip{858, kBtnY, 150, kBtnH};
constexpr Rect kSet{1022, kBtnY, 242, kBtnH};

constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kBlue = 0x04ff;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kMuted = 0x9cf3;

bool Hit(const Rect& r, int x, int y) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void Button(const Rect& r, const char* label, uint16_t fill, bool enabled) {
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 10,
                           enabled ? fill : 0x2124);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 10,
                           enabled ? TFT_LIGHTGREY : 0x4208);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(enabled ? TFT_WHITE : kMuted);
  M5.Display.drawString(label, r.x + r.w / 2, r.y + r.h / 2);
}

// The overview schema carries place points the renderer cannot label yet, so
// they are dropped rather than drawn as unlabelled dots.
bool IncludeBasemapLayer(const char* name, size_t name_len, void* ctx) {
  return orcmap::experimental::IncludeNoTextBasemapLayer(name, name_len, ctx);
}

struct RenderContext {
  const orcmap::TilePlacement* placement = nullptr;
  const orcmap::Viewport* viewport = nullptr;
  const orcmap::MapStyle* style = nullptr;
  orcmap::RenderTarget* target = nullptr;
  size_t features = 0;
};

// One feature at a time: the streaming decoder never materialises a whole
// tile, which is the same path the SD-backed maps use.
bool DrawFeature(const orcmap::Feature& feature, void* ctx) {
  RenderContext& render = *static_cast<RenderContext*>(ctx);
  orcmap::Feature shaped = feature;
  orcmap::FeatureKind kind{};
  if (orcmap::experimental::TryClassifyFeature(shaped, &kind)) {
    shaped.kind = kind;
    shaped.kind_assigned = true;
  }
  orcmap::RenderFeatureAt(shaped, *render.placement, *render.viewport,
                          *render.style, render.target);
  ++render.features;
  return true;
}

class Picker {
 public:
  bool Open() {
    source_ = new orcmap::esp_idf::PartitionByteSource(kPartitionLabel);
    if (!source_->Valid()) {
      ESP_LOGE(kTag, "partition '%s' not found", kPartitionLabel);
      return false;
    }
    reader_ = new orcmap::PmTilesReader(source_);
    if (!reader_->Open()) {
      ESP_LOGE(kTag, "embedded archive did not open (size=%llu)",
               static_cast<unsigned long long>(source_->Size()));
      return false;
    }
    ESP_LOGI(kTag, "embedded map open: partition=%llu B z%u-%u",
             static_cast<unsigned long long>(source_->Size()),
             reader_->Header().min_zoom, reader_->Header().max_zoom);
    // One scratch for the whole session; every buffer inside is reused, so a
    // steady-state frame performs no large allocation.
    orcmap::ReserveMvtStreamScratch(&scratch_, 64u * 1024u);
    return true;
  }

  ~Picker() {
    delete reader_;
    delete source_;
  }

  const orcmap::PmTilesHeader& Header() const { return reader_->Header(); }

  bool Begin(int32_t lat_e7, int32_t lon_e7, bool have_initial) {
    orcmap::Viewport viewport;
    viewport.width_px = kScreenW;
    viewport.height_px = kScreenH;
    viewport.tile_size_px = 256;

    orcmap::GeoBounds bounds;
    bounds.min_lon_deg = -180.0;
    bounds.max_lon_deg = 180.0;
    bounds.min_lat_deg = -orcmap::kMercatorMaxLatDeg;
    bounds.max_lat_deg = orcmap::kMercatorMaxLatDeg;

    orcmap::LocationPickerLimits limits;
    limits.data_min_zoom = reader_->Header().min_zoom;
    limits.data_max_zoom = reader_->Header().max_zoom;

    if (!picker_.Begin(viewport, bounds, limits)) return false;
    if (have_initial) {
      // A stored location or a network estimate positions the CAMERA only.
      // What gets returned is still whatever the user confirms.
      recenter_ = true;
      start_lat_e7_ = lat_e7;
      start_lon_e7_ = lon_e7;
    }
    return true;
  }

  // Positions the camera on a known location by projecting it through the
  // current view and moving the picker there, so the picker's own state
  // stays consistent rather than being written behind its back.
  void ApplyInitial() {
    if (!recenter_) return;
    recenter_ = false;
    const orcmap::LatLon target{start_lat_e7_ / 1.0e7, start_lon_e7_ / 1.0e7};
    // Zoom in first: at the opening world zoom a whole country is a few
    // pixels, so the projection has to be taken at a zoom where the target
    // is actually distinguishable.
    while (picker_.CanZoomIn() && picker_.Zoom() < kInitialDetailZoom) {
      picker_.ZoomIn();
    }
    for (int attempt = 0; attempt < 8; ++attempt) {
      double sx = 0.0;
      double sy = 0.0;
      if (!orcmap::ProjectLatLon(picker_.View(), target, &sx, &sy)) break;
      if (!picker_.RecenterAtScreen(sx, sy)) break;
      const orcmap::LatLon at = picker_.Selection();
      if (std::fabs(at.lat_deg - target.lat_deg) < 1e-6 &&
          std::fabs(at.lon_deg - target.lon_deg) < 1e-6) {
        break;
      }
    }
  }

  orcmap::LocationPicker& Camera() { return picker_; }

  // Draws the map area. Returns tiles drawn, for the boot log.
  int DrawMap() {
    orcmap::m5gfx_adapter::DisplayTarget target(M5.Display);
    const orcmap::MapStyle& style = orcmap::styles::OrcSdrDark();
    const orcmap::Viewport& viewport = picker_.View();

    orcmap::ClearMapBackground(viewport, style, &target);

    placements_.clear();
    orcmap::EnumerateVisibleTilePlacements(viewport, &placements_);

    int drawn = 0;
    for (const orcmap::TilePlacement& placement : placements_) {
      if (!reader_->TileExists(placement.tile.z, placement.tile.x,
                               placement.tile.y)) {
        continue;
      }
      RenderContext render;
      render.placement = &placement;
      render.viewport = &viewport;
      render.style = &style;
      render.target = &target;

      orcmap::MvtStreamOptions options;
      options.include_layer = &IncludeBasemapLayer;
      options.feature_sink = &DrawFeature;
      options.feature_sink_ctx = &render;
      if (reader_->StreamTile(placement.tile.z, placement.tile.x,
                              placement.tile.y, options, &scratch_)) {
        ++drawn;
      }
    }
    return drawn;
  }

 private:
  orcmap::esp_idf::PartitionByteSource* source_ = nullptr;
  orcmap::PmTilesReader* reader_ = nullptr;
  orcmap::LocationPicker picker_;
  orcmap::MvtStreamScratch scratch_;
  std::vector<orcmap::TilePlacement> placements_;
  bool recenter_ = false;
  int32_t start_lat_e7_ = 0;
  int32_t start_lon_e7_ = 0;
};

void DrawCrosshair() {
  constexpr int r = 20;
  M5.Display.drawCircle(kCrossX, kCrossY, r, TFT_WHITE);
  M5.Display.drawCircle(kCrossX, kCrossY, r + 1, kBlue);
  M5.Display.drawFastHLine(kCrossX - 38, kCrossY, 76, TFT_WHITE);
  M5.Display.drawFastVLine(kCrossX, kCrossY - 38, 76, TFT_WHITE);
  M5.Display.fillCircle(kCrossX, kCrossY, 3, kGreen);
}

void DrawChrome(orcmap::LocationPicker& picker) {
  M5.Display.fillRect(0, 0, kScreenW, kHeaderH, TFT_BLACK);
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawString("Choose your location", 24, kHeaderH / 2);

  char zoom[16];
  std::snprintf(zoom, sizeof(zoom), "Z%u", picker.Zoom());
  M5.Display.setTextDatum(middle_right);
  M5.Display.setTextColor(kBlue);
  M5.Display.drawString(zoom, kScreenW - 24, kHeaderH / 2);

  M5.Display.fillRect(0, kScreenH - kFooterH, kScreenW, kFooterH, kPanel);
  Button(kBack, "BACK", TFT_NAVY, true);
  Button(kZoomOut, "-", TFT_DARKCYAN, picker.CanZoomOut());
  Button(kZoomIn, "+", TFT_DARKCYAN, picker.CanZoomIn());
  Button(kSkip, "SKIP", TFT_NAVY, true);
  Button(kSet, "SET LOCATION", TFT_DARKGREEN, true);

  const orcmap::LatLon at = picker.Selection();
  char coords[48];
  std::snprintf(coords, sizeof(coords), "%.5f, %.5f", at.lat_deg, at.lon_deg);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawString(coords, (kZoomIn.x + kZoomIn.w + kSkip.x) / 2,
                        kBtnY + kBtnH / 2);
}

int32_t ToE7(double degrees) {
  return static_cast<int32_t>(std::llround(degrees * 1.0e7));
}

}  // namespace

bool available() {
  orcmap::esp_idf::PartitionByteSource source(kPartitionLabel);
  if (!source.Valid()) return false;
  orcmap::PmTilesReader reader(&source);
  return reader.Open();
}

Result run(lgfx::v1::LovyanGFX& display, int32_t initial_latitude_e7,
           int32_t initial_longitude_e7, bool have_initial) {
  (void)display;  // M5.Display is the same panel; kept for call-site clarity.
  Result result;

  Picker picker;
  if (!picker.Open()) return result;  // Outcome::unavailable
  if (!picker.Begin(initial_latitude_e7, initial_longitude_e7, have_initial)) {
    return result;
  }
  picker.ApplyInitial();

  const uint32_t started = millis();
  const int tiles = picker.DrawMap();
  ESP_LOGI(kTag, "first frame: %d tiles in %lu ms",
           tiles, static_cast<unsigned long>(millis() - started));
  DrawChrome(picker.Camera());
  DrawCrosshair();

  bool dragging = false;
  int press_x = 0;
  int press_y = 0;
  int last_x = 0;
  int last_y = 0;

  for (;;) {
    M5.update();
    const auto touch = M5.Touch.getDetail();

    if (touch.isPressed()) {
      if (!dragging) {
        dragging = true;
        press_x = last_x = touch.x;
        press_y = last_y = touch.y;
      } else {
        last_x = touch.x;
        last_y = touch.y;
      }
    } else if (dragging) {
      dragging = false;
      const int dx = last_x - press_x;
      const int dy = last_y - press_y;
      const bool was_drag =
          std::abs(dx) > kDragThresholdPx || std::abs(dy) > kDragThresholdPx;

      if (was_drag && press_y >= kMapTop && press_y < kMapBottom) {
        // The map follows the finger; the picker handles the sign.
        picker.Camera().DragByGesture(dx, dy);
        picker.DrawMap();
        DrawChrome(picker.Camera());
        DrawCrosshair();
      } else if (!was_drag) {
        if (Hit(kZoomIn, press_x, press_y)) {
          if (picker.Camera().ZoomIn()) {
            picker.DrawMap();
            DrawChrome(picker.Camera());
            DrawCrosshair();
          }
        } else if (Hit(kZoomOut, press_x, press_y)) {
          if (picker.Camera().ZoomOut()) {
            picker.DrawMap();
            DrawChrome(picker.Camera());
            DrawCrosshair();
          }
        } else if (Hit(kBack, press_x, press_y)) {
          result.outcome = Outcome::back;
          return result;
        } else if (Hit(kSkip, press_x, press_y)) {
          result.outcome = Outcome::skipped;
          return result;
        } else if (Hit(kSet, press_x, press_y)) {
          const orcmap::LatLon at = picker.Camera().Selection();
          result.outcome = Outcome::chosen;
          result.latitude_e7 = ToE7(at.lat_deg);
          result.longitude_e7 = ToE7(at.lon_deg);
          result.zoom = picker.Camera().Zoom();
          ESP_LOGI(kTag, "ORCSDR_LOCATION_CHOSEN lat=%.7f lon=%.7f zoom=%u",
                   at.lat_deg, at.lon_deg, result.zoom);
          return result;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(16));
  }
}

}  // namespace orcsdr::setup_map_picker
