// Native Linux CU8 producer/decoder pipeline. This is a local service primitive,
// not a second SDR decoder implementation or a network protocol.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>

namespace orc::linux_sdr {
enum class LiveDecoder { acars, adsb };
struct LiveCaptureOptions {
  LiveDecoder decoder=LiveDecoder::acars;
  std::uint32_t device_index=0;
  std::uint32_t center_hz=131490000;
  std::uint32_t sample_rate_sps=240000;
  std::uint32_t max_blocks=32; // bounded so the CLI terminates
  std::size_t complex_samples_per_block=32768;
};
struct LiveCaptureResult {
  std::uint64_t complex_samples_read=0;
  std::uint64_t complex_samples_decoded=0;
  std::uint64_t known_queue_dropped_samples=0;
  std::uint64_t decoder_resets=0;
  std::uint64_t short_reads=0;
  std::uint64_t decoded_messages=0;
  std::uint32_t applied_center_hz=0;
  std::uint32_t applied_rate_sps=0;
  // This is intentionally false: ordinary librtlsdr reads cannot tell us
  // exact lost samples upstream of the application queue.
  bool upstream_usb_loss_known=false;
};
using BlockReader=std::function<std::size_t(std::uint8_t*,std::size_t)>;
// Exact CU8 format is mandatory. 0 bytes from read means clean end of fixture
// (real hardware read failures throw); a short read remains an observed short read.
// A single producer thread never invokes decoder or blocking UI callbacks.
LiveCaptureResult run_live_blocks(const LiveCaptureOptions& options,
                                   const BlockReader& read,
                                   std::ostream& events,
                                   std::size_t queue_blocks=8);
LiveCaptureResult run_librtlsdr_blocks(const LiveCaptureOptions& options,
                                      std::ostream& events);
}