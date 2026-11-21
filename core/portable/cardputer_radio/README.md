# Shared OrcSDR Linux receive DSP source provenance

These C modules are copied **without algorithm modifications** from
`hardcoreerik/OrcSDR-Headless`, branch `feat/m5gfx-web-mirror`,
verified commit `67e535e3a8562709d190194363f7d81ce77fe2f3`.

- `fm_dsp.c/.h`: existing OrcSDR Web/OrcNode receive-side mono broadcast-FM DSP.
  RF channel filtering, +fs/4 mixer where applicable, phase discriminator,
  15 kHz audio filter, 48 kS/s resampling, 50/75 us de-emphasis, DC removal.
- `scan_dsp.c/.h` and `rate_defaults.h`: existing windowed 1024-point
  CU8 FFT, power spectrum and waterfall conversion code.

The modules are maintained here as a visible, source-pinned import rather
than a new FM algorithm or silent independent fork. Future canonical
`orcdec`/portable-core consolidation should reuse the authoritative source
and require parity tests before replacing these files. This import does
not modify ESP32-P4 firmware. OrcSDR's root AGPL-3.0-only license applies.

Source API and algorithms must remain byte-for-byte identical to the pinned
Headless revision unless the next change explicitly documents a portability
patch and demonstrates equal regression results.
