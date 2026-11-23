#pragma once
// M5 Unit ENV III for Dead Air: SHT30 (temperature, humidity) at 0x44 and QMP6988 (pressure) at 0x70 on Port A.
// Pure logic over an abstract bus, host-tested. Nothing here is simulated: a channel has a value only after a checked
// reading (SHT30: CRC-8; QMP6988: chip id and a plausible result), and a channel that stops answering is dropped.
// The QMP6988 compensation follows the manufacturer's integer reference (as published in M5Stack's M5Unit-ENV).
#include <cstddef>
#include <cstdint>

namespace orcsdr::dead_air::env {

constexpr uint8_t kSht30Address = 0x44;
constexpr uint8_t kQmp6988Address = 0x70;

uint8_t sht30_crc8(const uint8_t* data, size_t length);
// raw = temperature msb, lsb, crc, humidity msb, lsb, crc. False on a CRC error.
bool sht30_decode(const uint8_t raw[6], float* celsius, float* humidity_rh);

struct QmpCalibration {
  int64_t a0 = 0, b00 = 0, a1 = 0, a2 = 0;
  int64_t bt1 = 0, bt2 = 0, bp1 = 0, b11 = 0, bp2 = 0, b12 = 0, b21 = 0, bp3 = 0;
};
constexpr size_t kQmpCalibrationBytes = 25;  // registers 0xA0..0xB8
void qmp6988_parse_calibration(const uint8_t data[kQmpCalibrationBytes], QmpCalibration* out);
// raw = pressure (3 bytes) then temperature (3 bytes) from register 0xF7. False if the result is implausible
// (temperature outside -40..85 C or pressure outside 300..1100 hPa), which also covers "no conversion done yet".
bool qmp6988_compensate(const QmpCalibration& cal, const uint8_t raw[6], float* celsius, float* hpa);

class Bus {
 public:
  virtual ~Bus() = default;
  virtual bool probe(uint8_t address) = 0;
  virtual bool write(uint8_t address, const uint8_t* data, size_t length) = 0;
  virtual bool read(uint8_t address, uint8_t* data, size_t length) = 0;
  virtual bool read_reg(uint8_t address, uint8_t reg, uint8_t* data, size_t length) = 0;
};

struct Counters {
  uint32_t sht_ok = 0, sht_crc = 0, sht_bus = 0;
  uint32_t qmp_ok = 0, qmp_bad = 0, qmp_bus = 0;
  uint32_t attaches = 0;
};

class EnvSensor {
 public:
  static constexpr uint32_t kSamplePeriodMs = 1000;
  static constexpr uint32_t kShtConversionMs = 20;   // SHT30 single shot, high repeatability: 15 ms typical, 20 max
  static constexpr uint32_t kQmpFirstReadMs = 250;   // after the QMP6988 is started: one conversion plus margin
  static constexpr uint32_t kReprobeMs = 2000;
  static constexpr uint8_t kFailuresBeforeDrop = 5;

  void begin(Bus* bus);                // a null bus means "no sensor path": everything stays absent
  void service(uint32_t now_ms);       // non-blocking; call every UI tick
  void forget();                       // drop readings and detach (the next service re-probes)

  bool sht_present() const { return sht_present_; }
  bool qmp_present() const { return qmp_present_; }
  bool temperature(float* c, uint32_t* at_ms) const { return have_temp_ && (*c = temp_c_, *at_ms = temp_ms_, true); }
  bool humidity(float* rh, uint32_t* at_ms) const { return have_rh_ && (*rh = rh_, *at_ms = rh_ms_, true); }
  bool pressure(float* hpa, uint32_t* at_ms) const { return have_p_ && (*hpa = hpa_, *at_ms = p_ms_, true); }
  const Counters& counters() const { return counters_; }

 private:
  bool attach_sht();
  bool attach_qmp(uint32_t now_ms);
  void sht_fail(bool crc);
  void qmp_fail(bool bus);

  Bus* bus_ = nullptr;
  bool sht_present_ = false, qmp_present_ = false;
  bool probed_ = false, dropped_ = false;
  uint32_t last_probe_ms_ = 0;
  bool sht_pending_ = false;
  uint32_t sht_read_at_ms_ = 0;
  uint32_t last_cycle_ms_ = 0;
  bool cycle_started_ = false;
  uint32_t qmp_ready_ms_ = 0;
  uint8_t sht_failures_ = 0, qmp_failures_ = 0;
  QmpCalibration cal_{};
  bool have_temp_ = false, have_rh_ = false, have_p_ = false;
  float temp_c_ = 0, rh_ = 0, hpa_ = 0;
  uint32_t temp_ms_ = 0, rh_ms_ = 0, p_ms_ = 0;
  Counters counters_{};
};

}  // namespace orcsdr::dead_air::env
