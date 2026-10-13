// Image-fidelity regression: the ORC SSTV test pattern is sent through the real decoder and
// the decoded gallery image is compared with the source. Measures the receiver, not just
// that it decodes: per-pixel error, correlation, and fine-detail retention.
//
// The pattern asset is the same file embedded in the firmware for the on-device self-test.
// Set SSTV_PATTERN to its path (tools/sstv_host_tests.sh does). Writes the decoded images as
// PGM files into the launch directory so they can be inspected.
#include "sstv_receiver.hpp"
#include "sstv_test_signal.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace orcsdr::sstv;
using sstv_test::require;
using sstv_test::Signal;

namespace {

std::vector<uint8_t> load_pattern() {
  const char* path = std::getenv("SSTV_PATTERN");
  if (!path) path = "apps/orcsdr-tab5/main/sstv_testpattern_320x240.gray";
  std::vector<uint8_t> out(320u * 240u);
  std::FILE* f = std::fopen(path, "rb");
  if (!f) {
    std::fprintf(stderr, "SSTV FAIL: cannot open test pattern %s (set SSTV_PATTERN)\n", path);
    std::exit(1);
  }
  const size_t n = std::fread(out.data(), 1, out.size(), f);
  std::fclose(f);
  require(n == out.size(), "pattern: 320x240 grayscale asset has the expected size");
  return out;
}

// 8-bit gray from the receiver's RGB565 (it renders gray as equal channels).
uint8_t gray_of(uint16_t px) { return static_cast<uint8_t>(((px >> 5) & 63u) << 2); }

struct Metrics {
  double mae = 0;          // mean absolute error, 0..255
  double mae_blur = 0;     // same after a 3-pixel horizontal box blur of both images
  double correlation = 0;  // Pearson
  double fine_detail = 0;  // decoded / source horizontal edge energy (1.0 = fully retained)
};

// Source and decoded images are compared at the decoded size; the source is resampled by nearest
// neighbour onto the decoded grid exactly the way the transmitter built it.
Metrics compare(const Image& image, const std::vector<uint8_t>& src, unsigned skip_top = 0) {
  const unsigned w = image.width, h = image.height;
  std::vector<double> a(w * h), b(w * h);
  for (unsigned y = 0; y < h; ++y)
    for (unsigned x = 0; x < w; ++x) {
      a[y * w + x] = gray_of(image.pixels[y * w + x]);
      const unsigned sy = (w == 640 ? y * 240u / 496u : y), sx = (w == 640 ? x / 2u : x);
      b[y * w + x] = src[sy * 320u + sx];
    }
  auto blur = [&](const std::vector<double>& v) {
    std::vector<double> o(v);
    for (unsigned y = 0; y < h; ++y)
      for (unsigned x = 1; x + 1 < w; ++x) o[y * w + x] = (v[y * w + x - 1] + v[y * w + x] + v[y * w + x + 1]) / 3.0;
    return o;
  };
  const auto ab = blur(a), bb = blur(b);
  Metrics m;
  double n = 0, sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0, ea = 0, eb = 0;
  for (unsigned y = skip_top; y < h; ++y)
    for (unsigned x = 2; x + 2 < w; ++x) {
      const double da = a[y * w + x], db = b[y * w + x];
      m.mae += std::fabs(da - db);
      m.mae_blur += std::fabs(ab[y * w + x] - bb[y * w + x]);
      sa += da; sb += db; saa += da * da; sbb += db * db; sab += da * db; ++n;
      ea += std::fabs(a[y * w + x + 1] - da);
      eb += std::fabs(b[y * w + x + 1] - db);
    }
  m.mae /= n;
  m.mae_blur /= n;
  const double cov = sab / n - (sa / n) * (sb / n);
  const double va = saa / n - (sa / n) * (sa / n), vb = sbb / n - (sb / n) * (sb / n);
  m.correlation = cov / std::sqrt(va * vb);
  m.fine_detail = eb > 0 ? ea / eb : 0;
  return m;
}

void write_pgm(const char* name, const Image& image) {
  if (std::FILE* f = std::fopen(name, "wb")) {
    std::fprintf(f, "P5\n%u %u\n255\n", image.width, image.height);
    for (unsigned i = 0; i < static_cast<unsigned>(image.width) * image.height; ++i)
      std::fputc(gray_of(image.pixels[i]), f);
    std::fclose(f);
  }
}

void run(const char* label, bool pd, const std::vector<uint8_t>& src, double min_corr,
         double max_mae_blur, double min_detail) {
  stop();
  clear_gallery();
  set_mode(Mode::auto_vis);
  set_auto_strategy(AutoStrategy::vis_sync);
  start(145800000);
  const uint32_t bad_before = metrics().bad_vis;
  Signal sig;
  sig.header(pd ? 95 : 8);
  if (pd) sig.pd120_image(src.data()); else sig.robot36_image(src.data());
  sig.silence(400);
  sig.flush();
  require(archive_count() == 1, "pattern: the transmission produced one image");
  const Image image = archived_image(0);
  require(!image.partial && image.identification == Identification::vis, "pattern: complete, VIS-identified");
  require(image.decoded_rows == image.height, "pattern: every row decoded");
  require(image.sync_missed == 0, "pattern: no missed line syncs");
  // Flat gray stretches of a picture (neutral chroma is 1900 Hz, like a VIS leader) start false header
  // attempts. They fail harmlessly but must not be counted or shown as "bad VIS headers".
  require(metrics().bad_vis == bad_before, "pattern: false header attempts inside an image are not counted as bad VIS");
  const Metrics m = compare(image, src);
  std::fprintf(stderr,
               "pattern %-8s %ux%u  MAE=%.2f  MAE(blur3)=%.2f  correlation=%.4f  fine-detail=%.2f  sync %u/%u\n",
               label, image.width, image.height, m.mae, m.mae_blur, m.correlation, m.fine_detail,
               static_cast<unsigned>(image.sync_locked), static_cast<unsigned>(image.sync_missed));
  char name[64];
  std::snprintf(name, sizeof(name), "sstv-pattern-%s-decoded.pgm", label);
  write_pgm(name, image);
  require(m.correlation >= min_corr, "pattern: decoded image correlates with the source");
  require(m.mae_blur <= max_mae_blur, "pattern: mean error is small once the transmitter's pixel bandwidth is allowed for");
  require(m.fine_detail >= min_detail, "pattern: fine horizontal detail survives");
  stop();
  clear_gallery();
}

}  // namespace

int main() {
  const auto src = load_pattern();
  // Thresholds sit a little below the measured results (Robot 36: correlation ~0.95, error 5.1 blurred;
  // PD120: ~0.94, error 7.5 blurred) and far above the 0.42 / 0.48 an uncompensated 3 ms sync lag gave.
  run("robot36", false, src, 0.93, 7.0, 0.6);
  run("pd120", true, src, 0.92, 9.5, 0.8);
  std::puts("SSTV pattern fidelity: PASS");
  return 0;
}
