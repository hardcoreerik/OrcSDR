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
// Digital voice audio as the LIVE page shows it. Presentation only: the audio service owns every value.
enum class VoicePhase : uint8_t { unavailable, listening, acquiring, buffering, decoding, silence_frame, degraded, call_ended,
                                  protected_voice, privacy_unknown, muted };
enum class ListenMode : uint8_t { automatic, ts1, ts2, mute };
struct VoiceAudioView {
  bool available = false;
  VoicePhase phase = VoicePhase::unavailable;
  ListenMode listen = ListenMode::automatic;
  uint8_t active_slot = 0;               // 0 = none voiced
  uint8_t level = 0;                     // 0..8, coarse on purpose (limits repaint rate)
  uint8_t privacy_ts1 = 0, privacy_ts2 = 0;   // 0 unknown, 1 clear, 2 protected
  uint32_t speech_frames = 0, silence_frames = 0, error_frames = 0, concealed_frames = 0, underruns = 0;
  uint16_t buffered_ms = 0, latency_ms = 0;
  uint32_t source_id = 0, destination_id = 0;
  bool group_call = false;
};
enum class View : uint8_t { explore, live, last_heard, systems, signal, setup, count };
enum class Density : uint8_t { discovery_first, analysis_first };
// Scanner state is a presentation snapshot owned by the existing receiver/scan service.
// No scan engine or radio ownership is created by this UI.
enum class ScanPhase : uint8_t { unavailable, idle, settling, scanning, holding, paused, failed };
// What the scanner is working through. window_sweep needs a wideband survey stage that does not exist yet: it is reported
// as unavailable, never faked.
enum class ScanMode : uint8_t { unavailable, channel_list, frequency_range, window_sweep };
// The single status the SCAN screen leads with. Derived only from facts the receiver owner reports; there is no energy
// detector in the decoder chain, so no stage below `scanning` is ever shown without a decoder having said so.
enum class ScanStage : uint8_t { unavailable, ready, scanning, candidate, sync, integrity, identity, hold, paused, error };
constexpr size_t kPlanNameMax = 30;
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
  uint8_t timeslot = 0;
  bool timeslot_valid = false;
  uint32_t source_id = 0, destination_id = 0;
  bool has_source = false, has_destination = false, group_call = false;
  NxdnChannel nxdn_channel = NxdnChannel::unknown;
  bool nxdn_channel_valid = false;
  uint16_t seen_count = 0;               // times this channel produced this evidence (this table's own count)
  char first_time[12]{}, last_time[12]{};  // receiver-provided clock times; empty = unknown, never guessed
  bool is_new = false;                   // first observation happened during this visit to the dashboard
  bool saved = false;                    // already in the saved scan list
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
  ScanMode mode = ScanMode::unavailable;
  ScanStage stage = ScanStage::unavailable;
  uint16_t channel_index = 0;            // 1-based position in the plan; 0 = not scanning
  uint32_t passes = 0;
  uint16_t candidates = 0;               // channels that showed sync-level activity but nothing verified
  uint16_t validated = 0;                // channels with integrity- or identity-validated activity
  uint16_t new_this_session = 0;
  char plan_name[kPlanNameMax]{};        // "SAVED LIST" or the range preset name
  uint8_t plan_option = 0, plan_options = 0;
  uint32_t range_start_hz = 0, range_end_hz = 0, range_step_hz = 0;
  uint32_t est_pass_seconds = 0;         // channels x (settle + dwell): an estimate, shown as such
  uint8_t saved_channels = 0, saved_capacity = 0;
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
  VoiceAudioView voice{};
  bool save_speech = false;               // SETUP: record decoded DMR speech to the SD card
  bool can_save_speech = false;           // the app has storage and a settings store for it
  uint16_t speech_files = 0;              // recordings written this visit
  bool save_iq = false;                   // SETUP (DEV): keep IQ around call-like events
  uint16_t iq_files = 0;
  uint16_t band_mask = 0;                 // SETUP band picker: bit i = built-in band i is part of MY BANDS
  uint16_t band_channels = 0;             // channels in the selected bands
  uint16_t band_minutes = 0;              // rough time for one pass over them, in minutes
  uint32_t audio_revision = 0;           // moves when the voice panel's content changes; the page revision does not
};
struct UiState {
  View view = View::explore;
  Density density = Density::discovery_first;
  uint8_t history_offset = 0;
  uint8_t selected_history = 0;
  uint8_t selected_scan_hit = 0;
  uint8_t selected_system = 0;
  uint8_t system_offset = 0;   // Systems page: index of the first listed system (newest first), moves in pages of kObservedSystems
};
enum class ActionKind : uint8_t { none, switch_view, selected_history, save_channel, scan, setup_protocol,
                                  toggle_hold, skip_frequency, toggle_density, select_scan_hit,
                                  select_system, page_history, plan_prev, plan_next, set_listen, toggle_save_speech, toggle_band, toggle_save_iq, page_systems };
// Why a control is unavailable, in words the screen can show. `none` = the control is usable.
enum class Disabled : uint8_t { none, no_receiver, no_plan, not_scanning, no_signal_selected, not_verified, already_saved,
                                list_full, no_storage, scan_running, single_plan, no_frequency };
struct ActionState { bool enabled = false; Disabled reason = Disabled::none; };
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
// Single source of truth for whether a SCAN control is usable (the renderer draws it and the touch handler honours it).
ActionState action_state(ActionKind kind, const Snapshot& s, const UiState& u);
const char* disabled_text(Disabled d);
const char* scan_stage_name(ScanStage s);
const char* scan_mode_name(ScanMode m);
const char* voice_phase_name(VoicePhase p);
const char* listen_mode_name(ListenMode m);
}
