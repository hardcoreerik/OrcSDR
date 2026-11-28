#include "pluto_receiver.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

#include <iio.h>

namespace orc::linux_sdr {
namespace {

// The AD9363 delivers 12 significant bits in an int16, so one u8 step is 16 LSB.
// This mirrors the conversion validated in esp-pluto-sdr's pluto_iq.c.
constexpr int kCu8Shift = 4;

std::uint8_t to_cu8(int value) {
  const int scaled = (value >> kCu8Shift) + 128;
  return std::uint8_t(std::clamp(scaled, 0, 255));
}


// 64 KB lookup: int16 (12 significant bits) -> CU8. Built once, then every sample
// is one indexed load instead of a shift, an add and two compares.
const std::uint8_t* cu8_table() {
  static std::uint8_t table[65536];
  static bool ready = false;
  if (!ready) {
    for (int i = 0; i < 65536; ++i) {
      const int scaled = (std::int16_t(i) >> kCu8Shift) + 128;
      table[i] = std::uint8_t(scaled < 0 ? 0 : (scaled > 255 ? 255 : scaled));
    }
    ready = true;
  }
  return table;
}
}  // namespace

PlutoReceiver::~PlutoReceiver() { close(); }

void PlutoReceiver::close() noexcept {
  if (buffer_) {
    iio_buffer_destroy(static_cast<iio_buffer*>(buffer_));
    buffer_ = nullptr;
  }
  if (ctx_) {
    iio_context_destroy(static_cast<iio_context*>(ctx_));
    ctx_ = nullptr;
  }
  phy_ = nullptr;
  rx_ = nullptr;
  pending_.clear();
  pending_offset_ = 0;
}

PlutoInfo PlutoReceiver::detect() {
  PlutoInfo info;
  for (const char* uri : {"", "usb:", "ip:fishball.local"}) {
    iio_context* ctx = iio_create_context_from_uri(uri);
    if (!ctx) continue;
    if (iio_context_find_device(ctx, "ad9361-phy") &&
        iio_context_find_device(ctx, "cf-ad9361-lpc")) {
      info.found = true;
      info.uri = uri;
      const char* description = iio_context_get_description(ctx);
      info.name = description ? description : "PlutoSDR";
      iio_context_destroy(ctx);
      return info;
    }
    iio_context_destroy(ctx);
  }
  return info;
}

void PlutoReceiver::set_attr(const char* device, const char* channel, bool output,
                             const char* attr, long long value) {
  auto* ctx = static_cast<iio_context*>(ctx_);
  iio_device* dev = iio_context_find_device(ctx, device);
  if (!dev) throw std::runtime_error(std::string("missing IIO device ") + device);
  iio_channel* ch = iio_device_find_channel(dev, channel, output);
  if (!ch) throw std::runtime_error(std::string("missing channel ") + channel);
  if (iio_channel_attr_write_longlong(ch, attr, value) < 0)
    throw std::runtime_error(std::string("cannot write ") + attr + " on " + device);
}

long long PlutoReceiver::get_attr(const char* device, const char* channel, bool output,
                                  const char* attr, long long fallback) const {
  auto* ctx = static_cast<iio_context*>(ctx_);
  iio_device* dev = iio_context_find_device(ctx, device);
  if (!dev) return fallback;
  iio_channel* ch = iio_device_find_channel(dev, channel, output);
  if (!ch) return fallback;
  long long value = fallback;
  if (iio_channel_attr_read_longlong(ch, attr, &value) < 0) return fallback;
  return value;
}

void PlutoReceiver::open(const std::string& uri) {
  close();
  const std::string chosen = uri.empty() ? "usb:" : uri;
  iio_context* ctx = iio_create_context_from_uri(chosen.c_str());
  if (!ctx && uri.empty()) ctx = iio_create_context_from_uri("ip:fishball.local");
  if (!ctx) throw std::runtime_error("no PlutoSDR context on '" + chosen + "'");
  ctx_ = ctx;
  phy_ = iio_context_find_device(ctx, "ad9361-phy");
  rx_ = iio_context_find_device(ctx, "cf-ad9361-lpc");
  if (!phy_ || !rx_) {
    close();
    throw std::runtime_error("Pluto context lacks ad9361-phy / cf-ad9361-lpc");
  }
}

void PlutoReceiver::configure(std::uint32_t freq_hz, std::uint32_t rate_sps) {
  if (!ctx_) throw std::runtime_error("PlutoReceiver::configure before open");
  requested_rate_ = rate_sps;

  // The AD9363 cannot sample below ~2.083 MS/s, so a slower request is met by
  // sampling faster and decimating by an integer factor. Smallest factor wins so
  // the decimation filter stays as gentle as possible.
  int decim = 1;
  while (static_cast<std::uint64_t>(rate_sps) * decim < kMinDeviceRate) ++decim;
  decim_ = decim;
  const std::uint32_t device_rate = rate_sps * decim;

  set_attr("ad9361-phy", "voltage0", false, "sampling_frequency", device_rate);
  // Bandwidth a little wider than the rate so the edges are not brick-walled.
  set_attr("ad9361-phy", "voltage0", false, "rf_bandwidth",
           std::min<std::uint32_t>(device_rate + device_rate / 4, 56000000));
  retune(freq_hz);

  // Trust the device's answer, not the request: the driver rounds to rates it can
  // actually produce (2083333 is refused outright, 2400000 comes back 2399999).
  device_rate_ = std::uint32_t(
      get_attr("ad9361-phy", "voltage0", false, "sampling_frequency", device_rate));
  delivered_rate_ = device_rate_ / std::uint32_t(decim_);

  set_tuner_gain_auto();
}

void PlutoReceiver::retune(std::uint32_t freq_hz) {
  if (!ctx_) throw std::runtime_error("PlutoReceiver::retune before open");
  set_attr("ad9361-phy", "altvoltage0", true, "frequency", freq_hz);
  freq_hz_ = std::uint32_t(
      get_attr("ad9361-phy", "altvoltage0", true, "frequency", freq_hz));
}

void PlutoReceiver::set_tuner_gain_auto() {
  auto* ctx = static_cast<iio_context*>(ctx_);
  iio_device* phy = iio_context_find_device(ctx, "ad9361-phy");
  iio_channel* ch = phy ? iio_device_find_channel(phy, "voltage0", false) : nullptr;
  if (!ch) return;
  if (iio_channel_attr_write(ch, "gain_control_mode", "slow_attack") >= 0) {
    gain_auto_ = true;
    gain_tenth_db_ = int(get_attr("ad9361-phy", "voltage0", false, "hardwaregain", 0) * 10);
  }
}

void PlutoReceiver::set_tuner_gain_tenth_db(int tenth_db) {
  auto* ctx = static_cast<iio_context*>(ctx_);
  iio_device* phy = iio_context_find_device(ctx, "ad9361-phy");
  iio_channel* ch = phy ? iio_device_find_channel(phy, "voltage0", false) : nullptr;
  if (!ch) return;
  if (iio_channel_attr_write(ch, "gain_control_mode", "manual") < 0) return;
  const double db = std::clamp(tenth_db / 10.0, -1.0, 73.0);
  if (iio_channel_attr_write_double(ch, "hardwaregain", db) >= 0) {
    gain_auto_ = false;
    gain_tenth_db_ = tenth_db;
  }
}

int PlutoReceiver::tuner_gain_tenth_db() const {
  return int(get_attr("ad9361-phy", "voltage0", false, "hardwaregain", gain_tenth_db_) * 10);
}

std::size_t PlutoReceiver::read_cu8(std::uint8_t* data, std::size_t max_bytes) {
  if (!ctx_ || !rx_) return 0;
  auto* ctx = static_cast<iio_context*>(ctx_);
  iio_device* rx = static_cast<iio_device*>(rx_);
  iio_channel* ch_i = iio_device_find_channel(rx, "voltage0", false);
  iio_channel* ch_q = iio_device_find_channel(rx, "voltage1", false);
  if (!ch_i || !ch_q) return 0;

  while (pending_.size() - pending_offset_ < max_bytes) {
    if (!buffer_) {
      iio_channel_enable(ch_i);
      iio_channel_enable(ch_q);
      iio_buffer* buffer = iio_device_create_buffer(rx, kBufferSamples, false);
      if (!buffer) return 0;
      buffer_ = buffer;
    }
    auto* buffer = static_cast<iio_buffer*>(buffer_);
    const ssize_t got = iio_buffer_refill(buffer);
    if (got < 0) return 0;

    const char* start = static_cast<const char*>(iio_buffer_start(buffer));
    const char* end = static_cast<const char*>(iio_buffer_end(buffer));
    const std::ptrdiff_t step = iio_buffer_step(buffer);
    // Two channels of int16 per complex sample; with the channels enabled this is
    // the interleaved I/Q pair, matching esp-pluto-sdr's reading of the wire.
    // Table-driven conversion. The scalar clamp was the bottleneck: at 6.5 MS/s
    // the host reached only 56% of nominal and the per-sample branch dominated.
    const std::uint8_t* table = cu8_table();
    const std::ptrdiff_t sample_stride = step * std::ptrdiff_t(decim_);
    if (pending_.empty())
      pending_.reserve(std::size_t((end - start) / sample_stride + 1) * 2);
    for (const char* pos = start; pos + sizeof(std::int16_t) * 2 <= end;
         pos += sample_stride) {
      const auto* pair = reinterpret_cast<const std::int16_t*>(pos);
      pending_.push_back(table[std::uint16_t(pair[0])]);
      pending_.push_back(table[std::uint16_t(pair[1])]);
    }
  }

  const std::size_t available = pending_.size() - pending_offset_;
  const std::size_t take = std::min(max_bytes, available);
  std::memcpy(data, pending_.data() + pending_offset_, take);
  pending_offset_ += take;
  if (pending_offset_ >= pending_.size()) {
    pending_.clear();
    pending_offset_ = 0;
  } else if (pending_offset_ > 1u << 20) {
    pending_.erase(pending_.begin(),
                   pending_.begin() + std::ptrdiff_t(pending_offset_));
    pending_offset_ = 0;
  }
  return take;
}

}  // namespace orc::linux_sdr