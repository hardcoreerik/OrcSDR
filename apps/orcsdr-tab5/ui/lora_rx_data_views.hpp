#pragma once

#include "lora_rx_layout.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <M5Unified.h>

namespace orcsdr::lora::rx_data_views {
using rx_layout::Layout;
using rx_layout::Rect;

constexpr uint16_t kBg = 0x0000, kPanel = 0x0841, kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8, kYellow = 0xff24, kMuted = 0x8c71;
constexpr uint16_t kGrid = 0x2945, kWhite = 0xffff, kSelected = 0x1264;
constexpr size_t kRows = 6;

struct DataState {
  uint8_t indices[kNodeCapacity]{};
  uint8_t count = 0;
  unsigned offset = 0;
  bool details = false;
};

// A renderer can use one Cache for both sections. Keys describe only rendered
// fields, so an unrelated Snapshot revision does not repaint these widgets.
struct Cache {
  uint32_t node_rows[kRows]{};
  uint32_t node_detail = 0;
  uint32_t node_pages = 0;
  uint32_t traffic_rows[kRows]{};
  uint32_t traffic_pages = 0;
  bool node_detail_valid = false;
  bool node_pages_valid = false;
  bool traffic_pages_valid = false;
};

namespace detail {
inline uint32_t hash_byte(uint32_t h, uint8_t b) { return (h ^ b) * 16777619u; }
inline uint32_t hash_u32(uint32_t h, uint32_t v) {
  for (unsigned i = 0; i < 4; ++i) h = hash_byte(h, static_cast<uint8_t>(v >> (i * 8)));
  return h;
}
inline uint32_t hash_i32(uint32_t h, int32_t v) { return hash_u32(h, static_cast<uint32_t>(v)); }
inline uint32_t hash_text(uint32_t h, const char* text) {
  while (*text) h = hash_byte(h, static_cast<uint8_t>(*text++));
  return hash_byte(h, 0xff);
}
inline uint32_t finish(uint32_t h) { return h ? h : 1; }

template <typename G> void prepare(G& gfx) {
  gfx.setTextSize(2); // 16 px minimum on the native and scaled renderers.
  gfx.setTextDatum(middle_left);
}
template <typename G> int text_width(G& gfx, const char* text) {
  return static_cast<int>(gfx.textWidth(text));
}
template <typename G> void fitted_text(G& gfx, const char* source, int x, int y, int width,
                                       uint16_t color = kWhite) {
  if (width <= 0) return;
  char text[192]{};
  const size_t source_len = std::min(std::strlen(source), sizeof(text) - 1);
  std::memcpy(text, source, source_len);
  gfx.setTextColor(color);
  if (text_width(gfx, text) > width) {
    size_t n = source_len;
    for (;;) {
      if (n) {
        --n;
        while (n && (static_cast<unsigned char>(source[n]) & 0xc0u) == 0x80u) --n;
      }
      std::memcpy(text, source, n);
      text[n] = 0;
      if (n + 3 < sizeof(text)) { text[n] = '.'; text[n + 1] = '.'; text[n + 2] = '.'; text[n + 3] = 0; }
      if (text_width(gfx, text) <= width || n == 0) break;
    }
  }
  gfx.drawString(text, x, y);
}
template <typename G> void wrapped_text(G& gfx, const char* source, int x, int y, int width,
                                        unsigned rows, uint16_t color = kWhite) {
  char remaining[192]{};
  std::snprintf(remaining, sizeof(remaining), "%s", source);
  for (unsigned row = 0; row < rows && remaining[0]; ++row) {
    if (text_width(gfx, remaining) <= width) {
      fitted_text(gfx, remaining, x, y + static_cast<int>(row) * 24, width, color);
      break;
    }
    size_t cut = std::strlen(remaining);
    char candidate[192]{};
    for (;;) {
      std::memcpy(candidate, remaining, cut); candidate[cut] = 0;
      if (text_width(gfx, candidate) <= width || cut == 0) break;
      --cut;
      while (cut && (static_cast<unsigned char>(remaining[cut]) & 0xc0u) == 0x80u) --cut;
    }
    size_t split = cut;
    while (split && remaining[split] != ' ') --split;
    if (split) cut = split;
    if (row + 1 == rows || cut == 0) {
      fitted_text(gfx, remaining, x, y + static_cast<int>(row) * 24, width, color);
      break;
    }
    std::memcpy(candidate, remaining, cut); candidate[cut] = 0;
    fitted_text(gfx, candidate, x, y + static_cast<int>(row) * 24, width, color);
    size_t next = cut;
    while (remaining[next] == ' ') ++next;
    std::memmove(remaining, remaining + next, std::strlen(remaining + next) + 1);
  }
}
template <typename G> void centered_text(G& gfx, const char* text, Rect r, uint16_t color) {
  if (r.w <= 0 || r.h <= 0) return;
  gfx.setTextDatum(middle_center);
  fitted_text(gfx, text, r.x + r.w / 2, r.y + r.h / 2, r.w - 12, color);
  gfx.setTextDatum(middle_left);
}
template <typename G> void panel(G& gfx, Rect r, uint16_t edge = kGrid) {
  if (r.w <= 0 || r.h <= 0) return;
  gfx.fillRoundRect(r.x, r.y, r.w, r.h, 8, kPanel);
  gfx.drawRoundRect(r.x, r.y, r.w, r.h, 8, edge);
}
template <typename G> void page_button(G& gfx, Rect r, const char* title, bool enabled) {
  gfx.fillRoundRect(r.x, r.y, r.w, r.h, 7, kPanel);
  gfx.drawRoundRect(r.x, r.y, r.w, r.h, 7, enabled ? kCyan : kGrid);
  centered_text(gfx, title, r, enabled ? kCyan : kMuted);
}
inline const Node* node_at(const Snapshot& s, uint8_t index) {
  return index < s.node_count && index < kNodeCapacity ? &s.nodes[index] : nullptr;
}
inline const Event* event_at(const Snapshot& s, uint8_t index) {
  return index < s.event_count && index < kEventCapacity ? &s.events[index] : nullptr;
}
inline const Node* selected_node(const Snapshot& s, const DataState& state) {
  if (s.selected_node < s.node_count && s.selected_node < kNodeCapacity) {
    for (unsigned i = 0; i < state.count && i < kNodeCapacity; ++i)
      if (state.indices[i] == s.selected_node) return &s.nodes[s.selected_node];
  }
  const unsigned at = std::min<unsigned>(state.offset, state.count ? state.count - 1 : 0);
  return state.count ? node_at(s, state.indices[at]) : nullptr;
}
inline void format_id(char* out, size_t n, uint32_t id) {
  if (id) std::snprintf(out, n, "!%08lX", static_cast<unsigned long>(id));
  else std::snprintf(out, n, "--");
}
inline void format_coord(char* out, size_t n, int32_t value) {
  if (value == INT32_MAX) std::snprintf(out, n, "--");
  else std::snprintf(out, n, "%.5f", static_cast<double>(value) / 10000000.0);
}
inline void format_signal(char* out, size_t n, int16_t signal, int16_t snr) {
  if (signal == INT16_MAX && snr == INT16_MAX) std::snprintf(out, n, "RSSI --  SNR --");
  else if (signal == INT16_MAX) std::snprintf(out, n, "RSSI --  SNR %.1f", snr / 10.0);
  else if (snr == INT16_MAX) std::snprintf(out, n, "RSSI %.1f  SNR --", signal / 10.0);
  else std::snprintf(out, n, "RSSI %.1f  SNR %.1f", signal / 10.0, snr / 10.0);
}
inline void format_age(char* out, size_t n, uint32_t seen_ms, uint32_t now_ms) {
  if (!seen_ms) std::snprintf(out, n, "SAVED");
  else std::snprintf(out, n, "%lus", static_cast<unsigned long>((now_ms - seen_ms) / 1000u));
}
inline void format_node_compact(char* out, size_t n, const Node& node, uint32_t now_ms) {
  char id[16]{}, age[24]{}, signal[24]{}, snr[24]{}, hops[12]{}, battery[12]{};
  format_id(id, sizeof(id), node.id);
  format_age(age, sizeof(age), node.seen_ms, now_ms);
  if (node.signal_tenths == INT16_MAX) std::snprintf(signal, sizeof(signal), "R--");
  else std::snprintf(signal, sizeof(signal), "R%.1f", node.signal_tenths / 10.0);
  if (node.snr_tenths == INT16_MAX) std::snprintf(snr, sizeof(snr), "S--");
  else std::snprintf(snr, sizeof(snr), "S%.1f", node.snr_tenths / 10.0);
  if (node.hops == UINT8_MAX) std::snprintf(hops, sizeof(hops), "--");
  else std::snprintf(hops, sizeof(hops), "%u", node.hops);
  if (node.battery_percent == UINT8_MAX) std::snprintf(battery, sizeof(battery), "--");
  else std::snprintf(battery, sizeof(battery), "%u%%", node.battery_percent);
  std::snprintf(out, n, "%s H%s B%s A%s %s %s", id, hops, battery, age, signal, snr);
}
inline uint32_t node_key(const Node& node, bool selected, uint32_t now_ms) {
  uint32_t h = 2166136261u;
  h = hash_u32(h, node.id); h = hash_u32(h, node.seen_ms / 1000u);
  h = hash_byte(h, selected); h = hash_byte(h, node.favorite);
  h = hash_byte(h, node.hops); h = hash_byte(h, node.battery_percent);
  h = hash_i32(h, node.signal_tenths); h = hash_i32(h, node.snr_tenths);
  h = hash_text(h, node.name); h = hash_text(h, node.role);
  h = hash_u32(h, now_ms / 1000u);
  return finish(h);
}
inline uint32_t node_detail_key(const Node* node, uint32_t now_ms) {
  if (!node) return 1;
  uint32_t h = node_key(*node, false, now_ms);
  h = hash_i32(h, node->latitude_e7); h = hash_i32(h, node->longitude_e7);
  h = hash_i32(h, node->altitude_m);
  return finish(h);
}
inline uint32_t event_key(const Event& event, uint32_t now_ms, bool details) {
  uint32_t h = 2166136261u;
  h = hash_byte(h, static_cast<uint8_t>(event.source));
  h = hash_u32(h, event.sender); h = hash_u32(h, event.destination);
  h = hash_u32(h, event.packet_id); h = hash_u32(h, event.received_ms);
  h = hash_u32(h, event.received_utc); h = hash_u32(h, event.port);
  h = hash_i32(h, event.signal_tenths); h = hash_i32(h, event.snr_tenths);
  h = hash_byte(h, event.verified); h = hash_byte(h, event.encrypted);
  h = hash_text(h, event.text); h = hash_byte(h, details);
  h = hash_u32(h, now_ms / 1000u);
  return finish(h);
}
inline unsigned visible_count(const DataState& state, unsigned row_count) {
  if (state.offset >= state.count || state.offset >= kNodeCapacity) return 0;
  return std::min<unsigned>({state.count - state.offset, row_count, unsigned(kNodeCapacity) - state.offset});
}
inline unsigned item_count(const DataState& state) { return std::min<unsigned>(state.count, kNodeCapacity); }
inline const char* source_name(PacketSource source) {
  return source == PacketSource::sx1262 ? "SX1262" : "RTL-SDR";
}
inline void event_sender(const Snapshot& s, const Event& event, char* out, size_t size) {
  for (unsigned i = 0; i < s.node_count && i < kNodeCapacity; ++i) {
    if (s.nodes[i].id != event.sender) continue;
    if (s.nodes[i].name[0]) std::snprintf(out, size, "%s", s.nodes[i].name);
    else format_id(out, size, event.sender);
    return;
  }
  format_id(out, size, event.sender);
}
inline void event_time(char* out, size_t n, const Event& e, uint32_t now_ms) {
  const uint32_t age = e.sender ? (now_ms - e.received_ms) / 1000u : 0;
  if (!e.received_utc) std::snprintf(out, n, "TIME NOT SET %lus", static_cast<unsigned long>(age));
  else {
    const uint32_t day = e.received_utc % 86400u;
    std::snprintf(out, n, "%02lu:%02lu:%02luZ %lus",
      static_cast<unsigned long>(day / 3600u), static_cast<unsigned long>((day / 60u) % 60u),
      static_cast<unsigned long>(day % 60u), static_cast<unsigned long>(age));
  }
}
inline void event_details(char* out, size_t n, const Event& e) {
  char to[12]{}, signal[24]{};
  format_id(to, sizeof(to), e.destination);
  format_signal(signal, sizeof(signal), e.signal_tenths, e.snr_tenths);
  std::snprintf(out, n, "TO %s PORT %u %s", to, e.port, signal);
}
inline uint32_t page_key(unsigned offset, unsigned count, unsigned shown,
                         bool previous, bool next, bool details) {
  uint32_t h = 2166136261u;
  h = hash_u32(h, offset); h = hash_u32(h, count); h = hash_u32(h, shown);
  h = hash_byte(h, previous); h = hash_byte(h, next); h = hash_byte(h, details);
  return finish(h);
}
} // namespace detail

template <typename G> void draw_nodes_static(G& gfx, const Layout& l) {
  detail::panel(gfx, l.nodes_list);
  detail::prepare(gfx);
  detail::fitted_text(gfx, l.single_node_panel ? "NODES / DETAILS" : "MESH NODES",
                      l.nodes_list.x + 12, l.nodes_list.y + 16, l.nodes_list.w - 24, kCyan);
  if (!l.single_node_panel) {
    detail::panel(gfx, l.node_details);
    detail::prepare(gfx);
    detail::fitted_text(gfx, "SELECTED NODE", l.node_details.x + 12,
                        l.node_details.y + 16, l.node_details.w - 24, kCyan);
  }
}

template <typename G> void draw_nodes_values(G& gfx,const Layout& l,const Snapshot& s,
 const DataState& state,Cache& cache,uint32_t now_ms) {
  detail::prepare(gfx);
  const bool show_rows=!l.single_node_panel||!state.details;
  if(show_rows)for(unsigned row=0;row<unsigned(l.node_row_count);++row){
    const unsigned pos=state.offset+row;
    const Node* node=pos<detail::item_count(state)?detail::node_at(s,state.indices[pos]):nullptr;
    const bool selected=node&&s.selected_node<s.node_count&&node->id==s.nodes[s.selected_node].id;
    const uint32_t key=node?detail::node_key(*node,selected,now_ms):1;
    if(cache.node_rows[row]==key)continue;
    cache.node_rows[row]=key;
    const auto r=l.node_rows[row];
    gfx.fillRoundRect(r.x,r.y,r.w,r.h,6,kBg);gfx.drawRoundRect(r.x,r.y,r.w,r.h,6,node?kGrid:kPanel);
    char line[128]{},id[16]{};
    if(node){
      detail::format_id(id,sizeof(id),node->id);
      detail::fitted_text(gfx,node->name[0]?node->name:id,r.x+8,r.y+11,r.w-16,selected?kGreen:kWhite);
      detail::format_node_compact(line,sizeof(line),*node,now_ms);
      detail::fitted_text(gfx,line,r.x+8,r.y+33,r.w-16,kMuted);
    }else if(row==0&&!state.count)detail::fitted_text(gfx,"NO NODES",r.x+8,r.y+22,r.w-16,kMuted);
  }
  if(!l.single_node_panel||state.details){
    const Node* node=detail::selected_node(s,state);
    const auto key=detail::node_detail_key(node,now_ms);
    if(!cache.node_detail_valid||cache.node_detail!=key){
      cache.node_detail_valid=true;cache.node_detail=key;
      const auto pane=l.single_node_panel?l.nodes_list:l.node_details;
      const Rect r={pane.x+8,pane.y+30,pane.w-16,pane.h-(l.single_node_panel?90:38)};
      gfx.fillRect(r.x,r.y,r.w,r.h,kPanel);
      if(!node)detail::fitted_text(gfx,"NO NODE SELECTED",r.x+4,r.y+12,r.w-8,kMuted);
      else{
        char line[128]{},id[16]{},coord[32]{},signal[48]{};
        detail::format_id(id,sizeof(id),node->id);detail::format_signal(signal,sizeof(signal),node->signal_tenths,node->snr_tenths);
        int y=r.y+10;
        const auto write=[&](const char* value,uint16_t color=kWhite){if(y+8<=r.y+r.h)detail::fitted_text(gfx,value,r.x+4,y,r.w-8,color);y+=24;};
        write(node->name[0]?node->name:id,kGreen);write(id,kCyan);write(signal);
        detail::format_coord(coord,sizeof(coord),node->latitude_e7);std::snprintf(line,sizeof(line),"LAT %s",coord);write(line);
        detail::format_coord(coord,sizeof(coord),node->longitude_e7);std::snprintf(line,sizeof(line),"LON %s",coord);write(line);
        std::snprintf(line,sizeof(line),"ALT %d m",node->altitude_m);write(node->altitude_m==INT16_MIN?"ALT --":line);
        char battery[12]{},hops[12]{};
        if(node->battery_percent==UINT8_MAX)std::snprintf(battery,sizeof(battery),"--");
        else std::snprintf(battery,sizeof(battery),"%u%%",node->battery_percent);
        if(node->hops==UINT8_MAX)std::snprintf(hops,sizeof(hops),"--");
        else std::snprintf(hops,sizeof(hops),"%u",node->hops);
        std::snprintf(line,sizeof(line),"BAT %s HOPS %s",battery,hops);write(line);
      }
    }
  }
  const unsigned shown=detail::visible_count(state,l.node_row_count);
  const bool previous=state.offset>0,next=state.offset+shown<detail::item_count(state);
  const auto pages=detail::page_key(state.offset,state.count,shown,previous,next,state.details);
  if(!cache.node_pages_valid||cache.node_pages!=pages){
    cache.node_pages_valid=true;cache.node_pages=pages;
    detail::page_button(gfx,l.node_previous,"PREV",previous);detail::page_button(gfx,l.node_next,"NEXT",next);
  }
}

template <typename G> void draw_traffic_static(G& gfx, const Layout& l) {
  detail::panel(gfx, l.body);
}

template <typename G> void draw_traffic_values(G& gfx, const Layout& l, const Snapshot& s,
                                                 const DataState& state, Cache& cache,
                                                 uint32_t now_ms) {
  detail::prepare(gfx);
  const unsigned shown = detail::visible_count(state, l.traffic_row_count);
  for (unsigned row = 0; row < unsigned(l.traffic_row_count); ++row) {
    const unsigned pos = state.offset + row;
    const Event* event = pos < detail::item_count(state) ? detail::event_at(s, state.indices[pos]) : nullptr;
    const uint32_t key = event ? detail::event_key(*event, now_ms, state.details) : 1u;
    if (cache.traffic_rows[row] == key) continue;
    const Rect r = l.traffic_rows[row];
    const bool verified_text = event && event->verified && event->text[0] && !(event->encrypted && !event->port);
    const uint16_t edge = !event ? kGrid : (!event->verified ? kYellow : kGrid);
    gfx.fillRoundRect(r.x, r.y, r.w, r.h, 7, kPanel);
    gfx.drawRoundRect(r.x, r.y, r.w, r.h, 7, edge);
    if (!event) {
      if (row == 0 && state.count == 0) {
        detail::prepare(gfx); detail::fitted_text(gfx, "NO PACKETS", r.x + 10, r.y + r.h / 2, r.w - 20, kMuted);
      }
    } else {
      char sender[48]{}, title[112]{}, received[32]{}, line[160]{};
      detail::event_sender(s, *event, sender, sizeof(sender));
      std::snprintf(title, sizeof(title), "%s FROM %s", detail::source_name(event->source), sender);
      detail::event_time(received, sizeof(received), *event, now_ms);
      detail::prepare(gfx);
      const int inner_x = r.x + 9, inner_w = r.w - 18;
      detail::fitted_text(gfx,title,inner_x,r.y+15,inner_w,event->verified?kGreen:kYellow);
      std::snprintf(line,sizeof(line),"ID %08lX",static_cast<unsigned long>(event->packet_id));
      detail::fitted_text(gfx,line,inner_x,r.y+39,inner_w/2,kCyan);
      gfx.setTextDatum(middle_right);
      detail::fitted_text(gfx,received,r.x+r.w-9,r.y+39,inner_w/2,kMuted);
      gfx.setTextDatum(middle_left);
      if(!event->verified){
        detail::fitted_text(gfx,"UNVERIFIED / TEXT HIDDEN",inner_x,r.y+63,inner_w,kYellow);
      }else if(state.details){
        detail::event_details(line,sizeof(line),*event);
        detail::wrapped_text(gfx,line,inner_x,r.y+63,inner_w,2);
      }else if(verified_text){
        detail::wrapped_text(gfx,event->text,inner_x,r.y+63,inner_w,2);
      }else{
        const char* msg=!event->verified?"UNVERIFIED / TEXT HIDDEN":event->encrypted?"ENCRYPTED / TEXT UNAVAILABLE":"NO TEXT CONTENT";
        detail::fitted_text(gfx,msg,inner_x,r.y+63,inner_w,kYellow);
      }

    }
    cache.traffic_rows[row] = key;
  }
  const bool previous = state.offset > 0, next = state.offset + shown < detail::item_count(state);
  const uint32_t pages = detail::page_key(state.offset, state.count, shown, previous, next, state.details);
  if (!cache.traffic_pages_valid || cache.traffic_pages != pages) {
    detail::page_button(gfx, l.traffic_previous, "PREV", previous);
    detail::page_button(gfx, l.traffic_next, "NEXT", next);
    cache.traffic_pages = pages; cache.traffic_pages_valid = true;
  }
}

} // namespace orcsdr::lora::rx_data_views
