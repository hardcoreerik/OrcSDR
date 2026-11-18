#pragma once

#include "adsb_decoder.hpp"
#include "iq_queue.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace orc::zero::adsb {
constexpr uint32_t kCenterHz = 1090000000u;
constexpr uint32_t kRateSps = 2048000u;
constexpr std::size_t kAircraftCapacity = 32;
constexpr std::size_t kUniqueCapacity = 256;
constexpr std::size_t kVisibleRows = 6;
constexpr uint64_t kExpireNs = 90'000'000'000ull;
constexpr uint64_t kCprPairNs = 10'000'000'000ull;
constexpr uint64_t kStaleNs = 10'000'000'000ull;

struct RadioRequest { uint32_t center_hz=kCenterHz, rate_sps=kRateSps; };
enum class Status : uint8_t { no_receiver, connecting, waiting, receiving, stale, disconnected, error, demo };
enum class Result : uint8_t { accepted, invalid_data, invalid_radio, inactive, old_generation, invalid_time };
enum class Action : uint8_t { none, up, down, open_detail, back, request_gain };
struct Aircraft {
  uint32_t icao=0;
  char callsign[9]{};
  bool has_callsign=false, has_altitude=false, has_speed=false, has_heading=false;
  bool has_vertical_rate=false, has_position=false, has_signal=false;
  int altitude_ft=0, speed_kts=0, heading_deg=0, vertical_rate_fpm=0;
  double latitude=0.0, longitude=0.0;
  uint16_t relative_signal=0;
  uint64_t last_seen_ns=0;
};
struct Counters {
  uint64_t preambles=0, candidates=0, crc_ok=0, rejected=0, df17_candidates=0;
  uint64_t unique_seen=0, known_queue_drops=0, decoder_resets=0;
  bool unique_capped=false;
};
struct Snapshot {
  std::array<Aircraft,kAircraftCapacity> aircraft{};
  std::size_t count=0, selection=0, scroll=0;
  uint32_t selected_icao=0;
  uint64_t now_ns=0, generation=0;
  bool detail=false, is_demo=false, has_receiver=false;
  Status status=Status::no_receiver;
  Counters counters{};
  uint64_t revision=0;
};

// Only the decoder/consumer thread mutates Model. Snapshot is a bounded copy.
// apply_verified is an input for the *existing* decoder's CRC-valid callback;
// do not call it with preamble candidates or unverified network data.
class Model {
 public:
  void reset(uint64_t generation);
  void invalidate_cpr();
  void apply_verified(const orcsdr::adsb_rx::Frame&,uint64_t now_ns,uint64_t generation);
  void expire(uint64_t now_ns);
  void navigate(Action action);
  void set_counters(const Counters& counters) { const bool capped=counters_.unique_capped; counters_=counters; counters_.unique_capped|=capped; ++revision_; }
  Snapshot snapshot(uint64_t now_ns,Status status,bool demo,bool receiver) const;
  std::size_t count() const { return count_; }
 private:
  struct Report { orcsdr::adsb_rx::Frame frame{}; uint64_t time_ns=0,generation=0; bool valid=false; };
  struct Entry { Aircraft a{}; Report even{},odd{}; };
  std::array<Entry,kAircraftCapacity> entries_{};
  std::array<uint32_t,kUniqueCapacity> unique_{};
  size_t count_=0, unique_count_=0, selected_=0, scroll_=0;
  bool detail_=false;
  uint64_t generation_=0, revision_=0;
  Counters counters_{};
  void keep_selection_visible();
};

// Only one thread calls activate/ready/consume/stop; renderer/UI uses snapshots
// sent across the coordinator's existing queue/dispatch boundary.
class Controller {
 public:
  RadioRequest radio_request() const { return {}; }
  bool activate(uint64_t generation,uint64_t now_ns,bool demo=false);
  void receiver_ready(uint64_t now_ns);
  void no_receiver();
  void disconnected();
  void error();
  void stop();
  Result consume_cu8(const uint8_t* cu8,size_t bytes,const orc::linux_sdr::IqMetadata& meta);
  Snapshot snapshot(uint64_t now_ns);
  void navigate(Action action) { model_.navigate(action); }
  const Model& model() const { return model_; }
  static bool supported_radio(uint32_t center,uint32_t rate);
 private:
  static void on_frame(const orcsdr::adsb_rx::Frame& frame,void* ctx);
  void reset_decoder();
  Counters counters() const;
  Model model_{};
  orcsdr::adsb_rx::Decoder decoder_{};
  orcsdr::adsb_rx::Stats prior_{};
  uint64_t generation_=0,expected_sample_=0,last_ns_=0,last_valid_ns_=0;
  uint64_t known_drops_=0,resets_=0;
  bool active_=false,ready_=false,first_=true,demo_=false,has_valid_=false;
  Status status_=Status::no_receiver;
};
}  // namespace orc::zero::adsb
