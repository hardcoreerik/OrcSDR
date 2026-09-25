#include "airband_scanner.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace orcsdr::airband {
namespace {

constexpr float kSquelchHysteresisDb = 3.0f;
constexpr uint32_t kReleaseMs = 260u;
constexpr uint32_t kMinActivityMs = 180u;

bool reached(uint32_t now_ms, uint32_t deadline_ms) {
  return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

uint32_t clamp_band(uint32_t frequency_hz) {
  return std::min(kMaxFrequencyHz, std::max(kMinFrequencyHz, frequency_hz));
}

}  // namespace

uint32_t spacing_hz(Spacing spacing) {
  return spacing == Spacing::khz833 ? 8333u : 25000u;
}

const char* spacing_name(Spacing spacing) {
  return spacing == Spacing::khz833 ? "8.33 kHz" : "25 kHz";
}

const char* source_name(ScanSource source) {
  return source == ScanSource::full_band ? "FULL BAND" : "AIRPORT BANK";
}

const char* state_name(ScanState state) {
  switch (state) {
    case ScanState::off: return "MANUAL";
    case ScanState::scanning: return "SCANNING";
    case ScanState::settling: return "CHECKING";
    case ScanState::receiving: return "RECEIVING";
    case ScanState::hang: return "HANG";
    case ScanState::held: return "HOLD";
  }
  return "?";
}

bool in_band(uint32_t frequency_hz) {
  return frequency_hz >= kMinFrequencyHz && frequency_hz <= kMaxFrequencyHz;
}

uint32_t snap_frequency(uint32_t frequency_hz, Spacing spacing) {
  frequency_hz = clamp_band(frequency_hz);
  const uint32_t step = spacing_hz(spacing);
  const uint64_t offset = static_cast<uint64_t>(frequency_hz - kMinFrequencyHz);
  const uint64_t index = (offset + step / 2u) / step;
  const uint64_t snapped = static_cast<uint64_t>(kMinFrequencyHz) + index * step;
  return clamp_band(static_cast<uint32_t>(
      std::min<uint64_t>(snapped, static_cast<uint64_t>(kMaxFrequencyHz))));
}

uint32_t step_frequency(uint32_t frequency_hz, int direction, Spacing spacing) {
  const uint32_t current = snap_frequency(frequency_hz, spacing);
  const uint32_t step = spacing_hz(spacing);
  if (direction < 0) {
    if (current <= kMinFrequencyHz + step / 2u) return kMinFrequencyHz;
    return snap_frequency(current - step, spacing);
  }
  if (current >= kMaxFrequencyHz - step / 2u) return kMaxFrequencyHz;
  return snap_frequency(current + step, spacing);
}

void Scanner::reset() {
  const Settings saved = settings_;
  *this = Scanner{};
  settings_ = saved;
}

void Scanner::set_bank(const BankEntry* entries, size_t count) {
  bank_count_ = 0;
  bank_cursor_ = 0;

  auto append_unique = [this](const BankEntry& entry) {
    if (!in_band(entry.frequency_hz) || bank_count_ >= kBankCapacity) return;
    for (size_t i = 0; i < bank_count_; ++i)
      if (bank_[i].frequency_hz == entry.frequency_hz) return;
    bank_[bank_count_++] = entry;
  };

  if (settings_.priority_guard) {
    BankEntry guard{};
    guard.frequency_hz = kGuardFrequencyHz;
    std::strncpy(guard.label, "121.500 EMERGENCY / GUARD", sizeof(guard.label) - 1);
    append_unique(guard);
  }

  if (entries != nullptr) {
    for (size_t i = 0; i < count && bank_count_ < kBankCapacity; ++i)
      append_unique(entries[i]);
  }
}

void Scanner::start(uint32_t now_ms, uint32_t current_frequency_hz) {
  (void)now_ms;
  target_frequency_hz_ = in_band(current_frequency_hz)
                             ? current_frequency_hz
                             : kGuardFrequencyHz;
  state_ = ScanState::scanning;
  skip_once_hz_ = 0;
  since_guard_ = 0;
  episode_open_ = false;
}

void Scanner::stop() {
  state_ = ScanState::off;
  skip_once_hz_ = 0;
  episode_open_ = false;
}

void Scanner::hold(uint32_t now_ms, uint32_t current_frequency_hz) {
  if (state_ == ScanState::off) return;
  target_frequency_hz_ = current_frequency_hz;
  if (episode_open_) close_episode(now_ms);
  state_ = ScanState::held;
}

void Scanner::resume(uint32_t now_ms) {
  (void)now_ms;
  if (state_ == ScanState::held) state_ = ScanState::scanning;
}

void Scanner::skip(uint32_t now_ms, uint32_t current_frequency_hz) {
  if (state_ == ScanState::off) return;
  if (episode_open_) close_episode(now_ms);
  skip_once_hz_ = current_frequency_hz;
  state_ = ScanState::scanning;
}

void Scanner::clear_activity() {
  activity_head_ = 0;
  activity_count_ = 0;
}

const Activity* Scanner::activity(size_t index) const {
  if (index >= activity_count_) return nullptr;
  const size_t slot =
      (activity_head_ + kActivityCapacity - index) % kActivityCapacity;
  return &activity_[slot];
}

uint32_t Scanner::hang_remaining_ms(uint32_t now_ms) const {
  if (state_ != ScanState::hang || reached(now_ms, hang_until_ms_)) return 0;
  return hang_until_ms_ - now_ms;
}

bool Scanner::squelch_open(float signal_dbfs) const {
  return settings_.squelch_dbfs <= -100 ||
         signal_dbfs >= static_cast<float>(settings_.squelch_dbfs);
}

const char* Scanner::label_for(uint32_t frequency_hz) const {
  if (frequency_hz == kGuardFrequencyHz) return "121.500 EMERGENCY / GUARD";
  for (size_t i = 0; i < bank_count_; ++i)
    if (bank_[i].frequency_hz == frequency_hz && bank_[i].label[0])
      return bank_[i].label;
  return "AIRBAND";
}

void Scanner::begin_episode(uint32_t now_ms, uint32_t frequency_hz,
                            float signal_dbfs) {
  episode_open_ = true;
  episode_start_ms_ = now_ms;
  last_above_ms_ = now_ms;
  episode_frequency_hz_ = frequency_hz;
  episode_peak_dbfs_ = signal_dbfs;
}

void Scanner::update_episode(uint32_t now_ms, float signal_dbfs) {
  last_above_ms_ = now_ms;
  episode_peak_dbfs_ = std::max(episode_peak_dbfs_, signal_dbfs);
}

void Scanner::push_activity(const Activity& value) {
  activity_head_ = (activity_head_ + 1u) % kActivityCapacity;
  activity_[activity_head_] = value;
  if (activity_count_ < kActivityCapacity) ++activity_count_;
}

void Scanner::close_episode(uint32_t now_ms) {
  if (!episode_open_) return;
  const uint32_t duration_ms = now_ms - episode_start_ms_;
  if (duration_ms >= kMinActivityMs) {
    Activity item{};
    item.frequency_hz = episode_frequency_hz_;
    item.start_ms = episode_start_ms_;
    item.duration_ms = duration_ms;
    item.peak_dbfs = episode_peak_dbfs_;
    std::strncpy(item.label, label_for(item.frequency_hz), sizeof(item.label) - 1);
    push_activity(item);
  }
  episode_open_ = false;
}

uint32_t Scanner::next_target(uint32_t current_frequency_hz) {
  if (settings_.priority_guard && settings_.priority_every != 0 &&
      since_guard_ >= settings_.priority_every &&
      current_frequency_hz != kGuardFrequencyHz) {
    since_guard_ = 0;
    return kGuardFrequencyHz;
  }

  uint32_t target = 0;
  if (settings_.source == ScanSource::airport_bank && bank_count_ > 0) {
    for (size_t attempt = 0; attempt < bank_count_; ++attempt) {
      bank_cursor_ = (bank_cursor_ + 1u) % bank_count_;
      target = bank_[bank_cursor_].frequency_hz;
      if (target != skip_once_hz_ || bank_count_ == 1) break;
    }
  } else {
    target = step_frequency(target_frequency_hz_ ? target_frequency_hz_
                                                 : current_frequency_hz,
                            1, settings_.spacing);
    if (target == current_frequency_hz)
      target = step_frequency(target, 1, settings_.spacing);
    if (target > kMaxFrequencyHz || target <= current_frequency_hz &&
        current_frequency_hz >= kMaxFrequencyHz - spacing_hz(settings_.spacing))
      target = kMinFrequencyHz;
  }

  if (target == skip_once_hz_) {
    skip_once_hz_ = 0;
    return next_target(target);
  }
  skip_once_hz_ = 0;
  ++since_guard_;
  ++channels_checked_;
  return in_band(target) ? target : kGuardFrequencyHz;
}

uint32_t Scanner::begin_target(uint32_t now_ms, uint32_t current_frequency_hz,
                               uint32_t target_frequency_hz) {
  target_frequency_hz_ = target_frequency_hz;
  state_ = ScanState::settling;
  settle_until_ms_ = now_ms + settings_.settle_ms;
  if (target_frequency_hz == current_frequency_hz) return 0;
  return target_frequency_hz;
}

void Scanner::note_retuned(uint32_t now_ms, uint32_t frequency_hz) {
  target_frequency_hz_ = frequency_hz;
  state_ = ScanState::settling;
  settle_until_ms_ = now_ms + settings_.settle_ms;
}

uint32_t Scanner::service(uint32_t now_ms, uint32_t current_frequency_hz,
                          float signal_dbfs) {
  if (state_ == ScanState::off || state_ == ScanState::held) return 0;

  if (state_ == ScanState::scanning) {
    return begin_target(now_ms, current_frequency_hz,
                        next_target(current_frequency_hz));
  }

  if (state_ == ScanState::settling) {
    if (!reached(now_ms, settle_until_ms_)) return 0;
    if (squelch_open(signal_dbfs)) {
      ++stops_;
      begin_episode(now_ms, current_frequency_hz, signal_dbfs);
      state_ = ScanState::receiving;
      return 0;
    }
    state_ = ScanState::scanning;
    return 0;
  }

  const float close_level =
      static_cast<float>(settings_.squelch_dbfs) - kSquelchHysteresisDb;
  if (state_ == ScanState::receiving) {
    if (settings_.squelch_dbfs <= -100 || signal_dbfs >= close_level) {
      update_episode(now_ms, signal_dbfs);
      return 0;
    }
    if (now_ms - last_above_ms_ < kReleaseMs) return 0;
    state_ = ScanState::hang;
    hang_until_ms_ = now_ms + settings_.hang_ms;
    return 0;
  }

  if (state_ == ScanState::hang) {
    if (settings_.squelch_dbfs <= -100 || signal_dbfs >= close_level) {
      update_episode(now_ms, signal_dbfs);
      state_ = ScanState::receiving;
      return 0;
    }
    if (!reached(now_ms, hang_until_ms_)) return 0;
    close_episode(now_ms);
    state_ = ScanState::scanning;
  }

  return 0;
}

bool Scanner::self_check() {
  bool ok = spacing_hz(Spacing::khz25) == 25000u &&
            spacing_hz(Spacing::khz833) == 8333u &&
            snap_frequency(121501000u, Spacing::khz25) == kGuardFrequencyHz &&
            step_frequency(kGuardFrequencyHz, 1, Spacing::khz25) == 121525000u &&
            step_frequency(kMinFrequencyHz, -1, Spacing::khz25) ==
                kMinFrequencyHz;

  Scanner scanner;
  BankEntry bank[2]{};
  bank[0].frequency_hz = 118700000u;
  std::strncpy(bank[0].label, "TEST TOWER", sizeof(bank[0].label) - 1);
  bank[1].frequency_hz = 121900000u;
  std::strncpy(bank[1].label, "TEST GROUND", sizeof(bank[1].label) - 1);
  scanner.set_bank(bank, 2);
  ok = ok && scanner.bank_count() == 3;  // guard + two entries
  scanner.settings().priority_guard = false;
  scanner.set_bank(bank, 2);
  scanner.start(0, bank[0].frequency_hz);
  const uint32_t next = scanner.service(0, bank[0].frequency_hz, -100.0f);
  ok = ok && next == bank[1].frequency_hz &&
       scanner.state() == ScanState::settling;
  scanner.note_retuned(0, next);
  (void)scanner.service(scanner.settings().settle_ms, next, -60.0f);
  ok = ok && scanner.state() == ScanState::receiving && scanner.stops() == 1;
  (void)scanner.service(scanner.settings().settle_ms + kReleaseMs + 1u, next,
                        -100.0f);
  ok = ok && scanner.state() == ScanState::hang;
  (void)scanner.service(scanner.settings().settle_ms + kReleaseMs + 1u +
                            scanner.settings().hang_ms,
                        next, -100.0f);
  ok = ok && scanner.state() == ScanState::scanning &&
       scanner.activity_count() == 1 &&
       scanner.activity(0)->frequency_hz == next;
  scanner.hold(5000, next);
  ok = ok && scanner.state() == ScanState::held;
  scanner.resume(5001);
  ok = ok && scanner.state() == ScanState::scanning;
  return ok;
}

}  // namespace orcsdr::airband
