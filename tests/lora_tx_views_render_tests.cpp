// Runs the actual lora_tx_views.hpp M5GFX drawing calls against the repository
// recording display. It checks layout geometry and truthful blocked-TX labels.
// Host rendering does not establish on-device font/panel, touch, or RF acceptance.
#include <M5Unified.h>
#include "lora_tx_views.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>

namespace ui = orcsdr::lora::tx_views;
using orcsdr::lora::Snapshot;
using orcsdr::lora_tx::Readiness;

bool shows(const char* part) {
  for (const auto& p : host_display::prims)
    if (p.kind == Prim::text && p.str.find(part) != std::string::npos) return true;
  return false;
}

void test_dimensions_and_truth() {
  Snapshot s{};
  s.tx.readiness = Readiness::profile_only;
  s.frequency_hz = 906875000;
  s.sf = 11;
  s.bandwidth_hz = 250000;
  for (bool vertical : {false, true}) {
    const int width = vertical ? 568 : 1280;
    const int height = vertical ? 1232 : 720;
    for (int page_id = 0; page_id < 3; ++page_id) {
      host_display::reset();
      auto page = static_cast<ui::Page>(page_id);
      ui::draw_static(M5.Display, s, page, vertical);
      ui::menu(M5.Display, page, vertical);
      ui::draw_values(M5.Display, s, page, vertical);
      assert(!host_display::prims.empty());
      assert(shows("TX LAB") == false || !vertical);
      assert(shows("ARM LOCKED"));
      assert(shows("SEND LOCKED"));
      assert(!shows("TX READY"));
      assert(!shows("SUCCESS"));
      for (const auto& p : host_display::prims) {
        int x, y, w, h;
        if (!host_display::effective(p, &x, &y, &w, &h)) continue;
        if (x < 0 || y < 0 || x + w > width || y + h > height) {
          std::fprintf(stderr, "out-of-bounds %dx%d page=%d kind=%d at %d,%d %dx%d text=%s\n",
                       width, height, page_id, p.kind, x,y,w,h,p.str.c_str());
          assert(false);
        }
      }
      // All cards and values remain out of the shared native header/footer.
      for (const auto& p : host_display::prims) {
        int x, y, w, h;
        if (!host_display::effective(p, &x, &y, &w, &h)) continue;
        assert(y >= (vertical ? 155 : 138));
        assert(y + h <= (vertical ? 1125 : 620));
      }
    }
  }
}
void test_navigation_and_gate() {
  assert(ui::portrait(568, 1232));
  assert(!ui::portrait(1280, 720));
  assert(!ui::portrait(800, 480));
  assert(ui::page_at(700, 180, false) == 0);
  assert(ui::page_at(950, 180, false) == 1);
  assert(ui::page_at(1120, 180, false) == 2);
  assert(ui::page_at(120, 185, true) == 0);
  assert(ui::page_at(250, 185, true) == 1);
  assert(ui::page_at(480, 185, true) == 2);
  assert(ui::page_at(20, 20, true) == -1);
  assert(ui::arm_rect(120, 1060, true));
  assert(ui::send_rect(400, 1060, true));
  Snapshot s{};
  s.tx.readiness = Readiness::profile_only;
  assert(orcsdr::lora_tx::can_arm(s.tx) != orcsdr::lora_tx::Denial::none);
  assert(orcsdr::lora_tx::can_send(s.tx, ui::kBenchPacketBytes) !=
         orcsdr::lora_tx::Denial::none);
}
int main() {
  test_navigation_and_gate();
  test_dimensions_and_truth();
  std::puts("lora tx view recording-host tests: PASS");
}
