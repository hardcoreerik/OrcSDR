// Host replay: CU8 240 kS/s file (channel 60 kHz above the capture centre, as the Tab5 stream is laid out) through the
// production dv_radio chain. Prints every verified event and the final counters. Real captures: no synthetic data.
// usage: dv_replay <file.cu8> <channel_hz> [mask 1..7]
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "dv_radio.hpp"

using namespace orcsdr;
static uint32_t g_now = 1000;
static bool ready() { return true; }
static bool tune(uint32_t, uint32_t) { return true; }
static void release() {}
static uint32_t now() { return g_now; }
static uint32_t us() { return g_now * 1000u; }
static uint64_t lost() { return 0; }

int main(int argc, char** argv) {
  if (argc < 3) { std::puts("usage: dv_replay file.cu8 channel_hz [mask]"); return 2; }
  FILE* f = std::fopen(argv[1], "rb");
  if (!f) { std::puts("cannot open"); return 2; }
  dv_radio::Hooks h; h.device_ready = ready; h.tune = tune; h.release = release; h.now_ms = now; h.micros = us; h.lost_samples = lost;
  dv_radio::install(h);
  if (!dv_radio::start()) return 3;
  if (argc > 3) dv_radio::set_protocol_mask(static_cast<uint8_t>(std::atoi(argv[3])));
  dv_radio::tune_channel(static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10)));
  g_now += 700;
  std::vector<uint8_t> buf(16384);
  size_t n, total = 0;
  unsigned shown = 0;
  while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) {
    dv_radio::offer_iq(buf.data(), n, 240000);
    total += n / 2;
    g_now += static_cast<uint32_t>(n / 2 * 1000 / 240000);   // real-time spacing: no false gap resets
    dv_radio::Event e;
    while (dv_radio::pop_event(&e)) {
      if (shown++ < 40)
        std::printf("  t=%6.2fs %s ev=%s ran=%u%s cc=%u%s slot=%u%s src=%u%s dst=%u%s grp=%d rf=%u\n", total / 240000.0,
                    digital_voice_ui::protocol_name(e.protocol), digital_voice_ui::evidence_name(e.evidence), e.ran,
                    e.ran_valid ? "(v)" : "", e.colour_code, e.colour_code_valid ? "(v)" : "", e.slot, e.slot_valid ? "(v)" : "(unk)",
                    e.source_id, e.has_source ? "(v)" : "", e.destination_id, e.has_destination ? "(v)" : "", e.group_call,
                    e.nxdn_rf_channel);
    }
  }
  char line[700]; dv_radio::format_status(line, sizeof(line));
  std::printf("%s\nshown %u events (first 40 printed)\n", line, shown);
  dv_radio::stop();
  return 0;
}
