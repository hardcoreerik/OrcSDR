#include "radio_session.hpp"

namespace orcsdr::radio {

Token Session::acquire(Owner owner, Band band, uint32_t frequency_hz,
                       uint32_t sample_rate_sps) {
  if (owner == Owner::none) return {};
  uint32_t generation = generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
  if (generation == 0) {
    generation_.store(1, std::memory_order_release);
    generation = 1;
  }
  band_.store(band, std::memory_order_relaxed);
  frequency_hz_.store(frequency_hz, std::memory_order_relaxed);
  sample_rate_sps_.store(sample_rate_sps, std::memory_order_relaxed);
  owner_.store(owner, std::memory_order_release);
  return {owner, generation};
}

bool Session::owns(Token token) const {
  return token.owner != Owner::none &&
         token.owner == owner_.load(std::memory_order_acquire) &&
         token.generation == generation_.load(std::memory_order_acquire);
}

bool Session::retuned(Token token, uint32_t frequency_hz) {
  if (!owns(token) || frequency_hz == 0) return false;
  frequency_hz_.store(frequency_hz, std::memory_order_release);
  return true;
}

bool Session::set_state(Token token, ReceiverState state) {
  if (!owns(token)) return false;
  receiver_state_.store(state, std::memory_order_release);
  return true;
}

Snapshot Session::snapshot() const {
  Snapshot snapshot;
  snapshot.owner = owner_.load(std::memory_order_acquire);
  snapshot.band = band_.load(std::memory_order_relaxed);
  snapshot.state = receiver_state_.load(std::memory_order_relaxed);
  snapshot.frequency_hz = frequency_hz_.load(std::memory_order_relaxed);
  snapshot.sample_rate_sps = sample_rate_sps_.load(std::memory_order_relaxed);
  snapshot.generation = generation_.load(std::memory_order_acquire);
  return snapshot;
}

Owner owner_for_band(Band band) {
  switch (band) {
    case Band::fm: return Owner::fm;
    case Band::p25: return Owner::p25;
    case Band::adsb: return Owner::adsb;
    case Band::lora: return Owner::lora;
    default: return Owner::radio;
  }
}

bool Session::self_check() {
  Session session;
  const Token fm = session.acquire(Owner::fm, Band::fm, 96100000, 960000);
  if (!session.owns(fm) || !session.retuned(fm, 101700000)) return false;
  const Token p25 = session.acquire(Owner::p25, Band::p25, 453812500, 960000);
  return !session.owns(fm) && session.owns(p25) &&
         !session.retuned(fm, 102300000) &&
         session.snapshot().frequency_hz == 453812500;
}

}  // namespace orcsdr::radio
