// Colour regression for every supported mode.
//
// 1. The colour ORC test pattern is transmitted in all 14 modes by an independent encoder (sstv_test_colour.hpp)
//    and the decoded picture is compared with the source, channel by channel.
// 2. A flat-colour stripe card (R, G, B, C, M, Y, white, black) is sent in one mode of each colour layout. It
//    catches swapped or inverted channels, which can still correlate well on a natural picture.
//
// The pattern asset is the same file embedded for the on-device self-test: set SSTV_PATTERN_RGB (the host test
// script does). Decoded pictures are written as PPM files into the launch directory for inspection.
#include "sstv_receiver.hpp"
#include "sstv_test_colour.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace orcsdr::sstv;
using sstv_test::ColourMode;
using sstv_test::require;
using sstv_test::Rgb;
using sstv_test::Signal;

namespace {

std::vector<uint8_t> load_pattern() {
  const char* path = std::getenv("SSTV_PATTERN_RGB");
  if (!path) path = "apps/orcsdr-tab5/main/sstv_testpattern_320x240.rgb";
  std::vector<uint8_t> out(320u * 240u * 3u);
  std::FILE* f = std::fopen(path, "rb");
  if (!f) {
    std::fprintf(stderr, "SSTV FAIL: cannot open colour test pattern %s (set SSTV_PATTERN_RGB)\n", path);
    std::exit(1);
  }
  const size_t n = std::fread(out.data(), 1, out.size(), f);
  std::fclose(f);
  require(n == out.size(), "colour: 320x240 RGB asset has the expected size");
  return out;
}

Rgb unpack(uint16_t px) {
  return {static_cast<uint8_t>(((px >> 11) & 31u) << 3 | ((px >> 11) & 31u) >> 2),
          static_cast<uint8_t>(((px >> 5) & 63u) << 2 | ((px >> 5) & 63u) >> 4),
          static_cast<uint8_t>((px & 31u) << 3 | (px & 31u) >> 2)};
}

void write_ppm(const char* name, const Image& image) {
  if (std::FILE* f = std::fopen(name, "wb")) {
    std::fprintf(f, "P6\n%u %u\n255\n", image.width, image.height);
    for (unsigned i = 0; i < static_cast<unsigned>(image.width) * image.height; ++i) {
      const Rgb c = unpack(image.pixels[i]);
      std::fputc(c.r, f); std::fputc(c.g, f); std::fputc(c.b, f);
    }
    std::fclose(f);
  }
}

struct Score {
  double mae[3] = {0, 0, 0};
  double corr[3] = {0, 0, 0};
};

Score compare(const Image& image, const std::vector<uint8_t>& src) {
  Score s;
  const unsigned w = image.width, h = image.height;
  double n = 0, sa[3] = {}, sb[3] = {}, saa[3] = {}, sbb[3] = {}, sab[3] = {};
  for (unsigned y = 2; y + 2 < h; ++y)
    for (unsigned x = 2; x + 2 < w; ++x) {
      const Rgb d = unpack(image.pixels[y * w + x]);
      const Rgb e = sstv_test::source_at(src.data(), x, y, w, h);
      const double a[3] = {static_cast<double>(d.r), static_cast<double>(d.g), static_cast<double>(d.b)};
      const double b[3] = {static_cast<double>(e.r), static_cast<double>(e.g), static_cast<double>(e.b)};
      for (int c = 0; c < 3; ++c) {
        s.mae[c] += std::fabs(a[c] - b[c]);
        sa[c] += a[c]; sb[c] += b[c]; saa[c] += a[c] * a[c]; sbb[c] += b[c] * b[c]; sab[c] += a[c] * b[c];
      }
      ++n;
    }
  for (int c = 0; c < 3; ++c) {
    s.mae[c] /= n;
    const double cov = sab[c] / n - (sa[c] / n) * (sb[c] / n);
    const double va = saa[c] / n - (sa[c] / n) * (sa[c] / n), vb = sbb[c] / n - (sb[c] / n) * (sb[c] / n);
    s.corr[c] = cov / std::sqrt(va * vb);
  }
  return s;
}

bool start_clean() {
  stop();
  clear_gallery();
  set_mode(Mode::auto_vis);
  set_auto_strategy(AutoStrategy::vis_sync);
  start(145500000);
  return true;
}

// Receives one picture; returns false if nothing was archived.
bool receive(const ColourMode& m, const std::vector<uint8_t>& rgb, Image* out) {
  start_clean();
  Signal sig;
  sstv_test::send_colour(sig, m, rgb.data());
  sig.silence(400);
  sig.flush();
  if (archive_count() != 1) {
    const Metrics met = metrics();
    std::fprintf(stderr, "colour: %s produced %u images; state=%s row=%u/%u vis_ok=%u vis_bad=%u sync_locked=%u missed=%u\n", m.name,
                 static_cast<unsigned>(archive_count()), state_name(met.state), static_cast<unsigned>(met.row),
                 static_cast<unsigned>(met.rows), static_cast<unsigned>(met.good_vis), static_cast<unsigned>(met.bad_vis),
                 static_cast<unsigned>(met.session_sync_locked), static_cast<unsigned>(met.session_sync_missed));
    return false;
  }
  *out = archived_image(0);
  return true;
}

void pattern_all_modes(const std::vector<uint8_t>& src) {
  size_t count = 0;
  const ColourMode* modes = sstv_test::colour_modes(&count);
  const char* only = std::getenv("SSTV_COLOUR_ONLY");   // development aid: one mode by VIS code
  for (size_t i = 0; i < count; ++i) {
    const ColourMode& m = modes[i];
    if (only && std::atoi(only) != m.vis) continue;
    Image image;
    require(receive(m, src, &image), "colour: the transmission produced one image");
    require(image.mode == m.mode && image.identification == Identification::vis, "colour: identified from VIS");
    require(!image.partial && image.decoded_rows == image.height, "colour: complete picture, every row decoded");
    require(image.width == m.width && image.height == m.height, "colour: picture size matches the mode");
    const Score s = compare(image, src);
    std::fprintf(stderr, "colour %-11s %ux%u  corr R/G/B %.3f %.3f %.3f   MAE R/G/B %5.1f %5.1f %5.1f   sync %u/%u\n", m.name,
                 image.width, image.height, s.corr[0], s.corr[1], s.corr[2], s.mae[0], s.mae[1], s.mae[2],
                 static_cast<unsigned>(image.sync_locked), static_cast<unsigned>(image.sync_missed));
    char name[64];
    std::snprintf(name, sizeof(name), "sstv-colour-%u.ppm", static_cast<unsigned>(m.vis));
    write_ppm(name, image);
    for (int c = 0; c < 3; ++c) {
      require(s.corr[c] >= 0.86, "colour: every channel correlates with the source");
      require(s.mae[c] <= 40.0, "colour: every channel's mean error is small");
    }
    require(image.sync_missed == 0, "colour: no missed line syncs");
    stop();
    clear_gallery();
  }
}

// Eight flat stripes: R G B C M Y white black.
std::vector<uint8_t> stripe_card() {
  static const Rgb colours[8] = {{255, 0, 0},   {0, 255, 0},   {0, 0, 255},     {0, 255, 255},
                                 {255, 0, 255}, {255, 255, 0}, {255, 255, 255}, {0, 0, 0}};
  std::vector<uint8_t> out(320u * 240u * 3u);
  for (unsigned y = 0; y < 240; ++y)
    for (unsigned x = 0; x < 320; ++x) {
      const Rgb c = colours[x / 40u];
      uint8_t* p = &out[(static_cast<size_t>(y) * 320u + x) * 3u];
      p[0] = c.r; p[1] = c.g; p[2] = c.b;
    }
  return out;
}

void stripes(const ColourMode& m) {
  static const char* names[8] = {"red", "green", "blue", "cyan", "magenta", "yellow", "white", "black"};
  const auto card = stripe_card();
  Image image;
  require(receive(m, card, &image), "stripes: the transmission produced one image");
  require(!image.partial, "stripes: complete picture");
  write_ppm(m.vis == 8 ? "sstv-colour-stripes-robot36.ppm" : "sstv-colour-stripes.ppm", image);
  const unsigned w = image.width, h = image.height;
  int worst = 0;
  for (unsigned s = 0; s < 8; ++s) {
    const Rgb e = sstv_test::source_at(card.data(), s * 40u + 20u, 120, 320, 240);
    double sum[3] = {};
    unsigned n = 0;
    // The middle of the stripe: edges legitimately blur (channel bandwidth, chroma subsampling).
    for (unsigned y = h / 4; y < h * 3 / 4; ++y)
      for (unsigned x = w * (s * 40u + 10u) / 320u; x < w * (s * 40u + 30u) / 320u; ++x) {
        const Rgb d = unpack(image.pixels[y * w + x]);
        sum[0] += d.r; sum[1] += d.g; sum[2] += d.b; ++n;
      }
    const int dr = static_cast<int>(std::lround(sum[0] / n)) - e.r, dg = static_cast<int>(std::lround(sum[1] / n)) - e.g,
              db = static_cast<int>(std::lround(sum[2] / n)) - e.b;
    worst = std::max({worst, std::abs(dr), std::abs(dg), std::abs(db)});
    std::fprintf(stderr, "   stripe %-8s expect %3u %3u %3u   got %3.0f %3.0f %3.0f\n", names[s], e.r, e.g, e.b, sum[0] / n,
                 sum[1] / n, sum[2] / n);
    require(std::abs(dr) <= 40 && std::abs(dg) <= 40 && std::abs(db) <= 40, "stripes: flat colour is reproduced");
  }
  std::fprintf(stderr, "colour stripes %-11s worst channel error %d\n", m.name, worst);
  stop();
  clear_gallery();
}

}  // namespace

int main() {
  const auto src = load_pattern();
  pattern_all_modes(src);
  size_t count = 0;
  const ColourMode* modes = sstv_test::colour_modes(&count);
  // One mode per colour layout: Robot 36 (interlaced chroma), Robot 72, Martin, Scottie, PD.
  if (!std::getenv("SSTV_COLOUR_ONLY"))
    for (size_t i : {0u, 1u, 3u, 5u, 7u}) stripes(modes[i]);
  std::puts("SSTV colour: PASS");
  return 0;
}
