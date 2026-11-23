#pragma once
// Minimal SX1262 driver for the Cap LoRa-1262 on the Cardputer Zero.
//
// Scope: enough of the chip to bring a CAP up, prove it is really there, and
// move packets in both directions. Protocol framing (MeshCore, Meshtastic)
// lives above this layer.
#include <cstdint>
#include <string>
#include <vector>

#include "linux_bus.hpp"

namespace orc::zero::mesh {

struct LoraProfile {
  uint32_t frequency_hz = 910525000;  // MeshCore US
  uint32_t bandwidth_hz = 62500;
  uint8_t spreading_factor = 7;
  uint8_t coding_rate = 5;  // 4/5
  int tx_dbm = 20;
  uint16_t preamble_symbols = 16;
  uint16_t sync_word = 0x1424;  // MeshCore private network
  // SX126x has an IQ polarity *register* (0x0736) that must agree with the
  // inverted-IQ bit in the packet parameters. RadioLib always writes it; leaving
  // it at the reset value silently prevents sync detection.
  bool invert_iq = false;
  // 32MHz reference source. Most SX1262 modules have a TCXO powered from DIO3;
  // a board with a plain crystal must NOT be given a TCXO setting, or the
  // oscillator never starts and the radio is deaf and off-frequency.
  bool use_tcxo = true;
  uint8_t tcxo_voltage_code = 0x06;  // 3.0 V
  uint32_t tcxo_timeout_ticks = 640; // x 15.625 us = 10 ms
  // Some boards switch the antenna through DIO2, others through an expander.
  // Driving DIO2 on a board that does not use it can leave the path in the wrong
  // state for receive.
  bool use_dio2_rf_switch = true;
};

struct RxPacket {
  std::vector<uint8_t> payload;
  int rssi_dbm = 0;
  int snr_db = 0;
  bool crc_ok = false;
};

// The M5 product is called "Cap LoRa-1262", but the silicon on the tested unit
// reports SX1261. The PA differs: SX1261 tops out at +15 dBm and needs a
// different device selector than the SX1262's +22 dBm, so the datasheet
// signature must drive the PA configuration rather than the product name.
enum class ChipVariant { unknown, sx1261, sx1262, sx1268 };

class Sx1262 {
 public:
  // Radios are exclusive: two openers on the same CAP corrupt each other.
  Sx1262() = default;
  Sx1262(const Sx1262&) = delete;
  Sx1262& operator=(const Sx1262&) = delete;
  ~Sx1262() { close(); }

  // Opens SPI and the three control lines, then resets and configures the chip.
  bool open(std::string& error) { return open(kDefaultSpi, kDefaultChip, kDefaultReset,
                                              kDefaultBusy, kDefaultIrq, error); }
  bool open(const char* spi_path, const char* gpio_chip, int reset_pin, int busy_pin,
            int irq_pin, std::string& error);
  void close();
  bool is_open() const { return spi_.valid() && busy_.valid(); }

  // Cap LoRa-1262 wiring, confirmed against the board and the vendor image.
  static constexpr const char* kDefaultSpi = "/dev/spidev0.1";
  static constexpr const char* kDefaultChip = "/dev/gpiochip0";
  static constexpr int kDefaultReset = 26;
  static constexpr int kDefaultBusy = 22;
  static constexpr int kDefaultIrq = 23;

  // Brings the chip to a known standby state and programs the modem.
  bool configure(const LoraProfile& profile, std::string& error);
  bool apply_profile(const LoraProfile& profile, std::string& error);

  // 16-byte silicon signature at register 0x0320; starts with "SX126" on real
  // hardware and cannot be produced by a floating bus.
  bool read_signature(std::string& signature, std::string& error);
  bool read_chip_mode(uint8_t& mode, std::string& error);
  // Device error flags (opcode 0x17): calibration, oscillator start, PLL lock
  // and PA ramp failures. A radio can look "configured" while these are set.
  bool read_device_errors(uint16_t& errors, std::string& error);
  bool clear_device_errors(std::string& error);
  static const char* describe_device_errors(uint16_t errors);

  bool transmit(const std::vector<uint8_t>& payload, uint32_t timeout_ms, std::string& error);
  bool receive(RxPacket& packet, uint32_t timeout_ms, std::string& error);
  bool start_continuous_rx(std::string& error);
  // Channel activity detection: reports whether a LoRa preamble was heard at the
  // configured SF/BW. Unlike receive(), this does not depend on the other
  // network's sync word, so it can prove RF presence while the sync word is
  // still unknown.
  bool cad(bool& detected, int& rssi_dbm, uint32_t timeout_ms, std::string& error);
  // Instantaneous channel RSSI. Meaningful while the receiver is on, so this
  // enters continuous RX, lets the front end settle, then reads and stops.
  bool read_rssi(int& rssi_dbm, uint32_t settle_ms, std::string& error);
  // Counts LoRa preambles at the configured SF/BW for a while, ignoring the sync
  // word. Any node transmitting at these parameters is counted even if its
  // network parameters differ, which separates "no traffic" from "wrong sync".
  // Returns -1 on error.
  int count_preambles(uint32_t seconds, std::string& error);
  // Chip mode observed right after the receive command was issued.
  uint8_t last_mode() const { return last_mode_; }
  bool standby(std::string& error);
  // Forces STDBY_XOSC: the chip reports mode 3 only if the 32MHz reference
  // actually starts. Mode 2 (RC) means it fell back or failed.
  bool standby_xosc(std::string& error);
  bool sleep(std::string& error);

  // Detected from the silicon signature during configure().
  ChipVariant variant() const { return variant_; }
  const char* variant_name() const;
  int max_tx_dbm() const;
  // Last power actually programmed, after clamping to the variant's maximum.
  int applied_tx_dbm() const { return applied_tx_dbm_; }

  // LoRa airtime for a payload, used by the transmit budget.
  static uint32_t airtime_us(const LoraProfile& profile, size_t payload_bytes);

 private:
  bool reset(std::string& error);
  bool wait_busy(uint32_t timeout_ms, std::string& error);
  bool write_command(uint8_t op, const uint8_t* params, size_t count, std::string& error);
  bool read_command(uint8_t op, uint8_t* out, size_t count, std::string& error);
  bool read_register(uint16_t address, uint8_t* out, size_t count, std::string& error);
  bool write_register(uint16_t address, const uint8_t* data, size_t count, std::string& error);
  bool clear_irq(uint16_t mask, std::string& error);
  bool irq_status(uint16_t& status, std::string& error);
  bool set_modulation(const LoraProfile& profile, std::string& error);
  bool set_packet_params(const LoraProfile& profile, size_t payload, std::string& error);

  SpiBus spi_;
  GpioLine reset_;
  GpioLine busy_;
  GpioLine irq_;
  LoraProfile profile_{};
  bool configured_ = false;
  ChipVariant variant_ = ChipVariant::unknown;
  int applied_tx_dbm_ = 0;
  uint8_t last_mode_ = 0xFF;
};

}  // namespace orc::zero::mesh
