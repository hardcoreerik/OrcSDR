#include "radio_session.hpp"

namespace orcsdr::radio {
namespace {

bool idle(ReceiverState state) {
  return state == ReceiverState::disconnected || state == ReceiverState::ready ||
         state == ReceiverState::failed;
}

}  // namespace

void Session::lock() const {
  while (guard_.test_and_set(std::memory_order_acquire)) {}
}

void Session::unlock() const { guard_.clear(std::memory_order_release); }

uint32_t Session::next_generation_locked() {
  if (++generation_ == 0) generation_ = 1;
  return generation_;
}

bool Session::owns_locked(Token token) const {
  return token.owner != Owner::none && token.owner == owner_ && token.generation == generation_;
}

Token Session::acquire(Owner owner, Band band, uint32_t frequency_hz,
                       uint32_t sample_rate_sps) {
  if (owner == Owner::none) return {};
  lock();
  const uint32_t generation = next_generation_locked();
  band_ = band;
  frequency_hz_ = frequency_hz;
  sample_rate_sps_ = sample_rate_sps;
  owner_ = owner;
  unlock();
  return {owner, generation};
}

Token Session::try_acquire(Owner owner, Band band, uint32_t frequency_hz,
                           uint32_t sample_rate_sps) {
  if (owner == Owner::none) return {};
  lock();
  if (owner_ != Owner::none || !idle(receiver_state_)) {
    unlock();
    return {};
  }
  const uint32_t generation = next_generation_locked();
  band_ = band;
  frequency_hz_ = frequency_hz;
  sample_rate_sps_ = sample_rate_sps;
  owner_ = owner;
  unlock();
  return {owner, generation};
}

bool Session::release(Token token) {
  lock();
  if (!owns_locked(token)) {
    unlock();
    return false;
  }
  owner_ = Owner::none;
  receiver_state_ = ReceiverState::ready;
  next_generation_locked();
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
  if (frequency_hz == 0) return false;
  lock();
  if (!owns_locked(token)) {
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
  const Snapshot snapshot{owner_, band_, receiver_state_, frequency_hz_,
                          sample_rate_sps_, generation_};
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
  if (!session.owns(fm) || !session.retuned(fm, 101700000) ||
      !session.set_state(fm, ReceiverState::running))
    return false;
  if (session.try_acquire(Owner::weather, Band::wx, 162400000, 960000).owner !=
      Owner::none)
    return false;
  const Token weather =
      session.acquire(Owner::weather, Band::wx, 162400000, 960000);
  if (session.owns(fm) || !session.owns(weather) || session.release(fm) ||
      !session.release(weather))
    return false;
  const Snapshot released = session.snapshot();
  return released.owner == Owner::none &&
         released.state == ReceiverState::ready &&
         owner_for_band(Band::wx) == Owner::radio;
}

}  // namespace orcsdr::radio
