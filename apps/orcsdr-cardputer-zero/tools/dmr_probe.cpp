// Runs the DMR receive chain against the real RTL, to prove the links connect
// before any UI exists: 240 kS/s CU8 -> channeliser -> 48 kS/s complex ->
// d1_integration::Receiver -> protocol events.
//
// The channel is tuned 60 kHz above the RTL centre by the channeliser's own
// convention, so the radio is set to (channel - 60000).
//
// Build on the device via the app's CMake target orc_zero_dmr_probe, then:
//   stop the GUI first - only one process may own the RTL.
//   ./build/ux/orc_zero_dmr_probe 463406250 20
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#include "dv_channelizer.hpp"
#include "dv_decimator.hpp"
#include "orcsdr_dmr/d1_bridge.hpp"
#include "rtl_receiver.hpp"

namespace integration = orcsdr::dmr::d1::integration;

namespace {

struct Counters {
  unsigned events = 0;
  unsigned identified = 0;
  unsigned with_colour = 0;
};

const char* sync_name(orcsdr::dmr::SyncKind kind) {
  using orcsdr::dmr::SyncKind;
  switch (kind) {
    case SyncKind::bs_voice: return "BS voice";
    case SyncKind::ms_voice: return "MS voice";
    case SyncKind::bs_data: return "BS data";
    case SyncKind::ms_data: return "MS data";
    default: return "other";
  }
}

void on_event(const integration::Event& event, void* user) noexcept {
  auto* counters = static_cast<Counters*>(user);
  ++counters->events;
  if (event.decoded.integrity_valid) ++counters->identified;
  if (event.decoded.cach_present) ++counters->with_colour;

  // Print every validated identity and a short sample of weaker evidence, so a
  // quiet run still shows whether the chain is alive.
  const auto& d = event.decoded;
  const bool interesting = d.integrity_valid || (counters->events % 40 == 1);
  if (!interesting) return;
  std::printf("  %-9s slot %u cc %u %s", sync_name(d.sync), unsigned(d.slot),
              unsigned(d.colour_code), d.integrity_valid ? "VALID" : "sync");
  if (d.integrity_valid)
    std::printf(" src %u dst %u", d.source_id, d.destination_id);
  std::printf("\n");
  std::fflush(stdout);
}

}  // namespace

// Replays a known-good DMR IQ recording (CS16, 48 kS/s) straight into the
// receiver, bypassing the tuner and the channeliser. If this acquires where live
// RF does not, the protocol chain is sound and the problem is the front end.
int replay_file(const char* path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::printf("cannot open %s\n", path);
    return 2;
  }
  in.seekg(0, std::ios::end);
  const std::streamoff bytes = in.tellg();
  in.seekg(0);
  std::vector<std::int16_t> raw(size_t(bytes) / 2);
  in.read(reinterpret_cast<char*>(raw.data()), bytes);

  const size_t samples = raw.size() / 2;
  std::printf("replay %s: %zu complex samples (%.2f s at 48 kS/s)\n", path, samples,
              double(samples) / 48000.0);

  static integration::Receiver receiver;
  receiver.reset();
  Counters counters;
  std::vector<orcsdr::dmr::d1::ComplexSample> block(2400);
  constexpr float kScale = 1.0f / 32768.0f;
  for (size_t offset = 0; offset < samples; offset += block.size()) {
    const size_t count = std::min(block.size(), samples - offset);
    for (size_t n = 0; n < count; ++n) {
      block[n].re = float(raw[(offset + n) * 2 + 0]) * kScale;
      block[n].im = float(raw[(offset + n) * 2 + 1]) * kScale;
    }
    receiver.process(block.data(), count, on_event, &counters);
  }

  const auto& stats = receiver.stats();
  std::printf(
      "IQ %llu  dibits %llu  events %llu  acquisitions %llu  prereolls %llu\n"
      "identities %llu  lost locks %llu\n",
      (unsigned long long)stats.iq_samples, (unsigned long long)stats.dibits_sent,
      (unsigned long long)stats.protocol_events, (unsigned long long)stats.acquisitions,
      (unsigned long long)stats.acquisition_prerolls,
      (unsigned long long)stats.validated_identities, (unsigned long long)stats.lost_locks);
  std::printf("callback events %u, integrity-valid %u, CACH present %u\n", counters.events,
              counters.identified, counters.with_colour);
  return stats.acquisitions || counters.events ? 0 : 1;
}
int main(int argc, char** argv) {
  if (argc > 2 && std::strcmp(argv[1], "--file") == 0) return replay_file(argv[2]);
  const unsigned long channel_hz = argc > 1 ? std::strtoul(argv[1], nullptr, 0) : 463406250UL;
  const int seconds = argc > 2 ? std::atoi(argv[2]) : 20;
  // div > 1 runs the RTL at div*240 kS/s and subsamples into the channeliser, to
  // test whether 240 kS/s is too low a rate for this tuner.
  const unsigned div = argc > 3 ? (unsigned)std::atoi(argv[3]) : 1;

  std::printf("DMR probe: channel %lu Hz, %.1f s, RTL centre %lu Hz at 240 kS/s\n", channel_hz,
              double(seconds), channel_hz - 60000UL);

  orc::linux_sdr::RtlReceiver radio;
  radio.open(0);
  radio.set_tuner_gain_auto();
  radio.configure(std::uint32_t(channel_hz - 60000UL), 240000u * div);

  orcsdr::dv::Channelizer channelizer;
  // Static: the receiver holds its own state and must outlive the read loop.
  static integration::Receiver receiver;
  receiver.reset();

  Counters counters;
  std::vector<std::uint8_t> cu8(2400 * 2 * div);        // 10 ms at div*240 kS/s
  std::vector<std::uint8_t> decimated((2400 * 2 * div) / 10 + 16);
  std::vector<orcsdr::dv::Iq48> baseband(2400 / 5 + 16);

  double level_sum = 0; double level_max = 0; unsigned level_n = 0;
  unsigned long long processed = 0;
  const long long deadline = (long long)seconds * 1000;
  long long elapsed = 0;
  while (elapsed < deadline) {
    const size_t got = radio.read_cu8(cu8.data(), cu8.size());
    if (!got) continue;
    size_t pairs = got / 2;
    if (div > 1) {
      // 10:1 through the CIC, not naive subsampling: unfiltered decimation folds
      // everything above 120 kHz into the channel and buries the bursts.
      static orc::zero::dv::CicDecimator cic;
      if (div != orc::zero::dv::CicDecimator::kRate) { std::printf("div must be 10 for the CIC\n"); return 2; }
      pairs = cic.process_cu8(cu8.data(), pairs, decimated.data(), decimated.size() / 2);
      std::memcpy(cu8.data(), decimated.data(), pairs * 2);
    }
    const size_t produced =
        channelizer.process_cu8(cu8.data(), pairs, baseband.data(), baseband.size());
    if (!produced) continue;
    // Iq48 and ComplexSample are both two floats; the channeliser documents that
    // layout to match the decoders'.
    const auto* samples =
        reinterpret_cast<const orcsdr::dmr::d1::ComplexSample*>(baseband.data());
    const double level = double(channelizer.take_level());
    level_sum += level; level_max = level > level_max ? level : level_max; ++level_n;
    receiver.process(samples, produced, on_event, &counters);
    processed += produced;
    elapsed = (long long)(processed * 1000ULL / 48000ULL);
  }
  radio.close();

  const auto& stats = receiver.stats();
  std::printf(
      "\nIQ %llu  dibits %llu  events %llu  acquisitions %llu  prereolls %llu\n"
      "missed-after-acquire %llu  lost locks %llu  identities %llu  invalid calls %llu\n",
      (unsigned long long)stats.iq_samples, (unsigned long long)stats.dibits_sent,
      (unsigned long long)stats.protocol_events, (unsigned long long)stats.acquisitions,
      (unsigned long long)stats.acquisition_prerolls,
      (unsigned long long)stats.acquisition_bursts_missed,
      (unsigned long long)stats.lost_locks, (unsigned long long)stats.validated_identities,
      (unsigned long long)stats.invalid_calls);
  std::printf("channel level mean %.4f max %.4f over %u blocks\n", level_n ? level_sum / level_n : 0.0, level_max, level_n);
  std::printf("callback events %u, integrity-valid %u, CACH present %u\n", counters.events,
              counters.identified, counters.with_colour);
  return 0;
}
