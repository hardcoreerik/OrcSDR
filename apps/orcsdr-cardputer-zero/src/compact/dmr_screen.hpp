#pragma once
// DMR dashboard screen.
//
// Shows what the d1 chain actually proves: acquisition counts, and for a
// validated burst the colour code, slot, source and destination. Evidence gates
// are surfaced rather than smoothed over - a burst with valid integrity says
// VALID, anything weaker says SYNC, so the screen never implies an identity the
// decoder did not validate.
#include <cstdio>
#include <string>
#include <vector>

#include "orcsdr_dmr/d1_bridge.hpp"
#include "p25_screen.hpp"

namespace orc::zero {

struct DmrScreen {
  P25Screen tuning;
  CompactRenderer frame;
  bool layout = false;
  bool entry_active = false;

  uint32_t acquisitions = 0, events = 0, identities = 0, invalid_calls = 0;
  double level = 0;
  bool have_event = false;
  bool valid = false;
  uint8_t sync_kind = 0, colour_code = 0, slot = 0;
  uint32_t source_id = 0, destination_id = 0;
  std::vector<std::string> lines;
  uint64_t revision = 1, painted = ~uint64_t{0};

  DmrScreen() {
    tuning.requested = 463400000;  // a common business-band DMR channel
    tuning.step_index = 1;
  }

  // The radio tunes 60 kHz below the wanted channel: the DV channeliser expects
  // the channel a quarter of its input rate above centre.
  uint32_t radio_hz() const {
    return tuning.requested > 60000 ? tuning.requested - 60000 : tuning.requested;
  }

  void set_level(double value) {
    // Smooth, because a raw per-block level makes the bar strobe.
    level = level * 0.8 + value * 0.2;
    ++revision;
  }

  void observe(const orcsdr::dmr::d1::integration::Event& event) {
    ++events;
    if (event.decoded.integrity_valid) ++identities;
    have_event = true;
    valid = event.decoded.integrity_valid;
    sync_kind = uint8_t(event.decoded.sync);
    colour_code = event.decoded.colour_code;
    slot = event.decoded.slot;
    source_id = event.decoded.source_id;
    destination_id = event.decoded.destination_id;

    if (valid) {
      char text[48];
      std::snprintf(text, sizeof(text), "CC%u SRC %u DST %u", unsigned(colour_code),
                    source_id, destination_id);
      lines.insert(lines.begin(), text);
      if (lines.size() > 4) lines.resize(4);
    }
    ++revision;
  }

  void update_stats(const orcsdr::dmr::d1::integration::Stats& stats) {
    acquisitions = uint32_t(stats.acquisitions);
    identities = uint32_t(stats.validated_identities);
    events = uint32_t(stats.protocol_events);
    invalid_calls = uint32_t(stats.invalid_calls);
    ++revision;
  }

  bool input(Key key, char digit) {
    if (tuning.entry_active) {
      const bool handled = tuning.input(key, digit);
      ++revision;
      return handled;
    }
    if (key == Key::escape) return false;
    if (key == Key::frequency) {
      tuning.entry_active = true;
      tuning.entry.clear();
      tuning.error.clear();
      ++revision;
      return true;
    }
    if (key == Key::left || key == Key::right) {
      const int64_t step = key == Key::left ? -int64_t(P25Screen::steps[tuning.step_index])
                                                    : int64_t(P25Screen::steps[tuning.step_index]);
      const int64_t next = std::clamp<int64_t>(int64_t(tuning.requested) + step, 24000000, 1766000000);
      tuning.requested = uint32_t(next);
      ++revision;
      return true;
    }
    return true;
  }

  const auto& render() {
    if (!layout) {
      frame.status_bar("DMR");
      layout = true;
    }
    if (painted == revision) return frame.pixels;

    char text[64];
    frame.rect(0, 36, 320, 124, frame.bg);

    // Readout row: frequency and whether the channel is quiet.
    std::snprintf(text, sizeof(text), "%.4f MHz", tuning.requested / 1e6);
    frame.text(4, 42, text, frame.yellow, 2, 15);
    const char* state = level > 0.02 ? "ACTIVE" : "QUIET";
    frame.text(190, 44, state, level > 0.02 ? frame.green : frame.muted, 1, 6);
    std::snprintf(text, sizeof(text), "LVL %3.0f%%", level * 100.0);
    frame.text(250, 44, text, frame.cyan, 1, 7);

    // The last validated burst.
    frame.text(4, 66, "LAST BURST", frame.muted);
    if (!have_event) {
      frame.text(4, 80, "WAITING FOR DMR SYNC", frame.cyan);
      frame.text(4, 94, "NO BURST ACQUIRED YET", frame.muted, 1, 22);
    } else {
      const char* kinds[] = {"BS VOICE", "MS VOICE", "BS DATA", "MS DATA"};
      const unsigned k = sync_kind < 4 ? sync_kind : 0;
      std::snprintf(text, sizeof(text), "%s  %s", kinds[k], valid ? "VALID" : "SYNC ONLY");
      frame.text(4, 80, text, valid ? frame.green : frame.yellow, 1, 26);
      std::snprintf(text, sizeof(text), "CC %u   SLOT %u", unsigned(colour_code), unsigned(slot));
      frame.text(4, 94, text, frame.white);
      if (valid) {
        std::snprintf(text, sizeof(text), "SRC %u  DST %u", source_id, destination_id);
        frame.text(4, 106, text, frame.white);
      }
    }

    // Counters.
    std::snprintf(text, sizeof(text), "ACQ %u  EVENTS %u  IDS %u  BAD %u", acquisitions, events,
                  identities, invalid_calls);
    frame.text(4, 122, text, frame.muted, 1, 37);

    for (size_t n = 0; n < lines.size(); ++n)
      frame.text(4, 134 + int(n) * 9, lines[n], frame.white, 1, 26);

    frame.rect(0, 158, 320, 12, frame.panel);
    frame.keys(4, 160,
               {{"V", "FREQ"}, {"Z/C", "TUNE"}, {"TAB", "DASH"}, {"SPACE", "MENU"}});
    painted = revision;
    return frame.pixels;
  }
};

}  // namespace orc::zero
