#include "orcsdr_ui/painter.hpp"
#include <algorithm>
#include <cmath>

namespace orcsdr::ui {

int Painter::X(int x) const { return static_cast<int>(std::lround((x - ox_) * sx_)); }
int Painter::Y(int y) const { return static_cast<int>(std::lround((y - oy_) * sy_)); }
int Painter::W(int w) const { return std::max(1, static_cast<int>(std::lround(w * sx_))); }
int Painter::H(int h) const { return std::max(1, static_cast<int>(std::lround(h * sy_))); }
float Painter::R(int r) const { return r * 0.5f * (sx_ + sy_); }

void Painter::drawPixel(int x, int y, uint16_t c) { plots::set_pixel(s_, X(x), Y(y), c); }

void Painter::drawFastHLine(int x, int y, int w, uint16_t c) {
  plots::hline(s_, X(x), Y(y), W(w), c);
}
void Painter::drawFastVLine(int x, int y, int h, uint16_t c) {
  plots::vline(s_, X(x), Y(y), H(h), c);
}
void Painter::drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
  plots::line(s_, X(x0), Y(y0), X(x1), Y(y1), c);
}
void Painter::fillRect(int x, int y, int w, int h, uint16_t c) {
  const int px = X(x), py = Y(y), pw = std::max(1, X(x + w) - px), ph = std::max(1, Y(y + h) - py);
  for (int j = 0; j < ph; ++j) plots::hline(s_, px, py + j, pw, c);
}
void Painter::drawRect(int x, int y, int w, int h, uint16_t c) {
  const int px = X(x), py = Y(y), pw = std::max(1, X(x + w) - px), ph = std::max(1, Y(y + h) - py);
  plots::hline(s_, px, py, pw, c);
  plots::hline(s_, px, py + ph - 1, pw, c);
  plots::vline(s_, px, py, ph, c);
  plots::vline(s_, px + pw - 1, py, ph, c);
}

namespace {
// True if (dx, dy) relative to a corner centre is outside a circle of radius r.
bool outside_corner(float dx, float dy, float r) { return dx * dx + dy * dy > r * r; }
}  // namespace

void Painter::fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
  const int px = X(x), py = Y(y), pw = std::max(1, X(x + w) - px), ph = std::max(1, Y(y + h) - py);
  const float rr = std::min({R(r), pw / 2.0f, ph / 2.0f});
  for (int j = 0; j < ph; ++j) {
    for (int i = 0; i < pw; ++i) {
      float dx = 0, dy = 0;
      if (i < rr) dx = rr - i - 0.5f; else if (i >= pw - rr) dx = i - (pw - rr) + 0.5f;
      if (j < rr) dy = rr - j - 0.5f; else if (j >= ph - rr) dy = j - (ph - rr) + 0.5f;
      if (dx > 0 && dy > 0 && outside_corner(dx, dy, rr)) continue;
      plots::set_pixel(s_, px + i, py + j, c);
    }
  }
}
void Painter::drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
  const int px = X(x), py = Y(y), pw = std::max(1, X(x + w) - px), ph = std::max(1, Y(y + h) - py);
  const float rr = std::min({R(r), pw / 2.0f, ph / 2.0f});
  for (int j = 0; j < ph; ++j) {
    for (int i = 0; i < pw; ++i) {
      const bool edge = i == 0 || j == 0 || i == pw - 1 || j == ph - 1;
      float dx = 0, dy = 0;
      if (i < rr) dx = rr - i - 0.5f; else if (i >= pw - rr) dx = i - (pw - rr) + 0.5f;
      if (j < rr) dy = rr - j - 0.5f; else if (j >= ph - rr) dy = j - (ph - rr) + 0.5f;
      if (dx > 0 && dy > 0) {
        // In a corner: draw the pixels whose distance is within one pixel of the arc.
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d <= rr && d > rr - 1.0f) plots::set_pixel(s_, px + i, py + j, c);
      } else if (edge) {
        plots::set_pixel(s_, px + i, py + j, c);
      }
    }
  }
}

void Painter::fillCircle(int x, int y, int r, uint16_t c) {
  const int cx = X(x), cy = Y(y);
  const float rr = std::max(0.5f, R(r));
  const int n = static_cast<int>(std::ceil(rr));
  for (int j = -n; j <= n; ++j)
    for (int i = -n; i <= n; ++i)
      if (i * i + j * j <= rr * rr + 0.25f) plots::set_pixel(s_, cx + i, cy + j, c);
}
void Painter::drawCircle(int x, int y, int r, uint16_t c) {
  const int cx = X(x), cy = Y(y);
  const float rr = std::max(0.5f, R(r));
  const int n = static_cast<int>(std::ceil(rr)) + 1;
  for (int j = -n; j <= n; ++j)
    for (int i = -n; i <= n; ++i) {
      const float d = std::sqrt(static_cast<float>(i * i + j * j));
      if (d <= rr + 0.5f && d > rr - 0.5f) plots::set_pixel(s_, cx + i, cy + j, c);
    }
}

void Painter::drawArc(int x, int y, int r0, int r1, float a0, float a1, uint16_t c) {
  const int cx = X(x), cy = Y(y);
  float inner = R(std::min(r0, r1)), outer = R(std::max(r0, r1));
  if (outer - inner < 1.0f) { inner = outer - 1.0f; }
  const int n = static_cast<int>(std::ceil(outer)) + 1;
  auto norm = [](float a) { a = std::fmod(a, 360.0f); return a < 0 ? a + 360.0f : a; };
  const float s = norm(a0), e = norm(a1);
  for (int j = -n; j <= n; ++j)
    for (int i = -n; i <= n; ++i) {
      const float d = std::sqrt(static_cast<float>(i * i + j * j));
      if (d > outer + 0.5f || d <= inner - 0.5f) continue;
      const float a = norm(std::atan2(static_cast<float>(j), static_cast<float>(i)) * 57.2957795f);
      const bool in = s <= e ? (a >= s && a <= e) : (a >= s || a <= e);
      if (in) plots::set_pixel(s_, cx + i, cy + j, c);
    }
}

void Painter::fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
  int ax = X(x0), ay = Y(y0), bx = X(x1), by = Y(y1), cx = X(x2), cy = Y(y2);
  const int minx = std::min({ax, bx, cx}), maxx = std::max({ax, bx, cx});
  const int miny = std::min({ay, by, cy}), maxy = std::max({ay, by, cy});
  const auto edge = [](int px, int py, int qx, int qy, int rx, int ry) {
    return (qx - px) * (ry - py) - (qy - py) * (rx - px);
  };
  const int area = edge(ax, ay, bx, by, cx, cy);
  if (area == 0) {
    plots::line(s_, ax, ay, bx, by, c);
    plots::line(s_, bx, by, cx, cy, c);
    return;
  }
  for (int py = miny; py <= maxy; ++py)
    for (int px = minx; px <= maxx; ++px) {
      const int w0 = edge(bx, by, cx, cy, px, py), w1 = edge(cx, cy, ax, ay, px, py), w2 = edge(ax, ay, bx, by, px, py);
      const bool in = area > 0 ? (w0 >= 0 && w1 >= 0 && w2 >= 0) : (w0 <= 0 && w1 <= 0 && w2 <= 0);
      if (in) plots::set_pixel(s_, px, py, c);
    }
}

}  // namespace orcsdr::ui
