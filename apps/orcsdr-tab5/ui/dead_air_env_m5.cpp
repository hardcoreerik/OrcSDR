#include "dead_air_env_m5.hpp"

#include <M5Unified.h>

namespace orcsdr::dead_air::env {
namespace {

constexpr uint32_t kBusHz = 100000;  // the ENV III is not speed critical, and 100 kHz is forgiving of Grove cable length

class PortABus : public Bus {
 public:
  bool probe(uint8_t address) override { return M5.Ex_I2C.scanID(address, kBusHz); }
  bool write(uint8_t address, const uint8_t* data, size_t length) override {
    if (!M5.Ex_I2C.start(address, false, kBusHz)) return false;
    const bool ok = M5.Ex_I2C.write(data, length);
    const bool stopped = M5.Ex_I2C.stop();
    return ok && stopped;
  }
  bool read(uint8_t address, uint8_t* data, size_t length) override {
    if (!M5.Ex_I2C.start(address, true, kBusHz)) return false;
    const bool ok = M5.Ex_I2C.read(data, length, true);
    const bool stopped = M5.Ex_I2C.stop();
    return ok && stopped;
  }
  bool read_reg(uint8_t address, uint8_t reg, uint8_t* data, size_t length) override {
    return M5.Ex_I2C.readRegister(address, reg, data, length, kBusHz);
  }
};

}  // namespace

Bus* port_a_bus() {
  static PortABus bus;
  static bool started = false;
  if (!started) {
    if (!M5.Ex_I2C.isEnabled()) return nullptr;
    M5.Power.setExtOutput(true, m5::ext_PA);  // Port A only; the USB-A rail is left as the RTL-SDR host set it
    (void)M5.Ex_I2C.begin();
    started = true;
  }
  return &bus;
}

}  // namespace orcsdr::dead_air::env
