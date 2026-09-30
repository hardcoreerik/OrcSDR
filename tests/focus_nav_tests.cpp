#include <cstdio>
#include <cstdlib>

#include "focus_nav.hpp"

namespace {

[[noreturn]] void fail(const char* expression, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}
#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)

using namespace orcsdr::focus_nav;

bool at(int x, int y) {
  Rect r;
  return focused(&r) && r.x == x && r.y == y;
}

void test_grid_navigation() {
  clear();
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 4; ++col) note(40 + col * 300, 150 + row * 120, 260, 90);
  CHECK(count() == 12);
  focus_at(60, 170);
  CHECK(at(40, 150));
  CHECK(move(Direction::right) && at(340, 150));
  CHECK(move(Direction::right) && at(640, 150));
  CHECK(move(Direction::down) && at(640, 270));
  CHECK(move(Direction::down) && at(640, 390));
  CHECK(!move(Direction::down) && at(640, 390));      // bottom edge: stays put
  CHECK(move(Direction::left) && at(340, 390));
  CHECK(move(Direction::up) && at(340, 270));
  CHECK(move(Direction::left) && move(Direction::left) == false && at(40, 270));
}

void test_rail_and_content() {
  clear();
  // Left rail of five items, content grid to the right, tab bar along the bottom.
  for (int i = 0; i < 5; ++i) note(20, 115 + i * 66, 290, 58);
  note(330, 145, 888, 48);
  note(330, 205, 888, 48);
  note(330, 265, 430, 48);
  note(790, 265, 428, 48);
  for (int i = 0; i < 5; ++i) note(i * 256 + 4, 634, 248, 82);
  focus_at(30, 120 + 66 * 2);                          // third rail item, y ~ 247..305
  CHECK(at(20, 247));
  CHECK(move(Direction::right));                       // lands on the row aligned with it
  Rect r;
  CHECK(focused(&r) && r.x == 330 && r.y == 265);
  CHECK(move(Direction::right) && at(790, 265));
  CHECK(move(Direction::up) && at(330, 205));                // wide row above overlaps it
  focus_at(340, 275);
  CHECK(move(Direction::up) && at(330, 205));
  CHECK(move(Direction::down) && at(330, 265));
  CHECK(move(Direction::left) && at(20, 247));               // back to the rail item on that row
  focus_at(340, 275);
  CHECK(move(Direction::down));                        // reaches the bottom tab bar
  CHECK(focused(&r) && r.y == 634);
}

void test_overlap_beats_proximity() {
  clear();
  note(100, 300, 200, 60);    // current
  note(340, 300, 200, 60);    // same row, further
  note(320, 380, 200, 60);    // closer diagonally but on the next row
  focus_at(120, 320);
  CHECK(move(Direction::right) && at(340, 300));
}

void test_decoration_ignored() {
  clear();
  note(0, 0, 1280, 720);       // full-screen background
  note(24, 108, 520, 286);     // large card
  note(50, 50, 10, 10);        // tiny
  note(50, 50, 200, 10);       // too short
  note(1300, 100, 100, 50);    // off screen
  CHECK(count() == 0);
  note(50, 50, 200, 50);
  CHECK(count() == 1);
}

void test_duplicates_and_capacity() {
  clear();
  for (int i = 0; i < 5; ++i) note(10, 10, 100, 40);
  CHECK(count() == 1);
  for (int i = 0; i < 200; ++i) note(i * 3, 100 + i, 60, 30);
  CHECK(count() == kCapacity);
}

void test_focus_at_prefers_smallest() {
  clear();
  note(20, 200, 1200, 60);     // wide row
  note(1000, 205, 200, 50);    // button sitting inside it
  focus_at(1050, 220);
  CHECK(at(1000, 205));
  focus_at(100, 220);
  CHECK(at(20, 200));
  focus_at(5, 5);              // nothing there: focus unchanged
  CHECK(at(20, 200));
}

void test_empty_and_reset() {
  clear();
  CHECK(!move(Direction::right));
  Rect r;
  CHECK(!focused(&r));
  note(600, 340, 100, 50);
  CHECK(move(Direction::left) && focused(&r));   // first press just places focus
  clear();
  CHECK(!focused(&r) && count() == 0);
  CHECK(!get(0, &r));
  CHECK(!get(0, nullptr));
}

}  // namespace

int main() {
  test_grid_navigation();
  test_rail_and_content();
  test_overlap_beats_proximity();
  test_decoration_ignored();
  test_duplicates_and_capacity();
  test_focus_at_prefers_smallest();
  test_empty_and_reset();
  CHECK(orcsdr::focus_nav::self_check());
  std::puts("focus_nav_tests: PASS");
  return 0;
}
