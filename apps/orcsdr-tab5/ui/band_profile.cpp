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
    {Id::ham_10m, "10 M HAM", 28000000, 29700000, Mode::usb, Confidence::low, Control::free_tune, 0,
     4, 1, {100, 1000, 5000, 10000}, Kind::cb_ssb, Owner::home},
    {Id::ham_6m, "6 M HAM", 50000000, 54000000, Mode::usb, Confidence::low, Control::free_tune, 0,
     4, 1, {1000, 5000, 10000, 25000}, Kind::cb_ssb, Owner::home},
    // US broadcast channels sit on odd tenths: 88.1, 88.3 ... 107.9.
    {Id::fm_broadcast, "FM RADIO", 87500000, 108000000, Mode::wfm, Confidence::high,
     Control::free_tune, 88100000, 5, 2, {50000, 100000, 200000, 500000, 1000000}, Kind::wfm, Owner::fm},
    {Id::air_nav, "AIR NAV", 108000000, 118000000, Mode::am, Confidence::low, Control::free_tune, 0,
     3, 1, {25000, 50000, 100000}, Kind::airband_am, Owner::home},
    {Id::airband, "AIR BAND", 118000000, 137000000, Mode::am, Confidence::high, Control::free_tune, 0,
     3, 0, {25000, 100000, 1000000}, Kind::airband_am, Owner::airband},
    {Id::noaa_satellite, "WEATHER SAT", 137000000, 138000000, Mode::nfm, Confidence::low,
     Control::free_tune, 0, 3, 1, {5000, 12500, 25000}, Kind::nfm, Owner::satellite},
    {Id::ham_2m, "2 M HAM", 144000000, 148000000, Mode::nfm, Confidence::low, Control::free_tune, 0,
     4, 0, {5000, 10000, 12500, 25000}, Kind::nfm, Owner::home},
    {Id::marine, "MARINE", 156000000, 162025000, Mode::nfm, Confidence::high,
     Control::channelized, 0, 1, 0, {25000}, Kind::nfm, Owner::marine},
    {Id::noaa_weather, "WEATHER RADIO", 162400000, 162551000, Mode::nfm, Confidence::high,
     Control::channelized, 162400000, 1, 0, {25000}, Kind::nfm, Owner::weather},
    {Id::ham_1_25m, "1.25 M HAM", 222000000, 225000000, Mode::nfm, Confidence::low,
     Control::free_tune, 0, 4, 0, {5000, 10000, 12500, 25000}, Kind::nfm, Owner::home},
    {Id::distress_beacon, "DISTRESS BEACON", 406000000, 406100000, Mode::none, Confidence::high,
     Control::fixed, 0, 1, 0, {1000}, Kind::fixed, Owner::home},
    {Id::ham_70cm, "70 CM HAM", 420000000, 450000000, Mode::nfm, Confidence::low,
     Control::free_tune, 0, 3, 1, {5000, 12500, 25000}, Kind::nfm, Owner::home},
    {Id::frs_gmrs, "FRS / GMRS", 462550000, 467726000, Mode::nfm, Confidence::high, Control::free_tune,
     462550000, 2, 0, {12500, 25000}, Kind::nfm, Owner::home},
    {Id::pocsag, "PAGERS", 152000000, 152030000, Mode::none, Confidence::high, Control::fixed, 0,
     1, 0, {12500}, Kind::fixed, Owner::pocsag},
    {Id::uhf_general, "UHF", 300000000, 1000000000, Mode::nfm, Confidence::low, Control::free_tune, 0,
     5, 1, {5000, 12500, 25000, 100000, 1000000}, Kind::nfm, Owner::home},
    {Id::vhf_general, "VHF", 30000000, 300000000, Mode::nfm, Confidence::low, Control::free_tune, 0,
     5, 1, {5000, 12500, 25000, 100000, 1000000}, Kind::nfm, Owner::home},
    {Id::lora_ism, "LORA / ISM", 902000000, 928000001, Mode::none, Confidence::high,
     Control::channelized, 902000000, 1, 0, {125000}, Kind::fixed, Owner::lora},
    {Id::uat, "ADS-B UAT", 977900000, 978100000, Mode::none, Confidence::high, Control::fixed, 0,
     1, 0, {1000}, Kind::fixed, Owner::adsb},
    {Id::adsb, "ADS-B", 1089900000, 1090100000, Mode::none, Confidence::high, Control::fixed, 0,
     1, 0, {1000}, Kind::fixed, Owner::adsb},
    {Id::satcom_downlink, "SATCOM", 1525000000, 1559000000, Mode::none, Confidence::low,
     Control::free_tune, 0, 2, 0, {100000, 1000000}, Kind::fixed, Owner::home},
    {Id::gnss, "GPS", 1575000000, 1576000000, Mode::none, Confidence::high, Control::fixed, 0,
     1, 0, {1000}, Kind::fixed, Owner::home},
    {Id::satcom_mobile, "SATCOM", 1610600000, 1626500000, Mode::none, Confidence::low,
     Control::free_tune, 0, 2, 0, {100000, 1000000}, Kind::fixed, Owner::home},
    // US AM radio sits on a 10 kHz raster from 540 kHz (9 kHz in ITU regions 1 and 3, when those plans are added).
    {Id::am_broadcast, "AM RADIO", 530000, 1710001, Mode::am, Confidence::high, Control::free_tune, 540000,
     4, 3, {1000, 5000, 9000, 10000}, Kind::am_broadcast, Owner::am},
    {Id::ham_160m, "160 M HAM", 1800000, 2000000, Mode::lsb, Confidence::low, Control::free_tune, 0,
     4, 1, {100, 1000, 5000, 10000}, Kind::cb_ssb, Owner::home},
    {Id::ham_80m, "80 M HAM", 3500000, 4000000, Mode::lsb, Confidence::low, Control::free_tune, 0,
     4, 1, {100, 1000, 5000, 10000}, Kind::cb_ssb, Owner::home},
    {Id::ham_60m, "60 M HAM", 5330500, 5406500, Mode::usb, Confidence::low, Control::free_tune, 0,
     4, 1, {100, 1000, 5000, 10000}, Kind::cb_ssb, Owner::home},
    {Id::ham_40m, "40 M HAM", 7000000, 7300000, Mode::lsb, Confidence::low, Control::free_tune, 0,
     4, 1, {100, 1000, 5000, 10000}, Kind::cb_ssb, Owner::home},
    {Id::ham_30m, "30 M HAM", 10100000, 10150000, Mode::usb, Confidence::low, Control::free_tune, 0,
     4, 1, {100, 1000, 5000, 10000}, Kind::cb_ssb, Owner::home},
    {Id::ham_20m, "20 M HAM", 14000000, 14350000, Mode::usb, Confidence::low, Control::free_tune, 0,
     4, 1, {100, 1000, 5000, 10000}, Kind::cb_ssb, Owner::home},
    {Id::ham_17m, "17 M HAM", 18068000, 18168000, Mode::usb, Confidence::low, Control::free_tune, 0,
     4, 1, {100, 1000, 5000, 10000}, Kind::cb_ssb, Owner::home},
    {Id::ham_15m, "15 M HAM", 21000000, 21450000, Mode::usb, Confidence::low, Control::free_tune, 0,
     4, 1, {100, 1000, 5000, 10000}, Kind::cb_ssb, Owner::home},
    {Id::ham_12m, "12 M HAM", 24890000, 24990000, Mode::usb, Confidence::low, Control::free_tune, 0,
     4, 1, {100, 1000, 5000, 10000}, Kind::cb_ssb, Owner::home},
    {Id::sw_120m, "SHORTWAVE 120 M", 2300000, 2495000, Mode::am, Confidence::low, Control::free_tune, 0,
     4, 1, {1000, 5000, 9000, 10000}, Kind::am_shortwave, Owner::shortwave},
    {Id::sw_90m, "SHORTWAVE 90 M", 3200000, 3400000, Mode::am, Confidence::low, Control::free_tune, 0,
     4, 1, {1000, 5000, 9000, 10000}, Kind::am_shortwave, Owner::shortwave},
    {Id::sw_60m, "SHORTWAVE 60 M", 4750000, 5060000, Mode::am, Confidence::low, Control::free_tune, 0,
     4, 1, {1000, 5000, 9000, 10000}, Kind::am_shortwave, Owner::shortwave},
    {Id::sw_49m, "SHORTWAVE 49 M", 5900000, 6200000, Mode::am, Confidence::low, Control::free_tune, 0,
     4, 1, {1000, 5000, 9000, 10000}, Kind::am_shortwave, Owner::shortwave},
    {Id::sw_41m, "SHORTWAVE 41 M", 7300000, 7450000, Mode::am, Confidence::low, Control::free_tune, 0,
     4, 1, {1000, 5000, 9000, 10000}, Kind::am_shortwave, Owner::shortwave},
    {Id::sw_31m, "SHORTWAVE 31 M", 9400000, 9900000, Mode::am, Confidence::low, Control::free_tune, 0,
     4, 1, {1000, 5000, 9000, 10000}, Kind::am_shortwave, Owner::shortwave},
    {Id::sw_25m, "SHORTWAVE 25 M", 11600000, 12100000, Mode::am, Confidence::low, Control::free_tune, 0,
     4, 1, {1000, 5000, 9000, 10000}, Kind::am_shortwave, Owner::shortwave},
    {Id::sw_22m, "SHORTWAVE 22 M", 13570000, 13870000, Mode::am, Confidence::low, Control::free_tune, 0,
     4, 1, {1000, 5000, 9000, 10000}, Kind::am_shortwave, Owner::shortwave},
    {Id::sw_19m, "SHORTWAVE 19 M", 15100000, 15800000, Mode::am, Confidence::low, Control::free_tune, 0,
     4, 1, {1000, 5000, 9000, 10000}, Kind::am_shortwave, Owner::shortwave},
    {Id::sw_16m, "SHORTWAVE 16 M", 17480000, 17900000, Mode::am, Confidence::low, Control::free_tune, 0,
     4, 1, {1000, 5000, 9000, 10000}, Kind::am_shortwave, Owner::shortwave},
    {Id::sw_13m, "SHORTWAVE 13 M", 21450000, 21850000, Mode::am, Confidence::low, Control::free_tune, 0,
     4, 1, {1000, 5000, 9000, 10000}, Kind::am_shortwave, Owner::shortwave},
    {Id::sw_11m, "SHORTWAVE 11 M", 25670000, 26100000, Mode::am, Confidence::low, Control::free_tune, 0,
     4, 1, {1000, 5000, 9000, 10000}, Kind::am_shortwave, Owner::shortwave},
    {Id::low_band_vhf, "LOW BAND VHF", 30000000, 50000000, Mode::nfm, Confidence::low, Control::free_tune, 0,
     4, 1, {5000, 12500, 25000, 100000}, Kind::nfm, Owner::home},
    {Id::vhf_tv_lo1, "VHF TV", 54000000, 72000000, Mode::none, Confidence::low, Control::free_tune, 0,
     3, 0, {100000, 1000000, 6000000}, Kind::fixed, Owner::home},
    {Id::vhf_tv_lo2, "VHF TV", 76000000, 87500000, Mode::none, Confidence::low, Control::free_tune, 0,
     3, 0, {100000, 1000000, 6000000}, Kind::fixed, Owner::home},
    {Id::vhf_tv_hi, "VHF TV", 174000000, 216000000, Mode::none, Confidence::low, Control::free_tune, 0,
     3, 0, {100000, 1000000, 6000000}, Kind::fixed, Owner::home},
    {Id::govt_vhf_a, "GOVT VHF", 138000000, 144000000, Mode::nfm, Confidence::low, Control::free_tune, 0,
     4, 1, {5000, 12500, 25000, 100000}, Kind::nfm, Owner::home},
    {Id::govt_vhf_b, "GOVT VHF", 148000000, 150800000, Mode::nfm, Confidence::low, Control::free_tune, 0,
     4, 1, {5000, 12500, 25000, 100000}, Kind::nfm, Owner::home},
    {Id::land_mobile_vhf_a, "LAND MOBILE", 150800000, 152000000, Mode::nfm, Confidence::low, Control::free_tune, 0,
     4, 1, {5000, 12500, 25000, 100000}, Kind::nfm, Owner::home},
    {Id::land_mobile_vhf_b, "LAND MOBILE", 152030000, 156000000, Mode::nfm, Confidence::low, Control::free_tune, 0,
     4, 1, {5000, 12500, 25000, 100000}, Kind::nfm, Owner::home},
    {Id::land_mobile_vhf_c, "LAND MOBILE", 162551000, 174000000, Mode::nfm, Confidence::low, Control::free_tune, 0,
     4, 1, {5000, 12500, 25000, 100000}, Kind::nfm, Owner::home},
    {Id::mil_air, "MIL AIR", 225000000, 400000000, Mode::am, Confidence::low, Control::free_tune, 0,
     3, 0, {25000, 100000, 1000000}, Kind::airband_am, Owner::home},
    {Id::radiosonde, "RADIOSONDE", 400150000, 406000000, Mode::none, Confidence::low, Control::free_tune, 0,
     4, 1, {5000, 12500, 25000, 100000}, Kind::fixed, Owner::home},
    {Id::govt_uhf, "GOVT UHF", 406100000, 420000000, Mode::nfm, Confidence::low, Control::free_tune, 0,
     4, 1, {5000, 12500, 25000, 100000}, Kind::nfm, Owner::home},
    {Id::land_mobile_uhf_a, "LAND MOBILE", 450000000, 462550000, Mode::nfm, Confidence::low, Control::free_tune, 0,
     4, 1, {5000, 12500, 25000, 100000}, Kind::nfm, Owner::home},
    {Id::land_mobile_uhf_b, "LAND MOBILE", 467726000, 470000000, Mode::nfm, Confidence::low, Control::free_tune, 0,
     4, 1, {5000, 12500, 25000, 100000}, Kind::nfm, Owner::home},
    {Id::uhf_tv_a, "UHF TV", 470000000, 608000000, Mode::none, Confidence::low, Control::free_tune, 0,
     3, 0, {100000, 1000000, 6000000}, Kind::fixed, Owner::home},
    {Id::uhf_tv_b, "UHF TV", 614000000, 698000000, Mode::none, Confidence::low, Control::free_tune, 0,
     3, 0, {100000, 1000000, 6000000}, Kind::fixed, Owner::home},
    {Id::band_700, "700 MHZ", 698000000, 806000000, Mode::none, Confidence::low, Control::free_tune, 0,
     4, 0, {12500, 25000, 100000, 1000000}, Kind::fixed, Owner::p25},
    {Id::band_800_a, "800 MHZ", 806000000, 824000000, Mode::none, Confidence::low, Control::free_tune, 0,
     4, 0, {12500, 25000, 100000, 1000000}, Kind::fixed, Owner::p25},
    {Id::cell_850_up, "CELLULAR", 824000000, 849000000, Mode::none, Confidence::low, Control::free_tune, 0,
     2, 0, {100000, 1000000}, Kind::fixed, Owner::home},
    {Id::band_800_b, "800 MHZ", 851000000, 869000000, Mode::none, Confidence::low, Control::free_tune, 0,
     4, 0, {12500, 25000, 100000, 1000000}, Kind::fixed, Owner::p25},
    {Id::cell_850_dn, "CELLULAR", 869000000, 894000000, Mode::none, Confidence::low, Control::free_tune, 0,
     2, 0, {100000, 1000000}, Kind::fixed, Owner::home},
    {Id::band_900_a, "900 MHZ", 894000000, 902000000, Mode::none, Confidence::low, Control::free_tune, 0,
     4, 0, {12500, 25000, 100000, 1000000}, Kind::fixed, Owner::home},
    {Id::band_900_b, "900 MHZ", 928000001, 960000000, Mode::none, Confidence::low, Control::free_tune, 0,
     4, 0, {12500, 25000, 100000, 1000000}, Kind::fixed, Owner::home},
    {Id::aero_nav_a, "AERO NAV", 960000000, 977900000, Mode::none, Confidence::low, Control::free_tune, 0,
     2, 0, {100000, 1000000}, Kind::fixed, Owner::home},
    {Id::aero_nav_b, "AERO NAV", 978100000, 1089900000, Mode::none, Confidence::low, Control::free_tune, 0,
     2, 0, {100000, 1000000}, Kind::fixed, Owner::home},
    {Id::aero_nav_c, "AERO NAV", 1090100000, 1215000000, Mode::none, Confidence::low, Control::free_tune, 0,
     2, 0, {100000, 1000000}, Kind::fixed, Owner::home},
    {Id::gps_l2, "GPS", 1226600000, 1228600000, Mode::none, Confidence::low, Control::free_tune, 0,
     2, 0, {100000, 1000000}, Kind::fixed, Owner::home},
    {Id::ham_23cm, "23 CM HAM", 1240000000, 1300000000, Mode::nfm, Confidence::low, Control::free_tune, 0,
     4, 1, {5000, 12500, 25000, 100000}, Kind::nfm, Owner::home},
    {Id::hydrogen_line, "HYDROGEN LINE", 1420000000, 1421000000, Mode::none, Confidence::low, Control::free_tune, 0,
     3, 0, {1000, 10000, 100000}, Kind::fixed, Owner::home},
    {Id::glonass, "GLONASS", 1597000000, 1606000000, Mode::none, Confidence::low, Control::free_tune, 0,
     2, 0, {100000, 1000000}, Kind::fixed, Owner::home},
    {Id::weather_sat_l, "WEATHER SAT", 1670000000, 1710000000, Mode::none, Confidence::low, Control::free_tune, 0,
     2, 0, {100000, 1000000}, Kind::fixed, Owner::home},
    {Id::microwave_general, "L-BAND", 1000000000, kMax, Mode::nfm, Confidence::low,
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
