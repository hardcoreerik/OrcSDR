#include "band_profile.hpp"

namespace orcsdr::band_profile {
namespace {

using filter_standards::Kind;
using Owner = dashboards::Id;

constexpr uint32_t kMax = UINT32_MAX;

// Indexed by Id (checked in self_check). The named bands come first and must not overlap; the four general
// profiles at the end cover every other frequency. The US plan is a receive guide for identification, not
// authority to transmit.
constexpr Profile kUs[] = {
    // id, name, low, high, mode, confidence, control, origin, step_count, default_step, steps, filter, owner
    {Id::hf_general, "HF", 0, 30000000, Mode::am, Confidence::low, Control::free_tune, 0,
     4, 1, {100, 1000, 5000, 10000}, Kind::am_shortwave, Owner::shortwave},
    {Id::cb, "CB RADIO", 26965000, 27410000, Mode::am, Confidence::high, Control::channelized, 0,
     1, 0, {10000}, Kind::cb_am, Owner::cb},
    {Id::ham_10m, "10 M AMATEUR", 28000000, 29700000, Mode::usb, Confidence::low, Control::free_tune, 0,
     4, 1, {100, 1000, 5000, 10000}, Kind::cb_ssb, Owner::home},
    {Id::ham_6m, "6 M AMATEUR", 50000000, 54000000, Mode::usb, Confidence::low, Control::free_tune, 0,
     4, 1, {1000, 5000, 10000, 25000}, Kind::cb_ssb, Owner::home},
    // US broadcast channels sit on odd tenths: 88.1, 88.3 ... 107.9.
    {Id::fm_broadcast, "FM BROADCAST", 87500000, 108000000, Mode::wfm, Confidence::high,
     Control::free_tune, 88100000, 5, 2, {50000, 100000, 200000, 500000, 1000000}, Kind::wfm, Owner::fm},
    {Id::air_nav, "AIR NAV", 108000000, 118000000, Mode::am, Confidence::low, Control::free_tune, 0,
     3, 1, {25000, 50000, 100000}, Kind::airband_am, Owner::home},
    {Id::airband, "AIRBAND", 118000000, 137000000, Mode::am, Confidence::high, Control::free_tune, 0,
     3, 0, {25000, 100000, 1000000}, Kind::airband_am, Owner::airband},
    {Id::noaa_satellite, "NOAA SATELLITE", 137000000, 138000000, Mode::nfm, Confidence::low,
     Control::free_tune, 0, 3, 1, {5000, 12500, 25000}, Kind::nfm, Owner::satellite},
    {Id::ham_2m, "2 M AMATEUR", 144000000, 148000000, Mode::nfm, Confidence::low, Control::free_tune, 0,
     4, 0, {5000, 10000, 12500, 25000}, Kind::nfm, Owner::home},
    {Id::marine, "MARINE RADIO", 156000000, 162025000, Mode::nfm, Confidence::high,
     Control::channelized, 0, 1, 0, {25000}, Kind::nfm, Owner::marine},
    {Id::noaa_weather, "NOAA WEATHER", 162400000, 162551000, Mode::nfm, Confidence::high,
     Control::channelized, 162400000, 1, 0, {25000}, Kind::nfm, Owner::weather},
    {Id::ham_1_25m, "1.25 M AMATEUR", 222000000, 225000000, Mode::nfm, Confidence::low,
     Control::free_tune, 0, 4, 0, {5000, 10000, 12500, 25000}, Kind::nfm, Owner::home},
    {Id::distress_beacon, "DISTRESS BEACON", 406000000, 406100000, Mode::none, Confidence::high,
     Control::fixed, 0, 1, 0, {1000}, Kind::fixed, Owner::home},
    {Id::ham_70cm, "70 CM AMATEUR", 420000000, 450000000, Mode::nfm, Confidence::low,
     Control::free_tune, 0, 3, 1, {5000, 12500, 25000}, Kind::nfm, Owner::home},
    {Id::frs_gmrs, "FRS / GMRS", 462550000, 467726000, Mode::nfm, Confidence::high, Control::free_tune,
     462550000, 2, 0, {12500, 25000}, Kind::nfm, Owner::home},
    {Id::pocsag, "POCSAG PAGER", 152000000, 152030000, Mode::none, Confidence::high, Control::fixed, 0,
     1, 0, {12500}, Kind::fixed, Owner::pocsag},
    {Id::uhf_general, "UHF", 300000000, 1000000000, Mode::nfm, Confidence::low, Control::free_tune, 0,
     5, 1, {5000, 12500, 25000, 100000, 1000000}, Kind::nfm, Owner::home},
    {Id::vhf_general, "VHF", 30000000, 300000000, Mode::nfm, Confidence::low, Control::free_tune, 0,
     5, 1, {5000, 12500, 25000, 100000, 1000000}, Kind::nfm, Owner::home},
    {Id::lora_ism, "LORA / ISM", 902000000, 928000001, Mode::none, Confidence::high,
     Control::channelized, 902000000, 1, 0, {125000}, Kind::fixed, Owner::lora},
    {Id::uat, "ADS-B UAT", 977900000, 978100000, Mode::none, Confidence::high, Control::fixed, 0,
     1, 0, {1000}, Kind::fixed, Owner::adsb},
    {Id::adsb, "ADS-B / MODE S", 1089900000, 1090100000, Mode::none, Confidence::high, Control::fixed, 0,
     1, 0, {1000}, Kind::fixed, Owner::adsb},
    {Id::satcom_downlink, "SATCOM", 1525000000, 1559000000, Mode::none, Confidence::low,
     Control::free_tune, 0, 2, 0, {100000, 1000000}, Kind::fixed, Owner::home},
    {Id::gnss, "GNSS / GPS", 1575000000, 1576000000, Mode::none, Confidence::high, Control::fixed, 0,
     1, 0, {1000}, Kind::fixed, Owner::home},
    {Id::satcom_mobile, "SATCOM", 1610600000, 1626500000, Mode::none, Confidence::low,
     Control::free_tune, 0, 2, 0, {100000, 1000000}, Kind::fixed, Owner::home},
    {Id::microwave_general, "L-BAND +", 1000000000, kMax, Mode::nfm, Confidence::low,
     Control::free_tune, 0, 3, 1, {25000, 100000, 1000000}, Kind::nfm, Owner::home},
};

constexpr size_t kCount = sizeof(kUs) / sizeof(kUs[0]);

bool is_general(Id id) {
  return id == Id::hf_general || id == Id::vhf_general || id == Id::uhf_general ||
         id == Id::microwave_general;
}

int64_t floor_div(int64_t a, int64_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
  return q;
}

}  // namespace

const Profile& profile(Region, Id id) {
  // The table is in declaration order of Id except for the generals, so look it up rather than index.
  for (size_t i = 0; i < kCount; ++i) {
    if (kUs[i].id == id) return kUs[i];
  }
  return kUs[0];
}

const Profile& resolve(Region, uint32_t frequency_hz) {
  for (size_t i = 0; i < kCount; ++i) {
    const Profile& p = kUs[i];
    if (!is_general(p.id) && frequency_hz >= p.low_hz && frequency_hz < p.high_hz) return p;
  }
  for (size_t i = 0; i < kCount; ++i) {
    const Profile& p = kUs[i];
    if (is_general(p.id) && frequency_hz >= p.low_hz && frequency_hz < p.high_hz) return p;
  }
  return profile(Region::us, Id::microwave_general);  // 0xFFFFFFFF is the one value no range includes
}

uint32_t default_step_hz(const Profile& p) { return p.steps_hz[p.default_step]; }

uint32_t valid_step_hz(const Profile& p, uint32_t wanted_hz) {
  for (uint8_t i = 0; i < p.step_count; ++i) {
    if (p.steps_hz[i] == wanted_hz) return wanted_hz;
  }
  return default_step_hz(p);
}

uint32_t cycle_step_hz(const Profile& p, uint32_t current_hz, bool up) {
  uint8_t index = p.default_step;
  for (uint8_t i = 0; i < p.step_count; ++i) {
    if (p.steps_hz[i] == current_hz) index = i;
  }
  index = up ? static_cast<uint8_t>((index + 1) % p.step_count)
             : static_cast<uint8_t>((index + p.step_count - 1) % p.step_count);
  return p.steps_hz[index];
}

uint32_t step_frequency(const Profile& p, uint32_t frequency_hz, uint32_t step_hz, int direction,
                        uint32_t min_hz, uint32_t max_hz) {
  if (min_hz > max_hz) return frequency_hz;
  auto clamp = [&](int64_t hz) {
    if (hz < static_cast<int64_t>(min_hz)) return min_hz;
    if (hz > static_cast<int64_t>(max_hz)) return max_hz;
    return static_cast<uint32_t>(hz);
  };
  if (step_hz == 0) return clamp(frequency_hz);
  const int64_t offset = static_cast<int64_t>(frequency_hz) - static_cast<int64_t>(p.raster_origin_hz);
  int64_t n = floor_div(offset, step_hz);
  const bool on_grid = offset - n * static_cast<int64_t>(step_hz) == 0;
  if (direction > 0) ++n;
  else if (on_grid) --n;
  return clamp(static_cast<int64_t>(p.raster_origin_hz) + n * static_cast<int64_t>(step_hz));
}

bool has_step_control(const Profile& p) { return p.control == Control::free_tune; }

bool self_check() {
  bool seen[kProfileCount] = {};
  for (size_t i = 0; i < kCount; ++i) {
    const Profile& p = kUs[i];
    const size_t index = static_cast<size_t>(p.id);
    if (index >= kProfileCount || seen[index]) return false;
    seen[index] = true;
    if (p.low_hz >= p.high_hz) return false;
    if (p.step_count == 0 || p.step_count > kMaxSteps || p.default_step >= p.step_count) return false;
    for (uint8_t s = 0; s < p.step_count; ++s) {
      if (p.steps_hz[s] == 0) return false;
      if (s > 0 && p.steps_hz[s] <= p.steps_hz[s - 1]) return false;
    }
    if (p.control == Control::free_tune && p.step_count < 2) return false;
    if (p.raster_origin_hz != 0 && (p.raster_origin_hz < p.low_hz || p.raster_origin_hz >= p.high_hz)) {
      return false;
    }
    if (is_general(p.id)) continue;
    for (size_t j = i + 1; j < kCount; ++j) {
      const Profile& q = kUs[j];
      if (is_general(q.id)) continue;
      if (p.low_hz < q.high_hz && q.low_hz < p.high_hz) return false;
    }
  }
  for (size_t k = 0; k < kProfileCount; ++k) {
    if (!seen[k]) return false;
  }
  // The general profiles tile everything the named bands do not cover, with no gap and no overlap.
  uint32_t next = 0;
  const Id order[] = {Id::hf_general, Id::vhf_general, Id::uhf_general, Id::microwave_general};
  for (Id id : order) {
    const Profile& p = profile(Region::us, id);
    if (p.low_hz != next) return false;
    next = p.high_hz;
  }
  return next == kMax;
}

}  // namespace orcsdr::band_profile
