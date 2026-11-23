// Linked into standalone OrcDial security tests via -DORCSDR_SOURCE_DIR.
// A separate translation unit keeps the two repositories' control headers apart.
#include "../apps/orcsdr-tab5/ui/ft8_dial_delivery.hpp"
#include <cstring>
extern "C" bool ft8_receiver_frames(uint8_t* brief, uint8_t* text) {
  orcsdr::ft8::Decode d{};
  d.utc_epoch=1793303100; d.dial_hz=14074000; d.snr_db=-12;
  d.kind=orcsdr::ft8::DecodeKind::cq;
  std::strcpy(d.callsign,"W1AW");std::strcpy(d.grid,"FN31");
  std::strcpy(d.message,"CQ W1AW FN31");
  return orcsdr::ft8::dial_preview::encode(d,123,808,brief,text);
}
