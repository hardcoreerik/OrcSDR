// Web build of the ESP-DSP calls rf_analysis makes: an in-place radix-2 complex FFT on interleaved float pairs, with the bit-reversal
// as a separate pass, as the device's library does.
#pragma once
#include <math.h>
#include <stddef.h>
#include "esp_err.h"
static inline esp_err_t dsps_fft2r_init_fc32(float*, int) { return ESP_OK; }
static inline esp_err_t dsps_fft2r_fc32_ansi(float* data, int n) {
  // decimation in frequency: input in natural order, output in bit-reversed order
  for (int len = n; len >= 2; len >>= 1) {
    const int half = len >> 1;
    const double step = -2.0 * M_PI / len;
    for (int base = 0; base < n; base += len) {
      for (int k = 0; k < half; ++k) {
        const float wr = (float)cos(step * k), wi = (float)sin(step * k);
        float* a = data + 2 * (base + k);
        float* b = data + 2 * (base + k + half);
        const float tr = a[0] - b[0], ti = a[1] - b[1];
        a[0] += b[0];
        a[1] += b[1];
        b[0] = tr * wr - ti * wi;
        b[1] = tr * wi + ti * wr;
      }
    }
  }
  return ESP_OK;
}
static inline esp_err_t dsps_bit_rev_fc32_ansi(float* data, int n) {
  int j = 0;
  for (int i = 0; i < n - 1; ++i) {
    if (i < j) {
      float t0 = data[2 * i], t1 = data[2 * i + 1];
      data[2 * i] = data[2 * j];
      data[2 * i + 1] = data[2 * j + 1];
      data[2 * j] = t0;
      data[2 * j + 1] = t1;
    }
    int m = n >> 1;
    while (m >= 1 && j & m) { j ^= m; m >>= 1; }
    j |= m;
  }
  return ESP_OK;
}
