#include "drone_watch_model.hpp"

namespace orcsdr::drone_watch {

bool valid_packet_age(const SourceSnapshot& source, uint32_t now_ms) {
  // No timestamp means no known observation. uint32 subtraction handles millis() wrap.
  return source.capture_qualified && source.last_packet_valid &&
         static_cast<uint32_t>(now_ms - source.last_packet_ms) < 60000u;
}

Message describe(const SourceSnapshot& s, uint32_t now_ms) {
  if (s.test_data) return {"TEST DATA", "Replay observations are not live radio reception.", false, false};
  if (!s.capture_qualified) return {"SOURCE UNAVAILABLE", "No verified Remote ID acquisition source is connected.", false, false};
  switch (s.state) {
    case SourceState::listening:
      if (valid_packet_age(s, now_ms))
        return {"REMOTE ID RECEIVED", "A qualified source reported a recent broadcast.", true, true};
      return {"NO RID OBSERVED", "Receiver listening; no recent valid packet received.", true, false};
    case SourceState::paused: return {"SCANNER PAUSED", "Monitoring is interrupted. Check source status.", false, false};
    case SourceState::ready: return {"READY - NOT SCANNING", "A receiver is available, but is not listening.", false, false};
    case SourceState::degraded: return {"SOURCE DEGRADED", "Coverage is incomplete. Inspect receiver status.", false, false};
    case SourceState::error: return {"SOURCE ERROR", "Reception failed. Check the source details.", false, false};
    case SourceState::unsupported: return {"METHOD UNSUPPORTED", "This receiver does not support the selected capture path.", false, false};
    default: return {"SOURCE UNAVAILABLE", "No active Remote ID receiver.", false, false};
  }
}
}  // namespace orcsdr::drone_watch
