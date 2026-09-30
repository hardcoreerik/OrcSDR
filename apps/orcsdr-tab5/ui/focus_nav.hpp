#pragma once

#include <cstddef>
#include <cstdint>

// Keyboard focus navigation for an immediate-mode UI. Screens do not keep a widget tree, so each
// screen's own button helper reports the rectangle it just drew (note()), and arrow keys move
// focus spatially among those rectangles. Pure logic: no display, no RTOS, host-tested.
namespace orcsdr::focus_nav {

struct Rect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

enum class Direction : uint8_t { left, right, up, down };

constexpr size_t kCapacity = 96;

// Rectangles outside these limits are decoration (cards, panels, separators), not controls.
constexpr int kMinWidth = 28;
constexpr int kMinHeight = 20;
constexpr int kMaxArea = 120000;

// Called from the drawing helpers. Ignores non-control shapes and exact duplicates.
void note(int x, int y, int w, int h);

// Forget everything (the screen changed or is about to be fully redrawn).
void clear();

size_t count();
bool get(size_t index, Rect* out);

// Moves focus one control in `direction`. With no focus yet, focus starts at the control nearest
// the screen centre. Returns true if focus changed or was first placed.
bool move(Direction direction);

bool focused(Rect* out);
void clear_focus();

// Selects the control containing (or nearest to) a point, e.g. after a touch.
void focus_at(int x, int y);

bool self_check();

}  // namespace orcsdr::focus_nav
