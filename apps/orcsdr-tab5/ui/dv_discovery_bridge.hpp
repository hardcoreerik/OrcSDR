#pragma once
// The one mapping from a validated receive event to a discovery observation, shared by the app and the host tests so the
// database content the corpus tests check is exactly what the Tab5 records.
#include "dv_discovery.hpp"
#include "dv_radio.hpp"

namespace orcsdr::dv {

inline Observation observation_from(const dv_radio::Event& e, uint32_t utc) {
  using digital_voice_ui::Protocol;
  Observation o;
  o.protocol = static_cast<uint8_t>(e.protocol);
  o.frequency_hz = e.frequency_hz;
  if (e.protocol == Protocol::dmr && e.colour_code_valid) { o.area = e.colour_code; o.area_kind = AreaKind::colour_code; }
  else if ((e.protocol == Protocol::nxdn48 || e.protocol == Protocol::nxdn96) && e.ran_valid) { o.area = e.ran; o.area_kind = AreaKind::ran; }
  o.evidence = static_cast<uint8_t>(e.evidence);
  o.slot = e.slot_valid ? e.slot : 0;
  o.has_source = e.has_source; o.has_destination = e.has_destination; o.group_call = e.group_call;
  o.source_id = e.source_id; o.destination_id = e.destination_id;
  o.utc = utc;
  return o;
}

}  // namespace orcsdr::dv
