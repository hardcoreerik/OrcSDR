#include "sstv_status.hpp"

#include <cstdio>

namespace orcsdr::sstv_status {

using orcsdr::sstv::AutoStrategy;
using orcsdr::sstv::EndReason;
using orcsdr::sstv::Identification;
using orcsdr::sstv::Image;
using orcsdr::sstv::Metrics;
using orcsdr::sstv::Mode;
using orcsdr::sstv::SessionState;

namespace {
void set(Headline* h, Tone tone, const char* format, const char* a = "", const char* b = "") {
  std::snprintf(h->text, sizeof(h->text), format, a, b);
  h->tone = tone;
}
}  // namespace

const char* end_reason_label(EndReason reason) {
  switch (reason) {
    case EndReason::complete: return "COMPLETE";
    case EndReason::sync_lost: return "SYNC LOST";
    case EndReason::new_vis: return "NEW TRANSMISSION";
    case EndReason::retune: return "RETUNED";
    case EndReason::aborted: return "ABORTED";
    case EndReason::captured: return "CAPTURED LIVE";
    default: return "";
  }
}

Headline headline(const Metrics& m, const Image* last) {
  Headline h;
  const char* mode = orcsdr::sstv::mode_name(m.received_mode);
  if (!m.active) {
    set(&h, Tone::idle, "RECEIVER IDLE");
    return h;
  }
  switch (m.state) {
    case SessionState::vis_acquisition:
      set(&h, Tone::info, "VIS HEADER - ANALYZING");
      return h;
    case SessionState::sync_analysis:
      set(&h, Tone::info, "UNKNOWN MODE - ANALYZING");
      return h;
    case SessionState::provisional_lock:
      set(&h, Tone::warn, "%s - SYNC LIKELY - PARTIAL", mode);
      return h;
    case SessionState::loss_recovery:
      set(&h, Tone::warn, "%s - SYNC LOSS - HOLDING", mode);
      return h;
    case SessionState::receiving:
      if (m.identification == Identification::vis) {
        set(&h, Tone::good, "%s - VIS CONFIRMED", mode);
      } else {
        // Identified from line timing: locked on, but never "VIS confirmed" and never complete.
        set(&h, Tone::warn, "%s - SYNC LOCKED - PARTIAL", mode);
      }
      return h;
    case SessionState::finalizing:
    case SessionState::ready:
      if (last && last->pixels) {
        const char* name = orcsdr::sstv::mode_name(last->mode);
        if (last->partial && last->end_reason == EndReason::sync_lost)
          set(&h, Tone::warn, "SYNC LOST - PARTIAL IMAGE SAVED");
        else if (last->partial)
          set(&h, Tone::warn, "%s - PARTIAL IMAGE SAVED", name);
        else
          set(&h, Tone::good, "%s - IMAGE COMPLETE", name);
        return h;
      }
      set(&h, Tone::idle, "WAITING FOR SSTV");
      return h;
    default:
      set(&h, Tone::idle, "WAITING FOR SSTV");
      return h;
  }
}

void image_chips(const Image& image, ImageChips* out) {
  *out = ImageChips{};
  switch (image.identification) {
    case Identification::vis:
      std::snprintf(out->identification, sizeof(out->identification), "VIS %u",
                    static_cast<unsigned>(image.vis_code));
      break;
    case Identification::sync:
      std::snprintf(out->identification, sizeof(out->identification), "SYNC LIKELY (%u)",
                    static_cast<unsigned>(image.identification_score));
      break;
    case Identification::manual:
      std::snprintf(out->identification, sizeof(out->identification), "MANUAL");
      break;
    default:
      std::snprintf(out->identification, sizeof(out->identification), "UNIDENTIFIED");
      break;
  }
  if (!image.partial && image.decoded_rows >= image.height)
    std::snprintf(out->completeness, sizeof(out->completeness), "COMPLETE");
  else
    std::snprintf(out->completeness, sizeof(out->completeness), "PARTIAL %u/%u",
                  static_cast<unsigned>(image.decoded_rows), static_cast<unsigned>(image.height));
  if (image.start_unknown)
    std::snprintf(out->origin, sizeof(out->origin), "JOINED IN PROGRESS - START ROW UNKNOWN");
  else
    std::snprintf(out->origin, sizeof(out->origin), "STARTED AT TOP");
  std::snprintf(out->ending, sizeof(out->ending), "%s", end_reason_label(image.end_reason));
}

const char* strategy_label(AutoStrategy strategy, Mode manual) {
  if (manual != Mode::auto_vis) return "MANUAL (VIS FILTER)";
  return strategy == AutoStrategy::vis_only ? "VIS ONLY" : "AUTO (VIS + SYNC)";
}

}  // namespace orcsdr::sstv_status
