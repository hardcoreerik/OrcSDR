// Runs the actual lora_tx_views.hpp M5GFX drawing calls against the repository
// recording display. It checks layout geometry and truthful blocked-TX labels.
// Host rendering does not establish on-device font/panel, touch, or RF acceptance.
#include <M5Unified.h>
#include "lora_tx_views.hpp"
#include <array>
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
std::vector<std::string> rendered_message_text(int x0, int y0, int w0, int h0) {
  std::vector<std::string> result;
  for (const auto& p : host_display::prims) {
    if (p.kind != Prim::text) continue;
    int x, y, w, h;
    if (host_display::effective(p, &x, &y, &w, &h) && x < x0 + w0 && x + w > x0 &&
        y < y0 + h0 && y + h > y0)
      result.push_back(p.str);
  }
  return result;
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
void test_verified_message_rendering() {
  Snapshot s{};
  s.event_count = 1;
  auto& event = s.events[0];
  event.verified = true;
  event.frequency_hz = 433920000;
  event.sender = 0x1234;
  event.packet_id = 0x5678;
  event.received_ms = 100;
  event.port = 1;
  std::snprintf(event.text, sizeof(event.text), "%s",
      "Long received message wraps safely across bounded lines; nearby controls stay visible after truncation.");
  for (const bool vertical : {false, true}) {
    const int card_x = vertical ? 48 : 660;
    const int card_y = vertical ? 636 : 318;
    const int card_w = vertical ? 470 : 544;
    const int card_h = vertical ? 160 : 175;
    host_display::reset();
    ui::draw_values(M5.Display, s, ui::Page::messenger, vertical);
    assert(shows("Long received"));
    assert(shows("..."));
    assert(shows("RTL-SDR FROM 00001234"));
    assert(shows("TX COMPOSER UNAVAILABLE"));
    std::vector<std::string> initial_card;
    bool composer_outside_card = false;
    std::vector<std::array<int, 4>> card_text_bounds;
    for (const auto& p : host_display::prims) {
      if (p.kind != Prim::text) continue;
      int x, y, w, h;
      assert(host_display::effective(p, &x, &y, &w, &h));
      if (p.str.find("TX COMPOSER UNAVAILABLE") != std::string::npos) {
        assert(y >= card_y + card_h);
        composer_outside_card = true;
      } else if (x < card_x + card_w && x + w > card_x &&
                 y < card_y + card_h && y + h > card_y) {
        assert(x >= card_x && x + w <= card_x + card_w);
        assert(y >= card_y && y + h <= card_y + card_h);
        card_text_bounds.push_back({x, y, w, h});
        initial_card.push_back(p.str);
      }
    }
    assert(composer_outside_card);
    for (size_t i = 0; i < card_text_bounds.size(); ++i) {
      for (size_t j = i + 1; j < card_text_bounds.size(); ++j) {
        const auto& a = card_text_bounds[i];
        const auto& b = card_text_bounds[j];
        assert(a[0] + a[2] <= b[0] || b[0] + b[2] <= a[0] ||
               a[1] + a[3] <= b[1] || b[1] + b[3] <= a[1]);
      }
    }

    // The partial redraw used for an ordinary message revision must emit the
    // same message-card text as the initial page draw, without touching status.
    host_display::reset();
    ui::draw_rx_message(M5.Display, s, vertical);
    assert(rendered_message_text(card_x, card_y, card_w, card_h) == initial_card);
    assert(!shows("TX COMPOSER UNAVAILABLE"));
  }

  event.verified = false;
  host_display::reset();
  ui::draw_values(M5.Display, s, ui::Page::messenger, false);
  assert(shows("UNVERIFIED EVENT / TEXT HIDDEN"));
  assert(!shows("Long received"));

  event.encrypted = true;
  host_display::reset();
  ui::draw_values(M5.Display, s, ui::Page::messenger, false);
  assert(shows("ENCRYPTED / TEXT UNAVAILABLE"));
  assert(!shows("Long received"));

  event.verified = true;
  event.port = 0;
  std::snprintf(event.text, sizeof(event.text), "ENCRYPTED FRAME");
  host_display::reset();
  ui::draw_values(M5.Display, s, ui::Page::messenger, false);
  assert(shows("ENCRYPTED / TEXT UNAVAILABLE"));
  assert(!shows("ENCRYPTED FRAME"));

  event.port = 1;
  event.source = orcsdr::lora::PacketSource::sx1262;
  std::snprintf(event.text, sizeof(event.text), "Decrypted text payload");
  host_display::reset();
  ui::draw_values(M5.Display, s, ui::Page::messenger, false);
  assert(shows("Decrypted text payload"));
  assert(shows("SX1262 FROM 00001234"));
}
void test_latest_event_identity() {
  Snapshot a{}, b{};
  a.event_count = b.event_count = 1;
  a.events[0].verified = b.events[0].verified = true;
  a.events[0].sender = b.events[0].sender = 1;
  a.events[0].packet_id = b.events[0].packet_id = 2;
  a.events[0].received_ms = b.events[0].received_ms = 3;
  assert(ui::same_latest_verified_rx_event(a, b));
  b.events[0].source = orcsdr::lora::PacketSource::sx1262;
  assert(!ui::same_latest_verified_rx_event(a, b));
  b.events[0].source = a.events[0].source;
  b.events[0].frequency_hz = 915000000;
  assert(!ui::same_latest_verified_rx_event(a, b));
}
int main() {
  test_navigation_and_gate();
  test_dimensions_and_truth();
  test_verified_message_rendering();
  test_latest_event_identity();
  std::puts("lora tx view recording-host tests: PASS");
}
