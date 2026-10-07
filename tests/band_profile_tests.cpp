#include "band_profile.hpp"
#include "band_names.hpp"  // orcdial/src, added with -I

#include <cstdio>
#include <cstring>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      std::printf("BAND_PROFILE_TEST fail=%s line=%d\n", #expression, __LINE__); \
      return 1;                                                                \
    }                                                                          \
  } while (false)

int main() {
  using namespace orcsdr::band_profile;
  constexpr Region us = Region::us;
  constexpr uint32_t kMin = 24000000, kMax = 1766000000;

  CHECK(self_check());

  // The OrcDial shows band names from its own table; they must match the profiles, in id order.
  static_assert(orc::band_name_count == static_cast<int>(kProfileCount), "OrcDial band names out of date");
  for (size_t i = 0; i < kProfileCount; ++i)
    CHECK(std::strcmp(profile(us, static_cast<Id>(i)).name, orc::band_name(static_cast<uint8_t>(i + 1))) == 0);
  CHECK(std::strcmp(orc::band_name(0), "RADIO") == 0);   // an older Tab5 sends 0
  CHECK(std::strcmp(orc::band_name(200), "RADIO") == 0);

  // Resolution: named bands, their edges, and the general fallbacks.
  CHECK(resolve(us, 96100000).id == Id::fm_broadcast);
  CHECK(resolve(us, 87500000).id == Id::fm_broadcast);
  CHECK(resolve(us, 87499999).id == Id::vhf_general);
  CHECK(resolve(us, 107999999).id == Id::fm_broadcast);
  CHECK(resolve(us, 108000000).id == Id::air_nav);
  CHECK(resolve(us, 121500000).id == Id::airband);
  CHECK(resolve(us, 137000000).id == Id::noaa_satellite);
  CHECK(resolve(us, 146520000).id == Id::ham_2m);
  CHECK(resolve(us, 156800000).id == Id::marine);
  CHECK(resolve(us, 162400000).id == Id::noaa_weather);
  CHECK(resolve(us, 162550000).id == Id::noaa_weather);
  CHECK(resolve(us, 162300000).id == Id::vhf_general);
  CHECK(resolve(us, 152007500).id == Id::pocsag);
  CHECK(resolve(us, 444150000).id == Id::ham_70cm);
  CHECK(resolve(us, 462562500).id == Id::frs_gmrs);
  CHECK(resolve(us, 915000000).id == Id::lora_ism);
  CHECK(resolve(us, 1090000000).id == Id::adsb);
  CHECK(resolve(us, 27185000).id == Id::cb);
  CHECK(resolve(us, 14200000).id == Id::hf_general);
  CHECK(resolve(us, 700000000).id == Id::uhf_general);
  CHECK(resolve(us, 1300000000).id == Id::microwave_general);
  CHECK(resolve(us, 0).id == Id::hf_general);
  CHECK(resolve(us, UINT32_MAX).id == Id::microwave_general);

  // Every frequency a dongle can reach resolves to a profile that contains it, spot-checked across the range.
  for (uint64_t hz = 0; hz < 2000000000ull; hz += 250000) {
    const Profile& p = resolve(us, static_cast<uint32_t>(hz));
    CHECK(hz >= p.low_hz && hz < p.high_hz);
  }

  // Modes and ownership.
  CHECK(resolve(us, 96100000).mode == Mode::wfm);
  CHECK(resolve(us, 121500000).mode == Mode::am);
  CHECK(resolve(us, 121500000).owner == orcsdr::dashboards::Id::airband);
  CHECK(resolve(us, 146520000).confidence == Confidence::low);
  CHECK(resolve(us, 96100000).confidence == Confidence::high);
  CHECK(resolve(us, 1090000000).mode == Mode::none);

  // Step controls: channelized and fixed bands hide the stepper.
  CHECK(has_step_control(resolve(us, 96100000)));
  CHECK(has_step_control(resolve(us, 444150000)));
  CHECK(!has_step_control(resolve(us, 162400000)));
  CHECK(!has_step_control(resolve(us, 27185000)));
  CHECK(!has_step_control(resolve(us, 1090000000)));

  // Defaults, validation, cycling.
  const Profile& fm = resolve(us, 96100000);
  CHECK(default_step_hz(fm) == 200000);
  CHECK(valid_step_hz(fm, 100000) == 100000);
  CHECK(valid_step_hz(fm, 12500) == 200000);
  CHECK(cycle_step_hz(fm, 200000, true) == 500000);
  CHECK(cycle_step_hz(fm, 200000, false) == 100000);
  CHECK(cycle_step_hz(fm, 1000000, true) == 50000);   // wraps
  CHECK(cycle_step_hz(fm, 50000, false) == 1000000);  // wraps
  CHECK(cycle_step_hz(fm, 12500, true) == 500000);    // unknown value restarts from the default

  // The reported bug: stepping BROWSE at 444.150 MHz must use the 70 cm step, not FM's 100 kHz.
  const Profile& uhf = resolve(us, 444150000);
  CHECK(default_step_hz(uhf) == 12500);
  CHECK(step_frequency(uhf, 444150000, default_step_hz(uhf), 1, kMin, kMax) == 444162500);
  CHECK(step_frequency(uhf, 444150000, default_step_hz(uhf), -1, kMin, kMax) == 444137500);

  // FM raster: 200 kHz steps stay on 88.1, 88.3, ...; an off-grid start snaps in the direction of travel.
  CHECK(step_frequency(fm, 96100000, 200000, 1, kMin, kMax) == 96300000);
  CHECK(step_frequency(fm, 96100000, 200000, -1, kMin, kMax) == 95900000);
  CHECK(step_frequency(fm, 96200000, 200000, 1, kMin, kMax) == 96300000);
  CHECK(step_frequency(fm, 96200000, 200000, -1, kMin, kMax) == 96100000);
  CHECK(step_frequency(fm, 88100000, 200000, -1, kMin, kMax) == 87900000);
  // 100 kHz steps on the same raster visit every tenth.
  CHECK(step_frequency(fm, 96100000, 100000, 1, kMin, kMax) == 96200000);
  CHECK(step_frequency(fm, 96100000, 100000, -1, kMin, kMax) == 96000000);

  // Airband 25 kHz steps from an off-grid frequency.
  const Profile& air = resolve(us, 121500000);
  CHECK(step_frequency(air, 121510000, 25000, 1, kMin, kMax) == 121525000);
  CHECK(step_frequency(air, 121510000, 25000, -1, kMin, kMax) == 121500000);

  // Weather channels are 25 kHz apart from 162.400.
  const Profile& wx = resolve(us, 162450000);
  CHECK(step_frequency(wx, 162400000, 25000, 1, kMin, kMax) == 162425000);

  // Clamping at the dongle edges, in both directions, and a frequency below the origin.
  CHECK(step_frequency(fm, 90000000, 200000, 1, kMin, 90000000) == 90000000);
  CHECK(step_frequency(fm, 24000000, 200000, -1, kMin, kMax) == kMin);
  CHECK(step_frequency(fm, 1766000000, 200000, 1, kMin, kMax) == kMax);
  CHECK(step_frequency(fm, 50000000, 200000, 1, kMin, kMax) == 50100000);  // below the FM origin: 88.1 - n x 200 kHz
  CHECK(step_frequency(fm, 96100000, 0, 1, kMin, kMax) == 96100000);
  CHECK(step_frequency(fm, 96100000, 200000, 1, 100, 50) == 96100000);  // empty range leaves it alone

  std::printf("BAND_PROFILE_TEST pass\n");
  return 0;
}
