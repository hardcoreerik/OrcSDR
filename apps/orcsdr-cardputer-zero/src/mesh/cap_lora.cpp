#include "cap_lora.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "sx1262.hpp"

namespace orc::zero::mesh {
namespace {

constexpr int kI2cSlaveIoctl = 0x0703;  // I2C_SLAVE
constexpr const char* kExpanderPath = "/dev/i2c-1";
constexpr uint8_t kExpanderAddress = 0x43;
constexpr const char* kRails[] = {"/sys/class/leds/ext_5v_out/brightness",
                                 "/sys/class/leds/grove_5v_out/brightness"};

// PI4IOE5V6408 registers used for the antenna switch. The order below mirrors
// the working sequence: latch the output first, then release the pull and set
// the direction, so P0 never floats while switching.
constexpr uint8_t kRegOutput = 5;
constexpr uint8_t kRegPull = 11;
constexpr uint8_t kRegDirection = 3;
constexpr uint8_t kRegLatch = 7;

bool read_text(const char* path, std::string& out) {
  std::ifstream input(path);
  if (!input) return false;
  std::getline(input, out);
  return true;
}

bool write_text(const char* path, const std::string& value) {
  std::ofstream output(path);
  if (!output) return false;
  output << value;
  return output.good();
}

}  // namespace

bool CapLora::begin(std::string& error, bool enable_switch) {
  // Power the CAP. The vendor images gate the rail through an LED-class device
  // and the udev rule makes it group-writable, so no privilege is needed.
  for (const char* path : kRails) {
    std::string value;
    if (!read_text(path, value)) continue;  // some images keep the rail always on
    if (value == "1") continue;
    if (!write_text(path, "1")) {
      error = std::string("cannot enable CAP power at ") + path;
      return false;
    }
    rails_changed_.push_back({path, value});
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Probe the expander. A legacy CAP simply does not answer, which is not an
  // error; a permission failure or a wrong ID is.
  expander_fd_ = ::open(kExpanderPath, O_RDWR | O_CLOEXEC);
  if (expander_fd_ >= 0) {
    if (::ioctl(expander_fd_, kI2cSlaveIoctl, int(kExpanderAddress)) < 0) {
      error = std::string("select I2C address 0x43: ") + std::strerror(errno);
      end();
      return false;
    }
    uint8_t id = 0;
    bool answered = false;
    for (int attempt = 0; attempt < 10 && !answered; ++attempt) {
      std::string ignored;
      if (read_register(1, id, ignored)) {
        answered = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!answered) {
      // No expander: treat as the legacy CAP rather than failing outright.
      ::close(expander_fd_);
      expander_fd_ = -1;
      model_ = "Legacy CAP LoRa868";
    } else if ((id & 0xFC) != 0xA0) {
      error = "unrecognised CAP expander at 0x43 (id 0x" + std::to_string(id) + ")";
      end();
      return false;
    } else {
      const uint8_t registers[4] = {kRegDirection, kRegOutput, kRegLatch, kRegPull};
      for (int i = 0; i < 4; ++i) {
        if (!read_register(registers[i], saved_[i], error)) {
          end();
          return false;
        }
      }
      if (enable_switch && !enable_antenna_switch(error)) {
        end();
        return false;
      }
      model_ = "CAP LoRa-1262";
    }
  } else if (errno == ENOENT) {
    model_ = "Legacy CAP LoRa868";
  } else {
    error = std::string("open ") + kExpanderPath + ": " + std::strerror(errno);
    end();
    return false;
  }
  return true;
}

bool CapLora::enable_antenna_switch(std::string& error) {
  return set_antenna_switch(true, error);
}

bool CapLora::set_antenna_switch(bool high, std::string& error) {
  if (expander_fd_ < 0) return true;  // legacy CAP has no expander
  uint8_t output = 0, direction = 0, latch = 0, pull = 0;
  if (!read_register(kRegOutput, output, error) || !read_register(kRegDirection, direction, error) ||
      !read_register(kRegLatch, latch, error) || !read_register(kRegPull, pull, error)) {
    return false;
  }
  // Latch the level first, then make the pin an output with the pull released,
  // so P0 never floats while switching.
  const uint8_t next_output = high ? uint8_t(output | 0x01) : uint8_t(output & ~0x01);
  if (!write_register(kRegOutput, next_output, error)) return false;
  if (!write_register(kRegPull, uint8_t(pull & ~0x01), error)) return false;
  if (!write_register(kRegDirection, uint8_t(direction | 0x01), error)) return false;
  if (!write_register(kRegLatch, uint8_t(latch & ~0x01), error)) return false;

  uint8_t output_now = 0, direction_now = 0, latch_now = 0xFF;
  if (!read_register(kRegOutput, output_now, error) ||
      !read_register(kRegDirection, direction_now, error) ||
      !read_register(kRegLatch, latch_now, error)) {
    return false;
  }
  if (((output_now & 0x01) != 0) != high || (direction_now & 0x01) == 0 || (latch_now & 0x01) != 0) {
    error = "CAP antenna switch readback failed";
    return false;
  }
  switch_enabled_ = high;
  return true;
}

bool CapLora::confirm_radio(Sx1262& radio, std::string& error) {
  std::string signature;
  if (!radio.read_signature(signature, error)) return false;
  if (signature.rfind("SX126", 0) != 0) {
    error = "SX126x signature not recognised: " + signature;
    return false;
  }
  if (!has_expander()) model_ = "Legacy CAP LoRa868";
  return true;
}

bool CapLora::read_register(uint8_t reg, uint8_t& value, std::string& error) {
  if (expander_fd_ < 0) {
    error = "CAP expander not open";
    return false;
  }
  if (::write(expander_fd_, &reg, 1) != 1) {
    error = std::string("expander write: ") + std::strerror(errno);
    return false;
  }
  uint8_t raw = 0;
  if (::read(expander_fd_, &raw, 1) != 1) {
    error = std::string("expander read: ") + std::strerror(errno);
    return false;
  }
  value = raw;
  return true;
}

bool CapLora::write_register(uint8_t reg, uint8_t value, std::string& error) {
  if (expander_fd_ < 0) {
    error = "CAP expander not open";
    return false;
  }
  const uint8_t frame[2] = {reg, value};
  if (::write(expander_fd_, frame, 2) != 2) {
    error = std::string("expander write: ") + std::strerror(errno);
    return false;
  }
  return true;
}

void CapLora::restore_expander() {
  if (expander_fd_ < 0) return;
  std::string ignored;
  if (switch_enabled_) {
    // Disable the switch before restoring the surrounding configuration.
    uint8_t latch = 0;
    if (read_register(kRegLatch, latch, ignored))
      write_register(kRegLatch, uint8_t(latch | 0x01), ignored);
  }
  const uint8_t registers[4] = {kRegDirection, kRegOutput, kRegLatch, kRegPull};
  for (int i = 0; i < 4; ++i) {
    uint8_t current = 0;
    if (!read_register(registers[i], current, ignored)) continue;
    const uint8_t restored = uint8_t((current & ~0x01) | (saved_[i] & 0x01));
    write_register(registers[i], restored, ignored);
  }
  switch_enabled_ = false;
}

void CapLora::end() {
  restore_expander();
  if (expander_fd_ >= 0) {
    ::close(expander_fd_);
    expander_fd_ = -1;
  }
  // Restore the rail state that was found, in reverse order.
  for (auto it = rails_changed_.rbegin(); it != rails_changed_.rend(); ++it)
    write_text(it->path.c_str(), it->value);
  rails_changed_.clear();
}

}  // namespace orc::zero::mesh
