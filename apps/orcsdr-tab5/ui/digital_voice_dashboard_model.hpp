#pragma once
// Presentation contract ONLY. The radio/decoder owner supplies truth-qualified snapshots.
#include <cstddef>
#include <cstdint>

namespace orcsdr::digital_voice_ui {
enum class Protocol : uint8_t { unknown, p25_phase1, p25_phase2, dmr, dstar, ysf, nxdn48, nxdn96 };
enum class Receiver : uint8_t { disconnected, ready, receiving, paused, unavailable, error };
enum class Evidence : uint8_t { none, energy, candidate, sync, integrity_valid, identity_valid };
enum class Origin : uint8_t { live, real_iq_replay, synthetic_bits, demo };
enum class Audio : uint8_t { unknown, clear_available, encrypted, unavailable };
enum class View : uint8_t { explore, live, last_heard, systems, signal, setup, count };
enum class Density : uint8_t { discovery_first, analysis_first };
// Scanner state is a presentation snapshot owned by the existing receiver/scan service.
// No scan engine or radio ownership is created by this UI.
enum class ScanPhase : uint8_t { unavailable, idle, settling, scanning, holding, paused, failed };
enum class NxdnChannel : uint8_t { unknown, rdch, rcch, rtch, other };
enum class NxdnTopology : uint8_t { unknown, conventional, type_c, type_d };
struct NxdnFacts {
  NxdnChannel channel = NxdnChannel::unknown;
  NxdnTopology topology = NxdnTopology::unknown;
  bool channel_valid = false, topology_valid = false;
  uint16_t channel_number = 0;
  bool channel_number_valid = false;
  uint32_t cac_crc_valid = 0;
  bool cac_metrics_valid = false;
};
constexpr size_t kScanHits = 4;
struct ScanHit {
  Protocol protocol = Protocol::unknown;
  Evidence evidence = Evidence::none;
  uint32_t frequency_hz = 0;
  bool has_frequency = false;
  uint8_t ran = 0;
  bool ran_valid = false;
  uint8_t colour_code = 0;
  bool colour_code_valid = false;
};
constexpr size_t kObservedSystems = 4;
constexpr size_t kTopIds = 3;
struct ObservedSystem {
  Protocol protocol = Protocol::unknown;
  NxdnTopology topology = NxdnTopology::unknown;
  Evidence evidence = Evidence::none;
  uint32_t frequency_hz = 0;
  bool has_frequency = false, topology_valid = false;
  uint8_t ran = 0;
  bool ran_valid = false;
  uint16_t site = 0;
  bool site_valid = false;
  uint8_t colour_code = 0;
  bool colour_code_valid = false;
  uint8_t slots_seen = 0;                // DMR: bit0 TS1, bit1 TS2
  uint32_t heard_count = 0;
  uint16_t talkgroup_count = 0, subscriber_count = 0;
  uint32_t top_talkgroups[kTopIds]{}, top_subscribers[kTopIds]{};   // newest first, identity-validated only
  uint8_t top_talkgroup_n = 0, top_subscriber_n = 0;
  char last_time[12]{};                  // receiver-provided time, empty when unknown
};
struct ScanStatus {
  ScanPhase phase = ScanPhase::unavailable;
  uint16_t visited = 0, planned = 0;
  uint32_t current_frequency_hz = 0;
  bool current_frequency_valid = false;
  uint32_t validated_hits = 0;
  bool progress_valid = false;
};
constexpr size_t kHistory = 8;
struct Heard {
  Protocol protocol = Protocol::unknown;
  Evidence evidence = Evidence::none;
  Origin origin = Origin::live;
  uint32_t frequency_hz = 0;
  uint32_t source_id = 0, destination_id = 0;
  bool has_source = false, has_destination = false, has_time = false;
  uint8_t ran = 0;
  bool ran_valid = false;
  uint8_t colour_code = 0, timeslot = 0;
  bool colour_code_valid = false, timeslot_valid = false, group_call = false;
  char time_utc[12]{}; // time from receiver with known provenance, not guessed host time
};
struct Snapshot {
  uint32_t revision = 0;
  Receiver receiver = Receiver::disconnected;
  Protocol protocol = Protocol::unknown;
  Evidence evidence = Evidence::none;
  Origin origin = Origin::live;
  Audio audio = Audio::unavailable;
  uint32_t frequency_hz = 0, source_id = 0, destination_id = 0;
  bool has_frequency = false, has_source = false, has_destination = false;
  bool group_call = false, is_encrypted = false, encryption_known = false;
  uint8_t colour_code = 0, timeslot = 0, nac_valid = 0, ran = 0;
  bool colour_code_valid = false, timeslot_valid = false, ran_valid = false;
  NxdnFacts nxdn{};
  uint16_t nac = 0;
  char dstar_my[12]{}, dstar_ur[12]{}, dstar_rpt1[12]{}, dstar_rpt2[12]{};
  bool dstar_header_valid = false;
  uint32_t ysf_fich_raw = 0;
  bool ysf_fich_valid = false;
  uint32_t sync_count = 0, integrity_frames = 0, fec_corrected = 0;
  uint32_t crc_failures = 0, iq_drops = 0;
  bool diagnostics_valid = false;
  int32_t relative_level_tenths_db = 0;
  bool relative_level_valid = false;
  bool can_save_channel = false, can_scan = false, can_filter = false;
  bool can_hold = false, can_skip = false, scan_on_hold = false;
  ScanStatus scan{};
  uint8_t scan_hit_count = 0;
  ScanHit scan_hits[kScanHits]{};
  bool last_heard_available = false, systems_available = false;
  uint8_t observed_system_count = 0;
  ObservedSystem observed_systems[kObservedSystems]{};
  uint8_t history_count = 0;
  Heard history[kHistory]{};
  // Discovery database totals for the table shown in Systems (live database, or the replay's own RAM-only one).
  bool db_available = false;
  uint16_t db_systems = 0, db_talkgroups = 0, db_subscribers = 0;
};
struct UiState {
  View view = View::explore;
  Density density = Density::discovery_first;
  uint8_t history_offset = 0;
  uint8_t selected_history = 0;
  uint8_t selected_scan_hit = 0;
  uint8_t selected_system = 0;
};
enum class ActionKind : uint8_t { none, switch_view, selected_history, save_channel, scan, setup_protocol,
                                  toggle_hold, skip_frequency, toggle_density, select_scan_hit,
                                  select_system, page_history };
struct Action { ActionKind kind = ActionKind::none; uint8_t value = 0; };
const char* protocol_name(Protocol p);
const char* receiver_name(Receiver r);
const char* evidence_name(Evidence e);
const char* origin_name(Origin o);
const char* scan_phase_name(ScanPhase p);
const char* audio_name(Audio a);
const char* nxdn_channel_name(NxdnChannel c);
const char* nxdn_topology_name(NxdnTopology t);
// UI-only actions mutate the local presentation state; radio actions are returned to the
// existing receiver owner by the caller. No action here can retune or own USB.
bool apply_ui_action(UiState& ui, Action action, const Snapshot& snapshot);
// Compile-time product truth matrix: not inferred from a demonstration fixture.
const char* integration_name(Protocol p);
// Do not show metadata unless its field-level validity and event confidence suffice.
bool trusted_identity(const Snapshot& s);
}
