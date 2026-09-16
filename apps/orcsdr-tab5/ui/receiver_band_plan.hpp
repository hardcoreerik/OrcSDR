#pragma once

#include <cstdint>

namespace orcsdr::receiver_bands {

struct Profile {
  uint32_t min_hz;
  uint32_t max_hz;
  uint32_t default_hz;
  uint32_t default_step_hz;
  uint32_t default_bandwidth_hz;
};

constexpr Profile kAmBroadcast{520000, 1710000, 1000000, 10000, 10000};
constexpr Profile kShortwave{1710000, 30000000, 7100000, 1000, 6000};

constexpr bool valid(const Profile& profile) {
  return profile.min_hz <= profile.default_hz && profile.default_hz <= profile.max_hz &&
         profile.default_step_hz > 0 && profile.default_bandwidth_hz > 0;
}

static_assert(valid(kAmBroadcast));
static_assert(valid(kShortwave));
static_assert(kAmBroadcast.max_hz <= kShortwave.min_hz);

}  // namespace orcsdr::receiver_bands
