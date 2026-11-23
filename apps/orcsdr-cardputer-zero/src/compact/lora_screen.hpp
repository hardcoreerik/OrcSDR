#pragma once
#include "p25_screen.hpp"
#include <array>
#include <cstring>
#include "lora_native_decoder.hpp"
#include "lora_preamble_detector.hpp"
#include "lora_region_plan.hpp"
namespace orc::zero {

// LoRa / mesh workspace.
//
// Modelled on how Meshtastic and MeshCore split their own UIs: a live traffic
// view is the default surface, channel and radio configuration live on a
// separate screen (you do not retune from the message list), and heard nodes are
// their own list. Left/Right move between screens; Up/Down act within one.
//
//   TRAFFIC   what is arriving now, plus capture counters
//   CHANNELS  region, slot, frequency, modem and TX limit - the retune surface
//   NODES     peers heard, with packet counts
//   RADIO     preamble/CRC/decode telemetry and the hardware ceiling
struct LoraScreen {
  enum class Page { traffic, channels, nodes, radio };
  static constexpr size_t kPageCount = 4;

  struct NodeEntry {
    uint32_t address = 0;
    uint32_t packets = 0;
    uint16_t port = 0;
  };

  P25Screen tuning;
  CompactRenderer frame;
  uint8_t sf = 11;
  size_t region = 0;
  uint16_t slot = 20;
  bool layout = false, capturing = false, decoding = false, manual = false;
  uint32_t captures = 0, crc_ok = 0, crc_failed = 0, decode_ms = 0;
  uint64_t revision = 0, painted = ~uint64_t{0};
  size_t offset = 0;
  orcsdr::lora_preamble::Status preamble{};
  std::vector<std::string> lines;
  Page page = Page::traffic;
  std::array<NodeEntry, 16> nodes{};
  size_t node_count = 0;

  LoraScreen() {
    set_plan(0, orcsdr::lora_plan::default_slot(0));
    tuning.step_index = 1;
  }

  void set_plan(size_t requested_region, uint16_t requested_slot) {
    region = std::min(requested_region, orcsdr::lora_plan::kRegions.size() - 1);
    slot = std::clamp<uint16_t>(requested_slot, 1, orcsdr::lora_plan::slot_count(region));
    tuning.requested = orcsdr::lora_plan::frequency_hz(region, slot);
    ++revision;
  }

  static const char* page_name(Page p) {
    switch (p) {
      case Page::traffic: return "TRAFFIC";
      case Page::channels: return "CHANNELS";
      case Page::nodes: return "NODES";
      case Page::radio: return "RADIO";
    }
    return "?";
  }

  void turn_page(int delta) {
    int index = int(page) + delta;
    if (index < 0) index = int(kPageCount) - 1;
    if (index >= int(kPageCount)) index = 0;
    page = Page(index);
    offset = 0;
    ++revision;
  }

  void note_node(const orcsdr::lora_native::Packet& packet) {
    size_t index = 0;
    while (index < node_count && nodes[index].address != packet.sender) ++index;
    if (index == node_count) {
      if (node_count == nodes.size())  // full: replace the least-heard entry
        index = size_t(std::min_element(nodes.begin(), nodes.end(),
                                        [](const NodeEntry& a, const NodeEntry& b) {
                                          return a.packets < b.packets;
                                        }) -
                       nodes.begin());
      else
        ++node_count;
      nodes[index] = NodeEntry{};
      nodes[index].address = packet.sender;
    }
    ++nodes[index].packets;
    nodes[index].port = packet.port;
  }

  bool input(Key key, char digit) {
    if (tuning.entry_active) {
      const bool handled = tuning.input(key, digit);
      ++revision;
      return handled;
    }
    // Escape steps back to the traffic view first, and only leaves the dashboard
    // from there, so a misplaced Escape never drops the operator out.
    if (key == Key::escape) {
      if (page != Page::traffic) {
        page = Page::traffic;
        offset = 0;
        ++revision;
        return true;
      }
      return false;
    }
    if (key == Key::left) {
      turn_page(-1);
      return true;
    }
    if (key == Key::right) {
      turn_page(1);
      return true;
    }
    if (key == Key::frequency) {
      tuning.entry.clear();
      tuning.error.clear();
      tuning.entry_active = true;
      ++revision;
      return true;
    }
    if (key == Key::step) {  // region only makes sense on the channel surface
      set_plan((region + 1) % orcsdr::lora_plan::kRegions.size(),
               orcsdr::lora_plan::default_slot((region + 1) % orcsdr::lora_plan::kRegions.size()));
      return true;
    }
    if (key == Key::gain) {
      sf = sf == 12 ? 7 : sf + 1;
      ++revision;
      return true;
    }
    if (key == Key::enter) {
      // A manual capture belongs to the live view; on the other screens Enter
      // means nothing yet, so it must not fire a capture by surprise.
      if (page == Page::traffic) manual = true;
      ++revision;
      return true;
    }
    if (key == Key::up) {
      if (page == Page::channels)
        set_plan(region, slot + 1);
      else if (offset)
        --offset;
      ++revision;
      return true;
    }
    if (key == Key::down) {
      if (page == Page::channels)
        set_plan(region, slot == 1 ? orcsdr::lora_plan::slot_count(region) : slot - 1);
      else if (offset + 3 < lines.size())
        ++offset;
      ++revision;
      return true;
    }
    return true;  // swallow the rest; this page owns every key
  }

  void add(const orcsdr::lora_native::Packet& packet) {
    char b[64];
    std::snprintf(b, sizeof(b), "%08X > %08X PORT %u", packet.sender, packet.destination,
                  packet.port);
    std::string text(packet.text, strnlen(packet.text, sizeof(packet.text)));
    for (char& c : text)
      if (c < 32 || c > 126) c = '?';
    lines.insert(lines.begin(), text.empty() ? (packet.encrypted ? "ENCRYPTED PACKET" : "NO TEXT")
                                             : text);
    lines.insert(lines.begin(), b);
    if (lines.size() > 32) lines.resize(32);
    note_node(packet);
    offset = 0;
    ++revision;
  }

  void draw_header() {
    frame.status_bar("LORA/MESH",-1,int(page),int(kPageCount));
    char b[64];
    std::snprintf(b, sizeof(b), "%s %u/%u", page_name(page), unsigned(page) + 1,
                  unsigned(kPageCount));
    frame.text(33, 23, b, frame.cyan, 1, 20);
    std::snprintf(b, sizeof(b), "%.3f SF%u %s", tuning.requested / 1e6, sf,
                  tuning.requested == tuning.applied ? "RX" : "TUNE");
    frame.text(150, 23, b, tuning.requested == tuning.applied ? frame.white : frame.yellow, 1, 27);
    frame.line(0, 35, 319, 35, frame.grid);
  }

  void draw_footer() {
    frame.rect(0, 158, 320, 12, frame.bg);
    switch (page) {
      case Page::traffic:
        frame.keys(4, 160, {{"Z/C", "PAGE"}, {"F/X", "SCROLL"}, {"ENTER", "CAPTURE"},
                            {"SPACE", "DASH"}});
        break;
      case Page::channels:
        frame.keys(4, 160, {{"Z/C", "PAGE"}, {"F/X", "SLOT"}, {"T", "REGION"},
                            {"V", "MHZ"}, {"SPACE", "DASH"}});
        break;
      case Page::nodes:
      case Page::radio:
        frame.keys(4, 160, {{"Z/C", "PAGE"}, {"F/X", "SCROLL"}, {"SPACE", "DASH"}});
        break;
    }
  }

  void draw_traffic() {
    char b[80];
    std::snprintf(b, sizeof(b), "%s   WINDOWS %u  FIRES %u",
                  decoding ? "DECODING" : capturing ? "CAPTURING" : "LISTENING",
                  preamble.windows, preamble.fires);
    frame.text(4, 40, b, frame.cyan, 1, 52);
    std::snprintf(b, sizeof(b), "CAP %u  CRC OK %u  FAIL %u  %uMS", captures, crc_ok, crc_failed,
                  decode_ms);
    frame.text(4, 52, b, frame.white, 1, 52);
    if (lines.empty()) {
      frame.text(4, 82, "WAITING FOR RECEIVED PACKETS", frame.muted);
      frame.text(4, 94, "LoRa traffic appears here with", frame.muted, 1, 42);
      frame.text(4, 105, "sender, destination and port.", frame.muted, 1, 42);
    }
    for (size_t n = offset; n < std::min(lines.size(), offset + 6); ++n)
      frame.text(4, 70 + int(n - offset) * 11, lines[n], n % 2 ? frame.white : frame.green, 1, 52);
  }

  void draw_channels() {
    char b[80];
    frame.text(4, 42, "CHANNEL PLAN", frame.muted);

    frame.text(4, 56, orcsdr::lora_plan::kRegions[region].code, frame.green, 3, 6);
    std::snprintf(b, sizeof(b), "SLOT %u/%u", unsigned(slot),
                  unsigned(orcsdr::lora_plan::slot_count(region)));
    frame.text(76, 58, b, frame.white, 1, 16);

    std::snprintf(b, sizeof(b), "%.4f MHz", tuning.requested / 1e6);
    frame.text(4, 86, b, frame.yellow, 2, 15);

    std::snprintf(b, sizeof(b), "MODEM  SF%u  BW 250K  CR4/5", unsigned(sf));
    frame.text(4, 110, b, frame.white, 1, 40);
    frame.text(4, 122, "TX 15 DBM MAX (SX1261)", frame.muted, 1, 36);

    std::snprintf(b, sizeof(b), "%s", tuning.requested == tuning.applied
                                         ? "ON AIR - RX ACTIVE"
                                         : "PENDING - RETUNING");
    frame.text(4, 136, b, tuning.requested == tuning.applied ? frame.green : frame.yellow, 1, 34);
  }

  void draw_nodes() {
    char b[80];
    if (!node_count) {
      frame.text(4, 60, "NO NODES HEARD YET", frame.muted);
      frame.text(4, 74, "Decoded senders appear here.", frame.muted, 1, 36);
      return;
    }
    std::snprintf(b, sizeof(b), "HEARD %u NODE%s", unsigned(node_count),
                  node_count == 1 ? "" : "S");
    frame.text(4, 40, b, frame.cyan, 1, 24);
    frame.text(4, 54, "ADDRESS   PACKETS  PORT", frame.muted);
    for (size_t n = offset; n < std::min(node_count, offset + 7); ++n) {
      std::snprintf(b, sizeof(b), "%08X  %7u  %4u", nodes[n].address, unsigned(nodes[n].packets),
                    unsigned(nodes[n].port));
      frame.text(4, 68 + int(n - offset) * 11, b, frame.white, 1, 40);
    }
  }

  void draw_radio() {
    char b[80];
    frame.text(4, 40, "RECEIVE PATH", frame.muted);
    std::snprintf(b, sizeof(b), "PREAMBLE WINDOWS %u", preamble.windows);
    frame.text(4, 54, b, frame.white, 1, 40);
    std::snprintf(b, sizeof(b), "PREAMBLE FIRES   %u", preamble.fires);
    frame.text(4, 66, b, frame.white, 1, 40);
    std::snprintf(b, sizeof(b), "CAPTURES         %u", captures);
    frame.text(4, 78, b, frame.white, 1, 40);
    std::snprintf(b, sizeof(b), "CRC OK %u  FAIL %u", crc_ok, crc_failed);
    frame.text(4, 90, b, frame.white, 1, 40);
    std::snprintf(b, sizeof(b), "LAST DECODE      %u MS", decode_ms);
    frame.text(4, 102, b, frame.white, 1, 40);
    frame.line(4, 116, 315, 116, frame.grid);
    frame.text(4, 120, "CAP   SX1261  15 DBM MAX", frame.muted, 1, 30);
    frame.text(4, 132, "RTL   NOT USABLE AT 868/915", frame.muted, 1, 34);
  }

  const auto& render() {
    if (!layout) {
      draw_header();
      draw_footer();
      layout = true;
    }
    if (painted == revision) return frame.pixels;

    // Header line two and the body are the only parts that change.
    frame.rect(33, 21, 287, 13, frame.bg);
    char b[64];
    std::snprintf(b, sizeof(b), "%s %u/%u", page_name(page), unsigned(page) + 1,
                  unsigned(kPageCount));
    frame.text(33, 23, b, frame.cyan, 1, 20);
    std::snprintf(b, sizeof(b), "%.3f SF%u %s", tuning.requested / 1e6, sf,
                  tuning.requested == tuning.applied ? "RX" : "TUNE");
    frame.text(150, 23, b, tuning.requested == tuning.applied ? frame.white : frame.yellow, 1, 27);

    frame.rect(0, 36, 320, 122, frame.bg);
    if (tuning.entry_active) {
      frame.text(4, 52, "ENTER MHZ:" + tuning.entry + "_", frame.white, 2, 22);
      frame.text(4, 84, tuning.error.empty() ? "ENTER APPLY   ESC CANCEL" : tuning.error,
                 tuning.error.empty() ? frame.cyan : frame.yellow);
    } else {
      switch (page) {
        case Page::traffic: draw_traffic(); break;
        case Page::channels: draw_channels(); break;
        case Page::nodes: draw_nodes(); break;
        case Page::radio: draw_radio(); break;
      }
    }
    draw_footer();
    painted = revision;
    return frame.pixels;
  }
};
}
