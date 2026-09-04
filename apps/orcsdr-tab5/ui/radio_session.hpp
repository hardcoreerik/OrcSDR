#pragma once

#include <cstdint>
#include <atomic>

namespace orcsdr::radio {

enum class Band : uint8_t { fm, am, wx, cb, lora, browse, adsb, p25 };
enum class Owner : uint8_t { none, fm, p25, adsb, lora, radio, rf_lab, rf_visualizer };
enum class ReceiverState : uint8_t { disconnected, ready, starting, running, stopping, failed };

struct Token {
  Owner owner = Owner::none;
  uint32_t generation = 0;
};

struct Snapshot {
  Owner owner = Owner::none;
  Band band = Band::fm;
  ReceiverState state = ReceiverState::disconnected;
  uint32_t frequency_hz = 0;
  uint32_t sample_rate_sps = 0;
  uint32_t generation = 0;
};

class Session {
 public:
  Token acquire(Owner owner, Band band, uint32_t frequency_hz, uint32_t sample_rate_sps);
  bool owns(Token token) const;
  bool retuned(Token token, uint32_t frequency_hz);
  bool set_state(Token token, ReceiverState state);
  Snapshot snapshot() const;
  static bool self_check();

 private:
  std::atomic<Owner> owner_{Owner::none};
  std::atomic<Band> band_{Band::fm};
  std::atomic<ReceiverState> receiver_state_{ReceiverState::disconnected};
  std::atomic<uint32_t> frequency_hz_{0};
  std::atomic<uint32_t> sample_rate_sps_{0};
  std::atomic<uint32_t> generation_{0};
};

Owner owner_for_band(Band band);

}  // namespace orcsdr::radio
