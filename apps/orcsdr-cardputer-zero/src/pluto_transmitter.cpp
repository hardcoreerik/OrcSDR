#include "pluto_transmitter.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>

#include <iio.h>

namespace orc::linux_sdr {
namespace {

// TX takes int16 samples; the AD9363 uses 12 significant bits, so CU8 steps map
// back with the same >>4 scale the receiver divides by.
std::int16_t from_cu8(std::uint8_t value) {
  return std::int16_t((int(value) - 128) << 4);
}

constexpr std::uint32_t kMaxTxRate = 61440000;

}  // namespace

PlutoTransmitter::~PlutoTransmitter() { close(); }

void PlutoTransmitter::close() noexcept {
  if (buffer_) {
    iio_buffer_destroy(static_cast<iio_buffer*>(buffer_));
    buffer_ = nullptr;
  }
  if (staged_) {
    delete[] staged_;
    staged_ = nullptr;
  }
  if (ctx_) {
    iio_context_destroy(static_cast<iio_context*>(ctx_));
    ctx_ = nullptr;
  }
  phy_ = nullptr;
  tx_ = nullptr;
}

void PlutoTransmitter::open(const std::string& uri) {
  close();
  const std::string chosen = uri.empty() ? "usb:" : uri;
  iio_context* ctx = iio_create_context_from_uri(chosen.c_str());
  if (!ctx && uri.empty()) ctx = iio_create_context_from_uri("ip:fishball.local");
  if (!ctx) throw std::runtime_error("no PlutoSDR context on '" + chosen + "'");
  ctx_ = ctx;
  phy_ = iio_context_find_device(ctx, "ad9361-phy");
  tx_ = iio_context_find_device(ctx, "cf-ad9361-dds-core-lpc");
  if (!phy_ || !tx_) {
    close();
    throw std::runtime_error("Pluto context lacks ad9361-phy / cf-ad9361-dds-core-lpc");
  }
  // Come up muted. Done here as well as in configure() so that even a caller that
  // forgets to configure cannot transmit at a default level.
  set_attenuation_db(kSafeAttenuationDb);
}

void PlutoTransmitter::configure(std::uint32_t freq_hz, std::uint32_t rate_sps) {
  if (!ctx_) throw std::runtime_error("PlutoTransmitter::configure before open");
  auto* ctx = static_cast<iio_context*>(ctx_);
  iio_device* phy = static_cast<iio_device*>(phy_);

  iio_channel* lo = iio_device_find_channel(phy, "altvoltage1", true);
  if (!lo) throw std::runtime_error("missing TX_LO channel");
  if (iio_channel_attr_write_longlong(lo, "frequency", freq_hz) < 0)
    throw std::runtime_error("cannot set TX_LO");
  long long actual = freq_hz;
  if (iio_channel_attr_read_longlong(lo, "frequency", &actual) >= 0) freq_hz_ = std::uint32_t(actual);

  const std::uint32_t rate = std::clamp<std::uint32_t>(rate_sps, 2083333, kMaxTxRate);
  iio_channel* out = iio_device_find_channel(phy, "voltage0", true);
  if (!out) throw std::runtime_error("missing TX voltage0 channel");
  if (iio_channel_attr_write_longlong(out, "sampling_frequency", rate) < 0)
    throw std::runtime_error("cannot set TX sampling_frequency");
  long long actual_rate = rate;
  if (iio_channel_attr_read_longlong(out, "sampling_frequency", &actual_rate) >= 0)
    rate_sps_ = std::uint32_t(actual_rate);
  // Match the analogue bandwidth to the rate so the DAC is not left wide open.
  iio_channel_attr_write_longlong(out, "rf_bandwidth",
                                  std::min<std::uint32_t>(rate + rate / 4, 40000000));

  set_attenuation_db(kSafeAttenuationDb);
}

void PlutoTransmitter::set_attenuation_db(double db) {
  attenuation_db_ = std::clamp(db, kSafeAttenuationDb, 0.0);
  if (!ctx_) return;
  iio_device* phy = static_cast<iio_device*>(phy_);
  if (!phy) return;
  iio_channel* out = iio_device_find_channel(phy, "voltage0", true);
  if (!out) return;
  iio_channel_attr_write_double(out, "hardwaregain", attenuation_db_);
}

std::size_t PlutoTransmitter::write_cu8(const std::uint8_t* data, std::size_t bytes) {
  if (!ctx_ || !tx_ || !bytes) return 0;
  auto* ctx = static_cast<iio_context*>(ctx_);
  iio_device* tx = static_cast<iio_device*>(tx_);
  iio_channel* ch_i = iio_device_find_channel(tx, "voltage0", true);
  iio_channel* ch_q = iio_device_find_channel(tx, "voltage1", true);
  if (!ch_i || !ch_q) return 0;

  const std::size_t pairs = bytes / 2;
  const std::size_t chunk = std::min(pairs, kBufferSamples);
  if (!buffer_) {
    iio_channel_enable(ch_i);
    iio_channel_enable(ch_q);
    iio_buffer* buffer = iio_device_create_buffer(tx, chunk, false);
    if (!buffer) return 0;
    buffer_ = buffer;
    staged_ = new std::uint8_t[chunk * sizeof(std::int16_t) * 2];
  }

  auto* buffer = static_cast<iio_buffer*>(buffer_);
  char* start = static_cast<char*>(iio_buffer_start(buffer));
  auto* out = reinterpret_cast<std::int16_t*>(start);
  for (std::size_t n = 0; n < chunk; ++n) {
    out[n * 2 + 0] = from_cu8(data[n * 2 + 0]);
    out[n * 2 + 1] = from_cu8(data[n * 2 + 1]);
  }
  if (iio_buffer_push(buffer) < 0) return 0;
  samples_written_ += chunk;
  // Report only the bytes actually consumed; the caller loops for the rest.
  return chunk * 2;
}

}  // namespace orc::linux_sdr
