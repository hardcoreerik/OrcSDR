#pragma once
// The one mapping from a validated receive event to a discovery observation, shared by the app and the host tests so the
// database content the corpus tests check is exactly what the Tab5 records.
#include "dv_discovery.hpp"
#include "dv_dmr_control.hpp"
#include "dv_radio.hpp"

namespace orcsdr::dv {

inline Observation observation_from(const dv_radio::Event& e, uint32_t utc, const DmrControlInfo& ctrl,
                                    uint16_t level_milli = 0) {
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
  o.role_flags = ctrl.role_flags;
  o.has_sys = ctrl.has_sys;
  o.sys_model = ctrl.model;
  o.sys_code = ctrl.sys_code;
  o.net_id = ctrl.net_id;
  o.site_id = ctrl.site_id;
  o.level_milli = level_milli;
  return o;
}

inline Observation observation_from(const dv_radio::Event& e, uint32_t utc) {
  return observation_from(e, utc, parse_dmr_event(e), 0);
}

}  // namespace orcsdr::dv
