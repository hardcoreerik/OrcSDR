#pragma once

#include <atomic>
#include <cstdint>

namespace orcsdr::radio {

enum class Band : uint8_t { fm, am, wx, cb, lora, browse, adsb, p25, pocsag, shortwave, airband };
enum class Owner : uint8_t { none, fm, p25, adsb, lora, radio, rf_lab, rf_visualizer, pocsag, weather };
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
  Token try_acquire(Owner owner, Band band, uint32_t frequency_hz, uint32_t sample_rate_sps);
  bool release(Token token);
  bool owns(Token token) const;
  bool retuned(Token token, uint32_t frequency_hz);
  bool set_state(Token token, ReceiverState state);
  Snapshot snapshot() const;
  static bool self_check();

 private:
  void lock() const;
  void unlock() const;
  uint32_t next_generation_locked();
  bool owns_locked(Token token) const;

  mutable std::atomic_flag guard_ = ATOMIC_FLAG_INIT;
  Owner owner_ = Owner::none;
  Band band_ = Band::fm;
  ReceiverState receiver_state_ = ReceiverState::disconnected;
  uint32_t frequency_hz_ = 0;
  uint32_t sample_rate_sps_ = 0;
  uint32_t generation_ = 0;
};

Owner owner_for_band(Band band);

}  // namespace orcsdr::radio
