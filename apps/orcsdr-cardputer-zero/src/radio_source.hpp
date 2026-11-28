#pragma once
// One radio interface over two very different devices.
//
// The RTL-SDR and the Pluto both deliver CU8 IQ, but they differ in ways callers
// must not have to know: the Pluto cannot sample below ~2.083 MS/s and reaches a
// requested rate by decimating internally, and its gain is a dB figure rather than
// the RTL's tenth-dB steps. This wrapper presents the RTL's surface and hides the
// difference, so the demods and dashboards keep one code path.
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "rtl_receiver.hpp"
#include "pluto_receiver.hpp"

namespace orc::linux_sdr {

class RadioSource {
 public:
  enum class Kind { rtl, pluto };

  // Cheap probe used by the UI to offer the choice only when it is real.
  static bool pluto_present() {
    try {
      return PlutoReceiver::detect().found;
    } catch (...) {
      return false;
    }
  }

  void open(std::uint32_t device_index, Kind kind) {
    kind_ = kind;
    if (kind_ == Kind::pluto) {
      pluto_.open();
      device_index_ = 0;
      return;
    }
    rtl_.open(device_index);
    device_index_ = device_index;
  }


  // Frequency limits for the selected radio. The Pluto's figure comes from
  // measurement, not the datasheet: the driver refuses below 70 MHz, and uint32
  // caps the top at 4.29 GHz regardless of what the advertising says. The RTL is
  // the usual 24 MHz - 1.766 GHz.
  static std::pair<std::uint32_t, std::uint32_t> frequency_range(Kind kind) {
    if (kind == Kind::pluto) return {70000000u, 4294000000u};
    return {24000000u, 1766000000u};
  }

  std::pair<std::uint32_t, std::uint32_t> frequency_range() const {
    return frequency_range(kind_);
  }

  // True when the last requested frequency had to be moved to fit the radio.
  bool clamped() const { return clamped_; }
  std::uint32_t requested_hz() const { return requested_hz_; }
  void clear_clamped() { clamped_ = false; }
  static const char* radio_name(Kind kind) { return kind == Kind::pluto ? "Pluto" : "RTL-SDR"; }
  const char* radio_name() const { return radio_name(kind_); }


  // One line per distinct out-of-range request. The screens repaint continuously,
  // so logging at each tune turned a single misconfiguration into a log flood.
  void note_clamp(std::uint32_t requested, std::uint32_t applied) {
    clamped_ = applied != requested;
    if (!clamped_ || requested == last_clamped_log_) return;
    last_clamped_log_ = requested;
    const auto range = frequency_range();
    std::clog << radio_name() << " cannot tune " << requested << " Hz; range is "
              << range.first << " - " << range.second << " Hz, using " << applied << std::endl;
  }
  std::uint32_t clamp_hz(std::uint32_t hz) {
    const auto range = frequency_range();
    if (hz < range.first) return range.first;
    if (hz > range.second) return range.second;
    return hz;
  }
  void configure(std::uint32_t freq_hz, std::uint32_t rate_sps) {
    requested_hz_ = freq_hz;
    const std::uint32_t applied = clamp_hz(freq_hz);
    note_clamp(freq_hz, applied);
    if (kind_ == Kind::pluto) {
      pluto_.configure(applied, rate_sps);
      return;
    }
    rtl_.configure(applied, rate_sps);
  }

  // RTL-only direct sampling; the Pluto has no equivalent and ignores it.
  void configure_direct_q(std::uint32_t freq_hz, std::uint32_t rate_sps) {
    // Must clamp like configure(): the HF path reaches here, and the Pluto has no
    // HF coverage at all, so an unclamped request silently fails every repaint.
    requested_hz_ = freq_hz;
    const std::uint32_t applied = clamp_hz(freq_hz);
    note_clamp(freq_hz, applied);
    if (kind_ == Kind::pluto) {
      pluto_.configure(applied, rate_sps);
      return;
    }
    rtl_.configure_direct_q(applied, rate_sps);
  }

  void retune(std::uint32_t freq_hz) {
    requested_hz_ = freq_hz;
    const std::uint32_t applied = clamp_hz(freq_hz);
    note_clamp(freq_hz, applied);
    if (kind_ == Kind::pluto) {
      pluto_.retune(applied);
      return;
    }
    rtl_.retune(applied);
  }

  void set_tuner_gain_auto() {
    if (kind_ == Kind::pluto) {
      pluto_.set_tuner_gain_auto();
      return;
    }
    rtl_.set_tuner_gain_auto();
  }

  void set_tuner_gain_tenth_db(int tenth_db) {
    if (kind_ == Kind::pluto) {
      // The Pluto moves in hardware gain steps of ~0.25 dB, so a tenth-dB request
      // is passed through and read back rather than pretending it was honoured.
      pluto_.set_tuner_gain_tenth_db(tenth_db);
      return;
    }
    rtl_.set_tuner_gain_tenth_db(tenth_db);
  }

  int tuner_gain_tenth_db() const {
    return kind_ == Kind::pluto ? pluto_.tuner_gain_tenth_db() : rtl_.tuner_gain_tenth_db();
  }


  // Capabilities in the shape FmSession expects. The Pluto's gain is continuous,
  // so it reports a coarse ladder spanning its real range rather than pretending to
  // have the RTL's discrete tuner steps.
  ReceiverCapabilities capabilities() const {
    if (kind_ == Kind::pluto) {
      ReceiverCapabilities caps;
      caps.tuner_type = 0;
      for (int db = 0; db <= 73; db += 10) caps.tuner_gains_tenth_db.push_back(db * 10);
      caps.cu8_interleaved = true;
      return caps;
    }
    return rtl_.capabilities();
  }

  std::string receiver_name() const {
    return kind_ == Kind::pluto ? std::string("PlutoSDR AD9363") : std::string("RTL-SDR tuner");
  }
  std::size_t read_cu8(std::uint8_t* data, std::size_t max_bytes) {
    return kind_ == Kind::pluto ? pluto_.read_cu8(data, max_bytes)
                                : rtl_.read_cu8(data, max_bytes);
  }

  std::uint32_t frequency_hz() const {
    return kind_ == Kind::pluto ? pluto_.frequency_hz() : rtl_.frequency_hz();
  }

  // For the Pluto this is the rate the caller asked for, after its internal
  // decimation; the device itself runs faster.
  std::uint32_t sample_rate_sps() const {
    return kind_ == Kind::pluto ? pluto_.sample_rate_sps() : rtl_.sample_rate_sps();
  }

  void close() noexcept {
    pluto_.close();
    rtl_.close();
  }

  Kind kind() const { return kind_; }
  bool is_pluto() const { return kind_ == Kind::pluto; }
  bool is_open() const { return kind_ == Kind::pluto ? pluto_.is_open() : rtl_.is_open(); }
  std::uint32_t device_index() const { return device_index_; }

 private:
  RtlReceiver rtl_;
  PlutoReceiver pluto_;
  Kind kind_ = Kind::rtl;
  std::uint32_t device_index_ = 0;
  std::uint32_t requested_hz_ = 0;
  bool clamped_ = false;
  std::uint32_t last_clamped_log_ = 0;
};

}  // namespace orc::linux_sdr
