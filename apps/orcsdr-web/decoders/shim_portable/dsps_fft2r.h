// The ESP-DSP calls OrcSDR's LoRa code makes, as plain C++, for builds without the esp-dsp component (host, WebAssembly and the node's
// firmware). Same table layout and call order as the library: dsps_gen_w_r2_fc32 writes cos/sin pairs, dsps_bit_rev_fc32_ansi puts that
// table in bit-reversed order, dsps_fft2r_fc32_arp4_ is the radix-2 decimation-in-frequency transform that reads it, and dsps_bit_rev_fc32
// puts the result in natural order. (The device's "arp4" variant is the same transform with hardware loops.)
#pragma once
#include <esp_err.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

static inline esp_err_t dsps_gen_w_r2_fc32(float* w, int n) {
  if (!w || n < 2) return ESP_FAIL;
  const double step = 2.0 * M_PI / n;
  for (int i = 0; i < n / 2; ++i) {
    w[2 * i] = (float)cos(step * i);
    w[2 * i + 1] = (float)sin(step * i);
  }
  return ESP_OK;
}

// In-place bit reversal of `n` complex (interleaved float) values; n a power of two.
static inline esp_err_t dsps_bit_rev_fc32_ansi(float* data, int n) {
  if (!data || n < 2) return ESP_FAIL;
  int j = 0;
  for (int i = 0; i < n - 1; ++i) {
    if (i < j) {
      const float t0 = data[2 * i], t1 = data[2 * i + 1];
      data[2 * i] = data[2 * j];
      data[2 * i + 1] = data[2 * j + 1];
      data[2 * j] = t0;
      data[2 * j + 1] = t1;
    }
    int m = n >> 1;
    while (m >= 1 && (j & m)) { j ^= m; m >>= 1; }
    j |= m;
  }
  return ESP_OK;
}
static inline esp_err_t dsps_bit_rev_fc32(float* data, int n) { return dsps_bit_rev_fc32_ansi(data, n); }

static inline esp_err_t dsps_fft2r_fc32_arp4_(float* data, int n, float* table) {
  if (!data || !table || n < 2) return ESP_FAIL;
  int ie = 1;
  for (int n2 = n / 2; n2 > 0; n2 >>= 1) {
    int ia = 0;
    for (int j = 0; j < ie; ++j) {
      const float c = table[2 * j], s = table[2 * j + 1];
      for (int i = 0; i < n2; ++i) {
        const int m = ia + n2;
        const float re = c * data[2 * m] + s * data[2 * m + 1];
        const float im = c * data[2 * m + 1] - s * data[2 * m];
        data[2 * m] = data[2 * ia] - re;
        data[2 * m + 1] = data[2 * ia + 1] - im;
        data[2 * ia] += re;
        data[2 * ia + 1] += im;
        ++ia;
      }
      ia += n2;
    }
    ie <<= 1;
  }
  return ESP_OK;
}
