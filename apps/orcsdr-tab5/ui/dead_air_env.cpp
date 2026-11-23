#include "dead_air_env.hpp"

#include <cmath>

namespace orcsdr::dead_air::env {
namespace {

constexpr uint8_t kQmpChipIdReg = 0xD1;
constexpr uint8_t kQmpChipId = 0x5C;
constexpr uint8_t kQmpCalibrationStart = 0xA0;
constexpr uint8_t kQmpConfigReg = 0xF1;
constexpr uint8_t kQmpCtrlMeasReg = 0xF4;
constexpr uint8_t kQmpDataReg = 0xF7;
constexpr uint8_t kQmpFilter4 = 0x02;
constexpr uint8_t kQmpNormal8xP1xT = (1u << 5) | (4u << 2) | 0x03;  // T x1, P x8, normal mode
constexpr uint8_t kShtMeasure[2] = {0x24, 0x00};                     // single shot, high repeatability, no clock stretching

int32_t sign_extend_20(uint32_t value) {
  return static_cast<int32_t>(value & 0x80000u ? value - 0x100000u : value);
}
int16_t be16(const uint8_t* p) { return static_cast<int16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]); }

}  // namespace

uint8_t sht30_crc8(const uint8_t* data, size_t length) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) crc = static_cast<uint8_t>(crc & 0x80 ? (crc << 1) ^ 0x31 : crc << 1);
  }
  return crc;
}

bool sht30_decode(const uint8_t raw[6], float* celsius, float* humidity_rh) {
  if (sht30_crc8(raw, 2) != raw[2] || sht30_crc8(raw + 3, 2) != raw[5]) return false;
  const uint16_t t = static_cast<uint16_t>((raw[0] << 8) | raw[1]);
  const uint16_t h = static_cast<uint16_t>((raw[3] << 8) | raw[4]);
  if (celsius) *celsius = -45.0f + 175.0f * static_cast<float>(t) / 65535.0f;
  if (humidity_rh) *humidity_rh = 100.0f * static_cast<float>(h) / 65535.0f;
  return true;
}

void qmp6988_parse_calibration(const uint8_t d[kQmpCalibrationBytes], QmpCalibration* out) {
  const int32_t coe_a0 = sign_extend_20((static_cast<uint32_t>(d[18]) << 12) | (static_cast<uint32_t>(d[19]) << 4) | (d[24] & 0x0Fu));
  const int32_t coe_b00 = sign_extend_20((static_cast<uint32_t>(d[0]) << 12) | (static_cast<uint32_t>(d[1]) << 4) | ((d[24] & 0xF0u) >> 4));
  const int64_t a1 = be16(d + 20), a2 = be16(d + 22);
  const int64_t bt1 = be16(d + 2), bt2 = be16(d + 4), bp1 = be16(d + 6), b11 = be16(d + 8);
  const int64_t bp2 = be16(d + 10), b12 = be16(d + 12), b21 = be16(d + 14), bp3 = be16(d + 16);
  out->a0 = coe_a0;    // 20Q4
  out->b00 = coe_b00;  // 20Q4
  out->a1 = 3608 * a1 - 1731677965LL;
  out->a2 = 16889 * a2 - 87619360LL;
  out->bt1 = 2982 * bt1 + 107370906LL;
  out->bt2 = 329854 * bt2 + 108083093LL;
  out->bp1 = 19923 * bp1 + 1133836764LL;
  out->b11 = 2406 * b11 + 118215883LL;
  out->bp2 = 3079 * bp2 - 181579595LL;
  out->b12 = 6846 * b12 + 85590281LL;
  out->b21 = 13836 * b21 + 79333336LL;
  out->bp3 = 2915 * bp3 + 157155561LL;
}

bool qmp6988_compensate(const QmpCalibration& k, const uint8_t raw[6], float* celsius, float* hpa) {
  const int32_t dp = static_cast<int32_t>((static_cast<uint32_t>(raw[0]) << 16) | (static_cast<uint32_t>(raw[1]) << 8) | raw[2]) - 8388608;
  const int32_t dt = static_cast<int32_t>((static_cast<uint32_t>(raw[3]) << 16) | (static_cast<uint32_t>(raw[4]) << 8) | raw[5]) - 8388608;
  // Temperature, in 1/256 degree.
  int64_t wk1 = k.a1 * dt;
  int64_t wk2 = (k.a2 * dt) >> 14;
  wk2 = (wk2 * dt) >> 10;
  wk2 = ((wk1 + wk2) / 32767) >> 19;
  const int16_t tx = static_cast<int16_t>((k.a0 + wk2) >> 4);
  // Pressure, in 1/16 pascal.
  wk1 = k.bt1 * tx;
  wk2 = (k.bp1 * dp) >> 5;
  wk1 += wk2;
  wk2 = (k.bt2 * tx) >> 1;
  wk2 = (wk2 * tx) >> 8;
  int64_t wk3 = wk2;
  wk2 = (k.b11 * tx) >> 4;
  wk2 = (wk2 * dp) >> 1;
  wk3 += wk2;
  wk2 = (k.bp2 * dp) >> 13;
  wk2 = (wk2 * dp) >> 1;
  wk3 += wk2;
  wk1 += wk3 >> 14;
  wk2 = k.b12 * tx;
  wk2 = (wk2 * tx) >> 22;
  wk2 = (wk2 * dp) >> 1;
  wk3 = wk2;
  wk2 = (k.b21 * tx) >> 6;
  wk2 = (wk2 * dp) >> 23;
  wk2 = (wk2 * dp) >> 1;
  wk3 += wk2;
  wk2 = (k.bp3 * dp) >> 12;
  wk2 = (wk2 * dp) >> 23;
  wk2 = wk2 * dp;
  wk3 += wk2;
  wk1 += wk3 >> 15;
  wk1 /= 32767;
  wk1 >>= 11;
  wk1 += k.b00;
  const float c = static_cast<float>(tx) / 256.0f;
  const float h = static_cast<float>(static_cast<int32_t>(wk1)) / 16.0f / 100.0f;
  if (!std::isfinite(c) || !std::isfinite(h) || c < -40.0f || c > 85.0f || h < 300.0f || h > 1100.0f) return false;
  if (celsius) *celsius = c;
  if (hpa) *hpa = h;
  return true;
}

void EnvSensor::begin(Bus* bus) {
  bus_ = bus;
  forget();
}

void EnvSensor::forget() {
  sht_present_ = qmp_present_ = false;
  have_temp_ = have_rh_ = have_p_ = false;
  probed_ = false;
  sht_pending_ = false;
  cycle_started_ = false;
  sht_failures_ = qmp_failures_ = 0;
}

bool EnvSensor::attach_sht() {
  if (!bus_->probe(kSht30Address)) return false;
  sht_present_ = true;
  sht_failures_ = 0;
  sht_pending_ = false;
  ++counters_.attaches;
  return true;
}

bool EnvSensor::attach_qmp(uint32_t now_ms) {
  if (!bus_->probe(kQmp6988Address)) return false;
  uint8_t id = 0;
  if (!bus_->read_reg(kQmp6988Address, kQmpChipIdReg, &id, 1) || id != kQmpChipId) return false;
  uint8_t cal[kQmpCalibrationBytes] = {};
  for (size_t i = 0; i < sizeof(cal); ++i) {  // one register per transaction, as the manufacturer's reference does
    if (!bus_->read_reg(kQmp6988Address, static_cast<uint8_t>(kQmpCalibrationStart + i), &cal[i], 1)) return false;
  }
  const uint8_t filter[2] = {kQmpConfigReg, kQmpFilter4};
  const uint8_t control[2] = {kQmpCtrlMeasReg, kQmpNormal8xP1xT};
  if (!bus_->write(kQmp6988Address, filter, 2) || !bus_->write(kQmp6988Address, control, 2)) return false;
  qmp6988_parse_calibration(cal, &cal_);
  qmp_present_ = true;
  qmp_failures_ = 0;
  qmp_ready_ms_ = now_ms + kQmpFirstReadMs;
  ++counters_.attaches;
  return true;
}

void EnvSensor::sht_fail(bool crc) {
  if (crc) ++counters_.sht_crc; else ++counters_.sht_bus;
  if (++sht_failures_ >= kFailuresBeforeDrop) {
    sht_present_ = false;
    have_temp_ = have_rh_ = false;
    sht_pending_ = false;
    dropped_ = true;
  }
}

void EnvSensor::qmp_fail(bool bus) {
  if (bus) ++counters_.qmp_bus; else ++counters_.qmp_bad;
  if (++qmp_failures_ >= kFailuresBeforeDrop) {
    qmp_present_ = false;
    have_p_ = false;
    dropped_ = true;
  }
}

void EnvSensor::service(uint32_t now_ms) {
  if (!bus_) return;
  if ((!sht_present_ || !qmp_present_) && (!probed_ || now_ms - last_probe_ms_ >= kReprobeMs)) {
    probed_ = true;
    last_probe_ms_ = now_ms;
    if (!sht_present_) (void)attach_sht();
    if (!qmp_present_) (void)attach_qmp(now_ms);
  }
  if (sht_pending_ && static_cast<int32_t>(now_ms - sht_read_at_ms_) >= 0) {
    sht_pending_ = false;
    uint8_t raw[6] = {};
    float t = 0, h = 0;
    if (!bus_->read(kSht30Address, raw, sizeof(raw))) {
      sht_fail(false);
    } else if (!sht30_decode(raw, &t, &h)) {
      sht_fail(true);
    } else {
      sht_failures_ = 0;
      ++counters_.sht_ok;
      temp_c_ = t; rh_ = h;
      temp_ms_ = rh_ms_ = now_ms;
      have_temp_ = have_rh_ = true;
    }
  }
  if (!cycle_started_ || now_ms - last_cycle_ms_ >= kSamplePeriodMs) {
    cycle_started_ = true;
    last_cycle_ms_ = now_ms;
    if (sht_present_ && !sht_pending_) {
      if (bus_->write(kSht30Address, kShtMeasure, sizeof(kShtMeasure))) {
        sht_pending_ = true;
        sht_read_at_ms_ = now_ms + kShtConversionMs;
      } else {
        sht_fail(false);
      }
    }
    if (qmp_present_ && static_cast<int32_t>(now_ms - qmp_ready_ms_) >= 0) {
      uint8_t raw[6] = {};
      float c = 0, h = 0;
      if (!bus_->read_reg(kQmp6988Address, kQmpDataReg, raw, sizeof(raw))) {
        qmp_fail(true);
      } else if (!qmp6988_compensate(cal_, raw, &c, &h)) {
        qmp_fail(false);
      } else {
        qmp_failures_ = 0;
        ++counters_.qmp_ok;
        hpa_ = h;
        p_ms_ = now_ms;
        have_p_ = true;
      }
    }
  }
  if (dropped_) {  // a channel that just gave up waits a full interval before it is tried again
    dropped_ = false;
    probed_ = true;
    last_probe_ms_ = now_ms;
  }
}

}  // namespace orcsdr::dead_air::env
