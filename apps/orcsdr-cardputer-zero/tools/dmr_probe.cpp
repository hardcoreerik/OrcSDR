// Runs the DMR receive chain against the real RTL, to prove the links connect
#include <cmath>
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

std::string ambe_hex(const unsigned char* payload, int first_bit, int stride = 1);
struct VoiceCounters {
  unsigned groups = 0;
  unsigned lc_ok = 0;
  unsigned protected_groups = 0;
  unsigned printed = 0;
};

void on_voice(const orcsdr::dmr::d1::VoiceGroup& group, void* user) noexcept {
  auto* c = static_cast<VoiceCounters*>(user);
  if (!c) return;
  ++c->groups;
  if (group.lc_valid) ++c->lc_ok;
  if (group.pi_seen) ++c->protected_groups;
  if (c->printed < 2) {
    ++c->printed;
    std::printf("    A %s  B %s\n", ambe_hex(group.payload.data(), 0, 2).c_str(),
                ambe_hex(group.payload.data(), 1, 2).c_str());
    std::printf("    C %s  D %s\n", ambe_hex(group.payload.data(), 108, 2).c_str(),
                ambe_hex(group.payload.data(), 109, 2).c_str());
    std::printf("  voice group: slot %u cc %u lc %s pi %s payload %02X%02X%02X...\n",
                unsigned(group.slot), unsigned(group.colour_code),
                group.lc_valid ? "ok" : "no", group.pi_seen ? "SET" : "clear",
                group.payload[0], group.payload[1], group.payload[2]);
  }
}

// Candidate AMBE extraction. The burst README documents the split as
// 98-bit payload + 10-bit slot type + 48-bit SYNC + 10-bit slot type + 98-bit
// payload, so each 98-bit half should hold two 49-bit AMBE frames. VoiceGroup
// gives 27 bytes (216 bits) per burst with the sync already stripped, which is
// 98 + 10 + 98 + 10.
//
// Printed rather than trusted: the reference frame log from this same capture
// begins 81B7109B585500, 80BF7DB2858380, so the layout is right only if it
// reproduces those.
std::string ambe_hex(const unsigned char* payload, int first_bit, int stride) {
  unsigned char bits[49];
  for (int i = 0; i < 49; ++i) {
    const int bit = first_bit + i * stride;
    bits[i] = (payload[bit / 8] >> (7 - (bit % 8))) & 1;
  }
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (int i = 0; i < 7; ++i) {
    unsigned value = 0;
    for (int b = 0; b < 8; ++b) {
      const int idx = i * 8 + b;
      value = (value << 1) | (idx < 49 ? bits[idx] : 0);
    }
    out.push_back(hex[(value >> 4) & 0xF]);
    out.push_back(hex[value & 0xF]);
  }
  return out;
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
  std::printf("  %-9s slot %u cc %u ev %u id %u flco %u %s", sync_name(d.sync), unsigned(d.slot),
              unsigned(d.colour_code), unsigned(d.evidence), unsigned(d.identity), unsigned(d.flco),
              d.integrity_valid ? "VALID" : "sync");
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
// Replays a known-good 48 kS/s recording through the same stages the live path
// uses, to find which one damages it. The recording is upsampled by zero-order
// hold, which leaves images the following filter removes.
//
//   --chain <file>        x5 -> channeliser (240k -> 48k)
//   --chain <file> full   x20 -> CIC 4:1 (960k -> 240k) -> channeliser
// Replays a 240 kS/s CU8 recording through the exact live path: channeliser into
// the d1 receiver, no CIC. These are the corpus captures in
// tests/digital_voice_corpus/iq, which document their own content, so a mismatch
// here is a real regression rather than an unknown signal.
int replay_cu8(const char* path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::printf("cannot open %s\n", path);
    return 2;
  }
  in.seekg(0, std::ios::end);
  const std::streamoff bytes = in.tellg();
  in.seekg(0);
  const size_t byte_count = size_t(bytes);
  std::vector<std::uint8_t> cu8(byte_count);
  in.read(reinterpret_cast<char*>(cu8.data()), bytes);
  std::printf("cu8 %s: %zu pairs (%.1f s at 240 kS/s)\n", path, cu8.size() / 2,
              double(cu8.size() / 2) / 240000.0);

  static integration::Receiver receiver;
  receiver.reset();
  static VoiceCounters voices;
  receiver.set_voice_sink(on_voice, &voices);
  Counters counters;
  orcsdr::dv::Channelizer channelizer;
  std::vector<orcsdr::dv::Iq48> baseband(2400 / 5 + 16);
  for (size_t offset = 0; offset + 2 <= cu8.size(); offset += 2400 * 2) {
    const size_t chunk = std::min<size_t>(2400, (cu8.size() - offset) / 2);
    const size_t out_pairs =
        channelizer.process_cu8(cu8.data() + offset, chunk, baseband.data(), baseband.size());
    if (!out_pairs) continue;
    const auto* samples =
        reinterpret_cast<const orcsdr::dmr::d1::ComplexSample*>(baseband.data());
    receiver.process(samples, out_pairs, on_event, &counters);
  }
  const auto& stats = receiver.stats();
  std::printf("voice groups %u (lc ok %u, protected %u)\n", voices.groups, voices.lc_ok,
              voices.protected_groups);
  std::printf("acquisitions %llu  dibits %llu  events %llu  identities %llu\n",
              (unsigned long long)stats.acquisitions, (unsigned long long)stats.dibits_sent,
              (unsigned long long)stats.protocol_events,
              (unsigned long long)stats.validated_identities);
  return stats.validated_identities ? 0 : 1;
}

int replay_chain(const char* path, bool full) {
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

  static integration::Receiver receiver;
  receiver.reset();
  Counters counters;

  const unsigned hold = full ? 20 : 5;
  std::printf("chain %s: %zu samples, x%u upsample%s\n", path, samples, hold,
              full ? " + CIC 4:1" : "");

  std::vector<std::uint8_t> cu8(samples * hold * 2);
  for (size_t n = 0; n < samples; ++n) {
    const std::uint8_t re =
        std::uint8_t(std::max(0, std::min(255, 128 + (raw[n * 2 + 0] >> 8))));
    const std::uint8_t im =
        std::uint8_t(std::max(0, std::min(255, 128 + (raw[n * 2 + 1] >> 8))));
    for (unsigned h = 0; h < hold; ++h) {
      cu8[(n * hold + h) * 2 + 0] = re;
      cu8[(n * hold + h) * 2 + 1] = im;
    }
  }

  // The channeliser mixes by (-j)^n, expecting the wanted channel 60 kHz above
  // centre. A baseband recording must be rotated to +60 kHz first, or the mixer
  // throws it out of band - which is why the first run of this test read zero.
  {
    const double rate = 48000.0 * double(hold);
    for (size_t n = 0; n < cu8.size() / 2; ++n) {
      const double phase = 6.283185307179586 * 60000.0 * double(n) / rate;
      const double re = double(cu8[n * 2 + 0]) - 128.0;
      const double im = double(cu8[n * 2 + 1]) - 128.0;
      const double nr = re * std::cos(phase) - im * std::sin(phase);
      const double ni = re * std::sin(phase) + im * std::cos(phase);
      cu8[n * 2 + 0] = std::uint8_t(std::max(0, std::min(255, 128 + int(nr))));
      cu8[n * 2 + 1] = std::uint8_t(std::max(0, std::min(255, 128 + int(ni))));
    }
  }

  std::vector<std::uint8_t> stage;
  if (full) {
    orc::zero::dv::CicDecimator cic(4);
    stage.assign(cu8.size() / 4 + 16, 0);
    const size_t produced =
        cic.process_cu8(cu8.data(), cu8.size() / 2, stage.data(), stage.size() / 2);
    stage.resize(produced * 2);
  } else {
    stage = cu8;
  }

  orcsdr::dv::Channelizer channelizer;
  std::vector<orcsdr::dv::Iq48> baseband(2400 / 5 + 16);
  for (size_t offset = 0; offset + 2 <= stage.size(); offset += 2400 * 2) {
    const size_t chunk = std::min<size_t>(2400, (stage.size() - offset) / 2);
    const size_t out_pairs = channelizer.process_cu8(stage.data() + offset, chunk,
                                                     baseband.data(), baseband.size());
    if (!out_pairs) continue;
    const auto* samples_ptr =
        reinterpret_cast<const orcsdr::dmr::d1::ComplexSample*>(baseband.data());
    receiver.process(samples_ptr, out_pairs, on_event, &counters);
  }

  const auto& stats = receiver.stats();
  std::printf("acquisitions %llu  events %llu  identities %llu\n",
              (unsigned long long)stats.acquisitions,
              (unsigned long long)stats.protocol_events,
              (unsigned long long)stats.validated_identities);
  return stats.acquisitions ? 0 : 1;
}

int main(int argc, char** argv) {
  if (argc > 2 && std::strcmp(argv[1], "--file") == 0) return replay_file(argv[2]);
  if (argc > 2 && std::strcmp(argv[1], "--cu8") == 0) return replay_cu8(argv[2]);
  if (argc > 2 && std::strcmp(argv[1], "--chain") == 0)
    return replay_chain(argv[2], argc > 3 && std::strcmp(argv[3], "full") == 0);
  const unsigned long channel_hz = argc > 1 ? std::strtoul(argv[1], nullptr, 0) : 463406250UL;
  const int seconds = argc > 2 ? std::atoi(argv[2]) : 20;
  // div > 1 runs the RTL at div*240 kS/s and subsamples into the channeliser, to
  // test whether 240 kS/s is too low a rate for this tuner.
  const unsigned div = argc > 3 ? (unsigned)std::atoi(argv[3]) : 1;
  // 4FSK polarity is not exposed by the frontend; negating Q is the cheapest
  // way to test whether this carrier is inverted relative to the recording.
  const bool invert = argc > 4 && std::atoi(argv[4]) != 0;

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
  std::vector<std::uint8_t> decimated((2400 * 2 * div) / div + 16);
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
      static orc::zero::dv::CicDecimator cic(div);

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
    if (invert) {
      for (size_t n = 0; n < produced; ++n) {
        const_cast<orcsdr::dmr::d1::ComplexSample*>(samples)[n].im = -samples[n].im;
      }
    }
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
