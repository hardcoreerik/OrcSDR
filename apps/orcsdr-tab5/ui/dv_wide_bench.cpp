#include "dv_wide_bench.hpp"

#include <esp_heap_caps.h>

#include <cstdio>
#include <cstdlib>
#include <new>

#include "dv_channelizer.hpp"
#include "dv_wide_ddc.hpp"

namespace orcsdr::dv_wide_bench {

namespace {
constexpr size_t kBlockPairs = 16384;        // the stream's usual block: 32 KiB of CU8
constexpr size_t kBlocks = 75;               // 75 x 16384 pairs = 1.2288 M pairs = 0.512 s of a 2.4 MS/s stream
constexpr size_t kOut240 = kBlockPairs / 10 + 2, kOut48 = kBlockPairs / 50 + 2;

void* psram(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
}  // namespace

void run(uint32_t (*micros)(), void (*write_line)(const char*)) {
  auto* cu8 = static_cast<uint8_t*>(psram(kBlockPairs * 2));
  auto* mid = static_cast<dv::Iq240*>(psram(kOut240 * sizeof(dv::Iq240)));
  auto* out = static_cast<dv::Iq48*>(psram(kOut48 * sizeof(dv::Iq48)));
  auto* ddc_mem = psram(sizeof(dv::WideDdc));
  auto* ch_mem = psram(sizeof(dv::Channelizer));
  auto* ch_ref_mem = psram(sizeof(dv::Channelizer));
  if (!cu8 || !mid || !out || !ddc_mem || !ch_mem || !ch_ref_mem) {
    write_line("RTL_DV_DDCBENCH_FAILED no PSRAM");
    heap_caps_free(cu8); heap_caps_free(mid); heap_caps_free(out);
    heap_caps_free(ddc_mem); heap_caps_free(ch_mem); heap_caps_free(ch_ref_mem);
    return;
  }
  auto* ddc = new (ddc_mem) dv::WideDdc();
  auto* ch = new (ch_mem) dv::Channelizer();
  auto* ch_ref = new (ch_ref_mem) dv::Channelizer();
  uint32_t seed = 12345;
  for (size_t i = 0; i < kBlockPairs * 2; ++i) {
    seed = seed * 1664525u + 1013904223u;
    cu8[i] = static_cast<uint8_t>(seed >> 24);
  }
  ddc->set_offset_hz(200000);
  char line[200];

  // The wide path: mixer + 10:1 filter, then the existing 5:1 channelizer.
  uint32_t ddc_us = 0, ch_us = 0;
  size_t n240 = 0, n48 = 0;
  for (size_t b = 0; b < kBlocks; ++b) {
    const uint32_t a = micros();
    n240 = ddc->process_cu8(cu8, kBlockPairs, mid, kOut240);
    const uint32_t m = micros();
    n48 = ch->process_f32(reinterpret_cast<const float*>(mid), n240, out, kOut48);
    const uint32_t z = micros();
    ddc_us += m - a;
    ch_us += z - m;
  }
  // The path used today for comparison: a 240 kS/s stream straight into the channelizer (same amount of REAL time).
  const size_t ref_pairs_per_block = kBlockPairs / 10;
  uint32_t ref_us = 0;
  for (size_t b = 0; b < kBlocks; ++b) {
    const uint32_t a = micros();
    (void)ch_ref->process_cu8(cu8, ref_pairs_per_block, out, kOut48);
    ref_us += micros() - a;
  }
  const double real_s = static_cast<double>(kBlocks * kBlockPairs) / 2400000.0;   // seconds of stream represented
  const double wide_pct = (ddc_us + ch_us) / 1e6 / real_s * 100.0;
  std::snprintf(line, sizeof(line),
                "RTL_DV_DDCBENCH wide_total_pct_of_one_core=%.1f ddc_pct=%.1f channelizer_pct=%.1f today_240k_channelizer_pct=%.1f "
                "stream_seconds=%.3f outputs240=%u outputs48=%u",
                wide_pct, ddc_us / 1e6 / real_s * 100.0, ch_us / 1e6 / real_s * 100.0,
                ref_us / 1e6 / real_s * 100.0, real_s, static_cast<unsigned>(n240), static_cast<unsigned>(n48));
  write_line(line);
  std::snprintf(line, sizeof(line), "RTL_DV_DDCBENCH_NOTE synthetic noise input, idle serial task; real use adds the decoders (not measured here)");
  write_line(line);

  ddc->~WideDdc(); ch->~Channelizer(); ch_ref->~Channelizer();
  heap_caps_free(cu8); heap_caps_free(mid); heap_caps_free(out);
  heap_caps_free(ddc_mem); heap_caps_free(ch_mem); heap_caps_free(ch_ref_mem);
}

}  // namespace orcsdr::dv_wide_bench
