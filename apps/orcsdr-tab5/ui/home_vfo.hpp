#pragma once

#include <cstddef>
#include <cstdint>

#include "band_profile.hpp"
#include "filter_standards.hpp"

// The state behind Home's VFO: which demodulation Home has chosen, the step remembered for each band, and the mode
// pinned for each band. Pure logic with no hardware, display or RTOS, host-tested; main.cpp only stores the two
// small tables in NVS and applies the result to the receiver.
namespace orcsdr::home_vfo {

// The demodulation Home chooses. The numbering is the OrcDial mode code (1 NFM, 2 AM, 3 WFM, 4 USB, 5 LSB);
// by_band leaves it to the band's own demodulator (what the dashboards do).
enum class Demod : uint8_t { by_band, nfm, am, wfm, usb, lsb };

const char* demod_name(Demod demod);            // "AUTO", "NFM", "AM", "WFM", "USB", "LSB"
Demod demod_for_mode(band_profile::Mode mode);  // Mode::none maps to by_band

// The filter family whose widths suit `demod` at `frequency_hz` (AM is the airband width inside 118 to 137 MHz and
// the broadcast width elsewhere). Never `fixed`.
filter_standards::Kind filter_kind(Demod demod, uint32_t frequency_hz);

class Memory {
 public:
  static constexpr size_t kBands = band_profile::kProfileCount;
  static constexpr size_t kStepsBytes = kBands * sizeof(uint32_t);
  static constexpr size_t kPinsBytes = kBands;

  // The remembered step for the band, or its default when none is remembered or the remembered one is not on the list.
  uint32_t step_hz(const band_profile::Profile& profile) const;
  // True when the remembered step changed (the caller stores steps_data()).
  bool remember_step(const band_profile::Profile& profile, uint32_t step_hz);
  // Restores stored steps; any value that is not on its band's list is dropped.
  void load_steps(const uint32_t* stored, size_t count);
  const uint32_t* steps_data() const { return steps_; }

  // The mode pinned for the band (0 = none, otherwise a Demod value).
  uint8_t pin(const band_profile::Profile& profile) const;
  // True when the pin changed (the caller stores pins_data()). `by_band` clears the pin.
  bool set_pin(const band_profile::Profile& profile, Demod choice);
  void load_pins(const uint8_t* stored, size_t count);   // values above LSB are dropped
  const uint8_t* pins_data() const { return pins_; }

  // Auto mode. Entering a different band applies its pinned mode, else its usual one, and returns true with that mode
  // (by_band when the band has none); staying in the same band returns false, so tuning inside a band never changes
  // the mode and a manual choice sticks.
  bool enter_band(const band_profile::Profile& profile, Demod* mode);
  // The band the mode was just chosen for (a manual choice), so the next tune inside it does not re-apply Auto.
  void note_band(const band_profile::Profile& profile) { last_ = profile.id; }
  // Home came back on screen, or a tune did not happen: the next tune re-derives the mode.
  void forget_band() { last_ = band_profile::Id::count; }

 private:
  uint32_t steps_[kBands] = {};
  uint8_t pins_[kBands] = {};
  band_profile::Id last_ = band_profile::Id::count;
};

bool self_check();

}  // namespace orcsdr::home_vfo
