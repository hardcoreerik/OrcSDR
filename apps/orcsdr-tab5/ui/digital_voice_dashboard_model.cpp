#include "digital_voice_dashboard_model.hpp"
#include <algorithm>
namespace orcsdr::digital_voice_ui {
const char* protocol_name(Protocol p) {
  switch (p) {
    case Protocol::p25_phase1:return "P25 PHASE I";
    case Protocol::p25_phase2:return "P25 PHASE II";
    case Protocol::dmr:return "DMR";
    case Protocol::dstar:return "D-STAR";
    case Protocol::ysf:return "SYSTEM FUSION";
    case Protocol::nxdn48:return "NXDN48";
    case Protocol::nxdn96:return "NXDN96";
    default:return "UNKNOWN PROTOCOL";
  }
}
const char* receiver_name(Receiver r) {
  switch (r) {
    case Receiver::ready:return "TUNED / IDLE";
    case Receiver::receiving:return "RECEIVING";
    case Receiver::paused:return "RECEPTION PAUSED";
    case Receiver::unavailable:return "RECEIVER UNAVAILABLE";
    case Receiver::error:return "RECEIVER ERROR";
    default:return "RECEIVER DISCONNECTED";
  }
}
const char* evidence_name(Evidence e) {
  switch(e) {
    case Evidence::energy:return "ENERGY DETECTED";
    case Evidence::candidate:return "PROTOCOL CANDIDATE";
    case Evidence::sync:return "SYNC ONLY - NOT VERIFIED";
    case Evidence::integrity_valid:return "INTEGRITY VALIDATED";
    case Evidence::identity_valid:return "VERIFIED IDENTITY";
    default:return "NO VERIFIED SIGNAL";
  }
}
const char* origin_name(Origin o) {
  switch(o) {
    case Origin::real_iq_replay:return "RECORDED IQ REPLAY";
    case Origin::synthetic_bits:return "SYNTHETIC BITSTREAM";
    case Origin::demo:return "DEMO / NOT LIVE RF";
    default:return "LIVE RECEIVER";
  }
}
const char* scan_phase_name(ScanPhase p) {
  switch (p) {
    case ScanPhase::idle:return "SCAN READY / IDLE";
    case ScanPhase::settling:return "SCANNER SETTLING";
    case ScanPhase::scanning:return "SCAN RUNNING";
    case ScanPhase::holding:return "HOLD ON VERIFIED ACTIVITY";
    case ScanPhase::paused:return "SCAN PAUSED";
    case ScanPhase::failed:return "SCAN ERROR";
    default:return "SCAN NOT AVAILABLE";
  }
}
const char* nxdn_channel_name(NxdnChannel c) {
  switch(c) {
    case NxdnChannel::rdch:return "RDCH / TRAFFIC";
    case NxdnChannel::rcch:return "RCCH / CONTROL";
    case NxdnChannel::rtch:return "RTCH / TRAFFIC";
    case NxdnChannel::other:return "OTHER CHANNEL";
    default:return "UNKNOWN";
  }
}
const char* nxdn_topology_name(NxdnTopology t) {
  switch(t) {
    case NxdnTopology::conventional:return "CONVENTIONAL";
    case NxdnTopology::type_c:return "TYPE-C TRUNK";
    case NxdnTopology::type_d:return "TYPE-D TRUNK";
    default:return "UNKNOWN";
  }
}
bool apply_ui_action(UiState& ui,Action action,const Snapshot& s) {
  switch(action.kind) {
    case ActionKind::switch_view:
      if(action.value>=static_cast<uint8_t>(View::count))return false;
      ui.view=static_cast<View>(action.value);return true;
    case ActionKind::toggle_density:
      ui.density=ui.density==Density::discovery_first?Density::analysis_first:Density::discovery_first;
      return true;
    case ActionKind::selected_history:
      if(!s.last_heard_available || action.value>=s.history_count || action.value>=kHistory)return false;
      ui.selected_history=action.value;return true;
    case ActionKind::page_history:
      if(!s.last_heard_available)return false;
      if(action.value==0) {ui.history_offset=0;return true;}
      if(action.value==1 && s.history_count>6){ui.history_offset=static_cast<uint8_t>(std::min<size_t>(s.history_count,kHistory)-6);return true;}
      return false;
    case ActionKind::select_scan_hit:
      if(action.value>=s.scan_hit_count || action.value>=kScanHits)return false;
      ui.selected_scan_hit=action.value;return true;
    case ActionKind::select_system:
      if(!s.systems_available||action.value>=s.observed_system_count||action.value>=kObservedSystems)return false;
      ui.selected_system=action.value;return true;
    default:return false; // scan/hold/skip/save are receiver or storage actions, not local changes.
  }
}
const char* audio_name(Audio a) {
  switch(a) {
    case Audio::clear_available:return "CLEAR AUDIO AVAILABLE";
    case Audio::encrypted:return "ENCRYPTED / NO AUDIO";
    case Audio::unavailable:return "VOICE UNAVAILABLE";
    default:return "VOICE STATE UNKNOWN";
  }
}
const char* integration_name(Protocol p) {
  switch(p) {
    case Protocol::p25_phase1:return "NATIVE RECEIVE / VOICE";
    case Protocol::p25_phase2:return "PARTIAL / NO VOICE";
    case Protocol::dmr:return "HOST VALIDATED / NOT LIVE TAB5";
    case Protocol::dstar:return "HOST HEADER / NO IQ FRONTEND";
    case Protocol::ysf:return "HOST FICH / NO IQ FRONTEND";
    case Protocol::nxdn48:case Protocol::nxdn96:return "HOST IQ SYNTHETIC / NO TAB5";
    default:return "NO VERIFIED PROTOCOL";
  }
}
bool trusted_identity(const Snapshot& s) {
  return s.evidence == Evidence::identity_valid;
}
}
