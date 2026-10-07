#pragma once

#include <cstddef>
#include <cstdint>

#include "dashboard_registry.hpp"
#include "filter_standards.hpp"

// What Home should assume about a frequency: the band it is in, the usual mode, the tuning steps that make
// sense there, the channel raster, and the dashboard that owns the band. Pure data, no display or RTOS,
// host-tested (same shape as filter_standards). The values are suggestions for the person at the controls,
// never limits: they can always tune anywhere the dongle reaches.
//
// Region: only the US plan has data today. `Region` exists now so callers pass it from the start; the other
// regions will add their own tables and fall back to the US one until then (see docs/SMART_VFO_HOME.md).
namespace orcsdr::band_profile {

enum class Region : uint8_t { us };

enum class Mode : uint8_t {
  none,  // decoded by its own dashboard (pager, P25, ADS-B, LoRa, GNSS...)
  wfm,
  nfm,
  am,
  usb,
  lsb,
};

enum class Control : uint8_t {
  free_tune,     // any frequency; the step list applies
  channelized,   // fixed channels on the raster; Home hides the step control
  fixed,         // a single frequency or a decoder-owned range; Home hides the step control
};

// How sure the table is. Auto mode may switch on `high` only; `low` bands are ambiguous (for example 2 m
// amateur is FM or SSB) and are offered as a suggestion.
enum class Confidence : uint8_t { low, high };

constexpr size_t kMaxSteps = 6;

enum class Id : uint8_t {
  hf_general,
  cb,
  ham_10m,
  ham_6m,
  fm_broadcast,
  air_nav,
  airband,
  noaa_satellite,
  ham_2m,
  marine,
  noaa_weather,
  ham_1_25m,
  distress_beacon,
  ham_70cm,
  frs_gmrs,
  pocsag,
  uhf_general,
  vhf_general,
  lora_ism,
  uat,
  adsb,
  satcom_downlink,
  gnss,
  satcom_mobile,
  microwave_general,
  am_broadcast,
  ham_160m,
  ham_80m,
  ham_60m,
  ham_40m,
  ham_30m,
  ham_20m,
  ham_17m,
  ham_15m,
  ham_12m,
  sw_120m,
  sw_90m,
  sw_60m,
  sw_49m,
  sw_41m,
  sw_31m,
  sw_25m,
  sw_22m,
  sw_19m,
  sw_16m,
  sw_13m,
  sw_11m,
  low_band_vhf,
  vhf_tv_lo1,
  vhf_tv_lo2,
  vhf_tv_hi,
  govt_vhf_a,
  govt_vhf_b,
  land_mobile_vhf_a,
  land_mobile_vhf_b,
  land_mobile_vhf_c,
  mil_air,
  radiosonde,
  govt_uhf,
  land_mobile_uhf_a,
  land_mobile_uhf_b,
  uhf_tv_a,
  uhf_tv_b,
  band_700,
  band_800_a,
  cell_850_up,
  band_800_b,
  cell_850_dn,
  band_900_a,
  band_900_b,
  aero_nav_a,
  aero_nav_b,
  aero_nav_c,
  gps_l2,
  ham_23cm,
  hydrogen_line,
  glonass,
  weather_sat_l,
  count,
};

struct Profile {
  Id id;
  const char* name;          // "FM BROADCAST"
  uint32_t low_hz;           // inclusive
  uint32_t high_hz;          // exclusive
  Mode mode;                 // the usual mode here
  Confidence confidence;
  Control control;
  uint32_t raster_origin_hz; // steps are anchored here, so FM 200 kHz lands on 88.1, 88.3, ...
  uint8_t step_count;
  uint8_t default_step;      // index into steps_hz
  uint32_t steps_hz[kMaxSteps];  // ascending
  filter_standards::Kind filter;
  dashboards::Id owner;      // dashboard to offer, or home when none
};

constexpr size_t kProfileCount = static_cast<size_t>(Id::count);

// The profile that contains `frequency_hz`. Always returns something: a frequency outside every named band
// falls into one of the general profiles (HF, VHF, UHF, microwave).
const Profile& resolve(Region region, uint32_t frequency_hz);
const Profile& profile(Region region, Id id);

uint32_t default_step_hz(const Profile& p);

// `wanted_hz` if it is one of the profile's steps, else the profile's default. Keeps a remembered step valid
// if the table changes between firmware versions.
uint32_t valid_step_hz(const Profile& p, uint32_t wanted_hz);

// Next or previous entry of the step list, wrapping.
uint32_t cycle_step_hz(const Profile& p, uint32_t current_hz, bool up);

// The next frequency one `step_hz` away from `frequency_hz` in `direction` (-1 or +1) on the profile's
// raster, clamped to [min_hz, max_hz]. A frequency that is off the raster snaps to the nearest grid point in
// the direction of travel (88.2 MHz with a 200 kHz step goes to 88.3 up and 88.1 down).
uint32_t step_frequency(const Profile& p, uint32_t frequency_hz, uint32_t step_hz, int direction,
                        uint32_t min_hz, uint32_t max_hz);

// True when Home should show a step control for this band.
bool has_step_control(const Profile& p);

bool self_check();

}  // namespace orcsdr::band_profile
