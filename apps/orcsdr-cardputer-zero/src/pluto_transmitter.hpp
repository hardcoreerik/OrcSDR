#pragma once
// PlutoSDR (AD9363) transmitter for the CardPuter Zero, over libiio.
//
// Safety property, deliberate and load-bearing: the transmitter comes up at
// maximum attenuation (-89.75 dB) and stays there until something explicitly
// raises it. Selecting the Pluto as a radio source must never key a signal into
// whatever happens to be connected.
#include <cstddef>
#include <cstdint>
#include <string>

namespace orc::linux_sdr {

class PlutoTransmitter {
 public:
  PlutoTransmitter() = default;
  ~PlutoTransmitter();
  PlutoTransmitter(const PlutoTransmitter&) = delete;
  PlutoTransmitter& operator=(const PlutoTransmitter&) = delete;

  void open(const std::string& uri = "");
  // Sets TX LO and the TX sample rate; attenuation is forced back to the safe
  // value so a reconfigure cannot leave a previous session's level armed.
  void configure(std::uint32_t freq_hz, std::uint32_t rate_sps);
  void set_attenuation_db(double db);  // clamped to [-89.75, 0]
  double attenuation_db() const { return attenuation_db_; }
  bool armed() const { return attenuation_db_ > kSafeAttenuationDb + 0.01; }

  // CU8 (offset 128) is scaled to the AD9363's 12-bit range, as on receive.
  std::size_t write_cu8(const std::uint8_t* data, std::size_t bytes);
  void close() noexcept;
  bool is_open() const { return ctx_ != nullptr; }
  std::uint32_t frequency_hz() const { return freq_hz_; }
  std::uint32_t sample_rate_sps() const { return rate_sps_; }
  std::uint64_t samples_written() const { return samples_written_; }

  static constexpr double kSafeAttenuationDb = -89.75;

 private:
  void* ctx_ = nullptr;
  void* phy_ = nullptr;
  void* tx_ = nullptr;
  void* buffer_ = nullptr;
  std::uint32_t freq_hz_ = 0;
  std::uint32_t rate_sps_ = 0;
  double attenuation_db_ = kSafeAttenuationDb;
  std::uint64_t samples_written_ = 0;
  static constexpr std::size_t kBufferSamples = 16384;
  std::uint8_t* staged_ = nullptr;
};

}  // namespace orc::linux_sdr
