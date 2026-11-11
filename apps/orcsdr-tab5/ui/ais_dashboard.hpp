#pragma once
// Native AIS seven-panel M5GFX dashboard contract. UI task only; decoder and receiver are separate.
// No demo data ever enters the production renderer. The app provides real, bounded snapshots.
#include "ais_dashboard_layout.hpp"
#include <cstddef>
#include <cstdint>
namespace orcsdr::ais_ui {
enum class Station : uint8_t { vessel_a, vessel_b, base_station, aid_physical, aid_virtual, sar_aircraft, other, aid_2026 };
enum class IdentityKind : uint8_t { mmsi, source_id28 };
// Type 28 uses a Source ID, never silently label it as an MMSI.
enum class ReceiveState : uint8_t { disconnected, ready, recording, unavailable };
struct Target {
  uint32_t mmsi=0, source_id28=0;
  IdentityKind identity_kind=IdentityKind::mmsi;
  Station station=Station::other;
  char name[32]{}, callsign[12]{}, destination[32]{};
  char description[24]{}, nav_status[20]{};
  bool has_position=false,has_speed=false,has_course=false,has_heading=false,position_stale=false;
  int32_t lat_e7=0,lon_e7=0;
  uint16_t speed_tenths_kn=0, course_tenths_deg=0,heading_deg=0;
  uint32_t position_age_s=0,heard_age_s=0;
  // Actual received fixes only; never extrapolate missing vessel movement.
  struct TrackFix { int32_t lat_e7=0,lon_e7=0; uint32_t age_s=0; };
  TrackFix history[8]{};
  uint8_t history_count=0;
  uint8_t last_type=0;
};
struct Message {
  uint32_t mmsi=0, source_id28=0, age_ms=0;
  IdentityKind identity_kind=IdentityKind::mmsi;
  uint8_t type=0,channel=0; // channel 1 / 2, zero = not known
  bool crc_ok=false;
  char summary[72]{};
  char raw_hex[264]{}; // bounded; truncation indicated by raw_truncated
  bool raw_truncated=false;
};
struct Event {
  uint32_t mmsi=0,source_id28=0,age_s=0;
  IdentityKind identity_kind=IdentityKind::mmsi;
  uint8_t type=0;
  bool safety_related=false;
  char title[64]{}, detail[104]{};
};
constexpr size_t kTargets=32,kMessages=16,kEvents=14,kSpectrumBins=64;
struct Snapshot {
  uint32_t revision=0;
  ReceiveState receiver=ReceiveState::disconnected;
  bool receiver_settled=false, coordinates_valid=false;
  bool ais1_enabled=true,ais2_enabled=true,long_range=false,nmea_output=false,logging=false;
  bool sd_available=false,rtl_tcp=false;
  // Set only by the bench replay harness. Every page then says the data is replayed, not live RF.
  bool test_replay=false;
  bool spectrum_valid[2]{false,false};
  int battery_percent=-1;
  uint32_t center_hz=162000000,sample_rate_sps=0;
  uint32_t frames[2]{},crc_ok[2]{},crc_failed[2]{},iq_drops=0;
  uint32_t elapsed_s=0;
  int32_t receiver_lat_e7=0,receiver_lon_e7=0;
  int16_t spectrum_dbfs[2][kSpectrumBins]{}; // calibrated dBFS tenths; no plot unless spectrum_valid
  uint8_t target_count=0,message_count=0,event_count=0;
  Target targets[kTargets]{};
  Message messages[kMessages]{};
  Event events[kEvents]{};
};
enum class ActionKind : uint8_t {
  none, close, open_settings, target_selected, message_selected, set_range_nm,
  set_message_type, toggle_ais1, toggle_ais2, toggle_long_range, toggle_nmea, toggle_logging,
  set_receiver, open_full_map
};
struct Action { ActionKind kind=ActionKind::none; uint32_t value=0;
  IdentityKind identity_kind=IdentityKind::mmsi; // target_selected only
};
// Only call enter() after screen_controller has granted screens::Id::ais.
// All rendering/touches occur on the UI task, never from the receiver callback.
void enter(const Snapshot& snapshot);
void update(const Snapshot& snapshot);
void leave();
bool active();
View view();
uint8_t protocol_type(); // selected 1..28
Action handle_touch(int32_t x,int32_t y);
void show_preview_view(View v,const Snapshot& snapshot); // owner-controlled documentation capture, no synthetic data
bool self_check();
} // namespace orcsdr::ais_ui
