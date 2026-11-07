// Linux transport only. OrcSDR ESP-IDF USB receiver code is not a Linux driver.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace orc::linux_sdr {
struct ReceiverInfo {
  std::uint32_t index=0;
  std::string name, manufacturer, product, serial;
};
struct ReceiverCapabilities {
  int tuner_type=0; // librtlsdr's enum value, not a guaranteed RF range
  std::vector<int> tuner_gains_tenth_db;
  bool cu8_interleaved=true;
};
class RtlReceiver {
 public:
  RtlReceiver()=default;
  ~RtlReceiver();
  RtlReceiver(const RtlReceiver&)=delete;
  RtlReceiver& operator=(const RtlReceiver&)=delete;
  static bool available();
  static std::vector<ReceiverInfo> enumerate();
  void open(std::uint32_t index);
  void configure(std::uint32_t freq_hz,std::uint32_t rate_sps);
  std::size_t read_cu8(std::uint8_t* data,std::size_t max_bytes);
  void close() noexcept;
  bool is_open() const { return device_!=nullptr; }
  std::uint32_t frequency_hz() const;
  std::uint32_t sample_rate_sps() const;
  ReceiverCapabilities capabilities() const;
 private:
  void* device_=nullptr;
};
}
