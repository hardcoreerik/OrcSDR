#pragma once
// Cap LoRa-1262 board support: power rail, antenna switch, and CAP detection.
//
// Board facts, cross-checked against M5Stack's documentation and the wiring the
// vendor image uses:
//   - the CAP is powered from the ext_5v_out LED-class rail;
//   - the antenna switch hangs off a PI4IOE5V6408 I/O expander at I2C 0x43,
//     port P0, and must be driven high before transmitting;
//   - the legacy Cap LoRa868 has no expander at all.
// Nothing is written to the expander's other pins, and both the switch and the
// rail are restored when the object is destroyed.
#include <cstdint>
#include <string>
#include <vector>

namespace orc::zero::mesh {

class CapLora {
 public:
  CapLora() = default;
  CapLora(const CapLora&) = delete;
  CapLora& operator=(const CapLora&) = delete;
  ~CapLora() { end(); }

  // Applies power, detects the expander and enables the antenna switch. With
  // enable_switch false the expander is left exactly as found, which is the only
  // way to tell whether our P0 writes help or hurt.
  bool begin(std::string& error, bool enable_switch = true);
  // Restores the switch, then the rail state it found.
  void end();

  // "CAP LoRa-1262", "Legacy CAP LoRa868", or "Unknown CAP".
  const std::string& model() const { return model_; }
  bool has_expander() const { return expander_fd_ >= 0; }

  // Reads the SX126x silicon signature; separates a real CAP from a floating bus.
  bool confirm_radio(class Sx1262& radio, std::string& error);

  // Drives the antenna switch to an explicit state. begin() enables it; this
  // exists so the polarity can be tested on hardware rather than assumed.
  bool set_antenna_switch(bool high, std::string& error);

 private:
  struct RailState {
    std::string path;
    std::string value;
  };

  bool read_register(uint8_t reg, uint8_t& value, std::string& error);
  bool write_register(uint8_t reg, uint8_t value, std::string& error);
  bool enable_antenna_switch(std::string& error);
  void restore_expander();

  int expander_fd_ = -1;
  uint8_t saved_[4] = {0, 0, 0, 0};  // registers 3, 5, 7, 11
  bool switch_enabled_ = false;
  std::vector<RailState> rails_changed_;
  std::string model_ = "Unknown CAP";
};

}  // namespace orc::zero::mesh
