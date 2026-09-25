#include "cb_scanner.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace orcsdr::cb {
namespace {

constexpr uint64_t bit(size_t channel) { return uint64_t{1} << channel; }
constexpr uint64_t kAllChannels = (uint64_t{1} << kChannelCount) - 1u;

bool reached(uint32_t now_ms, uint32_t deadline_ms) {
  return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

}  // namespace

const char* channel_note(size_t channel) {
  switch (channel) {
    case kEmergencyChannel: return "EMERGENCY / ASSISTANCE";
    case kHighwayChannel: return "HIGHWAY / TRAVELERS";
    case 37: return "SSB CALLING (LSB)";
    case 35: case 36: case 38: case 39: return "SSB ACTIVITY";
    default: return channel < kChannelCount ? "GENERAL USE" : "";
  }
}

size_t nearest_channel(uint32_t frequency_hz) {
  size_t best = 0;
  uint32_t distance = UINT32_MAX;
  for (size_t channel = 0; channel < kChannelCount; ++channel) {
    const uint32_t hz = kChannelsHz[channel];
    const uint32_t d = hz > frequency_hz ? hz - frequency_hz : frequency_hz - hz;
    if (d < distance) {
      best = channel;
      distance = d;
    }
  }
  return best;
}

void channel_levels(const float* bins, size_t bin_count, uint32_t sample_rate_sps,
                    uint32_t center_hz, float out[kChannelCount]) {
  for (size_t channel = 0; channel < kChannelCount; ++channel) out[channel] = kNoLevel;
  if (!bins || bin_count < 16 || sample_rate_sps == 0) return;
  const double bins_per_hz = static_cast<double>(bin_count) / sample_rate_sps;
  const int64_t half_bins = std::max<int64_t>(
      1, static_cast<int64_t>(std::ceil(kChannelHalfWidthHz * bins_per_hz)));
  const int64_t usable_hz = static_cast<int64_t>(sample_rate_sps) * 45 / 100;
  for (size_t channel = 0; channel < kChannelCount; ++channel) {
    const int64_t offset_hz =
        static_cast<int64_t>(kChannelsHz[channel]) - static_cast<int64_t>(center_hz);
    if (std::llabs(offset_hz) + kChannelHalfWidthHz > usable_hz) continue;
    const int64_t center_bin = static_cast<int64_t>(bin_count / 2) +
                               std::llround(static_cast<double>(offset_hz) * bins_per_hz);
    const int64_t first = std::max<int64_t>(0, center_bin - half_bins);
    const int64_t last = std::min<int64_t>(static_cast<int64_t>(bin_count) - 1,
                                           center_bin + half_bins);
    if (first > last) continue;
    float peak = kNoLevel;
    for (int64_t bin = first; bin <= last; ++bin)
      if (std::isfinite(bins[bin])) peak = std::max(peak, bins[bin]);
    out[channel] = peak;
  }
}

float noise_floor(const float levels[kChannelCount]) {
  float valid[kChannelCount];
  size_t count = 0;
  for (size_t channel = 0; channel < kChannelCount; ++channel)
    if (levels[channel] > kNoLevel) valid[count++] = levels[channel];
  if (!count) return kNoLevel;
  std::nth_element(valid, valid + count / 2, valid + count);
  return valid[count / 2];
}

void Monitor::reset() { *this = Monitor{}; }

void Monitor::clear_log() {
  log_head_ = 0;
  log_count_ = 0;
  for (auto& stats : stats_) {
    stats.hits = 0;
    stats.active_ms = 0;
    stats.peak_snr_db = 0.0f;
    stats.seen = stats.active;
  }
}

void Monitor::push(const Hit& hit) {
  log_head_ = (log_head_ + 1) % kLogCapacity;
  log_[log_head_] = hit;
  if (log_count_ < kLogCapacity) ++log_count_;
}

const Hit* Monitor::log(size_t index) const {
  if (index >= log_count_) return nullptr;
  return &log_[(log_head_ + kLogCapacity - index) % kLogCapacity];
}

void Monitor::close_episode(size_t channel) {
  Episode& episode = episodes_[channel];
  if (!episode.open) return;
  episode.open = false;
  const uint32_t duration = episode.last_above_ms - episode.start_ms;
  if (duration < kMinHitMs) return;
  ++stats_[channel].hits;
  push({static_cast<uint8_t>(channel), episode.start_ms, duration, episode.peak_snr_db});
}

void Monitor::observe(uint32_t now_ms, const float levels[kChannelCount],
                      float floor_db, float threshold_db) {
  const uint32_t elapsed =
      have_last_ ? std::min<uint32_t>(now_ms - last_ms_, 1000u) : 0u;
  last_ms_ = now_ms;
  have_last_ = true;
  floor_db_ = floor_db;
  for (size_t channel = 0; channel < kChannelCount; ++channel) {
    ChannelStats& stats = stats_[channel];
    Episode& episode = episodes_[channel];
    const bool valid = levels[channel] > kNoLevel && floor_db > kNoLevel;
    const float snr = valid ? levels[channel] - floor_db : 0.0f;
    stats.snr_db = snr;
    const float needed = episode.open ? threshold_db - kHysteresisDb : threshold_db;
    if (stats.active) stats.active_ms += elapsed;
    if (valid && snr >= needed) {
      if (!episode.open) {
        episode.open = true;
        episode.start_ms = now_ms;
        episode.peak_snr_db = snr;
      }
      episode.last_above_ms = now_ms;
      episode.peak_snr_db = std::max(episode.peak_snr_db, snr);
      stats.active = true;
      stats.seen = true;
      stats.last_active_ms = now_ms;
      stats.peak_snr_db = std::max(stats.peak_snr_db, snr);
    } else if (episode.open && now_ms - episode.last_above_ms >= kReleaseMs) {
      close_episode(channel);
      stats.active = false;
    }
  }
}

bool Monitor::active(size_t channel) const {
  return channel < kChannelCount && stats_[channel].active;
}

size_t Monitor::active_count() const {
  size_t count = 0;
  for (const auto& stats : stats_) count += stats.active ? 1u : 0u;
  return count;
}

void Scanner::start(uint32_t now_ms, size_t tuned_channel) {
  (void)now_ms;
  channel_ = std::min(tuned_channel, kChannelCount - 1);
  skips_ = 0;
  state_ = State::watching;
}

void Scanner::stop() {
  state_ = State::off;
  skips_ = 0;
}

void Scanner::hold(size_t tuned_channel) {
  if (state_ == State::off) return;
  channel_ = std::min(tuned_channel, kChannelCount - 1);
  state_ = State::held;
}

void Scanner::release(uint32_t now_ms) {
  (void)now_ms;
  if (state_ == State::held) state_ = State::watching;
}

void Scanner::skip(uint32_t now_ms) {
  (void)now_ms;
  if (state_ == State::off) return;
  skips_ |= bit(channel_);
  state_ = State::watching;
}

void Scanner::note_manual_tune(size_t channel) {
  channel_ = std::min(channel, kChannelCount - 1);
  if (state_ != State::off) state_ = State::held;
}

uint32_t Scanner::hang_remaining_ms(uint32_t now_ms) const {
  if (state_ != State::hang || reached(now_ms, hang_until_ms_)) return 0;
  return hang_until_ms_ - now_ms;
}

void Scanner::set_lockout(size_t channel, bool locked) {
  if (channel >= kChannelCount) return;
  if (locked) lockouts_ |= bit(channel);
  else lockouts_ &= ~bit(channel);
}

bool Scanner::locked_out(size_t channel) const {
  return channel < kChannelCount && (lockouts_ & bit(channel)) != 0;
}

bool Scanner::skipped(size_t channel) const {
  return channel < kChannelCount && (skips_ & bit(channel)) != 0;
}

void Scanner::clear_lockouts() { lockouts_ = 0; }

void Scanner::set_lockout_mask(uint64_t mask) { lockouts_ = mask & kAllChannels; }

size_t Scanner::eligible_count() const {
  size_t count = 0;
  for (size_t channel = 0; channel < kChannelCount; ++channel)
    count += eligible(channel) ? 1u : 0u;
  return count;
}

bool Scanner::eligible(size_t channel) const {
  return channel < kChannelCount && !locked_out(channel) && !skipped(channel);
}

int Scanner::choose(const Monitor& monitor) const {
  const size_t priority = settings_.priority_channel;
  if (settings_.priority_enabled && eligible(priority) && monitor.active(priority))
    return static_cast<int>(priority);
  int best = -1;
  float best_snr = 0.0f;
  for (size_t channel = 0; channel < kChannelCount; ++channel) {
    if (!eligible(channel) || !monitor.active(channel)) continue;
    const float snr = monitor.stats(channel).snr_db;
    if (best < 0 || snr > best_snr) {
      best = static_cast<int>(channel);
      best_snr = snr;
    }
  }
  return best;
}

int Scanner::go(size_t channel, uint32_t now_ms) {
  channel_ = channel;
  state_ = State::settling;
  settle_until_ms_ = now_ms + settings_.settle_ms;
  rx_started_ms_ = now_ms;
  ++stops_;
  return static_cast<int>(channel);
}

int Scanner::update(uint32_t now_ms, const Monitor& monitor) {
  if (state_ == State::off || state_ == State::held) return -1;
  // A skipped channel becomes eligible again once its transmission ends.
  for (size_t channel = 0; channel < kChannelCount; ++channel)
    if ((skips_ & bit(channel)) && !monitor.active(channel)) skips_ &= ~bit(channel);

  if (state_ == State::settling) {
    if (!reached(now_ms, settle_until_ms_)) return -1;
    state_ = State::receiving;
    rx_started_ms_ = now_ms;
  }

  const size_t priority = settings_.priority_channel;
  if (state_ != State::watching && settings_.priority_enabled && priority != channel_ &&
      eligible(priority) && monitor.active(priority))
    return go(priority, now_ms);

  if (state_ == State::receiving) {
    if (!eligible(channel_)) {
      state_ = State::watching;
    } else if (!monitor.active(channel_)) {
      state_ = State::hang;
      hang_until_ms_ = now_ms + settings_.hang_ms;
      return -1;
    } else if (settings_.max_hold_s &&
               now_ms - rx_started_ms_ >= uint32_t{settings_.max_hold_s} * 1000u) {
      skips_ |= bit(channel_);
      state_ = State::watching;
    } else {
      return -1;
    }
  }

  if (state_ == State::hang) {
    if (eligible(channel_) && monitor.active(channel_)) {
      state_ = State::receiving;
      return -1;
    }
    if (!reached(now_ms, hang_until_ms_) && eligible(channel_)) return -1;
    state_ = State::watching;
  }

  const int next = choose(monitor);
  if (next < 0) return -1;
  if (static_cast<size_t>(next) == channel_) {
    state_ = State::receiving;
    rx_started_ms_ = now_ms;
    ++stops_;
    return -1;
  }
  return go(static_cast<size_t>(next), now_ms);
}

const char* state_name(State state) {
  switch (state) {
    case State::off: return "OFF";
    case State::watching: return "SCANNING";
    case State::settling: return "LOCKING";
    case State::receiving: return "RECEIVING";
    case State::hang: return "HANG";
    case State::held: return "HOLD";
  }
  return "?";
}

bool Scanner::self_check() {
  float quiet[kChannelCount];
  for (auto& level : quiet) level = -60.0f;
  float busy[kChannelCount];
  std::copy(std::begin(quiet), std::end(quiet), busy);
  busy[4] = -40.0f;
  Monitor monitor;
  Scanner scanner;
  scanner.settings().priority_enabled = false;
  scanner.start(0, kHighwayChannel);
  monitor.observe(0, busy, noise_floor(busy), scanner.settings().threshold_db);
  bool ok = scanner.update(0, monitor) == 4 && scanner.state() == State::settling;
  ok = ok && scanner.update(scanner.settings().settle_ms, monitor) == -1 &&
       scanner.state() == State::receiving;
  const uint32_t last_busy = 600;
  monitor.observe(last_busy, busy, noise_floor(busy), scanner.settings().threshold_db);
  monitor.observe(last_busy + 100, quiet, noise_floor(quiet),
                  scanner.settings().threshold_db);
  ok = ok && monitor.active(4);
  const uint32_t released = last_busy + Monitor::kReleaseMs;
  monitor.observe(released, quiet, noise_floor(quiet), scanner.settings().threshold_db);
  ok = ok && !monitor.active(4) && monitor.log_count() == 1 &&
       monitor.log(0)->duration_ms == last_busy &&
       scanner.update(released, monitor) == -1 && scanner.state() == State::hang;
  ok = ok && scanner.update(released + scanner.settings().hang_ms, monitor) == -1 &&
       scanner.state() == State::watching;
  float bins[64];
  for (auto& bin : bins) bin = -90.0f;
  bins[32] = -20.0f;
  float levels[kChannelCount];
  channel_levels(bins, 64, 2400000, kChannelsHz[kHighwayChannel], levels);
  ok = ok && levels[kHighwayChannel] == -20.0f && nearest_channel(27186000) == 18 &&
       nearest_channel(27250000) == 22;
  return ok;
}

}  // namespace orcsdr::cb
