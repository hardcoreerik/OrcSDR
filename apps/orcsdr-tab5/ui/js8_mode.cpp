#include "js8_mode.hpp"

namespace orcsdr::js8 {
namespace {

constexpr std::array<uint8_t, 7> kNormalSync{{4, 2, 5, 6, 1, 3, 0}};

constexpr Profile make_normal() {
  Profile p{};
  p.submode = Submode::normal;
  p.name = "Normal";
  p.slot_ms = 15000;
  p.tx_duration_ms = 12640;
  p.symbol_samples = 1920;
  p.tone_spacing_millihz = 6250;
  p.sync = {{SyncBlock{0, kNormalSync}, SyncBlock{36, kNormalSync}, SyncBlock{72, kNormalSync}}};
  p.sync_pattern_verified = true;
  p.readiness = Readiness::physical_layer_verified;
  return p;
}

constexpr Profile make_fast() {
  Profile p = make_normal();
  p.submode = Submode::fast;
  p.name = "Fast";
  p.slot_ms = 10000;
  p.tx_duration_ms = 7900;
  p.symbol_samples = 1200;
  p.tone_spacing_millihz = 10000;
  p.sync = {};
  p.sync_pattern_verified = false;
  p.readiness = Readiness::sync_pattern_pending;
  return p;
}

constexpr Profile make_turbo() {
  Profile p = make_normal();
  p.submode = Submode::turbo;
  p.name = "JS8 40";
  p.slot_ms = 6000;
  p.tx_duration_ms = 3950;
  p.symbol_samples = 600;
  p.tone_spacing_millihz = 20000;
  p.sync = {};
  p.sync_pattern_verified = false;
  p.readiness = Readiness::sync_pattern_pending;
  return p;
}

constexpr Profile make_slow() {
  Profile p = make_normal();
  p.submode = Submode::slow;
  p.name = "Slow";
  p.slot_ms = 30000;
  p.tx_duration_ms = 25280;
  p.symbol_samples = 3840;
  p.tone_spacing_millihz = 3125;
  p.sync = {};
  p.sync_pattern_verified = false;
  p.readiness = Readiness::sync_pattern_pending;
  return p;
}

constexpr Profile make_ultra() {
  Profile p = make_normal();
  p.submode = Submode::ultra_experimental;
  p.name = "JS8 60";
  p.slot_ms = 4000;
  p.tx_duration_ms = 2528;
  p.symbol_samples = 384;
  p.tone_spacing_millihz = 31250;
  p.sync = {};
  p.sync_pattern_verified = false;
  p.readiness = Readiness::experimental;
  return p;
}

constexpr std::array<Profile, static_cast<size_t>(Submode::count)> kProfiles{{
    make_normal(), make_fast(), make_turbo(), make_slow(), make_ultra()}};

}  // namespace

const Profile& profile(Submode submode) {
  const size_t i = static_cast<size_t>(submode);
  return i < kProfiles.size() ? kProfiles[i] : kProfiles[0];
}

bool valid_submode(uint8_t value) {
  return value < static_cast<uint8_t>(Submode::count);
}

bool physical_layer_ready(Submode submode) {
  const Profile& p = profile(submode);
  return p.sync_pattern_verified && p.readiness == Readiness::physical_layer_verified;
}

bool self_check() {
  const Profile& n = profile(Submode::normal);
  const Profile& f = profile(Submode::fast);
  const Profile& t = profile(Submode::turbo);
  const Profile& s = profile(Submode::slow);
  const Profile& u = profile(Submode::ultra_experimental);
  return n.sample_rate_hz == 12000 && n.channel_symbols == 79 && n.data_symbols == 58 &&
         n.symbol_samples == 1920 && n.slot_ms == 15000 && n.tx_duration_ms == 12640 &&
         n.tone_spacing_millihz == 6250 && n.sync[0].first_symbol == 0 &&
         n.sync[1].first_symbol == 36 && n.sync[2].first_symbol == 72 &&
         n.sync[0].tones == kNormalSync && physical_layer_ready(Submode::normal) &&
         f.symbol_samples == 1200 && f.slot_ms == 10000 && f.tone_spacing_millihz == 10000 &&
         t.symbol_samples == 600 && t.slot_ms == 6000 && t.tone_spacing_millihz == 20000 &&
         s.symbol_samples == 3840 && s.slot_ms == 30000 && s.tone_spacing_millihz == 3125 &&
         u.symbol_samples == 384 && u.slot_ms == 4000 && u.readiness == Readiness::experimental &&
         !physical_layer_ready(Submode::fast) && !physical_layer_ready(Submode::turbo) &&
         !physical_layer_ready(Submode::slow) && !physical_layer_ready(Submode::ultra_experimental);
}

}  // namespace orcsdr::js8
