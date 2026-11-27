# Measured receive capability of the Cardpter Zero's RTL front end

Produced by `tools/band_probe.cpp`, run on the device (2026-11-27). Each row takes
two 256 KiB CU8 reads after settling the tuner, discards the first, and reports
RMS, peak and crest factor. Higher RMS means more signal plus noise in the pass
band; a dead front end reads near zero.

| Point | RMS | Peak | Crest |
|---|---|---|---|
| 7.074 MHz — FT8 40 m, known good | 2.47 | 13 | 5.26 |
| 96.1 MHz — FM broadcast, known good | 79.34 | 128 | 1.61 |
| 152.1575 MHz — DMR VHF | 25.20 | 128 | 5.08 |
| 155.0 MHz | 73.99 | 128 | 1.73 |
| 433.92 MHz — ISM | 27.65 | 128 | 4.63 |
| 460.0 MHz — DMR UHF | 52.74 | 128 | 2.43 |
| 463.4 MHz — DMR UHF | 50.00 | 128 | 2.56 |
| 857.0 MHz | 9.99 | 48 | 4.81 |
| 906.875 MHz — Meshtastic LoRa | 14.28 | 80 | 5.60 |
| 915.0 MHz — US ISM | 10.84 | 50 | 4.61 |

## Correction to an earlier claim

An earlier note in this project stated the RTL was "deaf at 868/915 MHz", based on
RMS of 0.95–1.6 at 433/868/906 MHz. That measurement was wrong, and this probe
shows why: the application logs `Enabled direct sampling mode, input 2`, and direct
sampling bypasses the tuner entirely. Any VHF/UHF reading taken in that state is
meaningless.

Measured through the normal tuner path, the front end hears VHF and UHF fine —
DMR's 460/463 MHz reads RMS 50–53, comparable to 152 MHz at 25 and well above the
857–915 MHz range at 10–14.

Consequences:

- **DMR at 460/463 MHz and 152 MHz is receivable on this hardware.** The decoder
  work is worth doing, and either band is a viable target.
- The earlier conclusion that "the RTL cannot be a LoRa receiver at 902–928 MHz"
  is **withdrawn**. It hears 906.875 MHz at RMS 14.28. The CAP remains the better
  LoRa radio — its own antenna and its TX path — and it is still the only radio
  that can transmit, but it is no longer true that the RTL is deaf there. The
  original LoRa failures were the sync word (`0x2444`) and the bandwidth codes,
  not the front end.
- Anything measuring weak VHF/UHF signals must confirm direct sampling is off
  first. Treat `Enabled direct sampling mode` in a log as invalidating the
  measurement that follows.
