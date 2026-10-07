#include "radio_session.hpp"

namespace orcsdr::radio {

void Session::lock() const {
  while (guard_.test_and_set(std::memory_order_acquire)) {}
}

void Session::unlock() const { guard_.clear(std::memory_order_release); }

bool Session::owns_locked(Token token) const {
  return token.owner != Owner::none && token.owner == owner_ && token.generation == generation_;
}

Token Session::acquire_locked(Owner owner, Band band, uint32_t frequency_hz,
                              uint32_t sample_rate_sps) {
  if (owner == Owner::none || frequency_hz == 0 || sample_rate_sps == 0) return {};
  ++generation_;
  if (generation_ == 0) generation_ = 1;
  owner_ = owner;
  band_ = band;
  frequency_hz_ = frequency_hz;
  sample_rate_sps_ = sample_rate_sps;
  return {owner_, generation_};
}

Token Session::acquire(Owner owner, Band band, uint32_t frequency_hz,
                       uint32_t sample_rate_sps) {
  lock();
  const Token token = acquire_locked(owner, band, frequency_hz, sample_rate_sps);
  unlock();
  return token;
}

Token Session::try_acquire(Owner owner, Band band, uint32_t frequency_hz,
                           uint32_t sample_rate_sps) {
  lock();
  const bool idle = owner_ == Owner::none &&
                    (receiver_state_ == ReceiverState::disconnected ||
                     receiver_state_ == ReceiverState::ready ||
                     receiver_state_ == ReceiverState::failed);
  const Token token = idle ? acquire_locked(owner, band, frequency_hz, sample_rate_sps) : Token{};
  unlock();
  return token;
}

bool Session::release(Token token) {
  lock();
  if (!owns_locked(token)) {
    unlock();
    return false;
  }
  owner_ = Owner::none;
  frequency_hz_ = 0;
  sample_rate_sps_ = 0;
  unlock();
  return true;
}

bool Session::owns(Token token) const {
  lock();
  const bool result = owns_locked(token);
  unlock();
  return result;
}

bool Session::retuned(Token token, uint32_t frequency_hz) {
  lock();
  if (!owns_locked(token) || frequency_hz == 0) {
    unlock();
    return false;
  }
  frequency_hz_ = frequency_hz;
  unlock();
  return true;
}

bool Session::set_state(Token token, ReceiverState state) {
  lock();
  if (!owns_locked(token)) {
    unlock();
    return false;
  }
  receiver_state_ = state;
  unlock();
  return true;
}

Snapshot Session::snapshot() const {
  lock();
  const Snapshot snapshot{
      owner_, band_, receiver_state_, frequency_hz_, sample_rate_sps_, generation_};
  unlock();
  return snapshot;
}

Owner owner_for_band(Band band) {
  switch (band) {
    case Band::fm: return Owner::fm;
    case Band::p25: return Owner::p25;
    case Band::adsb: return Owner::adsb;
    case Band::lora: return Owner::lora;
    case Band::pocsag: return Owner::pocsag;
    default: return Owner::radio;
  }
}

bool Session::self_check() {
  Session session;
  const Token fm = session.acquire(Owner::fm, Band::fm, 96100000, 960000);
  if (!session.owns(fm) || !session.set_state(fm, ReceiverState::running)) return false;
  if (session.try_acquire(Owner::weather, Band::wx, 162400000, 960000).owner !=
      Owner::none)
    return false;
  const Token wx = session.acquire(Owner::weather, Band::wx, 162425000, 960000);
  if (session.owns(fm) || !session.owns(wx) ||
      !session.set_state(wx, ReceiverState::ready))
    return false;
  if (!session.release(wx) || session.snapshot().owner != Owner::none) return false;
  return owner_for_band(Band::wx) == Owner::radio;
}

}  // namespace orcsdr::radio
