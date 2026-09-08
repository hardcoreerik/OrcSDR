#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace orcsdr::spectrum_history {

constexpr size_t kBins = 256;
constexpr size_t kRows = 128;
constexpr int kWidth = 1196;
constexpr int kHeight = 590;

struct Row {
  uint32_t time_ms = 0;
  uint8_t level[kBins]{};  // Fixed -160..0 dBFS, independent of display gain.
};

struct Snapshot {
  Row rows[kRows]{};  // Newest first; copied together under the history lock.
  Row live{};
  size_t count = 0;
  uint32_t span_ms = 20000;
};

struct History {
  Row rows[kRows]{};
  Row live{};
  size_t head = 0, count = 0, slices = 64;
  uint32_t span_ms = 20000, center_hz = 0, span_hz = 0;

  void clear() { head = count = 0; }

  void push(const float* bins, size_t bins_count, uint32_t now,
            uint32_t center, uint32_t bandwidth, uint32_t duration, size_t requested) {
    if (!bins || !bins_count) return;
    duration = std::clamp<uint32_t>(duration, 2000, 60000);
    requested = std::clamp<size_t>(requested, 16, kRows);
    if (duration != span_ms || requested != slices || center != center_hz || bandwidth != span_hz)
      clear();
    span_ms = duration;
    slices = requested;
    center_hz = center;
    span_hz = bandwidth;
    live.time_ms = now;
    const uint32_t interval = (span_ms + slices - 1) / slices;
    size_t index = (head + kRows - 1) % kRows;
    if (!count || now - rows[index].time_ms >= interval) {
      // Missing input leaves a real time gap, never invented/interpolated RF data.
      index = head;
      rows[index] = {};
      rows[index].time_ms = now;
      head = (head + 1) % kRows;
      count = std::min(count + 1, kRows);
    }
    for (size_t x = 0; x < kBins; ++x) {
      const size_t first = x * bins_count / kBins;
      const size_t last = std::max(first + 1, (x + 1) * bins_count / kBins);
      float peak = -160;
      for (size_t bin = first; bin < last; ++bin)
        if (std::isfinite(bins[bin])) peak = std::max(peak, bins[bin]);
      const auto encoded = static_cast<uint8_t>(std::clamp((peak + 160) * (255.0f / 160), 0.0f, 255.0f));
      live.level[x] = encoded;
      // Max pool both frequency bins and frames within a slice; preserve brief/narrow peaks.
      rows[index].level[x] = std::max(rows[index].level[x], encoded);
    }
  }

  void copy(Snapshot& out) const {
    out.count = count;
    out.span_ms = span_ms;
    out.live = live;
    for (size_t i = 0; i < count; ++i) out.rows[i] = rows[(head + kRows - 1 - i) % kRows];
  }
};

inline float rolloff(float age) {
  const float t = std::clamp((age - 0.72f) / 0.28f, 0.0f, 1.0f);
  return 1 - t * t * (3 - 2 * t);
}

inline float depth(uint32_t now, uint32_t timestamp, uint32_t span_ms) {
  return static_cast<float>(now - timestamp) / std::max<uint32_t>(1, span_ms);
}

struct Camera {
  float elevation = 32, azimuth = 20, zoom = 1, depth = 1, gain = 1;
  int mode = 1, color = 2, detail = 0;
};

struct Plane { float left, width, base, amplitude; };

// One row of primitives, reused after drawing. Keeping row boundaries explicit
// prevents wire segments crossing an occlusion gap or joining different ages.
struct RowSpans {
  struct Span { int16_t x, top, bottom; uint16_t ridge, body; bool fill, edge; };
  Span spans[kWidth];
  size_t count = 0;
  void add(int x, int top, int bottom, uint16_t ridge, uint16_t body, bool fill, bool edge) {
    spans[count++] = {static_cast<int16_t>(x), static_cast<int16_t>(top),
                     static_cast<int16_t>(bottom), ridge, body, fill, edge};
  }
  template <typename Canvas>
  void draw(Canvas& canvas, int origin_x, int origin_y, int scale, int mode) {
    int previous_x = -2, previous_y = 0;
    for (size_t i = 0; i < count; ++i) {
      const auto& s = spans[i];
      if (s.fill && s.bottom > s.top + 1)
        canvas.fillRect(origin_x + s.x * scale, origin_y + (s.top + 1) * scale,
                        scale, (s.bottom - s.top - 1) * scale, s.body);
      if (s.edge) {
        // Connect only equal-height neighbours: a steep line could cross the
        // foreground horizon. Individual ridge cells retain those steep peaks.
        if (mode == 0 && s.x == previous_x + 1 && s.top == previous_y)
          canvas.drawLine(origin_x + previous_x * scale, origin_y + previous_y * scale,
                          origin_x + s.x * scale, origin_y + s.top * scale, s.ridge);
        canvas.fillRect(origin_x + s.x * scale, origin_y + s.top * scale, scale, scale, s.ridge);
      }
      previous_x = s.x;
      previous_y = s.top;
    }
    count = 0;
  }
};
struct RowDone { void operator()() const {} };
struct Projection {
  float width_px, height_px, scale, zoom, reach, shear, height;
  Projection(const Camera& camera, int w, int h)
      : width_px(w), height_px(h), scale(h / 295.0f), zoom(std::clamp(camera.zoom, 0.6f, 2.0f)) {
    const float elevation = std::clamp(camera.elevation, 10.0f, 70.0f) * 0.0174532925f;
    reach = std::min(245.0f, (85 + 160 * std::sin(elevation)) * camera.depth) * scale;
    shear = std::sin(camera.azimuth * 0.0174532925f) * 100 * scale;
    height = 116 * std::cos(elevation) * camera.gain * zoom * scale;
  }
  Plane at(float age) const {
    const float perspective = 1 - 0.36f * age;
    const float width = (width_px - 28 * scale) * perspective * zoom;
    return {(width_px - width) * 0.5f + shear * age, width,
            height_px - 10 * scale - age * reach, height * perspective * rolloff(age)};
  }
};

inline uint16_t blue(float level, float light) {
  const int g = static_cast<int>((8 + 55 * level) * light);
  const int b = static_cast<int>((18 + 13 * level) * light);
  return static_cast<uint16_t>((std::clamp(g, 0, 63) << 5) | std::clamp(b, 0, 31));
}

// Front-to-back height-field rendering: each output pixel is filled at most once.
// No z-buffer or offscreen bitmap; the caller emits bounded M5GFX vertical spans.
template <typename Span, typename Done = RowDone>
inline void render(const Snapshot& history, uint32_t now, float floor, float ceiling,
                   const Camera& camera, int width_px, int height_px, Span span, Done row_done = {}) {
  if (width_px < 1 || width_px > kWidth || height_px < 1 || height_px > kHeight) return;
  int horizon[kWidth];
  std::fill_n(horizon, width_px, height_px);
  const Projection projection(camera, width_px, height_px);
  const float range = std::max(20.0f, ceiling - floor);
  const size_t bin_step = size_t{1} << std::clamp(camera.detail, 0, 2);
  for (size_t r = 0; history.count && r <= history.count; ++r) {
    const Row& row = r == 0 ? history.live : history.rows[r - 1];
    const float age = depth(now, row.time_ms, history.span_ms);
    if (age >= 1) continue;
    const float fade = rolloff(age);
    const auto plane = projection.at(age);
    const float width = plane.width, left = plane.left, amplitude = plane.amplitude;
    const int base = static_cast<int>(plane.base);
    float levels[kBins];
    for (size_t b = 0; b < kBins; b += bin_step) {
      uint8_t peak = 0;
      for (size_t i = b; i < b + bin_step; ++i) peak = std::max(peak, row.level[i]);
      const float n = std::clamp((peak * (160.0f / 255) - 160 - floor) / range, 0.0f, 1.0f);
      for (size_t i = b; i < b + bin_step; ++i) levels[i] = n;
    }
    for (int x = std::max(0, static_cast<int>(std::ceil(left)));
         x < std::min(width_px, static_cast<int>(left + width)); ++x) {
      const float bin = std::clamp((x - left) * (kBins - 1) / width, 0.0f, float(kBins - 1));
      const size_t b = static_cast<size_t>(bin);
      const float n = levels[b] + (levels[std::min(b + 1, kBins - 1)] - levels[b]) * (bin - b);
      const int top = std::clamp(static_cast<int>(base - n * amplitude), 0, height_px - 1);
      const int bottom = std::min({base + 1, horizon[x], height_px});
      if (top >= bottom) continue;
      const float tint = camera.color == 1 ? 1 - age : camera.color == 2 ? (n + 1 - age) * 0.5f : n;
      const float light = fade * (r == 0 ? 1.0f : 0.90f - 0.30f * age);
      span(x, top, bottom, r == 0 ? blue(1, fade) : blue(0.45f + tint * 0.55f, light),
           blue(tint * 0.20f, light),
           camera.mode == 1, camera.mode != 2 || x % 3 == 0);
      // Hidden-line removal applies to Lines and Points as well as Surface.
      horizon[x] = top;
    }
    row_done();
  }
}

// Three buffers let rendering overlap scanout while retaining the old buffer for
// two refresh events. IDF 5.5.4's VSYNC callback does not identify the DMA buffer.
struct FlipBuffers {
  uint32_t retired[3]{};
  bool used[3]{};
  uint8_t front = 0;
  void reset() { *this = {}; used[0] = true; }
  int writable(uint32_t refresh) const {
    for (int i = 1; i <= 2; ++i) {
      const int candidate = (front + i) % 3;
      if (!used[candidate] || refresh - retired[candidate] >= 2) return candidate;
    }
    return -1;
  }
  void submitted(uint8_t next, uint32_t refresh) {
    retired[front] = refresh;
    used[next] = true;
    front = next;
  }
};

}  // namespace orcsdr::spectrum_history
