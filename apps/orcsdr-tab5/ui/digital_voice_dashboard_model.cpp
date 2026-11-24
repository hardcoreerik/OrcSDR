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
    case Protocol::dmr:return "IQ METADATA / RF UNVERIFIED";
    case Protocol::dstar:return "HOST HEADER / NO IQ FRONTEND";
    case Protocol::ysf:return "HOST FICH / NO IQ FRONTEND";
    case Protocol::nxdn48:case Protocol::nxdn96:return "IQ RAN ONLY / RF UNVERIFIED";
    default:return "NO VERIFIED PROTOCOL";
  }
}
const char* scan_stage_name(ScanStage s) {
  switch (s) {
    case ScanStage::ready:return "READY";
    case ScanStage::scanning:return "SCANNING";
    case ScanStage::candidate:return "PROTOCOL CANDIDATE";
    case ScanStage::sync:return "SYNC ACQUIRED";
    case ScanStage::integrity:return "INTEGRITY VALIDATED";
    case ScanStage::identity:return "IDENTITY VERIFIED";
    case ScanStage::hold:return "HOLD";
    case ScanStage::paused:return "PAUSED";
    case ScanStage::error:return "ERROR";
    default:return "SCANNER NOT CONNECTED";
  }
}
const char* scan_mode_name(ScanMode m) {
  switch (m) {
    case ScanMode::channel_list:return "CHANNEL LIST";
    case ScanMode::frequency_range:return "FREQUENCY RANGE";
    case ScanMode::window_sweep:return "WINDOW SWEEP";
    default:return "NO SCAN PLAN";
  }
}
const char* voice_phase_name(VoicePhase p) {
  switch (p) {
    case VoicePhase::listening:return "LISTENING";
    case VoicePhase::acquiring:return "ACQUIRING VOICE";
    case VoicePhase::buffering:return "BUFFERING";
    case VoicePhase::decoding:return "DECODING SPEECH";
    case VoicePhase::silence_frame:return "SILENCE FRAME";
    case VoicePhase::degraded:return "DEGRADED AUDIO";
    case VoicePhase::call_ended:return "CALL ENDED";
    case VoicePhase::protected_voice:return "PROTECTED VOICE";
    case VoicePhase::privacy_unknown:return "PRIVACY UNKNOWN";
    case VoicePhase::muted:return "AUDIO MUTED";
    default:return "AUDIO UNAVAILABLE";
  }
}
const char* listen_mode_name(ListenMode m) {
  switch (m) {
    case ListenMode::ts1:return "TS1";
    case ListenMode::ts2:return "TS2";
    case ListenMode::mute:return "MUTE";
    default:return "AUTO";
  }
}
const char* disabled_text(Disabled d) {
  switch (d) {
    case Disabled::no_receiver:return "NO RECEIVER";
    case Disabled::no_plan:return "NO SCAN PLAN";
    case Disabled::not_scanning:return "SCAN NOT RUNNING";
    case Disabled::no_signal_selected:return "SELECT A SIGNAL";
    case Disabled::not_verified:return "NOT VERIFIED YET";
    case Disabled::already_saved:return "ALREADY SAVED";
    case Disabled::list_full:return "SAVED LIST FULL";
    case Disabled::no_storage:return "STORAGE NOT CONNECTED";
    case Disabled::scan_running:return "STOP SCAN FIRST";
    case Disabled::single_plan:return "ONLY ONE PLAN";
    case Disabled::no_frequency:return "NO FREQUENCY";
    default:return "";
  }
}
ActionState action_state(ActionKind kind, const Snapshot& s, const UiState& u) {
  // A receiver in the error state is still present: START retries (the app reports the real failure again if it persists).
  const bool receiver_ok = s.receiver == Receiver::ready || s.receiver == Receiver::receiving || s.receiver == Receiver::paused || s.receiver == Receiver::error;
  const bool running = s.scan.phase == ScanPhase::settling || s.scan.phase == ScanPhase::scanning ||
                       s.scan.phase == ScanPhase::holding || s.scan.phase == ScanPhase::paused;
  switch (kind) {
    case ActionKind::scan:
      if (!s.can_scan && !running) return {false, receiver_ok ? Disabled::no_plan : Disabled::no_receiver};
      if (!receiver_ok && !running) return {false, Disabled::no_receiver};
      return {true, Disabled::none};
    case ActionKind::toggle_hold:
      if (!running || !s.can_hold) return {false, Disabled::not_scanning};
      return {true, Disabled::none};
    case ActionKind::skip_frequency:
      if (!running || !s.can_skip) return {false, Disabled::not_scanning};
      return {true, Disabled::none};
    case ActionKind::save_channel: {
      if (!s.can_save_channel) return {false, Disabled::no_storage};
      if (s.scan_hit_count == 0 || u.selected_scan_hit >= s.scan_hit_count) return {false, Disabled::no_signal_selected};
      const ScanHit& h = s.scan_hits[u.selected_scan_hit];
      if (h.evidence < Evidence::integrity_valid) return {false, Disabled::not_verified};
      if (!h.has_frequency) return {false, Disabled::no_frequency};
      if (h.saved) return {false, Disabled::already_saved};
      if (s.scan.saved_capacity && s.scan.saved_channels >= s.scan.saved_capacity) return {false, Disabled::list_full};
      return {true, Disabled::none};
    }
    case ActionKind::plan_prev:
    case ActionKind::plan_next:
      if (running) return {false, Disabled::scan_running};
      if (s.scan.plan_options < 2) return {false, Disabled::single_plan};
      return {true, Disabled::none};
    default:return {true, Disabled::none};
  }
}
bool trusted_identity(const Snapshot& s) {
  return s.evidence == Evidence::identity_valid;
}
}
