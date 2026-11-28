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
#include <string>
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

  void configure(std::uint32_t freq_hz, std::uint32_t rate_sps) {
    if (kind_ == Kind::pluto) {
      pluto_.configure(freq_hz, rate_sps);
      return;
    }
    rtl_.configure(freq_hz, rate_sps);
  }

  // RTL-only direct sampling; the Pluto has no equivalent and ignores it.
  void configure_direct_q(std::uint32_t freq_hz, std::uint32_t rate_sps) {
    if (kind_ == Kind::pluto) {
      pluto_.configure(freq_hz, rate_sps);
      return;
    }
    rtl_.configure_direct_q(freq_hz, rate_sps);
  }

  void retune(std::uint32_t freq_hz) {
    if (kind_ == Kind::pluto) {
      pluto_.retune(freq_hz);
      return;
    }
    rtl_.retune(freq_hz);
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
};

}  // namespace orc::linux_sdr
