#include "spectrum_history.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <vector>

// Compile this same workload against B/C/D headers. Host timings are not P4 FPS.
int main() {
  using namespace orcsdr::spectrum_history;
  History history;
  std::array<float, 1024> bins{};
  for (unsigned r = 0; r < 128; ++r) {
    for (size_t b = 0; b < bins.size(); ++b) {
      float power = 0;
      for (int carrier = 0; carrier < 9; ++carrier) {
        const float dx = (b - (65 + carrier * 108 + 22 * std::sin(r * .13f + carrier))) /
                         (5 + carrier % 3 * 4.0f);
        power += (40 + 35 * std::sin(r * .11f + carrier)) * std::exp(-dx * dx);
      }
      bins[b] = -108 + power + 2 * std::sin(b * 1.7f + r);
    }
    bins[(r * 7) % bins.size()] = -15;
    history.push(bins.data(), bins.size(), r * 160, 100000000, 960000, 20000, 128);
  }
  Snapshot snapshot;
  history.copy(snapshot);
  constexpr int width = 598, height = 295;
  Camera camera; camera.detail = 1;
  std::vector<uint16_t> pixels(width * height);
  std::vector<long long> times;
  uint64_t primitives = 0, checksum = 0;
  for (unsigned i = 0; i < 180; ++i) {
    const auto start = std::chrono::steady_clock::now();
    std::fill(pixels.begin(), pixels.end(), 0);
    render(snapshot, 20400 + (i % 60) * 16, -110, -20, camera, width, height,
        [&](int x, int top, int bottom, uint16_t ridge, uint16_t body, bool fill, bool edge) {
          if (i >= 30) ++primitives;
          if (fill) for (int y = top + 1; y < bottom; ++y) pixels[y * width + x] = body;
          if (edge) pixels[top * width + x] = ridge;
        });
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    if (i >= 30) times.push_back(elapsed);
    checksum += pixels[(i * 971) % pixels.size()];
  }
  long long total = 0;
  for (auto time : times) total += time;
  std::sort(times.begin(), times.end());
  std::cout << "host_only=1 raster=598x295 rows=128 buckets=128 mode=surface frames=" << times.size()
            << " mean_us=" << total / times.size() << " p95_us=" << times[(times.size() * 95 - 1) / 100]
            << " p99_us=" << times[(times.size() * 99 - 1) / 100]
            << " primitives=" << primitives << " checksum=" << checksum << '\n';
}
