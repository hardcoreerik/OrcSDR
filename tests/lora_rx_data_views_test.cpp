#include <M5Unified.h>
#include "lora_rx_data_views.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

namespace ui = orcsdr::lora::rx_data_views;
namespace layout = orcsdr::lora::rx_layout;
using orcsdr::lora::Snapshot;
using orcsdr::audio_header::HeaderRect;

bool contains(const char* text) {
  return std::any_of(host_display::prims.begin(), host_display::prims.end(), [text](const Prim& p) {
    return p.kind == Prim::text && p.str.find(text) != std::string::npos;
  });
}
bool has_full_clear(int width, int height) {
  return std::any_of(host_display::prims.begin(), host_display::prims.end(), [=](const Prim& p) {
    return p.kind == Prim::fill && p.x <= 0 && p.y <= 0 && p.x + p.w >= width && p.y + p.h >= height;
  });
}
void assert_text_inside(HeaderRect bounds) {
  for (const Prim& p : host_display::prims) {
    if (p.kind != Prim::text) continue;
    int x, y, w, h;
    assert(host_display::effective(p, &x, &y, &w, &h));
    if (x < bounds.x || y < bounds.y || x + w > bounds.x + bounds.w || y + h > bounds.y + bounds.h) {
      std::fprintf(stderr, "text outside %d,%d %dx%d: %s at %d,%d %dx%d\n",
                   bounds.x, bounds.y, bounds.w, bounds.h, p.str.c_str(), x, y, w, h);
      assert(false);
    }
  }
}
Snapshot populated() {
  Snapshot s{};
  s.node_count = 10;
  s.event_count = 10;
  for (unsigned i = 0; i < s.node_count; ++i) {
    auto& n = s.nodes[i];
    n.id = 0x12340000u + i;
    n.seen_ms = 12000u + i * 1000u;
    n.latitude_e7 = 451234567;
    n.longitude_e7 = -1229876543;
    n.signal_tenths = -523;
    n.snr_tenths = 75;
    n.altitude_m = 123;
    n.battery_percent = 87;
    n.hops = 4;
    n.favorite = (i == 0);
    std::snprintf(n.name, sizeof(n.name), "Long Mesh Node Name %02u", i);
  }
  s.selected_node = 0;
  for (unsigned i = 0; i < s.event_count; ++i) {
    auto& e = s.events[i];
    e.source = i % 2 ? orcsdr::lora::PacketSource::sx1262 : orcsdr::lora::PacketSource::rtl_sdr;
    e.frequency_hz = 906875000;
    e.sender = s.nodes[i].id;
    e.destination = 0xabcdef00u + i;
    e.packet_id = 0x98760000u + i;
    e.received_ms = 20000u + i * 1000u;
    e.received_utc = 1730000000u + i;
    e.signal_tenths = -611;
    e.snr_tenths = 82;
    e.port = 1;
    e.verified = true;
    std::snprintf(e.text, sizeof(e.text),
      "A long verified packet message with enough words to wrap cleanly inside a narrow row while controls fit.");
  }
  return s;
}
ui::DataState all_items() {
  ui::DataState state{};
  state.count = 10;
  for (unsigned i = 0; i < state.count; ++i) state.indices[i] = static_cast<uint8_t>(i);
  return state;
}

void test_viewports() {
  const HeaderRect viewports[] = {
    {72, 0, 424, 1064}, {72, 0, 1088, 424}, {0, 0, 1280, 720},
    {0, 0, 800, 480}, {0, 0, 480, 800},
  };
  constexpr uint32_t now = 65000;
  for (HeaderRect viewport : viewports) {
    for (auto view : {orcsdr::lora::View::nodes, orcsdr::lora::View::traffic}) {
      Snapshot s=populated();
      const layout::Layout l = layout::make(viewport, view, false);
      assert(l.valid);
      host_display::reset();
      ui::Cache cache{};
      ui::DataState state = all_items();
      if (view == orcsdr::lora::View::nodes) {
        ui::draw_nodes_static(M5.Display, l);
        ui::draw_nodes_values(M5.Display, l, s, state, cache, now);
        assert(contains("MESH NODES") || contains("NODES / DETAILS"));
        if(!l.single_node_panel)assert(contains("!12340000"));
        assert(contains("H4") || contains("HOPS 4"));
        if(!l.single_node_panel)assert(contains("45.12346"));
        if(!l.single_node_panel)assert(contains("ALT 123 m"));
        if(!l.single_node_panel)assert(contains("BAT 87%") || contains("BAT 91%"));
        if(!l.single_node_panel)assert(contains("SNR 7.5"));
        assert_text_inside(l.body);
        for (unsigned i = 0; i < unsigned(l.node_row_count); ++i)
          assert(l.node_rows[i].w > 0 && l.node_rows[i].h >= 44);
        assert(l.node_previous.w >= 44 && l.node_previous.h >= 44);
        assert(l.node_next.w >= 44 && l.node_next.h >= 44);
        assert(sizeof(cache) <= 128);
        assert(!has_full_clear(viewport.w, viewport.h));

        host_display::reset();
        ui::draw_nodes_values(M5.Display, l, s, state, cache, now);
        assert(host_display::prims.empty()); // unchanged tick draws nothing
        s.nodes[0].battery_percent = 91;
        host_display::reset();
        ui::draw_nodes_values(M5.Display, l, s, state, cache, now);
        assert(contains("B91%"));
        assert(!has_full_clear(viewport.w, viewport.h));
        if (l.single_node_panel) {
          state.details = true;
          cache = {};
          host_display::reset();
          ui::draw_nodes_values(M5.Display, l, s, state, cache, now);
          assert(contains("LON -122.98765"));
          assert_text_inside(l.nodes_list);
        }
      } else {
        ui::draw_traffic_static(M5.Display, l);
        ui::draw_traffic_values(M5.Display, l, s, state, cache, now);
        assert(contains("FROM"));
        if(l.traffic_row_count>1)assert(contains("SX1262"));
        assert(contains("98760000"));
        assert(contains("A long verified"));
        assert(contains("03:33:20Z") || contains("TIME NOT SET"));
        assert_text_inside(l.body);
        for (unsigned i = 0; i < unsigned(l.traffic_row_count); ++i)
          assert(l.traffic_rows[i].w > 0 && l.traffic_rows[i].h >= 44);
        assert(l.traffic_previous.w >= 44 && l.traffic_previous.h >= 44);
        assert(l.traffic_next.w >= 44 && l.traffic_next.h >= 44);
        assert(sizeof(cache) <= 128);
        assert(!has_full_clear(viewport.w, viewport.h));

        host_display::reset();
        ui::draw_traffic_values(M5.Display, l, s, state, cache, now);
        assert(host_display::prims.empty());
        state.offset = 1;
        host_display::reset();
        ui::draw_traffic_values(M5.Display, l, s, state, cache, now);
        assert(!host_display::prims.empty());
        assert(!has_full_clear(viewport.w, viewport.h));
        state.details = true;
        cache = {};
        host_display::reset();
        ui::draw_traffic_values(M5.Display, l, s, state, cache, now);
        assert(contains("TO !ABCDEF01"));
        assert_text_inside(l.body);
        s.events[1].verified = false;
        state.offset = 1;
        cache = {};
        host_display::reset();
        ui::draw_traffic_values(M5.Display, l, s, state, cache, now);
        assert(contains("UNVERIFIED / TEXT HIDDEN"));
        for(const auto& primitive:host_display::prims){
          if(primitive.kind!=Prim::text)continue;
          const auto first=l.traffic_rows[0];
          if(primitive.y>=first.y&&primitive.y<first.y+first.h)
            assert(primitive.str.find("A long verified")==std::string::npos);
        }
      }
    }
  }
}

void test_pagination_and_empty_states() {
  Snapshot s = populated();
  const layout::Layout nodes = layout::make({0, 0, 800, 480}, orcsdr::lora::View::nodes, false);
  ui::DataState state = all_items();
  state.offset = 7;
  ui::Cache cache{};
  host_display::reset();
  ui::draw_nodes_static(M5.Display, nodes);
  ui::draw_nodes_values(M5.Display, nodes, s, state, cache, 65000);
  assert(contains("NEXT"));
  s.nodes[0].battery_percent=UINT8_MAX;s.nodes[0].hops=UINT8_MAX;
  cache={};host_display::reset();
  ui::draw_nodes_values(M5.Display,nodes,s,state,cache,65000);
  assert(contains("BAT -- HOPS --")&&!contains("255%"));
  state = {};
  cache = {};
  host_display::reset();
  ui::draw_nodes_values(M5.Display, nodes, s, state, cache, 65000);
  assert(contains("NO NODES"));

  const layout::Layout traffic = layout::make({0, 0, 800, 480}, orcsdr::lora::View::traffic, false);
  cache = {};
  host_display::reset();
  ui::draw_traffic_static(M5.Display, traffic);
  ui::draw_traffic_values(M5.Display, traffic, s, state, cache, 65000);
  assert(contains("NO PACKETS"));
  assert_text_inside(traffic.body);
}

int main() {
  static_assert(sizeof(ui::Cache) <= 128, "view cache must remain bounded");
  test_viewports();
  test_pagination_and_empty_states();
  std::puts("LoRa RX data view recording tests: PASS");
}
