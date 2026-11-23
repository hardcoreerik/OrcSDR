// Host tests for the ENV III sensor logic: SHT30 CRC and decoding, QMP6988 compensation against values produced by the
// manufacturer's reference code, and the non-blocking attach / sample / drop state machine over a scripted bus.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "dead_air_env.hpp"

using namespace orcsdr::dead_air::env;

#define CHECK(cond)                                                              \
  do {                                                                           \
    if (!(cond)) {                                                               \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
      std::exit(1);                                                              \
    }                                                                            \
  } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs(static_cast<double>(a) - static_cast<double>(b)) <= (tol))

namespace {

// Calibration and raw bytes below, and the expected results, come from M5Stack's published QMP6988 reference code
// (M5Unit-ENV) run on these exact inputs.
struct Golden { uint8_t cal[25]; uint8_t raw[6]; double celsius, hpa; };
const Golden kGolden[] = {
    {{0x76, 0x08, 0xE7, 0x7C, 0xCF, 0x3F, 0x4E, 0xB7, 0x6A, 0x42, 0xBA, 0xBE, 0x0E, 0x7D, 0x20, 0x42, 0xD3, 0xEE, 0x04, 0x1B, 0x58, 0x15, 0xB6, 0xA1, 0x91},
     {0x82, 0x7F, 0xCF, 0x82, 0x20, 0x75}, 0.8281, 375.4281},
    {{0x78, 0x58, 0x55, 0xE9, 0xCA, 0x0D, 0x55, 0x6E, 0x37, 0xD3, 0xA5, 0xE4, 0xB2, 0x61, 0xF6, 0xDF, 0x0D, 0x88, 0x31, 0x46, 0x8F, 0xF1, 0x6C, 0x1A, 0xD2},
     {0x82, 0xCF, 0xB1, 0x7E, 0x29, 0x9C}, 52.4180, 419.3400},
    {{0x6A, 0x3E, 0x6F, 0xE4, 0xAB, 0xEB, 0xDE, 0x92, 0x5C, 0x08, 0x45, 0x4E, 0xCA, 0xBE, 0xEB, 0x72, 0x4A, 0x93, 0x51, 0xAF, 0xA3, 0x7D, 0x53, 0xCB, 0xE4},
     {0x81, 0x09, 0x1F, 0x82, 0x97, 0x60}, 77.3047, 327.6575},
};

struct FakeBus : Bus {
  bool sht = true, qmp = true;
  uint8_t qmp_id = 0x5C;
  bool bus_fail = false;          // every transfer fails
  bool corrupt_crc = false;       // the SHT30 answers with a wrong CRC
  bool qmp_zero = false;          // the QMP6988 reports raw zeros (no conversion yet)
  int probes = 0, measures = 0, sht_reads = 0, qmp_reads = 0;
  bool measuring = false;

  bool probe(uint8_t address) override {
    ++probes;
    if (bus_fail) return false;
    return (address == kSht30Address && sht) || (address == kQmp6988Address && qmp);
  }
  bool write(uint8_t address, const uint8_t* data, size_t length) override {
    if (bus_fail) return false;
    if (address == kSht30Address && sht && length == 2 && data[0] == 0x24 && data[1] == 0x00) { measuring = true; ++measures; return true; }
    return address == kQmp6988Address && qmp;
  }
  bool read(uint8_t address, uint8_t* data, size_t length) override {
    if (bus_fail || address != kSht30Address || !sht || !measuring || length != 6) return false;
    measuring = false;
    ++sht_reads;
    data[0] = 0x66; data[1] = 0x66; data[2] = sht30_crc8(data, 2);  // 25.0 C
    data[3] = 0x7F; data[4] = 0xFF; data[5] = sht30_crc8(data + 3, 2);  // 50.0 %RH
    if (corrupt_crc) data[5] ^= 0x55;
    return true;
  }
  bool read_reg(uint8_t address, uint8_t reg, uint8_t* data, size_t length) override {
    if (bus_fail || address != kQmp6988Address || !qmp) return false;
    if (reg == 0xD1) { data[0] = qmp_id; return true; }
    if (reg >= 0xA0 && reg < 0xA0 + 25) { for (size_t i = 0; i < length; ++i) data[i] = kGolden[0].cal[reg - 0xA0 + i]; return true; }
    if (reg == 0xF7 && length == 6) {
      ++qmp_reads;
      if (qmp_zero) std::memset(data, 0, 6); else std::memcpy(data, kGolden[0].raw, 6);
      return true;
    }
    return false;
  }
};

void test_crc_and_decode() {
  const uint8_t beef[2] = {0xBE, 0xEF};
  CHECK(sht30_crc8(beef, 2) == 0x92);  // the Sensirion datasheet's check value
  uint8_t raw[6] = {0x66, 0x66, 0, 0x7F, 0xFF, 0};
  raw[2] = sht30_crc8(raw, 2);
  raw[5] = sht30_crc8(raw + 3, 2);
  float c = 0, h = 0;
  CHECK(sht30_decode(raw, &c, &h));
  NEAR(c, 25.0, 0.01);
  NEAR(h, 50.0, 0.01);
  raw[2] ^= 1;
  CHECK(!sht30_decode(raw, &c, &h));
  raw[2] ^= 1;
  raw[5] ^= 1;
  CHECK(!sht30_decode(raw, &c, &h));
  const uint8_t low[6] = {0x00, 0x00, 0x81, 0x00, 0x00, 0x81};  // crc8(00 00) = 0x81
  CHECK(sht30_decode(low, &c, &h));
  NEAR(c, -45.0, 1e-4);
  NEAR(h, 0.0, 1e-4);
}

void test_qmp_matches_reference() {
  for (const Golden& g : kGolden) {
    QmpCalibration cal;
    qmp6988_parse_calibration(g.cal, &cal);
    float c = 0, h = 0;
    CHECK(qmp6988_compensate(cal, g.raw, &c, &h));
    NEAR(c, g.celsius, 0.001);
    NEAR(h, g.hpa, 0.001);
  }
  // A sensor that has not converted yet reads zero: never a pressure.
  QmpCalibration cal;
  qmp6988_parse_calibration(kGolden[0].cal, &cal);
  const uint8_t zeros[6] = {};
  float c = 0, h = 0;
  CHECK(!qmp6988_compensate(cal, zeros, &c, &h));
}

void test_absent_stays_absent() {
  EnvSensor s;
  s.begin(nullptr);
  s.service(0);
  CHECK(!s.sht_present() && !s.qmp_present());
  FakeBus bus;
  bus.sht = bus.qmp = false;
  s.begin(&bus);
  float v; uint32_t at;
  for (uint32_t t = 0; t <= 10000; t += 20) s.service(t);
  CHECK(!s.sht_present() && !s.qmp_present());
  CHECK(!s.temperature(&v, &at) && !s.humidity(&v, &at) && !s.pressure(&v, &at));
  CHECK(bus.probes <= 2 * 6);  // re-probed about every 2 s, never every tick
  CHECK(bus.probes >= 2 * 4);
}

void test_normal_sampling() {
  FakeBus bus;
  EnvSensor s;
  s.begin(&bus);
  float v; uint32_t at;
  s.service(0);
  CHECK(s.sht_present() && s.qmp_present());
  CHECK(!s.temperature(&v, &at));  // nothing until a checked reading
  s.service(10);
  CHECK(!s.temperature(&v, &at));  // the SHT30 is still converting
  s.service(25);
  CHECK(s.temperature(&v, &at)); NEAR(v, 25.0, 0.01); CHECK(at == 25);
  CHECK(s.humidity(&v, &at)); NEAR(v, 50.0, 0.01);
  CHECK(!s.pressure(&v, &at));     // the QMP6988 is not read before its first conversion is done
  for (uint32_t t = 30; t < 1000; t += 10) s.service(t);
  CHECK(!s.pressure(&v, &at));
  s.service(1000);
  CHECK(s.pressure(&v, &at)); NEAR(v, 375.4281, 0.001); CHECK(at == 1000);
  for (uint32_t t = 1010; t <= 5030; t += 10) s.service(t);
  CHECK(bus.measures == 6 && bus.sht_reads == 6);  // one measurement per second: 0, 1, 2, 3, 4, 5 s
  CHECK(bus.qmp_reads == 5);
  CHECK(s.counters().sht_ok == 6 && s.counters().sht_crc == 0);
}

void test_crc_failures_drop_and_recover() {
  FakeBus bus;
  bus.corrupt_crc = true;
  EnvSensor s;
  s.begin(&bus);
  float v; uint32_t at;
  uint32_t t = 0;
  for (; t <= 4100; t += 10) s.service(t);
  CHECK(!s.sht_present());  // five bad answers in a row: dropped, and no value was ever kept
  CHECK(!s.temperature(&v, &at) && !s.humidity(&v, &at));
  CHECK(s.counters().sht_crc >= 5);
  CHECK(s.qmp_present());   // the other channel is independent
  bus.corrupt_crc = false;
  for (; t <= 14000; t += 10) s.service(t);
  CHECK(s.sht_present());
  CHECK(s.temperature(&v, &at));
}

void test_bus_failure_clears_readings() {
  FakeBus bus;
  EnvSensor s;
  s.begin(&bus);
  float v; uint32_t at;
  uint32_t t = 0;
  for (; t <= 2000; t += 10) s.service(t);
  CHECK(s.temperature(&v, &at) && s.pressure(&v, &at));
  bus.bus_fail = true;  // module unplugged
  for (; t <= 10000; t += 10) s.service(t);
  CHECK(!s.sht_present() && !s.qmp_present());
  CHECK(!s.temperature(&v, &at) && !s.humidity(&v, &at) && !s.pressure(&v, &at));
  bus.bus_fail = false;  // and plugged back in
  for (; t <= 16000; t += 10) s.service(t);
  CHECK(s.sht_present() && s.qmp_present());
  CHECK(s.pressure(&v, &at));
}

void test_wrong_chip_and_implausible_pressure() {
  FakeBus bus;
  bus.qmp_id = 0x58;  // not a QMP6988
  EnvSensor s;
  s.begin(&bus);
  float v; uint32_t at;
  for (uint32_t t = 0; t <= 3000; t += 10) s.service(t);
  CHECK(s.sht_present() && !s.qmp_present());
  CHECK(!s.pressure(&v, &at));

  FakeBus zero;
  zero.qmp_zero = true;
  EnvSensor z;
  z.begin(&zero);
  for (uint32_t t = 0; t <= 5100; t += 10) z.service(t);
  CHECK(!z.pressure(&v, &at));
  CHECK(z.counters().qmp_bad >= 5 && !z.qmp_present());  // never a made-up pressure; dropped after repeated nonsense
  CHECK(z.temperature(&v, &at));
}

void test_forget() {
  FakeBus bus;
  EnvSensor s;
  s.begin(&bus);
  float v; uint32_t at;
  for (uint32_t t = 0; t <= 1500; t += 10) s.service(t);
  CHECK(s.temperature(&v, &at));
  s.forget();
  CHECK(!s.sht_present() && !s.qmp_present() && !s.temperature(&v, &at) && !s.pressure(&v, &at));
}

}  // namespace

int main() {
  test_crc_and_decode();
  test_qmp_matches_reference();
  test_absent_stays_absent();
  test_normal_sampling();
  test_crc_failures_drop_and_recover();
  test_bus_failure_clears_readings();
  test_wrong_chip_and_implausible_pressure();
  test_forget();
  std::printf("dead_air_env_tests: PASS\n");
  return 0;
}
