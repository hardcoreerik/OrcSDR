// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "acars_core.hpp"
#include "iq_queue.hpp"
#include "control_model.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace orc::zero::acars_dashboard {
constexpr size_t kHistory = 16;
constexpr size_t kPending = 16;
constexpr size_t kRows = 5;
constexpr uint32_t kDefaultChannelHz = 131550000;
constexpr uint32_t kInputRate = 240000;
constexpr uint32_t kChannelRate = 24000;

// One radio owner: this is a request to the host, never a hardware tune.
struct RadioRequest { uint32_t center_hz=0, channel_hz=0, rate_sps=kInputRate; };
bool make_radio_request(uint32_t channel_hz, RadioRequest& output);

enum class ReceiverState : uint8_t { disconnected, connecting, receiving, error };
enum class Source : uint8_t { live, demo };
enum class View : uint8_t { activity, aircraft, archive };
enum class Input : uint8_t { next, previous, detail, back, scroll_up, scroll_down, mark_read, next_view };

struct Record {
  uint64_t serial=0;
  uint64_t monotonic_ns=0; // observation time supplied by host, not UTC or RF time
  orcsdr::acars::Message message{};
  bool unread=true;
};
struct Counters {
  uint32_t valid=0, corrected=0, crc_rejected=0, framing_rejected=0, overflows=0;
  uint32_t sync_candidates=0;
  uint64_t known_queue_dropped_samples=0, stream_resets=0, dropped_display_events=0;
};
struct Aircraft {
  char tail[8]{};
  char flight[7]{};
  uint32_t messages=0;
  uint64_t last_seen_ns=0;
};
struct Snapshot {
  Source source=Source::live;
  ReceiverState receiver=ReceiverState::disconnected;
  bool active=false, detail=false, auto_gain=true;
  uint32_t requested_channel_hz=kDefaultChannelHz, applied_channel_hz=0;
  uint32_t applied_center_hz=0, applied_rate_sps=0;
  int gain_tenth_db=0;
  uint64_t generation=0, revision=0, selected_serial=0;
  View view=View::activity;
  size_t held=0, selected=0, detail_scroll=0,aircraft_held=0,archive_held=0,archive_total=0;
  std::array<Record,kHistory> history{}; // oldest is [0]; only the first held entries are valid
  std::array<Aircraft,kHistory> aircraft{};
  std::array<Record,kHistory> archive{}; // newest first
  Counters counters{};
  char error[48]{};
};

// Decoder thread owns start/consume/stop; UI thread owns service/snapshot/input.
// Host serializes start/stop with consume and calls service after handoffs.
class Dashboard {
 public:
  Dashboard();
  Dashboard(const Dashboard&)=delete;
  Dashboard& operator=(const Dashboard&)=delete;
  static constexpr size_t history_capacity() { return kHistory; }
  // On successful host tune/readback: start on the decoder worker, not the UI thread.
  bool start(const RadioRequest& requested, uint64_t generation);
  void stop();
  // A malformed/mismatched buffer returns false and leaves the dashboard in error state.
  bool consume(const uint8_t* cu8, size_t bytes, const linux_sdr::IqMetadata& meta);
  void receiver_failed(); // called by worker on disconnect; no reconnect loop
  // UI thread:
  void service();
  Snapshot snapshot() const { return view_; }  // after service; an intentionally bounded value copy
  bool input(Input action);
  // Existing compact control-model keys; shared F/Space/P/Tab remain host-owned.
  bool handle_compact_key(orc::zero::Key key);
  void set_gain(bool auto_gain, int gain_tenth_db);
  void receiver_connecting();
  // Strictly offline DEMO input, never enters the decoder or live counters.
  void begin_demo();
  bool append_demo(const orcsdr::acars::Message& message, uint64_t observation_ns);
  using ArchiveListener=void(*)(void*,const Record&);
  void set_archive_listener(ArchiveListener listener,void* context){archive_listener_=listener;archive_context_=context;}
  void set_archive(const Record* records,size_t count,size_t total,uint32_t dropped,bool failed);
 private:
  struct Event { orcsdr::acars::Message message{}; uint64_t generation=0, ns=0; };
  static void decoded(void* self, const orcsdr::acars::Message&);
  static void channel_sample(void* self,float i,float q);
  void reset_stream();
  void append(const orcsdr::acars::Message&,uint64_t);
  void bump();
  bool push(Event event);
  orcsdr::acars::Frontend240k front_{};
  orcsdr::acars::Decoder decoder_{};
  std::array<Event,kPending> pending_{};
  std::atomic<uint32_t> head_{0},tail_{0};
  std::atomic<uint64_t> lost_events_{0},resets_{0},known_drop_{0};
  std::array<std::atomic<uint32_t>,6> stats_{};
  std::atomic<uint64_t> active_generation_{0};
  std::atomic<uint32_t> worker_channel_{0};
  std::atomic<uint32_t> worker_center_{0};
  std::atomic<uint8_t> worker_state_{static_cast<uint8_t>(ReceiverState::disconnected)};
  bool worker_active_=false, first_=true;
  uint64_t last_started_generation_=0;
  bool ui_connecting_=false;
  uint64_t expected_next_=0, block_observed_ns_=0;
  RadioRequest radio_{};
  Snapshot view_{};
  uint64_t next_serial_=1;
  ArchiveListener archive_listener_=nullptr;
  void* archive_context_=nullptr;
};
}
