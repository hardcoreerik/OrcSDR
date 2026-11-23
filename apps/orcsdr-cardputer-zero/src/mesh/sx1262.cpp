// SX1262 command layer for the Cap LoRa-1262.
//
// Register and opcode values follow the SX126x datasheet. The bring-up order is
// the one the datasheet prescribes: reset, standby, regulator, TCXO, calibrate,
// then packet type and modem settings. Nothing here is protocol specific; the
// sync word and framing parameters are inputs.
#include "sx1262.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

namespace orc::zero::mesh {
namespace {

// Opcodes.
constexpr uint8_t kOpSetSleep = 0x84;
constexpr uint8_t kOpSetStandby = 0x80;
constexpr uint8_t kOpSetTx = 0x83;
constexpr uint8_t kOpSetRx = 0x82;
constexpr uint8_t kOpSetPacketType = 0x8A;
constexpr uint8_t kOpSetRfFrequency = 0x86;
constexpr uint8_t kOpSetTxParams = 0x8E;
constexpr uint8_t kOpSetPaConfig = 0x95;
constexpr uint8_t kOpSetBufferBaseAddress = 0x8F;
constexpr uint8_t kOpSetModulationParams = 0x8B;
constexpr uint8_t kOpSetPacketParams = 0x8C;
constexpr uint8_t kOpSetDioIrqParams = 0x08;
constexpr uint8_t kOpSetDio2AsRfSwitchCtrl = 0x9D;
constexpr uint8_t kOpSetDio3AsTcxoCtrl = 0x97;
constexpr uint8_t kOpSetRegulatorMode = 0x96;
constexpr uint8_t kOpCalibrate = 0x89;
constexpr uint8_t kOpCalibrateImage = 0x98;
constexpr uint8_t kOpClearIrqStatus = 0x02;
constexpr uint8_t kOpGetIrqStatus = 0x12;
constexpr uint8_t kOpGetRxBufferStatus = 0x13;
constexpr uint8_t kOpGetPacketStatus = 0x14;
constexpr uint8_t kOpGetStatus = 0xC0;
constexpr uint8_t kOpSetCadParams = 0x88;
constexpr uint8_t kOpSetCad = 0xC5;
constexpr uint8_t kOpGetRssiInst = 0x15;
constexpr uint8_t kOpSetLoRaSymbNumTimeout = 0xA0;
constexpr uint8_t kOpGetDeviceErrors = 0x17;
constexpr uint8_t kOpClearDeviceErrors = 0x07;
constexpr uint8_t kOpReadBuffer = 0x1E;
constexpr uint8_t kOpWriteBuffer = 0x0E;
constexpr uint8_t kOpReadRegister = 0x1D;
constexpr uint8_t kOpWriteRegister = 0x0D;
constexpr uint8_t kNop = 0x00;

// IRQ bits.
constexpr uint16_t kIrqTxDone = 0x0001;
constexpr uint16_t kIrqRxDone = 0x0002;
constexpr uint16_t kIrqPreamble = 0x0004;
constexpr uint16_t kIrqSync = 0x0008;
constexpr uint16_t kIrqHeaderValid = 0x0010;
constexpr uint16_t kIrqHeaderErr = 0x0020;
constexpr uint16_t kIrqCrcErr = 0x0040;
constexpr uint16_t kIrqCadDone = 0x0080;
constexpr uint16_t kIrqCadDetected = 0x0100;
constexpr uint16_t kIrqTimeout = 0x0200;

// LoRa bandwidth encoding: (bandwidth_hz, sx126x code).
struct BandwidthCode {
  uint32_t hz;
  uint8_t code;
};
constexpr BandwidthCode kBandwidths[] = {
    {7800, 0x00}, {10400, 0x08}, {15600, 0x01}, {20800, 0x09}, {31250, 0x02},
    {41700, 0x0A}, {62500, 0x03}, {125000, 0x04}, {250000, 0x05}, {500000, 0x06},
};

uint8_t coding_rate_code(uint8_t denominator) {
  switch (denominator) {
    case 5: return 0x01;
    case 6: return 0x02;
    case 7: return 0x03;
    case 8: return 0x04;
    default: return 0x01;
  }
}

uint8_t bandwidth_code(uint32_t hz, bool& ok) {
  ok = false;
  for (const auto& entry : kBandwidths) {
    if (entry.hz == hz) {
      ok = true;
      return entry.code;
    }
  }
  return 0x03;
}

// Image calibration band selection required after a reset.
void calibrate_image_bytes(uint32_t hz, uint8_t& first, uint8_t& second) {
  if (hz < 446000000) {
    first = 0x6B;
    second = 0x6F;
  } else if (hz < 734000000) {
    first = 0x75;
    second = 0x81;
  } else if (hz < 828000000) {
    first = 0xC1;
    second = 0xC5;
  } else if (hz < 877000000) {
    first = 0xD7;
    second = 0xDB;
  } else {
    first = 0xE1;
    second = 0xE9;
  }
}

// Symbol time in microseconds, used for the low-data-rate optimisation flag and
// for airtime.
double symbol_time_us(uint8_t sf, uint32_t bandwidth_hz) {
  const double bw = bandwidth_hz > 0 ? double(bandwidth_hz) : 125000.0;
  return (double(1u << sf) / bw) * 1e6;
}

}  // namespace

bool Sx1262::open(const char* spi_path, const char* gpio_chip, int reset_pin, int busy_pin,
                  int irq_pin, std::string& error) {
  close();
  if (!spi_.open(spi_path, 8000000, error)) return false;
  if (!reset_.open(gpio_chip, uint32_t(reset_pin), true, true, error)) return false;
  if (!busy_.open(gpio_chip, uint32_t(busy_pin), false, false, error)) return false;
  if (!irq_.open(gpio_chip, uint32_t(irq_pin), false, false, error)) return false;
  if (!reset(error)) return false;
  return true;
}

void Sx1262::close() {
  configured_ = false;
  irq_.close();
  busy_.close();
  reset_.close();
  spi_.close();
}

bool Sx1262::reset(std::string& error) {
  // Datasheet: NRESET low for at least 100us, then released; the chip signals
  // readiness by dropping BUSY.
  if (!reset_.set(false)) {
    error = "drive NRESET low failed";
    return false;
  }
  std::this_thread::sleep_for(std::chrono::microseconds(300));
  if (!reset_.set(true)) {
    error = "release NRESET failed";
    return false;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  return wait_busy(100, error);
}

bool Sx1262::wait_busy(uint32_t timeout_ms, std::string& error) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    bool high = false;
    if (!busy_.get(high)) {
      error = "read BUSY line failed";
      return false;
    }
    if (!high) return true;
    if (std::chrono::steady_clock::now() >= deadline) {
      error = "SX1262 stayed busy";
      return false;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(200));
  }
}

bool Sx1262::write_command(uint8_t op, const uint8_t* params, size_t count, std::string& error) {
  std::vector<uint8_t> frame;
  frame.reserve(count + 1);
  frame.push_back(op);
  for (size_t i = 0; i < count; ++i) frame.push_back(params[i]);
  if (!spi_.transfer(frame.data(), nullptr, frame.size(), error)) return false;
  return wait_busy(100, error);
}

bool Sx1262::read_command(uint8_t op, uint8_t* out, size_t count, std::string& error) {
  std::vector<uint8_t> tx(count + 2, kNop);
  std::vector<uint8_t> rx(count + 2, 0);
  tx[0] = op;
  if (!spi_.transfer(tx.data(), rx.data(), tx.size(), error)) return false;
  if (!wait_busy(100, error)) return false;
  std::memcpy(out, rx.data() + 2, count);
  return true;
}

bool Sx1262::read_register(uint16_t address, uint8_t* out, size_t count, std::string& error) {
  std::vector<uint8_t> tx(count + 4, kNop);
  std::vector<uint8_t> rx(count + 4, 0);
  tx[0] = kOpReadRegister;
  tx[1] = uint8_t(address >> 8);
  tx[2] = uint8_t(address & 0xFF);
  if (!spi_.transfer(tx.data(), rx.data(), tx.size(), error)) return false;
  if (!wait_busy(100, error)) return false;
  std::memcpy(out, rx.data() + 4, count);
  return true;
}

bool Sx1262::write_register(uint16_t address, const uint8_t* data, size_t count,
                            std::string& error) {
  std::vector<uint8_t> frame;
  frame.reserve(count + 3);
  frame.push_back(kOpWriteRegister);
  frame.push_back(uint8_t(address >> 8));
  frame.push_back(uint8_t(address & 0xFF));
  for (size_t i = 0; i < count; ++i) frame.push_back(data[i]);
  if (!spi_.transfer(frame.data(), nullptr, frame.size(), error)) return false;
  return wait_busy(100, error);
}

bool Sx1262::read_signature(std::string& signature, std::string& error) {
  uint8_t raw[16]{};
  if (!read_register(0x0320, raw, sizeof(raw), error)) return false;
  signature.assign(reinterpret_cast<const char*>(raw), sizeof(raw));
  while (!signature.empty() && (signature.back() == '\0' || signature.back() == ' '))
    signature.pop_back();
  // An unpowered or unseated CAP returns 0x00 or 0xFF on every byte.
  bool floating = true;
  for (uint8_t value : raw)
    if (value != 0x00 && value != 0xFF) floating = false;
  if (floating) {
    error = "no SX126x signature (CAP unpowered or not seated)";
    return false;
  }
  return true;
}

bool Sx1262::read_chip_mode(uint8_t& mode, std::string& error) {
  uint8_t status[2]{};
  if (!read_command(kOpGetStatus, status, sizeof(status), error)) return false;
  mode = uint8_t((status[1] >> 4) & 0x07);
  return true;
}

bool Sx1262::read_device_errors(uint16_t& errors, std::string& error) {
  uint8_t raw[2]{};
  if (!read_command(kOpGetDeviceErrors, raw, sizeof(raw), error)) return false;
  errors = uint16_t((raw[0] << 8) | raw[1]);
  return true;
}

bool Sx1262::clear_device_errors(std::string& error) {
  return write_command(kOpClearDeviceErrors, nullptr, 0, error);
}

const char* Sx1262::describe_device_errors(uint16_t errors) {  static char text[192];
  if (!errors) return "none";
  text[0] = '\0';
  struct Entry {
    uint16_t bit;
    const char* name;
  };
  const Entry entries[] = {{0x0001, "RC64K_CALIB"}, {0x0002, "RC13M_CALIB"}, {0x0004, "PLL_CALIB"},
                           {0x0008, "ADC_CALIB"},   {0x0010, "IMG_CALIB"},  {0x0020, "XOSC_START"},
                           {0x0040, "PLL_LOCK"},    {0x0100, "PA_RAMP"}};
  for (const auto& entry : entries) {
    if (!(errors & entry.bit)) continue;
    if (text[0]) std::strncat(text, " ", sizeof(text) - std::strlen(text) - 1);
    std::strncat(text, entry.name, sizeof(text) - std::strlen(text) - 1);
  }
  return text;
}

bool Sx1262::clear_irq(uint16_t mask, std::string& error) {
  const uint8_t params[2] = {uint8_t(mask >> 8), uint8_t(mask & 0xFF)};
  return write_command(kOpClearIrqStatus, params, 2, error);
}

bool Sx1262::irq_status(uint16_t& status, std::string& error) {
  uint8_t raw[2]{};
  if (!read_command(kOpGetIrqStatus, raw, sizeof(raw), error)) return false;
  status = uint16_t((raw[0] << 8) | raw[1]);
  return true;
}

bool Sx1262::set_modulation(const LoraProfile& profile, std::string& error) {
  bool ok = false;
  const uint8_t bw = bandwidth_code(profile.bandwidth_hz, ok);
  if (!ok) {
    error = "unsupported LoRa bandwidth " + std::to_string(profile.bandwidth_hz);
    return false;
  }
  const uint8_t sf = uint8_t(std::clamp<int>(profile.spreading_factor, 5, 12));
  const uint8_t cr = coding_rate_code(profile.coding_rate);
  // Low-data-rate optimisation is mandatory once a symbol exceeds 16ms.
  const uint8_t ldro = symbol_time_us(sf, profile.bandwidth_hz) > 16000.0 ? 0x01 : 0x00;
  const uint8_t params[4] = {sf, bw, cr, ldro};
  return write_command(kOpSetModulationParams, params, sizeof(params), error);
}

bool Sx1262::set_packet_params(const LoraProfile& profile, size_t payload, std::string& error) {
  const uint8_t params[6] = {
      uint8_t(profile.preamble_symbols >> 8), uint8_t(profile.preamble_symbols & 0xFF),
      0x00,                                            // explicit header
      uint8_t(std::min<size_t>(payload, 255)),         // payload length
      0x01,                                            // CRC on
      uint8_t(profile.invert_iq ? 0x01 : 0x00),        // IQ polarity
  };
  return write_command(kOpSetPacketParams, params, sizeof(params), error);
}

bool Sx1262::configure(const LoraProfile& profile, std::string& error) {
  if (!is_open()) {
    error = "SX1262 not open";
    return false;
  }
  profile_ = profile;

  // Identify the silicon first: the PA configuration below depends on it.
  std::string signature;
  if (!read_signature(signature, error)) return false;
  if (signature.rfind("SX1261", 0) == 0)
    variant_ = ChipVariant::sx1261;
  else if (signature.rfind("SX1262", 0) == 0)
    variant_ = ChipVariant::sx1262;
  else if (signature.rfind("SX1268", 0) == 0)
    variant_ = ChipVariant::sx1268;
  else
    variant_ = ChipVariant::unknown;

  const uint8_t standby[1] = {0x00};  // STDBY_RC
  if (!write_command(kOpSetStandby, standby, 1, error)) return false;

  const uint8_t regulator[1] = {0x01};  // DC-DC
  if (!write_command(kOpSetRegulatorMode, regulator, 1, error)) return false;

  // DIO3 feeds the TCXO, but only on boards that actually have one. Programming
  // a TCXO setting onto a crystal board stops the oscillator from starting.
  if (profile.use_tcxo) {
    const uint8_t tcxo[4] = {profile.tcxo_voltage_code, uint8_t(profile.tcxo_timeout_ticks >> 16),
                             uint8_t(profile.tcxo_timeout_ticks >> 8),
                             uint8_t(profile.tcxo_timeout_ticks)};
    if (!write_command(kOpSetDio3AsTcxoCtrl, tcxo, sizeof(tcxo), error)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  const uint8_t calibrate[1] = {0x7F};
  if (!write_command(kOpCalibrate, calibrate, 1, error)) return false;

  uint8_t image_first = 0, image_second = 0;
  calibrate_image_bytes(profile.frequency_hz, image_first, image_second);
  const uint8_t image[2] = {image_first, image_second};
  if (!write_command(kOpCalibrateImage, image, sizeof(image), error)) return false;

  const uint8_t packet_type[1] = {0x01};  // LoRa
  if (!write_command(kOpSetPacketType, packet_type, 1, error)) return false;

  // DIO2 drives the RF switch on some boards and must be left alone on others.
  const uint8_t dio2[1] = {uint8_t(profile.use_dio2_rf_switch ? 0x01 : 0x00)};
  if (!write_command(kOpSetDio2AsRfSwitchCtrl, dio2, 1, error)) return false;

  configured_ = true;
  return apply_profile(profile, error);
}

bool Sx1262::apply_profile(const LoraProfile& profile, std::string& error) {
  if (!configured_) {
    error = "SX1262 not configured";
    return false;
  }
  profile_ = profile;

  // frf = frequency * 2^25 / 32MHz
  const uint64_t frf =
      (uint64_t(profile.frequency_hz) << 25) / 32000000ull;
  const uint8_t frequency[4] = {uint8_t(frf >> 24), uint8_t(frf >> 16), uint8_t(frf >> 8),
                                uint8_t(frf)};
  if (!write_command(kOpSetRfFrequency, frequency, sizeof(frequency), error)) return false;

  // The PA configuration is part specific. SX1261 needs device select 0x01 and
  // reaches +15 dBm; SX1262/SX1268 use 0x00 and reach +22 dBm. Getting this
  // wrong overdrives the 1261, so the detected variant decides it.
  const bool low_power = variant_ != ChipVariant::sx1262 && variant_ != ChipVariant::sx1268;
  const uint8_t pa[4] = {0x04, uint8_t(low_power ? 0x00 : 0x07),
                         uint8_t(low_power ? 0x01 : 0x00), 0x01};
  if (!write_command(kOpSetPaConfig, pa, sizeof(pa), error)) return false;

  applied_tx_dbm_ = std::clamp(profile.tx_dbm, -9, max_tx_dbm());
  const uint8_t power = uint8_t(applied_tx_dbm_ & 0xFF);
  const uint8_t tx_params[2] = {power, 0x04};  // 200us ramp
  if (!write_command(kOpSetTxParams, tx_params, sizeof(tx_params), error)) return false;

  const uint8_t base[2] = {0x00, 0x00};
  if (!write_command(kOpSetBufferBaseAddress, base, sizeof(base), error)) return false;

  if (!set_modulation(profile, error)) return false;
  if (!set_packet_params(profile, 255, error)) return false;

  // The sync word is not a command parameter on SX126x; it is register 0x0740.
  const uint8_t sync[2] = {uint8_t(profile.sync_word >> 8), uint8_t(profile.sync_word & 0xFF)};
  if (!write_register(0x0740, sync, sizeof(sync), error)) return false;
  uint8_t readback[2]{};
  if (!read_register(0x0740, readback, sizeof(readback), error)) return false;
  if (readback[0] != sync[0] || readback[1] != sync[1]) {
    error = "SX1262 sync word readback mismatch";
    return false;
  }

  // IQ polarity must match the packet parameters. Bit 2 of register 0x0736 is
  // set for standard IQ and clear for inverted; the reset value alone is not
  // trustworthy, which is why RadioLib rewrites it on every configuration.
  uint8_t iq = 0;
  if (!read_register(0x0736, &iq, 1, error)) return false;
  const uint8_t next_iq = profile.invert_iq ? uint8_t(iq & ~0x04) : uint8_t(iq | 0x04);
  if (!write_register(0x0736, &next_iq, 1, error)) return false;

  // Route the interesting events to DIO1 so the driver can wait on the line.
  // The mask here also enables the IRQs globally: a bit left out of it never
  // latches in GetIrqStatus, which is what silently broke CAD at first.
  const uint16_t mask = kIrqTxDone | kIrqRxDone | kIrqCrcErr | kIrqTimeout | kIrqHeaderErr |
                        kIrqCadDone | kIrqCadDetected;
  const uint8_t irq[8] = {uint8_t(mask >> 8), uint8_t(mask & 0xFF), uint8_t(mask >> 8),
                          uint8_t(mask & 0xFF), 0x00, 0x00, 0x00, 0x00};
  if (!write_command(kOpSetDioIrqParams, irq, sizeof(irq), error)) return false;

  // Bound how long the receiver hunts for a sync word after a preamble. With
  // this disabled (the reset default) a preamble whose sync never matches
  // leaves the radio waiting forever, so one stray LoRa packet carrying another
  // network's sync silences the receiver until something else times it out.
  // 16 symbols is comfortably longer than the 2-symbol sync plus header.
  const uint8_t symb_timeout[1] = {16};
  if (!write_command(kOpSetLoRaSymbNumTimeout, symb_timeout, 1, error)) return false;

  // Clear latched errors so a later read reflects this configuration rather than
  // whatever happened during power-up before the TCXO was programmed.
  clear_device_errors(error);
  error.clear();
  return clear_irq(0xFFFF, error);
}

bool Sx1262::transmit(const std::vector<uint8_t>& payload, uint32_t timeout_ms,
                      std::string& error) {
  if (!configured_) {
    error = "SX1262 not configured";
    return false;
  }
  if (payload.empty() || payload.size() > 255) {
    error = "payload must be 1..255 bytes";
    return false;
  }
  if (!clear_irq(0xFFFF, error)) return false;
  if (!set_packet_params(profile_, payload.size(), error)) return false;

  // WriteBuffer: opcode, offset, then data.
  std::vector<uint8_t> frame;
  frame.reserve(payload.size() + 2);
  frame.push_back(kOpWriteBuffer);
  frame.push_back(0x00);
  frame.insert(frame.end(), payload.begin(), payload.end());
  if (!spi_.transfer(frame.data(), nullptr, frame.size(), error)) return false;
  if (!wait_busy(100, error)) return false;

  // Timeout 0 means "no timeout" for TX; the driver waits on IRQ instead.
  const uint8_t tx[3] = {0x00, 0x00, 0x00};
  if (!write_command(kOpSetTx, tx, sizeof(tx), error)) return false;

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    uint16_t status = 0;
    if (!irq_status(status, error)) return false;
    if (status & kIrqTxDone) break;
    if (status & kIrqTimeout) {
      error = "SX1262 TX timeout";
      clear_irq(0xFFFF, error);
      return false;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      error = "SX1262 TX did not complete";
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return clear_irq(0xFFFF, error);
}

bool Sx1262::receive(RxPacket& packet, uint32_t timeout_ms, std::string& error) {
  if (!configured_) {
    error = "SX1262 not configured";
    return false;
  }
  if (!clear_irq(0xFFFF, error)) return false;
  if (!set_packet_params(profile_, 255, error)) return false;

  // 0xFFFFFF selects continuous RX; the caller's timeout bounds the wait.
  const uint8_t rx[3] = {0xFF, 0xFF, 0xFF};
  if (!write_command(kOpSetRx, rx, sizeof(rx), error)) return false;

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    uint16_t status = 0;
    if (!irq_status(status, error)) return false;
    if (status & kIrqRxDone) {
      packet.crc_ok = (status & kIrqCrcErr) == 0;
      break;
    }
    if (status & kIrqTimeout) {
      // The symbol timeout fired because a preamble never produced a matching
      // sync, i.e. traffic from another network. Re-arm and keep listening
      // rather than abandoning the caller's window.
      if (!clear_irq(0xFFFF, error)) return false;
      if (!write_command(kOpSetRx, rx, sizeof(rx), error)) return false;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      error = "no packet";
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  uint8_t buffer_status[2]{};
  if (!read_command(kOpGetRxBufferStatus, buffer_status, sizeof(buffer_status), error))
    return false;
  const size_t length = buffer_status[0];
  const size_t offset = buffer_status[1];
  if (length == 0) {
    error = "empty packet";
    return false;
  }

  std::vector<uint8_t> tx(length + 3, kNop);
  std::vector<uint8_t> rx_raw(length + 3, 0);
  tx[0] = kOpReadBuffer;
  tx[1] = uint8_t(offset);
  if (!spi_.transfer(tx.data(), rx_raw.data(), tx.size(), error)) return false;
  if (!wait_busy(100, error)) return false;
  packet.payload.assign(rx_raw.begin() + 3, rx_raw.begin() + 3 + length);

  uint8_t packet_status[3]{};
  if (read_command(kOpGetPacketStatus, packet_status, sizeof(packet_status), error)) {
    // RSSI is reported in -0.5 dBm steps, SNR in 0.25 dB steps.
    packet.rssi_dbm = -int(packet_status[0]) / 2;
    packet.snr_db = int(int8_t(packet_status[1])) / 4;
  }
  return clear_irq(0xFFFF, error);
}

bool Sx1262::start_continuous_rx(std::string& error) {
  if (!configured_) {
    error = "SX1262 not configured";
    return false;
  }
  if (!clear_irq(0xFFFF, error)) return false;
  if (!set_packet_params(profile_, 255, error)) return false;
  const uint8_t rx[3] = {0xFF, 0xFF, 0xFF};
  return write_command(kOpSetRx, rx, sizeof(rx), error);
}

const char* Sx1262::variant_name() const {
  switch (variant_) {
    case ChipVariant::sx1261: return "SX1261";
    case ChipVariant::sx1262: return "SX1262";
    case ChipVariant::sx1268: return "SX1268";
    default: return "unknown";
  }
}

int Sx1262::max_tx_dbm() const {
  return (variant_ == ChipVariant::sx1262 || variant_ == ChipVariant::sx1268) ? 22 : 15;
}

bool Sx1262::cad(bool& detected, int& rssi_dbm, uint32_t timeout_ms, std::string& error) {
  if (!configured_) {
    error = "SX1262 not configured";
    return false;
  }
  if (!clear_irq(0xFFFF, error)) return false;
  if (!set_modulation(profile_, error)) return false;

  // CAD on 2 symbols, exit to standby when done, no hardware timeout.
  const uint8_t cad_params[7] = {0x01, 0x16, 0x0A, 0x00, 0x00, 0x00, 0x00};
  if (!write_command(kOpSetCadParams, cad_params, sizeof(cad_params), error)) return false;
  if (!write_command(kOpSetCad, nullptr, 0, error)) return false;

  detected = false;
  rssi_dbm = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    uint16_t status = 0;
    if (!irq_status(status, error)) return false;
    if (status & kIrqCadDone) {
      detected = (status & kIrqCadDetected) != 0;
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      error = "CAD did not complete";
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  uint8_t power = 0;
  if (read_command(kOpGetRssiInst, &power, 1, error)) rssi_dbm = -int(power) / 2;
  return clear_irq(0xFFFF, error);
}

bool Sx1262::read_rssi(int& rssi_dbm, uint32_t settle_ms, std::string& error) {
  if (!configured_) {
    error = "SX1262 not configured";
    return false;
  }
  if (!start_continuous_rx(error)) return false;
  std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms));
  uint8_t raw = 0;
  const bool ok = read_command(kOpGetRssiInst, &raw, 1, error);
  // Reported in -0.5 dBm steps.
  rssi_dbm = -int(raw) / 2;
  std::string ignored;
  standby(ignored);
  return ok;
}

int Sx1262::count_preambles(uint32_t seconds, std::string& error) {
  if (!configured_) {
    error = "SX1262 not configured";
    return -1;
  }
  // Enable PreambleDetected in the global mask; the default profile mask does
  // not include it because normal reception never needs it.
  const uint16_t mask = kIrqPreamble | kIrqRxDone | kIrqTimeout;
  const uint8_t irq[8] = {uint8_t(mask >> 8), uint8_t(mask & 0xFF), uint8_t(mask >> 8),
                          uint8_t(mask & 0xFF), 0x00, 0x00, 0x00, 0x00};
  if (!write_command(kOpSetDioIrqParams, irq, sizeof(irq), error)) return -1;
  if (!clear_irq(0xFFFF, error)) return -1;
  if (!set_packet_params(profile_, 255, error)) return -1;

  const uint8_t rx[3] = {0xFF, 0xFF, 0xFF};
  if (!write_command(kOpSetRx, rx, sizeof(rx), error)) return -1;
  std::string mode_error;
  if (!read_chip_mode(last_mode_, mode_error)) last_mode_ = 0xFF;

  int count = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  while (std::chrono::steady_clock::now() < deadline) {
    uint16_t status = 0;
    if (!irq_status(status, error)) return count;
    if (status & kIrqPreamble) {
      ++count;
      clear_irq(kIrqPreamble, error);
    }
    if (status & (kIrqRxDone | kIrqTimeout)) {
      // A packet with a non-matching sync word never completes, but a timeout or
      // an accidental CRC failure restarts the receiver.
      clear_irq(0xFFFF, error);
      write_command(kOpSetRx, rx, sizeof(rx), error);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return count;
}

bool Sx1262::standby(std::string& error) {  const uint8_t params[1] = {0x00};
  return write_command(kOpSetStandby, params, 1, error);
}

bool Sx1262::standby_xosc(std::string& error) {
  const uint8_t params[1] = {0x01};  // STDBY_XOSC
  return write_command(kOpSetStandby, params, 1, error);
}

bool Sx1262::sleep(std::string& error) {
  const uint8_t params[1] = {0x04};  // retain configuration
  return write_command(kOpSetSleep, params, 1, error);
}

uint32_t Sx1262::airtime_us(const LoraProfile& profile, size_t payload_bytes) {
  const double tsym_us = symbol_time_us(profile.spreading_factor, profile.bandwidth_hz);
  const double preamble = (double(profile.preamble_symbols) + 4.25) * tsym_us;

  const int sf = profile.spreading_factor;
  const int cr = profile.coding_rate;  // denominator: 5..8
  // Coding-rate offset is CR-4 where CR is the denominator minus 4.
  const int cr_offset = std::max(1, cr - 4);
  const int ldro = (tsym_us > 16000.0) ? 1 : 0;
  const int numerator = 8 * int(payload_bytes) - 4 * sf + 28 + 16 /*CRC*/ - 20 * 0 /*explicit header*/;
  const int denominator = 4 * (sf - 2 * ldro);
  const int symbols = 8 + std::max(0, (numerator + denominator - 1) / denominator) * cr_offset;
  const double payload = double(symbols) * tsym_us;
  return uint32_t(std::lround(preamble + payload));
}

}  // namespace orc::zero::mesh
