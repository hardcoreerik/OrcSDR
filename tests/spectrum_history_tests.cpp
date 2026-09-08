#include "spectrum_history.hpp"
#include "rf_visualizer_controls.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>

using namespace orcsdr::spectrum_history;

std::vector<uint16_t> image(const Snapshot& snapshot, uint32_t now, Camera camera = {}) {
  std::vector<uint16_t> pixels(kWidth * kHeight);
  std::vector<bool> written(kWidth * kHeight);
  render(snapshot, now, -110, -20, camera, kWidth, kHeight,
      [&](int x, int top, int bottom, uint16_t ridge, uint16_t body, bool fill, bool edge) {
        assert(x >= 0 && x < kWidth && top >= 0 && top < bottom && bottom <= kHeight);
        for (int y = top; y < bottom; ++y) {
          assert(!written[y * kWidth + x]); // Occluded pixels must never be overdrawn.
          written[y * kWidth + x] = true;
          if (y == top ? edge : fill) pixels[y * kWidth + x] = y == top ? ridge : body;
        }
      });
  return pixels;
}

struct Canvas {
  int width, height;
  std::vector<uint16_t> pixels;
  Canvas(int w, int h) : width(w), height(h), pixels(w * h) {}
  void fillRect(int x, int y, int w, int h, uint16_t color) {
    assert(x >= 0 && y >= 0 && x + w <= width && y + h <= height);
    for (int yy = y; yy < y + h; ++yy)
      std::fill_n(pixels.begin() + yy * width + x, w, color);
  }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t color) {
    assert(y0 == y1 && x1 >= x0);
    fillRect(x0, y0, x1 - x0 + 1, 1, color);
  }
};

std::vector<uint16_t> adapted(const Snapshot& snapshot, uint32_t now, Camera camera = {}, int scale = 1) {
  Canvas canvas(kWidth, kHeight);
  RowSpans row{};
  render(snapshot, now, -110, -20, camera, kWidth / scale, kHeight / scale,
      [&](int x, int top, int bottom, uint16_t ridge, uint16_t body, bool fill, bool edge) {
        row.add(x, top, bottom, ridge, body, fill, edge);
      }, [&] { row.draw(canvas, 0, 0, scale, camera.mode); });
  assert(row.count == 0);
  return canvas.pixels;
}

int main(int argc, char** argv) {
  assert(orcsdr::visualizer::controls_self_check());
  // Explicit row and occlusion boundaries, including reduced raster coordinates.
  Canvas check(32, 32);
  RowSpans row{};
  row.add(2, 3, 6, 1, 2, false, true);
  row.add(3, 3, 6, 1, 2, false, true);
  row.add(6, 3, 6, 1, 2, false, true);
  row.draw(check, 0, 0, 2, 0);
  assert(check.pixels[6 * 32 + 4] == 1 && check.pixels[6 * 32 + 6] == 1);
  assert(check.pixels[6 * 32 + 10] == 0);
  row.add(7, 3, 6, 3, 2, false, true);
  row.draw(check, 0, 0, 2, 0);
  assert(check.pixels[6 * 32 + 13] == 1); // Previous cell, never bridged with new colour.
  History history;
  Snapshot snapshot;
  history.copy(snapshot);
  assert(snapshot.count == 0);
  std::array<float, 1024> bins;
  bins.fill(-110);
  bins[3] = -5; bins[4] = -10; bins[511] = -1; bins[1023] = 0;
  bins[20] = std::numeric_limits<float>::quiet_NaN();
  history.push(bins.data(), bins.size(), 100, 100000000, 960000, 5000, 128);
  history.copy(snapshot);
  assert(snapshot.count == 1 && snapshot.rows[0].time_ms == 100);
  assert(snapshot.rows[0].level[0] >= 247 && snapshot.rows[0].level[1] >= 239);
  assert(snapshot.rows[0].level[127] >= 253 && snapshot.rows[0].level[255] == 255);
  bins.fill(-110);
  history.push(bins.data(), bins.size(), 110, 100000000, 960000, 5000, 128);
  history.copy(snapshot);
  assert(snapshot.count == 1 && snapshot.rows[0].level[255] == 255); // Brief carrier retained.
  assert(snapshot.live.time_ms == 110 && snapshot.live.level[255] < snapshot.rows[0].level[255]);
  for (uint32_t now : {175u, 240u, 400u, 1200u})
    history.push(bins.data(), bins.size(), now, 100000000, 960000, 5000, 128);
  history.copy(snapshot);
  assert(snapshot.count == 5 && snapshot.rows[1].time_ms == 400);
  assert(depth(1200, snapshot.rows[1].time_ms, 5000) == 0.16f);
  assert(depth(5000, 5000, 5000) == 0 && depth(5000, 2500, 5000) == 0.5f);
  assert(depth(5000, 0, 5000) == 1 && depth(5001, 0, 5000) > 1);
  assert(depth(20, UINT32_MAX - 29, 5000) == 0.01f);
  assert(rolloff(0) == 1 && rolloff(1) == 0 && rolloff(1.1f) == 0);
  for (int i = 1; i <= 100; ++i) assert(rolloff(i / 100.0f) <= rolloff((i - 1) / 100.0f));
  for (uint32_t i = 0; i < 400; ++i)
    history.push(bins.data(), bins.size(), 2000 + i * 50, 100000000, 960000, 5000, 128);
  history.copy(snapshot);
  assert(snapshot.count == kRows && snapshot.rows[0].time_ms == 21950);
  for (size_t i = 0; i < kRows; ++i) assert(snapshot.rows[i].time_ms == 21950 - i * 50);
  const auto expired = image(snapshot, 30000);
  assert(std::all_of(expired.begin(), expired.end(), [](uint16_t p) { return p == 0; }));
  history.push(bins.data(), bins.size(), 22000, 100000000, 960000, 2000, 32);
  assert(history.count == 1 && history.span_ms == 2000); // Reconfiguration starts a new window.
  history.push(bins.data(), bins.size(), 22100, 101000000, 960000, 2000, 32);
  assert(history.count == 1); // Never mix tuned frequency axes.
  history.clear();
  history.push(bins.data(), bins.size(), UINT32_MAX - 100, 101000000, 960000, 2000, 32);
  history.push(bins.data(), bins.size(), 50, 101000000, 960000, 2000, 32);
  history.copy(snapshot);
  assert(snapshot.count == 2 && depth(50, snapshot.rows[1].time_ms, 2000) == 0.0755f);

  // A single narrow carrier must survive the production adapter at both scales.
  History carrier;
  bins.fill(-110);
  carrier.push(bins.data(), bins.size(), 10, 100000000, 960000, 2000, 32);
  Snapshot quiet; carrier.copy(quiet);
  bins[511] = -10;
  carrier.push(bins.data(), bins.size(), 11, 100000000, 960000, 2000, 32);
  Snapshot loud; carrier.copy(loud);
  for (int scale : {1, 2}) assert(adapted(quiet, 11, {}, scale) != adapted(loud, 11, {}, scale));
  for (size_t b = 0; b < bins.size(); ++b) bins[b] = -110 + 80.0f * b / bins.size();
  carrier.push(bins.data(), bins.size(), 120, 100000000, 960000, 2000, 32);
  carrier.copy(loud);
  assert(adapted(loud, 120) == image(loud, 120));
  assert(adapted(loud, 120) != adapted(loud, 140)); // Resume advances elapsed time.

  FlipBuffers flips;
  flips.reset();
  assert(flips.writable(10) == 1);
  flips.submitted(1, 10);
  assert(flips.writable(10) == 2);
  flips.submitted(2, 11);
  assert(flips.writable(11) == -1 && flips.writable(12) == 0);
  flips.submitted(0, 12);
  assert(flips.writable(12) == -1 && flips.writable(13) == 1);
  flips.reset(); flips.submitted(1, UINT32_MAX); flips.submitted(2, 0);
  assert(flips.writable(0) == -1 && flips.writable(1) == 0);

  history.clear();
  for (uint32_t r = 0; r < 128; ++r) {
    for (size_t b = 0; b < bins.size(); ++b) {
      float power = 0;
      for (int carrier = 0; carrier < 9; ++carrier) {
        const float center = 65 + carrier * 108 + 22 * std::sin(r * 0.13f + carrier);
        const float dx = (b - center) / (5 + carrier % 3 * 4.0f);
        power += (40 + 35 * std::sin(r * 0.11f + carrier)) * std::exp(-dx * dx);
      }
      bins[b] = -108 + power + 2 * std::sin(b * 1.7f + r);
    }
    bins[(r * 7) % bins.size()] = -15; // Deterministic moving one-bin signal.
    history.push(bins.data(), bins.size(), r * 160, 100000000, 960000, 20000, 128);
  }
  history.copy(snapshot);
  const auto pixels = image(snapshot, 20400);
  assert(adapted(snapshot, 20400) == pixels); // Actual device adapter matches the occlusion oracle.
  assert(adapted(snapshot, 20400) == adapted(snapshot, 20400)); // Frozen time is stable.
  for (int scale : {1, 2}) for (int mode : {0, 1, 2}) {
    Camera camera; camera.mode = mode;
    const auto output = adapted(snapshot, 20400, camera, scale);
    assert(std::count_if(output.begin(), output.end(), [](uint16_t p) { return p != 0; }) > 100);
  }
  const auto bench_started = std::chrono::steady_clock::now();
  for (int i = 0; i < 60; ++i) (void)adapted(snapshot, 20400 + i * 16, {}, 2);
  std::cout << "host_adapter_us_per_frame=" <<
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - bench_started).count() / 60
      << " raster=598x295 rows=128 bins=256 mode=surface (not Tab5 FPS)\n";
  const auto moved = image(snapshot, 20416);
  assert(pixels != moved); // Render motion between source measurements.
  for (int control = 0; control < 8; ++control) {
    Camera camera;
    if (control == 0) camera.elevation = 50;
    if (control == 1) camera.azimuth = -30;
    if (control == 2) camera.zoom = 0.6f;
    if (control == 3) camera.depth = 0.5f;
    if (control == 4) camera.gain = 2;
    if (control == 5) camera.mode = 0;
    if (control == 6) camera.color = 0;
    if (control == 7) camera.detail = 2;
    assert(image(snapshot, 20400, camera) != pixels);
    if (camera.mode == 1) assert(adapted(snapshot, 20400, camera) == image(snapshot, 20400, camera));
  }
  for (int mode = 0; mode < 3; ++mode) {
    Camera camera; camera.mode = mode;
    for (int detail = 0; detail < 3; ++detail) {
      camera.detail = detail; camera.elevation = 70; camera.zoom = 2; camera.gain = 4;
      (void)image(snapshot, 20400, camera); // Clipping and occlusion at control extremes.
    }
  }
  if (argc > 1) {
    std::ofstream out(argv[1], std::ios::binary);
    out << "P6\n" << kWidth << ' ' << kHeight << "\n255\n";
    for (auto p : pixels) {
      const char rgb[] = {char(((p >> 11) & 31) * 255 / 31),
                          char(((p >> 5) & 63) * 255 / 63), char((p & 31) * 255 / 31)};
      out.write(rgb, 3);
    }
  }
  std::cout << "PASS history, timing, wrap, peaks, frame ownership, raster bounds/occlusion, animation\n"
            << "Row=" << sizeof(Row) << " History=" << sizeof(History) << " Snapshot=" << sizeof(Snapshot) << '\n';
}
