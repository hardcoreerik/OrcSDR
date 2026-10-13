#include "acars_demo_data.hpp"

#include <cstdio>
#include <cstring>

namespace orcsdr::acars_demo {
namespace {

using namespace orcsdr::acars_ui;

constexpr uint32_t kDemoUtc = 1791900000u;     // arbitrary fixed instant, not a real reception time

struct Row {
  const char* tail;
  const char* flight;      // may be empty
  const char* label;
  char block;
  uint8_t end;             // 0x03 ETX / 0x17 ETB / 0 unknown
  bool corrected;
  uint8_t fixed_bits;
  uint8_t freq_slot;       // index into the north-american presets used for the frequency
  const char* text;        // raw bytes; embedded control bytes via \x escapes
  size_t text_len;         // 0 = strlen
  bool timed;
};

constexpr Row kRows[] = {
    {"N101DM", "DM0101", "H1", '2', 0x03, false, 0, 0, "M01ADM0101POS N40123W074456,1234,FL350", 0, true},
    {"N101DM", "DM0101", "5Z", '3', 0x03, false, 0, 0, "M02ADM0101DEMO GATE B12 ETA 1432", 0, true},
    {"G-DEMO", "DM0202", "Q0", '4', 0x03, false, 0, 1, "M03ADM0202LINK TEST", 0, true},
    {"N202DM", "DM0303", "H1", '5', 0x17, false, 0, 0,
     "M04ADM0303DEMO LONG BLOCK 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 CONTINUES", 0, true},
    {"D-ADEM", "", "SA", '6', 0x03, true, 1, 2, "2SDEMO", 0, true},
    {"VH-DMO", "DM0404", "10", '7', 0x03, false, 0, 3, "M05ADM0404OUT 1301 OFF 1311 ON 1809 IN 1819", 0, false},
    {"N202DM", "DM0303", "H1", '8', 0x03, true, 2, 0, "M06ADM0303DEMO CONTROL\x01\x02 BYTES\r\nAND NUL\x00IN TEXT", 47, true},
    {"C-FDMO", "DM0505", "_d", '9', 0x03, false, 0, 4, "", 0, true},
    {"N303DM", "DM0606", "B9", 'A', 0x03, false, 0, 5, "M07ADM0606TURB MOD FL330 EB", 0, true},
    {"N101DM", "DM0101", "H1", 'B', 0x03, false, 0, 0, "M08ADM0101POS N40201W074311,1241,FL350", 0, true},
    {"G-DEMO", "DM0202", "Q0", 'C', 0x03, true, 1, 1, "M09ADM0202LINK TEST 2", 0, false},
    {"D-ADEM", "DM0707", "80", 'D', 0x17, false, 0, 2, "M10ADM0707FUEL 12.4 12.1", 0, true},
    {"N404DM", "", "RA", 'E', 0, false, 0, 0, "UNKNOWN END MARKER DEMO", 0, true},
    {"", "", "5Z", 'F', 0x03, false, 0, 0, "BLOCK WITHOUT AN ADDRESS DEMO", 0, true},
};

void build(MessageRecord& r, const Row& row, uint32_t index, bool timed) {
  r = MessageRecord{};
  r.source = SourceKind::demo;
  r.integrity = row.corrected ? Integrity::crc_corrected : Integrity::crc_verified;
  r.corrected_bits = row.fixed_bits;
  r.mode = '2';
  r.acknowledgement = '\x15';                   // NAK shown as the standard ACARS "no ack" marker
  r.block_id = row.block;
  r.end_marker = row.end;
  std::snprintf(r.label, sizeof(r.label), "%s", row.label);
  std::snprintf(r.tail, sizeof(r.tail), "%s", row.tail);
  std::snprintf(r.flight, sizeof(r.flight), "%s", row.flight);
  const size_t len = row.text_len ? row.text_len : std::strlen(row.text);
  r.text_length = static_cast<uint16_t>(len < sizeof(r.text) ? len : sizeof(r.text));
  std::memcpy(r.text, row.text, r.text_length);
  r.has_text = r.text_length != 0;
  r.raw_bytes = static_cast<uint16_t>(r.text_length + 16);
  r.frequency_hz = preset(Region::north_america, row.freq_slot).frequency_hz;
  r.uptime_ms = 1000u * (index + 1);
  r.time_valid = timed && row.timed;
  r.utc_epoch = r.time_valid ? kDemoUtc + 37u * index : 0;
  sanitize_preview(r.preview, sizeof(r.preview), r.text, r.text_length);
}

acars::Stats fake_stats(uint32_t ok, uint32_t corrected) {
  acars::Stats s;
  s.crc_ok = ok;
  s.corrected = corrected;
  s.sync_candidates = ok + ok / 3 + 4;
  s.crc_rejected = ok / 5 + 1;
  s.framing_rejected = ok / 9;
  s.overflows = 0;
  s.bits = ok * 1800u + 12000u;
  return s;
}

void add_rows(Model& model, size_t count, bool timed) {
  uint32_t corrected = 0;
  for (size_t i = 0; i < count; ++i) {
    MessageRecord r;
    const Row& row = kRows[i % (sizeof(kRows) / sizeof(kRows[0]))];
    build(r, row, static_cast<uint32_t>(i), timed);
    if (r.integrity == Integrity::crc_corrected) ++corrected;
    model.inject_demo(r);
  }
  model.inject_demo_stats(fake_stats(static_cast<uint32_t>(count), corrected));
}

}  // namespace

const char* scenario_name(DemoScenario scenario) {
  switch (scenario) {
    case DemoScenario::mixed: return "MIXED";
    case DemoScenario::full: return "FULL HISTORY";
    case DemoScenario::empty: return "EMPTY";
    case DemoScenario::disconnected: return "DISCONNECTED";
    case DemoScenario::paused: return "PAUSED";
    case DemoScenario::no_time: return "NO CLOCK";
    default: return "?";
  }
}

void populate(Model& model, DemoScenario scenario) {
  constexpr size_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);
  switch (scenario) {
    case DemoScenario::mixed:
      add_rows(model, kRowCount, true);
      model.inject_demo_receiver(ReceiverState::running);
      break;
    case DemoScenario::full:
      add_rows(model, kHistoryCapacity + 12, true);   // more than the ring holds: oldest are evicted
      model.inject_demo_receiver(ReceiverState::running);
      break;
    case DemoScenario::empty:
      model.inject_demo_stats(acars::Stats{});
      model.inject_demo_receiver(ReceiverState::running);
      break;
    case DemoScenario::disconnected:
      model.inject_demo_receiver(ReceiverState::disconnected);
      break;
    case DemoScenario::paused:
      add_rows(model, 5, true);
      model.inject_demo_receiver(ReceiverState::paused);
      break;
    case DemoScenario::no_time:
      add_rows(model, kRowCount, false);
      model.inject_demo_receiver(ReceiverState::running);
      break;
    default:
      break;
  }
}

}  // namespace orcsdr::acars_demo
