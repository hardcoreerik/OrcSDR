# ATSC on the Pluto: what a working implementation proves

Notes from reading [pit711/muxwright](https://github.com/pit711/muxwright), a
multi-standard test transmitter for the ADALM-Pluto (MIT licence for the modulators).
Recorded because it answers questions that eight rounds of experimentation here could
not, and because it names the reference to build against instead of guessing.

## The line that matters

From its standards table:

> **ATSC 1.0** | Pluto ARM, 10.762 MS/s (1 sample/symbol) | 8-VSB, 19.39 Mbit/s,
> PSIP: MGT, TVCT, STT | **symbols bit-exact against gr-dtv, loopback through
> gr-dtv `atsc_rx` without packet errors**

Three consequences for this project:

1. **`sps = 1.0` at 10.762 MS/s is a working configuration.** Rounds 3-8 tried 6.5
   MS/s at sps 1.0, 21.52 MS/s at sps 2.0, and 6.25 MS/s at sps 1.5. Only the first
   is what a verified implementation uses.
2. **gr-dtv's `atsc_rx` is not the broken part.** A third party loops it back without
   packet errors at exactly this rate, so the false lock observed here is a fault in
   the *transmit* chain assembled in `tools/pluto/atsc_loopback.py`.
3. **It runs in real time on the Pluto's two Cortex-A9 cores.** The CPU question
   raised in earlier rounds is answered: 10.762 MS/s 8-VSB is achievable on the Zynq's
   ARM side, no FPGA work required.

## Where the reference lives

```text
pluto_fw/mod/atsc_mod        ATSC 1.0 modulator, C, MIT, ARM + NEON
pluto_fw/mod/tests/          comparison harness against gr-dtv
```

It is deliberately not copied into this repository. The useful step is to build it
here and use it as a known-good transmitter: its IQ at 10.762 MS/s fed into
`dtv.atsc_rx(SYMBOL_RATE, 1.0)` should yield a transport stream that matches its
input. That gives this project its missing ground truth without needing an antenna,
and it separates the two questions that have been conflated since round 3.

## Other facts worth keeping

- The Pluto sustains roughly **7 MS/s over USB** in that project's experience, which
  matches the ~7.4 MB/s ceiling measured here. It is a known property of the board,
  not a fault.
- Their DVB-T path says 8 MHz DVB-T needs 9.143 MS/s, which USB cannot carry, so they
  modulate **on the Pluto's ARM** and send only the transport stream. This is the same
  architecture conclusion reached here from measurement, independently.
- Transmit safety in that project: start at **-89.75 dB**, enforce a minimum
  attenuation, verify read-back every 2 s. This project's `PlutoTransmitter` already
  defaults to -89.75 dB, which is reassuring agreement.

## Recommended next step

Build `pluto_fw/mod/atsc_mod` for the Pluto, generate IQ at 10.762 MS/s with 1
sample/symbol, and decode it with `dtv.atsc_rx(SYMBOL_RATE, 1.0)`.

- If the payload matches: the receiver is proven, this project's transmit chain was
  the fault all along, and the remaining work is capture hardware (UHF antenna).
- If it does not: the receiver or its configuration is at fault, and that is now
  attributable rather than ambiguous.
