#pragma once
// Linux userspace access to the two buses the Cap LoRa-1262 uses: SPI for the
// SX1262 and the GPIO character device for reset/busy/IRQ.
//
// There is no SX126x kernel driver on the Cardputer Zero image, so the radio is
// driven from here. libgpiod headers are not installed, so the GPIO line is
// requested through the kernel's GPIO v2 uAPI directly; that keeps the receiver
// free of an added build dependency.
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace orc::zero::mesh {

// --- GPIO v2 uAPI -----------------------------------------------------------
// Structures mirror linux/gpio.h; keeping them local avoids requiring the
// kernel headers package on the build host.
struct GpioLineValues {
  uint64_t bits = 0;
  uint64_t mask = 0;
};
struct GpioLineAttribute {
  uint32_t id = 0;
  uint32_t padding = 0;
  union {
    uint64_t flags;
    uint32_t values[2];
    uint32_t debounce_period_us;
  } value{};
};
struct GpioLineConfigAttribute {
  GpioLineAttribute attr{};
  uint64_t mask = 0;
};
struct GpioLineConfig {
  uint64_t flags = 0;
  uint32_t num_attrs = 0;
  uint32_t padding[5]{};
  GpioLineConfigAttribute attrs[10]{};
};
struct GpioLineRequest {
  uint32_t offsets[64]{};
  char consumer[32]{};
  GpioLineConfig config{};
  uint32_t num_lines = 0;
  uint32_t event_buffer_size = 0;
  uint32_t padding[5]{};
  int32_t fd = -1;
};

// GPIO v2 line flags. Note these are _BITULL(2) and _BITULL(3); the numbering
// differs from the older v1 uAPI, and passing the v1 values makes the kernel
// reject the request with EINVAL.
inline constexpr uint64_t kGpioFlagInput = uint64_t(1) << 2;
inline constexpr uint64_t kGpioFlagOutput = uint64_t(1) << 3;

inline constexpr unsigned long kGpioV2GetLineIoctl =
    _IOWR(0xB4, 0x07, struct GpioLineRequest);
inline constexpr unsigned long kGpioV2GetValuesIoctl =
    _IOWR(0xB4, 0x0E, struct GpioLineValues);
inline constexpr unsigned long kGpioV2SetValuesIoctl =
    _IOWR(0xB4, 0x0F, struct GpioLineValues);

// One requested line. Movable, not copyable.
class GpioLine {
 public:
  GpioLine() = default;
  GpioLine(const GpioLine&) = delete;
  GpioLine& operator=(const GpioLine&) = delete;
  ~GpioLine() { close(); }

  bool open(const char* chip, uint32_t offset, bool output, bool initial_high,
            std::string& error) {
    close();
    const int chip_fd = ::open(chip, O_RDWR | O_CLOEXEC);
    if (chip_fd < 0) {
      error = std::string("open ") + chip + ": " + std::strerror(errno);
      return false;
    }
    GpioLineRequest request{};
    request.offsets[0] = offset;
    std::snprintf(request.consumer, sizeof(request.consumer), "orcsdr-zero");
    request.num_lines = 1;
    request.config.flags = output ? kGpioFlagOutput : kGpioFlagInput;
    if (::ioctl(chip_fd, kGpioV2GetLineIoctl, &request) < 0) {
      error = std::string("request GPIO ") + std::to_string(offset) + ": " +
              std::strerror(errno);
      ::close(chip_fd);
      return false;
    }
    ::close(chip_fd);
    fd_ = request.fd;
    output_ = output;
    // The v2 uAPI carries no initial-value flag; drive it right after claiming.
    if (output && !set(initial_high)) {
      error = std::string("set initial level on GPIO ") + std::to_string(offset);
      close();
      return false;
    }
    return true;
  }

  bool set(bool high) {
    if (fd_ < 0 || !output_) return false;
    GpioLineValues values{};
    values.bits = high ? 1u : 0u;
    values.mask = 1u;
    return ::ioctl(fd_, kGpioV2SetValuesIoctl, &values) == 0;
  }

  bool get(bool& high) const {
    if (fd_ < 0) return false;
    GpioLineValues values{};
    values.mask = 1u;
    if (::ioctl(fd_, kGpioV2GetValuesIoctl, const_cast<GpioLineValues*>(&values)) < 0)
      return false;
    high = (values.bits & 1u) != 0;
    return true;
  }

  void close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
  }

  bool valid() const { return fd_ >= 0; }

 private:
  int fd_ = -1;
  bool output_ = false;
};

// --- SPI --------------------------------------------------------------------
class SpiBus {
 public:
  SpiBus() = default;
  SpiBus(const SpiBus&) = delete;
  SpiBus& operator=(const SpiBus&) = delete;
  ~SpiBus() { close(); }

  bool open(const char* path, uint32_t hz, std::string& error) {
    close();
    fd_ = ::open(path, O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
      error = std::string("open ") + path + ": " + std::strerror(errno);
      return false;
    }
    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    if (::ioctl(fd_, SPI_IOC_WR_MODE, &mode) < 0 ||
        ::ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ::ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &hz) < 0) {
      error = std::string("configure ") + path + ": " + std::strerror(errno);
      close();
      return false;
    }
    return true;
  }

  // Full-duplex transfer; either buffer may be null.
  bool transfer(const uint8_t* tx, uint8_t* rx, size_t len, std::string& error) const {
    if (fd_ < 0) {
      error = "SPI not open";
      return false;
    }
    spi_ioc_transfer transfer{};
    transfer.tx_buf = reinterpret_cast<unsigned long>(tx);
    transfer.rx_buf = reinterpret_cast<unsigned long>(rx);
    transfer.len = static_cast<uint32_t>(len);
    transfer.speed_hz = speed_hz_;
    transfer.bits_per_word = 8;
    if (::ioctl(fd_, SPI_IOC_MESSAGE(1), &transfer) < 0) {
      error = std::string("SPI transfer: ") + std::strerror(errno);
      return false;
    }
    return true;
  }

  void close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
  }

  bool valid() const { return fd_ >= 0; }

 private:
  int fd_ = -1;
  uint32_t speed_hz_ = 0;
};

}  // namespace orc::zero::mesh
