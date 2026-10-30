# Native VDL Mode 2 receiver: Claude integration handoff

**Prepared:** 2026-10-28. **Native source pushed:** commit `efd8027135c8f4a5a9c6933cb9b8d5bf5dac8cba`. **Baseline:** `hardcoreerik/OrcSDR`, branch `claude/acars-data-decoding-6e3ed0`. Do not merge into `main` without a physical test. No changes to `main.cpp` required or authorized.

## What is implemented

Original C++17 receive-only chain (not a wrapper around dumpvdl2 or xng):

`RTL-SDR CU8 -> vdl2::Channel (DDC + 127-tap anti-alias FIR + /5 or /10) -> vdl2::Receiver (UW acquisition, coherent 16-symbol timing+CFO fit, D8PSK differential decisions, pilot-based PLL, bit LFSR, header FEC) -> vdl2::RsCodec (GF(256) 0x187, roots 120..125, RS255/249, shortened checks, column interleave, hard-decision errors/erasures) -> existing vdl2::FrameDecoder (HDLC flags / destuffing / AVLC FCS / AOA inner BCS) -> acars::Message callback`. Input target can be 250 kS/s -> 50 kS/s or 1.05 MS/s -> 105 kS/s, or a prefiltered 50/105 kS/s capture. The 105k option is an exact 10 samples/symbol. No additional RX RRC filter (ETSI's TX response is full RC alpha 0.6; xng reports RX RRC can degrade actual off-air decodes). Loop gains and thresholds are **receiver design candidates**, not standardized constants.

No allocation in production sample path. Note: no firmware CMake wiring or ACARS dashboard connection has been added yet. Bounded static arrays owned by decoder instance. All direct samples are complex baseband; optional channel-mixer offset in Hz. ACARS/AVLC callback works on received frame, and unknown ATN stays raw hex.

## Files in this bundle

- `apps/orcsdr-tab5/ui/vdl2_rs.{hpp,cpp}` — RS codec and deinterleave.
- `apps/orcsdr-tab5/ui/vdl2_receiver.{hpp,cpp}` — VDL2 PHY burst acquisition, timing, D8PSK and post-FEC integration.
- `apps/orcsdr-tab5/ui/vdl2_channel.{hpp,cpp}` — RF channelizer and sample-rate conversion.
- `tests/vdl2_rs_test.cpp`, `tests/vdl2_receiver_test.cpp`, `tests/vdl2_channel_test.cpp` — synthetic independent stage and end-to-end tests.
- `tools/vdl2_replay_iq.cpp` — host offline replay, `i16` or `cu8`; prints every verified AVLC frame, ACARS, status counters; exit 1 if no frames.
- `tools/vdl2_prepare_iq.py`, `tests/test_vdl2_prepare_iq.py` — host CU8/ORCIQ conversion to 105k replay (Python with NumPy/SciPy).
- `tools/test-vdl2-native.sh` — compile and run host tests, optional off-air replay; saves logfile to invoking directory.

These new files are committed on the requested branch alongside the existing, already committed `vdl2_frames.{hpp,cpp}` and `vdl2_burst.{hpp,cpp}` on the requested branch. Do not overwrite `acars_core.hpp` (the sandbox's stub is excluded from the bundle). Do not copy compiled test binaries or standalone stubs.

## Test results from sandbox

GCC 14.2, `-std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined`:

- RS (255,249) zero through 3 byte errors repaired, shortened checks and multi-row interleaving: **PASS**, synthetic.
- 50 and 105 kS/s 16-symbol UW, D8PSK, header, LFSR, RS, AVLC end-to-end at 0 / +20 Hz CFO: **PASS**, synthetic.
- 250 -> 50 kS/s and 1.05 M -> 105 kS/s FIR decimation: **PASS**, synthetic.
- 25 kHz DDC offset (stream chunked into 257-sample blocks): **PASS**, synthetic.
- ACARS-over-AVLC from synthetic D8PSK IQ to `acars::Message`: **PASS**, synthetic.
- `tools/vdl2_replay_iq.cpp` compiled with strict C++17 flags: **PASS**, no real IQ run.

**NOT DONE:** Real off-air fixture decode (file is downloadable on public GitHub but sandbox cannot fetch non-UTF-8 binaries), ESP-IDF firmware build, M5 Tab5 flash, on-device CPU/PSRAM/USB measurement, BER/SNR sensitivity campaign, collision testing, loss-of-stream handling, fully bounded UI raw-hex rendering, repeated multi-burst capture, long soak or RF regression.

## Get the known-good off-air reference

`https://github.com/airframesio/xng/blob/master/crates/xng-mode-vdl2/tests/data/vdl2_offair_6s.i16`

The file is **1,200,000 bytes**, 6 seconds at 50 kS/s, interleaved signed 16-bit LE I/Q, **Q already inverted** from original capture. xng's `tests/offair.rs` expects **at least two AVLC frames** and an **HB-IJW** ACARS record. The user-friendly script optionally accepts the raw fixture path:

```bash
bash tools/test-vdl2-native.sh /path/to/vdl2_offair_6s.i16
```

For the native host replay only:

```bash
# tools/test-vdl2-native.sh builds the replay tool into a temporary directory. To run it by hand, build it first:
g++ -std=c++17 -O2 -I apps/orcsdr-tab5/ui tools/vdl2_replay_iq.cpp apps/orcsdr-tab5/ui/vdl2_burst.cpp apps/orcsdr-tab5/ui/vdl2_channel.cpp     apps/orcsdr-tab5/ui/vdl2_frames.cpp apps/orcsdr-tab5/ui/vdl2_receiver.cpp apps/orcsdr-tab5/ui/vdl2_rs.cpp -o vdl2_replay_iq
./vdl2_replay_iq /path/to/vdl2_offair_6s.i16 i16 50000
```

Do **not** use `--conjugate` with the already-fixed xng fixture. Original SigIDWiki IQ may require Q inversion. If we decode fewer than 2 AVLC frames or fail to produce HB-IJW, mark the on-air gate **FAIL** and iterate burst acquisition/filtering before firmware integration.

## Host local capture (no decoder required to record)

When a separate RTL-SDR is plugged into the workstation:

```bash
rtl_sdr -f 136975000 -s 1050000 -g 30 -n 10500000 vdl2_10s.cu8
./vdl2_replay_iq vdl2_10s.cu8 cu8 1050000
# Cross-check independent reference oracle:
dumpvdl2 --iq-file vdl2_10s.cu8 --sample-format U8 --oversample 10 --centerfreq 136975000 136975000
```

Gain `30 dB` is a starting experiment, **not** a recommended universal gain; inspect gain and clipping. For Tab5, reuse existing pre-DSP PSRAM CU8 recording and `RTL_IQ_GET_*` retrieval; do not modify main.cpp. Existing `.orciq` includes a 36-byte header; strip the header using validated converter and preserve center/rate metadata. A 2-second 1.05 MSPS CU8 snapshot is 4.2 MB, near the old 4.8 MB PSRAM IQ capture precedent; do not assume runtime budget without measuring. For extended recordings, bounded SD write after sampling or USB/host streaming requires independent acceptance gates.

## Claude integration tasks

1. Pull the exact remote branch `claude/acars-data-decoding-6e3ed0` (never `main`); verify applicable AGENTS.md and the new native files are present. No ZIP/patch needed when pulling GitHub.
2. Run `bash tools/test-vdl2-native.sh` with ASan/UBSan and full real repo headers; correct compilation issues and unverified assumptions.
3. Retrieve xng reference fixture, replay with our binary, independently replay with dumpvdl2 (50 kS/s input must be resampled to integer multiple of 105k for dumpvdl2). Compare AVLC raw hex, CRC and expected HB-IJW.
4. Improve real-capture demod until deterministic off-air acceptance passes, document all tuning values and errors. Add failing/negative fixture checks, BER/CFO/SNR sweep.
5. Only then wire `Channel::receiver().set_callback()` to existing ACARS dashboard's `Model::post_message` path and appropriate radio session ownership. Make VDL2 and existing PoA ACARS independent receiver choices; do not call the 2400-bps ACARS modem on D8PSK. Keep ATN etc. as bounded raw hex. No main.cpp growth.
6. Profile ESP32-P4 CPU/USB continuity, memory, sample-rate negotiation, and UI redraw on actual Tab5; test hardware only with explicit operator authorization. Save device logs and failing captures; do not declare production ready based on synthetic tests.

## Sources

- ETSI EN 301 841-1, VDL Mode 2 D8PSK and burst structure, sections 5.3/5.4.
- ICAO Annex 10 Vol III: header, training sequence, scrambler and FEC parameters.
- dumpvdl2 GPL-3 reference for facts, no copied code: `src/demod.c`, `src/decode.c`, `src/rs.c`, `src/avlc.c`.
- MIT/Apache xng implementations as independent research cross-check: `crates/xng-mode-vdl2/src/{demod,header,interleave}.rs`, `tests/data/README.md`.
- OrcSDR branch: `apps/orcsdr-tab5/ui/{acars_core,acars_receiver,acars_dashboard_model}.hpp`, existing `vdl2_frames.{cpp,hpp}` and `vdl2_burst.{cpp,hpp}`.
