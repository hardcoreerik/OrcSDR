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
  ScanStatus scan{};
  bool last_heard_available = false, systems_available = false;
  uint8_t history_count = 0;
  Heard history[kHistory]{};
};
struct UiState {
  View view = View::explore;
  Density density = Density::discovery_first;
  uint8_t history_offset = 0;
  uint8_t selected_history = 0;
};
enum class ActionKind : uint8_t { none, switch_view, selected_history, save_channel, scan, setup_protocol };
struct Action { ActionKind kind = ActionKind::none; uint8_t value = 0; };
const char* protocol_name(Protocol p);
const char* receiver_name(Receiver r);
const char* evidence_name(Evidence e);
const char* origin_name(Origin o);
const char* scan_phase_name(ScanPhase p);
const char* audio_name(Audio a);
// Compile-time product truth matrix: not inferred from a demonstration fixture.
const char* integration_name(Protocol p);
// Do not show metadata unless its field-level validity and event confidence suffice.
bool trusted_identity(const Snapshot& s);
}
