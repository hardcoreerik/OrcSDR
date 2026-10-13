#include "lora_preamble_detector.hpp"

#include <dsps_fft2r.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include <atomic>
#include <cmath>
#include <cstring>

namespace orcsdr::lora_preamble {
namespace {

constexpr uint32_t kBandwidthHz = 250000;
// 960 kS/s -> 250 kS/s is 3.84 input samples per output sample = 96/25. Each
// input sample is 25 units and each output sample 96 units; an output is the
// area average of the input samples that overlap its interval.
constexpr uint32_t kInputUnits = 25;
constexpr uint32_t kOutputUnits = 96;

// Peak / mean-of-the-other-bins. Noise alone has a median of about 8 and the
// peak bin moves at random, so firing needs a run of windows whose peaks agree.
constexpr float kFireRatio = 9.0f;
constexpr uint8_t kFireRun = 6;
// Windows above this ratio count as "chirp activity" for end-of-packet timing.
// It sits well above the noise tail so noise cannot keep a capture open.
constexpr float kActivityRatio = 16.0f;
constexpr uint8_t kReleaseWindows = 4;

constexpr size_t kMaxN = 4096;  // SF12

struct State {
  float* fft = nullptr;       // 2 * N floats, complex
  float* ref = nullptr;       // 2 * N floats, conj chirp
  float* twiddle = nullptr;   // N floats
  uint8_t sf = 0;
  size_t n = 0;
  // Decimator and window fill.
  uint32_t phase = 0;
  float acc_re = 0;
  float acc_im = 0;
  size_t fill = 0;
  // Detection.
  uint8_t run = 0;
  uint8_t idle = 0;
  bool latched = false;
  uint16_t prev_bin = 0;
  uint32_t quiet_samples = 0;
  // Telemetry.
  uint32_t windows = 0;
  uint32_t fires = 0;
  uint32_t max_window_us = 0;
  uint16_t last_bin = 0;
  float last_ratio = 0;
};

State g;

// configure() and reset() are called from other tasks (channel changes, band switches) while
// feed() runs on the DSP task, so they only post a request; feed() applies it at a block
// boundary. The first configure, before any buffers exist, runs inline because feed() returns
// early until ready().
std::atomic<uint8_t> g_pending_sf{0};
std::atomic<bool> g_pending_reset{false};

void reset_now() {
  g.phase = 0;
  g.acc_re = g.acc_im = 0.0f;
  g.fill = 0;
  g.run = 0;
  g.idle = 0;
  g.latched = false;
  g.quiet_samples = 0;
}

void free_all() {
  heap_caps_free(g.fft);
  heap_caps_free(g.ref);
  heap_caps_free(g.twiddle);
  g.fft = g.ref = g.twiddle = nullptr;
  g.sf = 0;
  g.n = 0;
}

bool near_bin(uint16_t a, uint16_t b, size_t n) {
  const size_t d = a > b ? a - b : b - a;
  return (d < n - d ? d : n - d) <= 1;
}

// One full window is in g.fft (already multiplied by the reference chirp).
void evaluate(Result* out) {
  const int64_t started = esp_timer_get_time();
  const size_t n = g.n;
  if (dsps_fft2r_fc32_arp4_(g.fft, static_cast<int>(n), g.twiddle) != ESP_OK ||
      dsps_bit_rev_fc32(g.fft, static_cast<int>(n)) != ESP_OK) {
    g.fill = 0;
    return;
  }
  float best = 0.0f;
  float sum = 0.0f;
  size_t best_bin = 0;
  for (size_t bin = 0; bin < n; ++bin) {
    const float re = g.fft[bin * 2];
    const float im = g.fft[bin * 2 + 1];
    const float power = re * re + im * im;
    sum += power;
    if (power > best) {
      best = power;
      best_bin = bin;
    }
  }
  const float others = (sum - best) / static_cast<float>(n - 1);
  const float ratio = others > 0.0f ? best / others : 0.0f;
  const uint16_t bin = static_cast<uint16_t>(best_bin);

  ++g.windows;
  ++out->windows;
  g.last_bin = bin;
  g.last_ratio = ratio;

  const bool strong = ratio > kFireRatio;
  if (strong && g.run > 0 && near_bin(bin, g.prev_bin, n)) {
    if (g.run < 255) ++g.run;
  } else {
    g.run = strong ? 1 : 0;
  }
  g.prev_bin = bin;

  if (strong) {
    g.idle = 0;
  } else if (g.idle < 255 && ++g.idle >= kReleaseWindows) {
    g.latched = false;
  }
  if (g.run >= kFireRun && !g.latched) {
    g.latched = true;
    ++g.fires;
    out->fired = true;
  }
  // quiet_samples marks the end of a packet. Once a preamble is confirmed, a window that
  // still clears the detection ratio counts as activity, so a weak packet whose windows sit
  // between the two ratios is not reported as already over.
  if (ratio > kActivityRatio || (g.latched && strong)) g.quiet_samples = 0;

  const uint32_t elapsed = static_cast<uint32_t>(esp_timer_get_time() - started);
  if (elapsed > g.max_window_us) g.max_window_us = elapsed;
}

}  // namespace

namespace {

bool apply_configure(uint8_t spreading_factor) {
  if (g.fft != nullptr && g.sf == spreading_factor) return true;
  constexpr uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  if (g.fft == nullptr) {
    g.fft = static_cast<float*>(heap_caps_malloc(sizeof(float) * kMaxN * 2, caps));
    g.ref = static_cast<float*>(heap_caps_malloc(sizeof(float) * kMaxN * 2, caps));
    g.twiddle = static_cast<float*>(heap_caps_malloc(sizeof(float) * kMaxN, caps));
    if (g.fft == nullptr || g.ref == nullptr || g.twiddle == nullptr) {
      free_all();
      return false;
    }
  }
  const size_t n = static_cast<size_t>(1u) << spreading_factor;
  if (dsps_gen_w_r2_fc32(g.twiddle, static_cast<int>(n)) != ESP_OK ||
      dsps_bit_rev_fc32_ansi(g.twiddle, static_cast<int>(n / 2)) != ESP_OK) {
    free_all();
    return false;
  }
  // Conjugate chirp at one sample per chip: exp(-j*pi*k*(k-N)/N).
  for (size_t k = 0; k < n; ++k) {
    const double phase = M_PI * static_cast<double>(k) * (static_cast<double>(k) - n) / n;
    g.ref[k * 2] = static_cast<float>(std::cos(phase));
    g.ref[k * 2 + 1] = static_cast<float>(-std::sin(phase));
  }
  g.sf = spreading_factor;
  g.n = n;
  reset_now();
  return true;
}

}  // namespace

bool configure(uint8_t spreading_factor) {
  if (spreading_factor < 7 || spreading_factor > 12) return false;
  if (g.fft == nullptr) return apply_configure(spreading_factor);  // feed() is idle until ready()
  // The caller runs this on every IQ block, so an unchanged SF must stay a no-op (it also
  // cancels a pending change back to the running SF). Only a different SF is posted.
  g_pending_sf.store(g.sf == spreading_factor ? 0 : spreading_factor, std::memory_order_release);
  return true;
}

bool ready() { return g.fft != nullptr && g.n != 0; }

void reset() { g_pending_reset.store(true, std::memory_order_release); }

Result feed(const uint8_t* iq, size_t bytes) {
  Result out{};
  const uint8_t pending_sf = g_pending_sf.exchange(0, std::memory_order_acq_rel);
  if (pending_sf != 0) (void)apply_configure(pending_sf);
  if (g_pending_reset.exchange(false, std::memory_order_acq_rel)) reset_now();
  if (!ready() || iq == nullptr) return out;
  const size_t n = g.n;
  for (size_t i = 0; i + 1 < bytes; i += 2) {
    const float re = static_cast<float>(iq[i]) - 127.5f;
    const float im = static_cast<float>(iq[i + 1]) - 127.5f;
    uint32_t remaining = kInputUnits;
    while (remaining > 0) {
      const uint32_t room = kOutputUnits - g.phase;
      const uint32_t take = room < remaining ? room : remaining;
      g.acc_re += re * static_cast<float>(take);
      g.acc_im += im * static_cast<float>(take);
      g.phase += take;
      remaining -= take;
      if (g.phase == kOutputUnits) {
        // Mix with the reference chirp and store; the 1/96 scale is irrelevant
        // because detection uses a ratio.
        const float cr = g.ref[g.fill * 2];
        const float ci = g.ref[g.fill * 2 + 1];
        g.fft[g.fill * 2] = g.acc_re * cr - g.acc_im * ci;
        g.fft[g.fill * 2 + 1] = g.acc_re * ci + g.acc_im * cr;
        g.acc_re = g.acc_im = 0.0f;
        g.phase = 0;
        if (++g.fill == n) {
          evaluate(&out);
          g.fill = 0;
        }
      }
    }
    if (g.quiet_samples < 0xffffffffu) ++g.quiet_samples;
  }
  out.quiet_samples = g.quiet_samples;
  return out;
}

Status status() {
  Status s{};
  s.windows = g.windows;
  s.fires = g.fires;
  s.max_window_us = g.max_window_us;
  s.last_bin = g.last_bin;
  s.last_ratio = g.last_ratio;
  s.run = g.run;
  s.spreading_factor = g.sf;
  return s;
}

}  // namespace orcsdr::lora_preamble
