#include "focus_nav.hpp"

#include <algorithm>
#include <cstdlib>

namespace orcsdr::focus_nav {
namespace {

Rect g_rects[kCapacity];
size_t g_count = 0;
int g_focus = -1;

int cx(const Rect& r) { return r.x + r.w / 2; }
int cy(const Rect& r) { return r.y + r.h / 2; }

bool same(const Rect& a, const Rect& b) {
  return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

// Distance from the current control's centre to a candidate's, weighted so a candidate straight
// ahead beats a nearer one that is off to the side. Returns a negative value if the candidate is
// not in the requested direction.
long score(const Rect& from, const Rect& to, Direction direction) {
  long along = 0;
  long across = 0;
  long overlap = 0;  // span the two controls share on the cross axis
  switch (direction) {
    case Direction::right:
      along = to.x - (from.x + from.w);
      if (along < -from.w / 2 || cx(to) <= cx(from)) return -1;
      across = std::labs(static_cast<long>(cy(to) - cy(from)));
      overlap = std::min(from.y + from.h, to.y + to.h) - std::max(from.y, to.y);
      break;
    case Direction::left:
      along = from.x - (to.x + to.w);
      if (along < -from.w / 2 || cx(to) >= cx(from)) return -1;
      across = std::labs(static_cast<long>(cy(to) - cy(from)));
      overlap = std::min(from.y + from.h, to.y + to.h) - std::max(from.y, to.y);
      break;
    case Direction::down:
      along = to.y - (from.y + from.h);
      if (along < -from.h / 2 || cy(to) <= cy(from)) return -1;
      across = std::labs(static_cast<long>(cx(to) - cx(from)));
      overlap = std::min(from.x + from.w, to.x + to.w) - std::max(from.x, to.x);
      break;
    case Direction::up:
      along = from.y - (to.y + to.h);
      if (along < -from.h / 2 || cy(to) >= cy(from)) return -1;
      across = std::labs(static_cast<long>(cx(to) - cx(from)));
      overlap = std::min(from.x + from.w, to.x + to.w) - std::max(from.x, to.x);
      break;
  }
  if (along < 0) along = 0;
  // Controls that line up with the current one are strongly preferred.
  const long penalty = overlap > 0 ? across : across * 3 + 200;
  return along * 2 + penalty + 1;
}

}  // namespace

void note(int x, int y, int w, int h) {
  if (w < kMinWidth || h < kMinHeight || static_cast<long>(w) * h > kMaxArea) return;
  if (x + w <= 0 || y + h <= 0 || x >= 1280 || y >= 720) return;
  const Rect rect{x, y, w, h};
  for (size_t i = 0; i < g_count; ++i)
    if (same(g_rects[i], rect)) return;
  if (g_count >= kCapacity) return;
  g_rects[g_count++] = rect;
}

void clear() {
  g_count = 0;
  g_focus = -1;
}

size_t count() { return g_count; }

bool get(size_t index, Rect* out) {
  if (index >= g_count || out == nullptr) return false;
  *out = g_rects[index];
  return true;
}

bool focused(Rect* out) {
  if (g_focus < 0 || static_cast<size_t>(g_focus) >= g_count || out == nullptr) return false;
  *out = g_rects[g_focus];
  return true;
}

void clear_focus() { g_focus = -1; }

void focus_at(int x, int y) {
  int best = -1;
  long best_distance = 0;
  for (size_t i = 0; i < g_count; ++i) {
    const Rect& r = g_rects[i];
    if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
      // Smallest containing control wins (a button on top of a wider row).
      const long area = static_cast<long>(r.w) * r.h;
      if (best < 0 || area < best_distance) {
        best = static_cast<int>(i);
        best_distance = area;
      }
    }
  }
  if (best >= 0) g_focus = best;
}

bool move(Direction direction) {
  if (g_count == 0) return false;
  if (g_focus < 0 || static_cast<size_t>(g_focus) >= g_count) {
    // First placement: the control nearest the screen centre.
    long best = -1;
    for (size_t i = 0; i < g_count; ++i) {
      const long dx = cx(g_rects[i]) - 640;
      const long dy = cy(g_rects[i]) - 360;
      const long d = dx * dx + dy * dy;
      if (best < 0 || d < best) {
        best = d;
        g_focus = static_cast<int>(i);
      }
    }
    return true;
  }
  const Rect& from = g_rects[g_focus];
  int best_index = -1;
  long best_score = 0;
  for (size_t i = 0; i < g_count; ++i) {
    if (static_cast<int>(i) == g_focus) continue;
    const long s = score(from, g_rects[i], direction);
    if (s < 0) continue;
    if (best_index < 0 || s < best_score) {
      best_index = static_cast<int>(i);
      best_score = s;
    }
  }
  if (best_index < 0) return false;
  g_focus = best_index;
  return true;
}

bool self_check() {
  clear();
  // 3 x 2 grid of buttons.
  for (int row = 0; row < 2; ++row)
    for (int col = 0; col < 3; ++col) note(100 + col * 220, 200 + row * 100, 200, 80);
  Rect r;
  bool ok = count() == 6 && move(Direction::right) && focused(&r);   // nearest the centre
  clear_focus();
  focus_at(120, 220);                                                // top-left
  ok = ok && move(Direction::right) && focused(&r) && r.x == 320 && r.y == 200;
  ok = ok && move(Direction::down) && focused(&r) && r.x == 320 && r.y == 300;
  ok = ok && move(Direction::left) && focused(&r) && r.x == 100 && r.y == 300;
  ok = ok && !move(Direction::left);                                 // at the left edge
  ok = ok && move(Direction::up) && focused(&r) && r.x == 100 && r.y == 200;
  ok = ok && !move(Direction::up);
  clear();
  return ok;
}

}  // namespace orcsdr::focus_nav
