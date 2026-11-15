# OrcSDR — D-STAR and Yaesu System Fusion receive-only protocol foundation

**Engineering status: HOST SYNTHETIC METADATA VERIFIED (source-branch Release/ASan CI PASS); NOT REAL-AIR/DEVICE VALIDATED.**

This module is an independently implemented C++17, fixed-memory **already-demodulated bitstream** metadata receiver for two important amateur-radio digital-voice formats. It deliberately does **not** transmit RF, select a tuner, own USB/IQ, create another SDR stack, play AMBE voice, decode encrypted speech, modify P25 or DMR, or wire itself into Tab5 firmware. This is a foundation, **not** complete D-STAR or Fusion reception on OrcSDR hardware.

## Implemented interfaces

- **Yaesu System Fusion / YSF**: recognizes exact 40-bit frame synchronization, buffers the 200-bit FICH, deinterleaves, performs terminated K=5 Viterbi convolutional decoding, checks four extended Golay (24,12) words with up to 3 bit corrections each, verifies protected FICH CRC-16 and exposes frame type, calling mode, frame numbering, data mode and squelch fields. FICH is not an amateur callsign decoder; DG-ID and voice metadata outside the FICH are **not** claimed.
- **D-STAR DV**: recognizes a 15-bit radio-header sync, descrambles/deinterleaves a 660-bit channel header, decodes a terminated K=3 convolutional code, checks CRC-16/X.25 and extracts flags, MY/UR/repeater callsigns and suffix. Not a DD-mode, AMBE or slow-data decoder.
- Both expose bounded streaming scanners accepting one hard bit (a byte 0/1) at a time and return events **only after** verified coding checks. No event on sync alone.
- The portable host CLI accepts `ysf` or `dstar` and a raw stream of single-byte hard bits on standard input; output is evidence-qualified JSONL with exact **bit offsets**.

## Current limits, and how this becomes true on-air OrcSDR support

1. **48 kS/s IQ-to-bits frontend missing.** Fusion needs an independently verified 4-level FM/C4FM demodulation path with symbol timing, polarity, CFO handling, framing. D-STAR needs a binary GMSK frontend with carrier/timing tracking and correct signal polarity. Existing P25 or DMR DSP should only be reused when parity tests prove compatibility; never inject these protocols into the current DMR radio owner.
2. **Real-air validation missing.** Obtain independently licensed, source-pinned receiver-side complex IQ with ground-truth on-air FICH / D-STAR headers; verify bytes/hashes, test all bit polarities, multiple RF devices, channel impairments, chunk-invariance and zero unsupported identities. A synthetic frame constructed by an independent transmitter-like test oracle is not proof of an RF decoder.
3. **Tab5 firmware integration missing.** Cross-compile for ESP-IDF / ESP32-P4, benchmark memory/CPU, then add an optional read-only subscription to approved channelized IQ behind existing receiver ownership. Avoid USB callbacks, SDR driver duplication, blocking, or main.cpp growth.
4. **Voice is out of scope.** AMBE and AMBE+2 clear voice need separate technical/licensing qualification; metadata comes first.
5. **Standard/source differences.** Radio-header FEC/interleave and scrambler conventions must be validated against independently captured genuine D-STAR bit streams; synthetic self-consistency alone cannot establish exact wire compatibility.

## Build and test on the host

Run from the repo root. `cmake` >= 3.16 and a C++17 compiler required. Save logs to **the same directory where the commands are run**:

```bash
set -o pipefail
cmake -S components/orcsdr_digital_voice_rx -B build/dv-rx-release -DCMAKE_BUILD_TYPE=Release 2>&1 | tee ./dv-rx-release-configure.log
cmake --build build/dv-rx-release --parallel 2 2>&1 | tee ./dv-rx-release-build.log
ctest --test-dir build/dv-rx-release --output-on-failure -V 2>&1 | tee ./dv-rx-release-test.log

cmake -S components/orcsdr_digital_voice_rx -B build/dv-rx-asan -DCMAKE_BUILD_TYPE=Debug -DORC_DV_SANITIZERS=ON 2>&1 | tee ./dv-rx-asan-configure.log
cmake --build build/dv-rx-asan --parallel 2 2>&1 | tee ./dv-rx-asan-build.log
ctest --test-dir build/dv-rx-asan --output-on-failure -V 2>&1 | tee ./dv-rx-asan-test.log
```

To replay known already-demodulated bits (one byte 0x00 or 0x01 per RF bit, **not** interleaved IQ or packed bytes):

```bash
build/dv-rx-release/orcsdr_dv_rx_cli ysf < input-hard-bits-01.bin > ./ysf-events.jsonl 2> ./ysf-replay.log
build/dv-rx-release/orcsdr_dv_rx_cli dstar < input-hard-bits-01.bin > ./dstar-events.jsonl 2> ./dstar-replay.log
```

The CLI fails on invalid input values. A successful zero-event run is **not** decoder acceptance; it only means no CRC/FEC-validated metadata was emitted.

## Validation already included

- Protocol-specific independent **transmit-side-only** reference encoders construct synthetic valid FICH and D-STAR DV frames; these are never linked into the production core.
- YSF: exact sync/framing; two consecutive frame detections; BCH/Golay minimum-distance 8; 3-bit correction and 4-bit rejection; CRC corruption and invalid hard-bit rejection.
- D-STAR: correct source/routing callsigns, expected frame offset, valid X.25 CRC, forced CRC rejection, invalid callsign rejection, single-channel-bit correction.
- Both: seeded 300,000-bit random negative stream must emit **zero validated metadata events**; this is not real-world false-alarm calibration.
- The GitHub workflow builds and executes the **actual first-party code** in Release and ASan/UBSan. Check the run's exact source SHA before treating a green check as support.

## Independent source authorities, not copied source

- Yaesu, *Amateur Radio Digital Specifications*, Version 1.02, YSF frame/FICH framing and coding: https://www.yaesu.com/Files/DFDF4D53-1B78-7375-E6C7A6E4B192BE13/Yaesu_Amateur%20Radio%20Digital%20Specs_1V02_EN-GB.pdf
- JARL, *D-STAR Amateur Radio Standard*, including DV RF header structure and coding: https://www.jarl.org/English/7_dstar/STD7_0_E.pdf
- Reference inspection only: https://github.com/g4klx/MMDVM-Host (GPL-2.0-or-later; no implementation code copied).

OrcSDR implementation is AGPL-3.0-or-later; license compatibility and vocoder rights must be separately evaluated for third-party components. Neither these sources nor the synthetic tests prove over-the-air interoperability.

## Delivery boundary

**Host protocol metadata complete as an initial foundation; receive IQ frontend and device integration explicitly NOT IMPLEMENTED.** This module is integrated into the canonical receive-only digital-voice development branch, `feature/dmr-digital-voice-foundation`, from the original D-STAR/YSF branch. This is **not merged to main**. Do not label the Tab5 application D-STAR/YSF-capable until recorded-IQ and device qualification pass. See `docs/digital-voice/CONSOLIDATION_2026-11-15.md`.
