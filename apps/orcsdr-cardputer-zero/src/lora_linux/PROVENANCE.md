# Linux LoRa FFT dependency

The three FFT/twiddle/bit-reversal functions in esp_fft_ansi.c are derived
from Espressif ESP-DSP v1.7.1, commit fd110921fb57b4de79f092337b9e047130762eca,
modules/fft/float/dsps_fft2r_fc32_ansi.c. Apache-2.0; LICENSE and source
copyright notice retained. Upstream SHA-256: 10e9d9916c691a8bc61df0d6cf91c9c4d7ee98240e64dbf059991dac340d3d8c.

Changes: selected only the three used functions, added Linux declarations and
power-of-two bounds, replaced the ESP global initialization guard with pointer
validation. The caller continues to prepare and bit-reverse each twiddle table.
The ANSI function replaces the ESP assembly entry point; output remains
bit-reversed until the existing caller performs bit reversal.

The production LoRa decoder and preamble detector compile directly from the
shared Tab5 source. Linux uses malloc/free, steady_clock, thread yield, and
system mbedTLS AES. Ordinary RAM satisfies scratch allocation requirements;
PSRAM reporting remains false. No private channel key is loaded by this adapter.
