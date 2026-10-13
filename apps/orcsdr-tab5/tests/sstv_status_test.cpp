// Host regression for the SSTV status wording: honest identification labels.
#include "sstv_status.hpp"
#include "sstv_test_signal.hpp"
#include <cstdio>
#include <cstring>

using namespace orcsdr::sstv;
using namespace orcsdr::sstv_status;
using sstv_test::require;

namespace {
bool contains(const char* text, const char* needle) { return std::strstr(text, needle) != nullptr; }

Metrics metrics_for(SessionState state, Identification id, Mode mode = Mode::robot36) {
  Metrics m;
  m.active = true;
  m.state = state;
  m.identification = id;
  m.received_mode = mode;
  m.receiving = state == SessionState::provisional_lock || state == SessionState::receiving ||
                state == SessionState::loss_recovery;
  return m;
}
}  // namespace

int main() {
  // The wording used in the product brief.
  require(std::strcmp(headline(metrics_for(SessionState::receiving, Identification::vis), nullptr).text,
                      "ROBOT 36 - VIS CONFIRMED") == 0, "headline: VIS confirmed");
  require(std::strcmp(headline(metrics_for(SessionState::provisional_lock, Identification::sync, Mode::pd120),
                               nullptr).text, "PD 120 - SYNC LIKELY - PARTIAL") == 0,
          "headline: sync likely, partial");
  require(std::strcmp(headline(metrics_for(SessionState::sync_analysis, Identification::none), nullptr).text,
                      "UNKNOWN MODE - ANALYZING") == 0, "headline: analysing");
  require(std::strcmp(headline(metrics_for(SessionState::searching, Identification::none), nullptr).text,
                      "WAITING FOR SSTV") == 0, "headline: waiting");

  Image lost;
  std::uint16_t dummy = 0;
  lost.pixels = &dummy;
  lost.partial = true;
  lost.end_reason = EndReason::sync_lost;
  lost.mode = Mode::robot36;
  require(std::strcmp(headline(metrics_for(SessionState::ready, Identification::none), &lost).text,
                      "SYNC LOST - PARTIAL IMAGE SAVED") == 0, "headline: sync lost, partial saved");
  Image done;
  done.pixels = &dummy;
  done.partial = false;
  done.end_reason = EndReason::complete;
  done.mode = Mode::pd120;
  require(std::strcmp(headline(metrics_for(SessionState::finalizing, Identification::vis), &done).text,
                      "PD 120 - IMAGE COMPLETE") == 0, "headline: complete image");

  // Exhaustive: "VIS CONFIRMED" appears only for a VIS-identified, receiving state; a sync
  // identification must never read as VIS-confirmed or complete while receiving.
  const SessionState states[] = {SessionState::searching, SessionState::vis_acquisition,
                                 SessionState::sync_analysis, SessionState::provisional_lock,
                                 SessionState::receiving, SessionState::loss_recovery,
                                 SessionState::finalizing, SessionState::ready};
  const Identification ids[] = {Identification::none, Identification::vis, Identification::sync,
                                Identification::manual};
  for (SessionState state : states)
    for (Identification id : ids) {
      const Headline h = headline(metrics_for(state, id), nullptr);
      const bool vis_ok = state == SessionState::receiving && id == Identification::vis;
      require(contains(h.text, "VIS CONFIRMED") == vis_ok, "headline: VIS CONFIRMED only for a VIS identification");
      require(!contains(h.text, "COMPLETE"), "headline: never COMPLETE without a finished image");
      require(h.text[0] != '\0', "headline: never blank");
    }
  {
    Metrics m = metrics_for(SessionState::receiving, Identification::sync);
    require(contains(headline(m, nullptr).text, "SYNC LOCKED") && contains(headline(m, nullptr).text, "PARTIAL"),
            "headline: confirmed sync lock stays partial");
    m.active = false;
    require(std::strcmp(headline(m, nullptr).text, "RECEIVER IDLE") == 0, "headline: idle receiver");
  }

  // Gallery chips: a late join must be unmistakable.
  Image joined;
  joined.width = 320;
  joined.height = 240;
  joined.decoded_rows = 214;
  joined.partial = true;
  joined.start_unknown = true;
  joined.identification = Identification::sync;
  joined.identification_score = 87;
  joined.end_reason = EndReason::sync_lost;
  ImageChips chips;
  image_chips(joined, &chips);
  require(std::strcmp(chips.identification, "SYNC LIKELY (87)") == 0, "chips: sync identification with score");
  require(std::strcmp(chips.completeness, "PARTIAL 214/240") == 0, "chips: partial row count");
  require(contains(chips.origin, "JOINED IN PROGRESS") && contains(chips.origin, "START ROW UNKNOWN"),
          "chips: joined-in-progress image says the start row is unknown");
  require(std::strcmp(chips.ending, "SYNC LOST") == 0, "chips: end reason");
  Image full;
  full.width = 320;
  full.height = 240;
  full.decoded_rows = 240;
  full.identification = Identification::vis;
  full.vis_code = 8;
  full.end_reason = EndReason::complete;
  image_chips(full, &chips);
  require(std::strcmp(chips.identification, "VIS 8") == 0 && std::strcmp(chips.completeness, "COMPLETE") == 0 &&
              std::strcmp(chips.origin, "STARTED AT TOP") == 0, "chips: complete VIS image");

  require(std::strcmp(strategy_label(AutoStrategy::vis_sync, Mode::auto_vis), "AUTO (VIS + SYNC)") == 0 &&
              std::strcmp(strategy_label(AutoStrategy::vis_only, Mode::auto_vis), "VIS ONLY") == 0 &&
              std::strcmp(strategy_label(AutoStrategy::vis_sync, Mode::pd120), "MANUAL (VIS FILTER)") == 0,
          "strategy labels, manual is described as a VIS filter");
  std::puts("SSTV status: PASS");
  return 0;
}
