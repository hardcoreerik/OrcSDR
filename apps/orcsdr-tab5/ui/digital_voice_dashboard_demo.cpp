#include "digital_voice_dashboard_demo.hpp"
#include <cstring>
namespace orcsdr::digital_voice_ui::demo {
Snapshot make(Case id) {
  Snapshot s{};
  s.revision = 1;
  s.origin = Origin::demo;
  if (id == Case::disconnected) return s;
  s.receiver = Receiver::ready;
  s.has_frequency = true;
  s.frequency_hz = 446500000;  // synthetic UI tuning example, NOT a recorded RF fact
  if (id == Case::tuned_no_signal) return s;
  if (id == Case::candidate) { s.evidence=Evidence::candidate;return s; }
  if (id == Case::dmr_replay || id == Case::dmr_unknown_slot || id == Case::history_demo) {
    s.protocol=Protocol::dmr;s.origin=Origin::real_iq_replay;
    s.receiver=Receiver::ready;s.evidence=Evidence::identity_valid;
    s.has_frequency=false; // pinned DMR source metadata does not establish an on-air frequency here
    s.has_source=true;s.source_id=3024109;
    s.has_destination=true;s.destination_id=99;s.group_call=true;
    s.colour_code_valid=true;s.colour_code=1;
    s.timeslot_valid=false;s.timeslot=0;
    s.audio=Audio::unavailable;
    // No inferred RF level, call duration, encryption state or wall-clock time.
    if (id == Case::history_demo) {
      s.last_heard_available=true;s.history_count=2;
      s.history[0].protocol=Protocol::dmr;s.history[0].origin=Origin::real_iq_replay;
      s.history[0].evidence=Evidence::identity_valid;
      s.history[0].has_source=s.history[0].has_destination=true;
      s.history[0].source_id=3024109;s.history[0].destination_id=99;
      s.history[1]=s.history[0]; // second independently accepted source-pinned recording; no timestamp inferred
    }
    return s;
  }
  if (id == Case::p25_phase1) {s.protocol=Protocol::p25_phase1;s.evidence=Evidence::none;return s;}
  if (id == Case::dstar_bits) {
    s.protocol=Protocol::dstar;s.origin=Origin::synthetic_bits;
    s.evidence=Evidence::none; // no actual hard-bit fixture loaded, so no integrity claim
    s.dstar_header_valid=false;
    return s;
  }
  if (id == Case::ysf_bits) {
    s.protocol=Protocol::ysf;s.origin=Origin::synthetic_bits;
    s.evidence=Evidence::none;s.ysf_fich_valid=false;return s;
  }
  if (id == Case::nxdn_planned) {
    s.protocol=Protocol::nxdn48;s.evidence=Evidence::none;return s;
  }
  if (id == Case::discontinuity) {
    s.receiver=Receiver::error;s.evidence=Evidence::none;
    s.diagnostics_valid=true;s.iq_drops=1; // synthetic demonstration only
    return s;
  }
  return s;
}
}
