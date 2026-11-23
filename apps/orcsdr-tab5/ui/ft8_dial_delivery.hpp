#pragma once
#include "ft8_dial_preview.hpp"
#include "../../../orcdial/src/control/projection_capabilities.hpp"

namespace orcsdr::ft8::dial_preview {
// Latest-only, UI-task producer. No allocations, callback work or control IDs.
class Delivery {
 public:
  uint32_t observe(const Decode& decode, uint32_t now) {
    latest_ = decode; captured_ = now; available_ = true;
    if (!++record_) ++record_;
    return record_;
  }
  template<class Send>
  bool poll(uint32_t now, uint32_t sender, bool ready, Send send) {
    if (!ready || !available_ || uint32_t(now-captured_) >= orc::projection::kFreshMs ||
        (sent_ && uint32_t(now-last_attempt_) < orc::projection::kIntervalMs)) return false;
    // Retry the same identity after loss; a later decode replaces it, never queues.
    last_attempt_ = now; sent_ = true;
    uint8_t brief[64]{}, text[64]{};
    return encode(latest_, sender, record_, brief, text) && send(brief) && send(text);
  }
 private:
  Decode latest_{};
  uint32_t captured_ = 0, record_ = 0, last_attempt_ = 0;
  bool available_ = false, sent_ = false;
};
} // namespace orcsdr::ft8::dial_preview
