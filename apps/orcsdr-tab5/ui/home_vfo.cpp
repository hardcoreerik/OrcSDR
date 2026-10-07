#include "home_vfo.hpp"

namespace orcsdr::home_vfo {
namespace {

constexpr band_profile::Region kRegion = band_profile::Region::us;

size_t index_of(const band_profile::Profile& profile) { return static_cast<size_t>(profile.id); }

}  // namespace

const char* demod_name(Demod demod) {
  switch (demod) {
    case Demod::nfm: return "NFM";
    case Demod::am: return "AM";
    case Demod::wfm: return "WFM";
    case Demod::usb: return "USB";
    case Demod::lsb: return "LSB";
    default: return "AUTO";
  }
}

Demod demod_for_mode(band_profile::Mode mode) {
  using band_profile::Mode;
  switch (mode) {
    case Mode::wfm: return Demod::wfm;
    case Mode::nfm: return Demod::nfm;
    case Mode::am: return Demod::am;
    case Mode::usb: return Demod::usb;
    case Mode::lsb: return Demod::lsb;
    default: return Demod::by_band;
  }
}

filter_standards::Kind filter_kind(Demod demod, uint32_t frequency_hz) {
  using Kind = filter_standards::Kind;
  switch (demod) {
    case Demod::wfm: return Kind::wfm;
    case Demod::am: return frequency_hz >= 118000000 && frequency_hz <= 137000000 ? Kind::airband_am : Kind::am_broadcast;
    case Demod::usb:
    case Demod::lsb: return Kind::cb_ssb;
    default: return Kind::nfm;
  }
}

uint32_t Memory::step_hz(const band_profile::Profile& profile) const {
  return band_profile::valid_step_hz(profile, steps_[index_of(profile)]);
}

bool Memory::remember_step(const band_profile::Profile& profile, uint32_t step_hz) {
  uint32_t& slot = steps_[index_of(profile)];
  if (slot == step_hz) return false;
  slot = step_hz;
  return true;
}

void Memory::load_steps(const uint32_t* stored, size_t count) {
  for (size_t i = 0; i < kBands; ++i) {
    steps_[i] = 0;
    if (stored == nullptr || i >= count || stored[i] == 0) continue;
    const auto& profile = band_profile::profile(kRegion, static_cast<band_profile::Id>(i));
    // Keep only a value that is on the band's list; anything else (an older table) falls back to the default.
    if (band_profile::valid_step_hz(profile, stored[i]) == stored[i]) steps_[i] = stored[i];
  }
}

uint8_t Memory::pin(const band_profile::Profile& profile) const { return pins_[index_of(profile)]; }

bool Memory::set_pin(const band_profile::Profile& profile, Demod choice) {
  uint8_t& slot = pins_[index_of(profile)];
  if (slot == static_cast<uint8_t>(choice)) return false;
  slot = static_cast<uint8_t>(choice);
  return true;
}

void Memory::load_pins(const uint8_t* stored, size_t count) {
  for (size_t i = 0; i < kBands; ++i)
    pins_[i] = stored != nullptr && i < count && stored[i] <= static_cast<uint8_t>(Demod::lsb) ? stored[i] : 0;
}

bool Memory::enter_band(const band_profile::Profile& profile, Demod* mode) {
  if (profile.id == last_) return false;
  last_ = profile.id;
  const uint8_t pinned = pin(profile);
  if (mode != nullptr) *mode = pinned ? static_cast<Demod>(pinned) : demod_for_mode(profile.mode);
  return true;
}

bool self_check() {
  Memory memory;
  const auto& fm = band_profile::resolve(kRegion, 96100000);
  Demod mode = Demod::by_band;
  return memory.step_hz(fm) == band_profile::default_step_hz(fm) && memory.enter_band(fm, &mode) &&
         mode == Demod::wfm && !memory.enter_band(fm, &mode) && static_cast<uint8_t>(Demod::lsb) == 5 &&
         filter_kind(Demod::wfm, 96100000) == filter_standards::Kind::wfm;
}

}  // namespace orcsdr::home_vfo
