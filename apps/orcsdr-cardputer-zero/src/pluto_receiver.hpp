#pragma once
// PlutoSDR (AD9363) receiver for the CardPuter Zero, over libiio.
//
// Deliberately mirrors orc::linux_sdr::RtlReceiver so the existing demods and
// dashboards can use either radio without knowing which one they have. The one
// behavioural difference that matters: the Pluto's AD9363 cannot sample below
// ~2.083 MS/s, so a requested rate below that is reached by sampling faster and
// decimating by an integer factor. The delivered rate is always read back from
// the device rather than assumed.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace orc::linux_sdr {

struct PlutoInfo {
  bool found = false;
  std::string uri;      // e.g. "usb:1.53.4"
  std::string name;     // model string from ad9361-phy
  std::string serial;
};

class PlutoReceiver {
 public:
  PlutoReceiver() = default;
  ~PlutoReceiver();
  PlutoReceiver(const PlutoReceiver&) = delete;
  PlutoReceiver& operator=(const PlutoReceiver&) = delete;

  // Tries the given uri, then "usb:", then the ip: forms the Pluto exposes.
  static PlutoInfo detect();

  // Opens the context and finds ad9361-phy / cf-ad9361-lpc. Throws on failure,
  // like RtlReceiver does, so callers keep one error path.
  void open(const std::string& uri = "");
  void configure(std::uint32_t freq_hz, std::uint32_t rate_sps);
  void retune(std::uint32_t freq_hz);
  void set_tuner_gain_auto();
  void set_tuner_gain_tenth_db(int tenth_db);
  int tuner_gain_tenth_db() const;
  std::size_t read_cu8(std::uint8_t* data, std::size_t max_bytes);
  void close() noexcept;
  bool is_open() const { return ctx_ != nullptr; }
  std::uint32_t frequency_hz() const { return freq_hz_; }
  std::uint32_t sample_rate_sps() const { return delivered_rate_; }
  std::uint32_t device_rate_sps() const { return device_rate_; }
  int decimation() const { return decim_; }

 private:
  void set_attr(const char* device, const char* channel, bool output, const char* attr,
                long long value);
  long long get_attr(const char* device, const char* channel, bool output, const char* attr,
                     long long fallback) const;

  void* ctx_ = nullptr;      // iio_context*
  void* phy_ = nullptr;      // iio_device* ad9361-phy
  void* rx_ = nullptr;       // iio_device* cf-ad9361-lpc
  void* buffer_ = nullptr;   // iio_buffer*
  std::uint32_t freq_hz_ = 0;
  std::uint32_t requested_rate_ = 0;
  std::uint32_t device_rate_ = 0;
  std::uint32_t delivered_rate_ = 0;
  int decim_ = 1;
  int gain_tenth_db_ = 0;
  bool gain_auto_ = true;
  // Leftover bytes of a partially consumed iio buffer, so a small read_cu8 does
  // not discard the rest of a refill.
  std::vector<std::uint8_t> pending_;
  std::size_t pending_offset_ = 0;
  static constexpr std::uint32_t kMinDeviceRate = 2083333;
  static constexpr std::size_t kBufferSamples = 32768;
};

}  // namespace orc::linux_sdr
