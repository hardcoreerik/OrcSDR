# OrcSDR DSP architecture audit and multirate plan

Status: **Phase 1-4 report** (investigation and measurement). No DSP behavior
has been changed yet, except a task-watchdog safety valve and instrumentation
(see §6). Branch `claude/dsp-multirate`, built on `claude/rc4-controls`.
Hardware: M5Stack Tab5 (ESP32-P4, 360 MHz, `-O2`), RTL-SDR Blog V4, esp-rtl-sdr
`f62c5cd` (0.8.0-rc4 line; 0.9.0 pending in hardcoreerik/esp-rtl-sdr#29).
Measured 2026-09-26.

---

## 0. Findings that change the plan

1. **The current DSP is already near its CPU limit at 2.4 MS/s.** FM uses
   **68 %** of core 1, and its demodulator alone costs **3.9 ms per 16 384-sample
   block (57 % of the core)**. Scaled linearly, the same code needs about
   **100 % of a core at 3.2 MS/s** before spectrum, level or UI work. A
   resampler alone cannot deliver 2.56/3.2 MS/s audio; the front end must get
   roughly 3-4x cheaper per input sample.
2. **Overload has been crashing the Tab5.** The `rtl_dsp` task (priority 6,
   core 1) never blocks when it falls behind, so IDLE1 starves and the task
   watchdog resets the device. Captured on AM at 2.4 MS/s: `task_wdt: IDLE1
   (CPU 1)`, running task `rtl_dsp`, PC in `demodulate_am()` (main.cpp:7697).
   This matches the unexplained resets on CB (AM demod) and the RF Lab/FM
   crash. A safety valve now forces a 1 ms sleep after 50 ms without blocking,
   so overload becomes visible IQ drops instead of a reset. It addresses the
   symptom only; the cost is the real fix.
3. **Any non-default device rate disables demodulation entirely, 2.88 MS/s
   included.** The DSP task only demodulates when `!block.custom_rate`
   (device rate == band default). `rtl_rf_decimation()` would accept 2.88 MS/s,
   but the custom-rate gate stops it first. Measured: demod time is 1 µs/block at
   2.56, 2.88 and 3.2 MS/s.
4. **The transport is not the problem.** At 2.40/2.56/2.88/3.20 MS/s the
   measured sample rate matched nominal (2 400 420 / 2 559 921 / 2 879 482 /
   3 197 247 samples/s over ~13 s windows) with **0 IQ drops**. The driver
   needs no change for rate flexibility.
5. **Cost is dominated by per-sample overhead, not math.** AM (4 IIR updates per
   input sample) and SSB (2 per sample) cost nearly the same (2.79 vs 2.72 ms per
   block, about 59 CPU cycles per sample for about 10 flops). The demodulators
   read IQ through `const uint8_t*` and keep all filter state in the global
   `rtl_audio`. Because a byte pointer may alias anything, the compiler must
   reload and store that state on every sample. This is a cheap, high-value fix
   to prove first (Stage 1).

---

## 1. Current DSP map

```
RTL-SDR (USB bulk, 3 URBs x 32 KiB)
  │  esp-rtl-sdr client task, core 0
  ▼
rtl_driver_event_cb (IQ_BLOCK, borrow mode)
  │  memcpy 32 KiB -> rtl_ring_slots[3] (PSRAM)            copy #1
  │  RtlIqBlock{band, rate, custom_rate, audio_scale}
  ▼  rtl_filled_q (depth 3)
rtl_dsp_task (core 1, prio 6, 8 KiB stack in PSRAM)
  ├─ am_finder::offer_iq                     (AM finder only; returns)
  ├─ ADS-B: memcpy -> adsb_iq_blocks         copy #2 (ADS-B only)
  ├─ update_signal_level_from_iq            full-rate scan: clip + power
  ├─ POCSAG process_cu8   (960k, !custom_rate)
  ├─ P25 process_cu8      (960k, !custom_rate)
  ├─ iq_rec_append        (raw CU8, !custom_rate)
  ├─ spectrum_offer_iq_snapshot             copy #3 (4 KiB snapshot)
  ├─ lora_iq_offer        (!custom_rate)
  └─ demod (!custom_rate, audio wanted):
       FM/WX:  demodulate_fm   CU8->float, 1-pole IIR, boxcar ÷N -> 240k,
               discriminator, 19k/38k resonator stereo, RDS, 1-pole LPF,
               boxcar ÷5 -> 48k, de-emphasis, DC, AGC+soft limit
       AM/SW:  demodulate_am   CU8->float, 2x 1-pole IIR, boxcar ÷N -> 240k,
               |IQ|, 1-pole, boxcar ÷5 -> 48k, DC, AGC (+ shortwave audio_dsp)
       CB:     AM as above, or demodulate_ssb (1-pole IIR, ÷N, BFO mix, ÷5)
         ▼
       queue_audio_samples -> speaker / web audio / recorder (48 kHz, mono)

Side consumers of the same raw IQ (other tasks):
  rf_visualizer channelizer   up to 8 NCO+1-pole channels at full rate, ÷(rate/48000)
  rf_analysis demodulate_audio  FM/AM on raw full rate, ÷(rate/48000)
```

## 2. Rate-assumption audit

| Location | Assumption | Class |
|---|---|---|
| main.cpp `rtl_rf_decimation()` | device rate % 240 000 == 0 | **architectural limitation** |
| main.cpp DSP task `!block.custom_rate` gates (demod, POCSAG, P25, LoRa, recording) | device rate == band default | **architectural limitation** (also disables 2.88 MS/s) |
| main.cpp `kFmAudioDecim = 5`, AM/SSB `audio_phase == 5` | 240k -> 48k by boxcar | valid fixed internal rate (weak filter) |
| main.cpp `rtl_filter_alpha()` | 1-pole cutoff from device rate | valid, but **weak anti-alias** for ÷N |
| main.cpp SSB `step = ... / kRtlDemodRateSps` | BFO at 240k | valid fixed internal rate |
| main.cpp RDS `kRdsChipInc`, pilot/RDS NCO nominal | 240 kS/s MPX | **valid intentional** (keep) |
| main.cpp `kRdsMpxRateHz == 240000` capture format | 240 kS/s file | valid intentional |
| rf_analysis.cpp:79 `sample_rate / audio_rate` | integer ÷ -> 48 302 Hz at 2.56M, 48 485 Hz at 3.2M | **bug** |
| rf_analysis.cpp FM mode | discriminator on unfiltered full-rate IQ | **bug** (no channel filter) |
| rf_visualizer.cpp:859 `sample_rate_sps / 48000u` | same truncation | **bug** |
| rf_visualizer.cpp LSB/USB `I ± Q` | not a sideband demodulator | **bug** |
| rf_visualizer.cpp per-channel NCO at full rate | N channels x full rate | needs investigation (cost) |
| pocsag_decoder_core.hpp `kInputSampleRateHz = 960000`, internal 38 400 | input == 960k | internal rate intentional; **input is an accidental device-rate assumption** |
| p25_decoder_core.cpp `kInputRate = 960000`, 48k channel, 4 800 sym/s | input == 960k | same |
| lora_native_decoder.cpp `kDecodeRate = 500000` | input rate assumed | needs investigation |
| ADS-B 2 048 000 | 2 MS/s for 1 µs pulses | **valid intentional protocol rate** |
| am_finder.hpp `kSampleRateSps = 2400000` | device rate 2.4M | accidental device-rate assumption |
| SSB mode selection | no sideband filter; mode only flips BFO | **bug** (both sidebands pass) |
| FM stereo | pilot/38k recovery runs, speaker gets mono (L+R) | documented truth, later improvement |

## 3. DSP duplication matrix

| Primitive | Implementations | Consumers | Rate | Consolidate? |
|---|---|---|---|---|
| CU8 -> centered float | demodulate_fm/am/ssb, rf_analysis, rf_visualizer, spectrum, POCSAG/P25/LoRa | all | full | **yes**: one block-local converter feeding the front end |
| Channel LPF + ÷N | fm/am/ssb (1-pole IIR + boxcar), visualizer (1-pole + phase counter), rf_analysis (boxcar only) | demod, visualizer, analysis | full -> 240k/48k | **yes**: the shared multirate front end is the core of this work |
| NCO / mixer | SSB BFO, visualizer per channel, RDS NCO, pilot | several | 240k / full | yes: one NCO primitive (phase-accumulator + renorm) |
| FM discriminator | main.cpp fast_phase, rf_analysis cross/dot, visualizer atan2f | 3 | 240k / full | yes, after rate plan |
| AM envelope | main.cpp sqrtf, rf_analysis \|I\|+\|Q\|, visualizer hypotf | 3 | 240k / full | yes |
| DC block | fm (0.0008), am/ssb (0.002), rf_analysis, visualizer | many | 48k | yes (trivial, shared audio stage) |
| AGC + soft limiter | shape_audio_sample (shared) | fm/am/ssb | 48k | already shared; keep |
| De-emphasis | fm only | fm | 48k | keep in FM |
| Signal level / clipping | update_signal_level_from_iq | all bands | full | **optimize** (11 % of a core) |
| Audio ÷5 boxcar | fm/am/ssb | all audio | 240k->48k | replace with a proper decimating FIR in the shared audio stage |

## 4. Driver assessment (esp-rtl-sdr)

**No change required for rate flexibility.** Discontinuity detection needs
work on both sides (corrected 2026-09-26, see §11).
- Transport delivered exact rates with 0 drops up to 3.2 MS/s (§0.4).
- Every `esp_rtl_sdr_iq_block_t` carries `sequence`, `sample_rate_sps`,
  `frequency_hz`, `host_timestamp_us` and `flags` (`OVERRUN`, `SHORT_TRANSFER`).
  OrcSDR drops all of it at the ring: `RtlIqBlock.sequence` is OrcSDR's own
  `rtl_iq_sequence.fetch_add(1)`, not `iq->sequence`, and its own pipeline drops
  never reach the DSP.
- The driver metadata alone is **not** a sufficient continuity signal: a
  sequence gap or `OVERRUN` says the driver lost data, but not that OrcSDR
  dropped a block, retuned, or changed rate. See the continuity condition in §11.
- Candidate later improvement (measure first): `iq_acquire_mode`
  (release_iq_block) is declared but "currently ignored (borrow mode only)".
  Implementing it would remove copy #1 (32 KiB/block, about 6.4 MB/s at 3.2 MS/s).
  The copy costs little CPU next to the demodulator, so this is low priority.

## 5. Memory baseline (Tab5, 2026-09-26)

| Pool | Value |
|---|---|
| Internal free after boot / ready / after Wi-Fi | 204 815 / 178 751 / 163 087 B |
| DMA-capable free (steady, streaming) | ~112 KB, largest block 69 632 B |
| PSRAM free | ~24.5 MB (largest 24.6 MB) |
| Tasks | 24; main loop stack high-water ~5.3 KB free |
| IQ ring | 3 x 33 280 B in **PSRAM** (+ queues in PSRAM) |
| `rtl_dsp` stack | 8 KiB in **PSRAM** (benchmark vs internal) |
| `rtl_iq_processing` | **33 280 B internal SRAM**; live users are P25 SD replay and the disabled legacy USB path (the ring uses its `sizeof`). Candidate to move to PSRAM |
| Audio | `rtl_audio_play_blocks` 24 KB + `rtl_audio_buffers` 12 KB, internal |
| Spectrum | 9 x 8 KiB float arrays, PSRAM |
| ADS-B decoder | 33 KB internal |
| `rtl_audio` DSP state | internal SRAM (0x4ff24cc8) |

## 6. Performance baseline

Instrumentation added: `dsp/dsp_stats.{hpp,cpp}` and the read-only
`RTL_DSP STATS` command (per-stage µs/block, load, queue high-water, backlog,
overload yields). Block = 16 384 complex samples (6.83 ms at 2.4 MS/s).

| Case | Load | Demod | Level | Spectrum | Queue hwm | Drops |
|---|---|---|---|---|---|---|
| FM 2.40 (Radio) | **68 %** | 3.90 ms | 0.77 ms | 0.02 ms | 0 | 0 |
| AM 2.40 | 52 % | 2.79 ms | 0.78 ms | 0.04 ms | 0 | 0 |
| CB 2.40 (AM) | 51 % | 2.72 ms | 0.78 ms | 0.03 ms | 0 | 0 |
| CB scanning | 52 % | 2.73 ms | 0.78 ms | 0.03 ms | 0 | 0 |
| RF Lab 2.40 (CB) | 67 % | 3.42 ms | 0.95 ms | 0.26 ms | 0 | 0 |
| RF Lab 2.56 | 18 % | off | 0.93 ms | 0.25 ms | 0 | 0 |
| RF Lab 2.88 | 19 % | off | 0.87 ms | 0.25 ms | 0 | 0 |
| RF Lab 3.20 | 22 % | off | 0.88 ms | 0.26 ms | 0 | 0 |

Before the safety valve: FM/AM overload plus RF Lab drawing crashed the Tab5
(task watchdog). With it: `overload_yields` 1-8 during band switches, no resets.
Audio underruns and speaker rejects: `audio_dropped=0` throughout.

## 7. Candidate architectures

All three keep the existing 240 kS/s WFM MPX chain (stereo, RDS) unchanged and
end in exact 48 kHz. They differ in how the device rate reaches 240k (and the
channel rates for AM/SSB/NFM).

| | A: float polyphase | B: integer halfband + float rational | C: ESP-DSP FIR decimate + custom polyphase |
|---|---|---|---|
| Full-rate stage | CU8->float, polyphase at full rate | CU8 int, **halfband ÷2 cascade (int16)** | ESP-DSP `dsps_fird` (float or s16) at full rate |
| Fractional step | full-rate polyphase | float polyphase L/M at 480-800k | custom polyphase at low rate |
| Est. cost at 3.2M (full-rate part) | high: 8 B/sample float traffic, ~30-60 cycles/sample | **low: ~8-15 cycles/sample** (symmetric halfbands, half the taps are zero) | medium; depends on P4 PIE SIMD support in ESP-DSP (to benchmark) |
| Rate flexibility | any | any (halfbands to ~0.5-0.8M, then L/M) | any |
| Memory | largest (float blocks) | small (int16 working block, float only after ÷4) | medium |
| Filter quality | excellent | excellent (designed halfbands + FIR) | excellent |
| Complexity | medium | medium | medium; external dependency behavior |

Rate plans for B (halfband ÷2 stages, then rational L/M to 240k):

| Device | Halfband stages | Intermediate | Rational to 240k |
|---|---|---|---|
| 2.40M | ÷2 ÷2 | 600k | 2/5 |
| 2.56M | ÷2 ÷2 | 640k | 3/8 |
| 2.88M | ÷2 ÷2 | 720k | 1/3 (integer) |
| 3.20M | ÷2 ÷2 | 800k | 3/10 |

AM/SSB/NFM then take a further ÷5 (or polyphase) to 48k, with a proper
channel FIR at that rate, instead of running at 240k.

## 8. Recommendation

**Candidate B**, validated by benchmarks before commitment:
- It performs the only full-rate work (÷4) in integer halfbands, the cheapest
  correct filter, with no full-rate float expansion (§ Phase 8).
- The fractional step runs at ≤ 800k, where a float polyphase is affordable.
- The 240k MPX domain and everything downstream stay bit-for-bit comparable,
  which enables the old-vs-new regression.
- C remains the fallback if ESP-DSP's P4 kernels beat hand-written halfbands.
  Stage 2 benchmarks B vs C on the Tab5; A is kept only as the host-test
  reference implementation.

## 9. Proposed module layout (out of main.cpp)

```
apps/orcsdr-tab5/dsp/
  dsp_stats.*          (done) instrumentation
  rate_plan.*          DeviceRate -> {halfband stages, L/M, channel rate}
  halfband.*           int16 ÷2 halfband (CU8/int16 in, int16 out)
  polyphase.*          rational L/M resampler (complex float, state in struct)
  frontend.*           CU8 block -> 240k (or channel-rate) complex IQ; discontinuity reset
  nco.*                phase accumulator / mixer
  demod_fm.*           discriminator + stereo + RDS hand-off (moved from main.cpp)
  demod_am.*, demod_ssb.*
  audio_stage.*        240k -> 48k decimating FIR, DC, de-emphasis hooks
  tests/host/          synthetic-IQ tests (CW, AM, WFM mono/stereo, pilot, SSB, interferers)
```
main.cpp keeps only routing/lifecycle: it hands `IqBlock` views to `frontend`
and receives 48k audio. Its line count must go down.

## 10. Implementation sequence and gates

| Stage | Work | Gate (must pass before next) |
|---|---|---|
| 0 (done) | Safety valve + `RTL_DSP STATS` | No task-watchdog resets on FM/AM/CB/RF Lab |
| 1 | Register-local DSP state in existing demods; SWAR clip/level scan | Same audio (host bit-compare on saved IQ); FM load ≤ ~45 % at 2.4M |
| 2 | Host test lab + benchmark halfband (B) vs ESP-DSP (C) on Tab5 | Filter specs met; cycles/sample measured |
| 3 | `frontend` + `rate_plan`; FM through 240k MPX at 2.40/2.56/2.88/3.20 | §28 acceptance: pitch, rate, stereo/RDS ≥ baseline, load ≤ 70 %, no drops |
| 4 | Remove custom-rate gate for demod; discontinuity contract (driver `sequence`/`OVERRUN` + pipeline drops -> reset) | Hot tune / rate change / restart clean |
| 5 | AM/SSB/CB on shared channel path; real SSB sideband filter | Pitch, bandwidth, sideband rejection measured |
| 6 | rf_analysis + rf_visualizer on shared primitives (fix ÷48000 bugs) | Exact 48k over long windows |
| 7 | Decoder inputs via rate conversion (POCSAG/P25/LoRa/AM finder) | Decode rate ≥ baseline on saved IQ |
| 8 | Home RATE (AUTO/2.40/2.56/2.88/3.20) separate from SPAN; wider spans | UX check; no restart on SPAN |
| later | Driver acquire mode (zero-copy), true stereo output, sync AM, noise blanker | Each measured independently |

Also found during the audit (not DSP, tracked separately): the CB scanner kicks
the screen out of RF Lab while scanning; RF Lab text too small and it flickers.

---

## 11. Required contracts and corrections (added 2026-09-26)

- **Correction:** §4 originally said the driver already gives everything needed
  to detect discontinuities. That was too strong. OrcSDR invents its own block
  sequence (`rtl_iq_sequence`) instead of carrying `iq->sequence`, and the
  driver's sequence gaps and `OVERRUN` flag cannot see OrcSDR-side drops or
  transitions.
- **`RtlIqBlock` must retain the driver metadata**: driver sequence, flags,
  host timestamp, plus OrcSDR's own facts: an app-pipeline-drop latch (set on a
  free-slot or filled-queue miss, cleared by the next block delivered), and a
  rate/tune transition marker (set by retune, rate change and stream start).
- **Continuity condition (Stage 4):** a block continues the previous one only if
  the driver sequence is exactly previous + 1, no `OVERRUN`/`SHORT_TRANSFER`
  flag is set, the driver loss counters did not move, the OrcSDR drop latch is
  clear, and no transition is marked. Anything else is a discontinuity, and
  each module applies its reset/reacquire rule (§13).
- **Driver-interface follow-up (logged, no driver change now):** confirm the
  driver's sequence counts every transfer including dropped ones, and expose
  cumulative loss counters in the block (or a cheap getter) so the consumer can
  tell a gap from a reorder. Out of scope for this DSP task unless authorized.
- **Every stateful DSP module must define its reset/reacquire behavior after a
  discontinuity**: FIR histories, resampler phase, FM discriminator previous
  sample, stereo resonators, RDS timing, AGC, squelch and decoder timing.
  Stage 4 implements this contract.
- **2.88 MS/s is currently NOT a working demodulation rate.** An earlier
  assumption (including in the brief) that 2.88 MS/s produces audio was wrong:
  any non-default device rate disables demodulation through the `custom_rate`
  gate, even though `rtl_rf_decimation()` would accept 2.88.
- **The driver transport was measured stable at 2.40, 2.56, 2.88 and
  3.20 MS/s with zero IQ drops** during this audit (§0.4).
- **The CB scanner / RF Lab interaction is a separate UI/state bug.** The
  scanner closes RF Lab while scanning. It is not a DSP issue and must not be
  mixed into DSP changes.
- **P4 silicon revision 1.3** (`"revision":103`). ESP-DSP `_arp4` kernels have
  reported FIR corruption on rev 1.3 and hardware-loop state loss across
  context switches. No optimized ESP-DSP kernel ships without an ANSI/scalar
  oracle comparison, forced-preemption stress with Wi-Fi/UI/USB active, and a
  soak. Existing `_ansi` calls stay until then.

## 12. Stage 1 results (optimization only)

Verification: an on-device old-vs-new A/B harness (`ui/dsp_ab_harness.inc`,
built only with `ORCSDR_DSP_AB=1`). It captures 64 consecutive live IQ blocks
and the exact DSP state, then runs verbatim pre-Stage-1 copies and the new code
over the same saved input from the same state.

| Check (64 blocks, same saved IQ) | FM | AM | CB (AM) |
|---|---|---|---|
| Audio byte differences | 0 | 0 | 0 |
| FM MPX byte differences (RDS input) | 0 | - | - |
| State after every block | identical | identical | identical |
| Meter / level byte differences | 0 | 0 | 0 |

RDS per-sample (old) vs per-block (new), 104 858 MPX samples: 0 byte
differences in the full state, 124 384 chips both ways; 32.1 -> 24.6 ms.

Changes, all bit-identical:
1. Demodulator hot state in locals for a block. The AM inner loop went from
   ~31 instructions with 14 state loads/stores per sample to 19 with none.
2. Clipping counted inside the demodulator pass; power stays a 1-in-16 strided
   pass before demod, because the CB squelch reads it.
3. RDS front end processed once per block with its state in locals.
4. Resets are block-boundary requests owned by the DSP task (final design,
   §13). The interim generation counter was replaced because it could not be
   proven safe: a reset landing mid-write-back could leave mixed state.

Measured cost model: cycle counters show about 1 instruction per cycle; bare
byte sum = 8 cycles/sample. Interrupts, PSRAM data and XIP code placement made
no measurable difference for these loops, so cost is instruction count and FP
latency.

Interim live DSP load, 2.4 MS/s (before RDS batching; superseded by §12.1):

| Case | Before | After |
|---|---|---|
| FM | 68 % | 50 % |
| AM | 52 % | 36 % |
| CB | 51 % | 35 % |
| RF Lab 2.4 | 67 % | 45 % |

Signal-level stage: ~0.78 -> ~0.07 ms per block. Queue high-water 0, no new
steady-state IQ drops, no watchdog resets. RDS alone measured 0.56 ms per FM
block before batching.

### 12.1 Stage 1 final (release build, 2026-09-26)

Build switches are now explicit. CMake `ORCSDR_DSP_AB` (default 0) and
`ORCSDR_DSP_STAGE_TIMING` (default 1) are always passed by
`tools/build-tab5-idf.ps1`; `install-tab5.ps1` forwards `-DspAb` /
`-NoDspStageTiming`. So a cached value from an earlier test build cannot leak
into a release build. With A/B off, the harness buffers (~23 MB PSRAM when
armed), the `RTL_DSP AB ...` / `RTL_RDS_REPLAY_SAMPLE` commands and all hooks
compile out.

**Correctness (A/B firmware, same saved IQ, old vs new, final code):**

| 64 live blocks | FM | NFM (Weather) | AM | CB AM | CB LSB | CB USB |
|---|---|---|---|---|---|---|
| Audio byte diffs | 0 | 0 | 0 | 0 | 0 | 0 |
| State after every block | identical | identical | identical | identical | identical | identical |
| Meter / level diffs | 0 | 0 | 0 | 0 | 0 | 0 |

- RDS live A/B: 0 state diffs, 126 200 chips both ways.
- **RDS replay** (8 s MPX capture from SD): batched feed vs old per-sample feed
  give the same full-state hash (`1782db85`) and 75 999 chips each. The replay
  path now converts each 512-sample chunk and calls `rds_process_mpx_block`.
- **Reset stress during FM:**
  - 4 000 mixed reset requests (demod, RDS, SSB BFO, 1 in 25 full) from the
    command task with 0-2 ms gaps: 587 applied at block boundaries (coalesced,
    one per block), 0 left pending, audio flowing (12 chunks per 500 ms).
  - 30 real hot retunes via `RTL_UI ACTION FM UP/DOWN`.
  - RDS relocked with full PS/RT after both. No watchdog or other resets.

**Found during final measurement: inlining cost 20 % on FM.** In the release
build GCC inlined `demodulate_fm` and `demodulate_ssb` into `rtl_dsp_task`, and
the FM loop ran 3.73 ms/block instead of 3.00 ms. The A/B build, where the
harness also calls them, keeps them out of line. Both are now
`__attribute__((noinline))`. This is a code-layout change only; the release
build now matches the A/B-verified code generation.

**Performance, release build, 2.40 MS/s** (block = 16 384 samples = 6.83 ms;
steady 23 s windows; stage timing on):

| Case | Load before | Load after | Block avg / max (ms) | Demod (ms) | RDS (ms) | Level (ms) | Spectrum (ms) |
|---|---|---|---|---|---|---|---|
| FM (stereo + RDS) | 68 % | **45 %** | 3.10 / 4.26 | 3.00 (was 3.90) | 0.31 | 0.065 (was 0.77) | 0.03 |
| AM | 52 % | **35 %** | 2.45 / 4.40 | 2.33 (was 2.79) | - | 0.071 | 0.04 |
| CB AM | 51 % | **35 %** | 2.42 / 4.05 | 2.31 (was 2.72) | - | 0.069 | 0.03 |
| CB LSB | n/a | **30 %** | 2.07 / 3.25 | 1.96 | - | 0.069 | 0.03 |
| RF Lab 2.40 (CB) | 67 % | **45 %** | 3.13 / 7.30 | 2.76 (was 3.42) | - | 0.11 (was 0.95) | 0.25 |

In every steady window:
- queue high-water 0, backlog blocks 0, overload yields 0;
- audio dropped chunks 0, audio ring overruns 0, speaker submit failures 0;
- watchdog resets 0 (every boot in these runs was `reset_reason=11`, the USB
  reset from flashing).

Other observations:
- Windows that include a band switch show 1-3 overload yields and max block
  times of 9-13 ms, which are tune transients.
- FM showed 2 driver overruns / 2 driver drops / 2 OrcSDR pipeline drops, all
  at stream start. They are cumulative counters and did not grow in steady
  state. AM, CB and RF Lab showed 0.
- `queue_hwm` is the filled-queue depth sampled **right after** a block is
  dequeued. It counts blocks waiting behind the one being processed, so 0
  means keeping up.

**Profiler observer effect** (same firmware, `-NoDspStageTiming`):

| Case | Block avg, timing on | Block avg, timing off |
|---|---|---|
| FM | 3.102 ms | 3.105 ms |
| AM | 2.446 ms | 2.446 ms |
| CB LSB | 2.066 ms | 2.060 ms |

The instrumentation cost is below the measurement noise, so stage timing stays
on in release builds.

**Memory (release, FM streaming):**

| Pool | Value |
|---|---|
| Internal free / min | 151.7 KB / 151.0 KB (182 KB at 4 s after boot, before USB/Wi-Fi) |
| Largest internal block | 69 632 B |
| DMA-capable free / largest | 112 KB / 69 632 B |
| PSRAM free / largest | 24.74 MB / 24.64 MB (24.35 MB with RF Lab open) |

Stage 1 adds no internal SRAM. The only new buffer is the 8 KB MPX block in
PSRAM.

**Stage 1 gate (FM ≤ ~45 % at 2.4M): met.**

## 13. DSP state ownership and reset contract (Stage 1 final)

`rtl_audio` (all demod, RDS and audio-conditioning state) has a single owner:
the `rtl_dsp` task.
- Other tasks never write it. They call `rtl_dsp_request_reset(bits)`, an
  atomic OR into `rtl_dsp_reset_requests`.
- At the top of each block, before any processing, the DSP task runs
  `rtl_dsp_apply_reset_requests()`: it exchanges the mask to 0 and applies it
  in the fixed order full → demod (includes RDS) → RDS → SSB BFO.
- A reset therefore always lands between blocks. It cannot interleave with a
  demodulator's end-of-block write-back, so no stale or partial state is
  possible and no lock surrounds the DSP loop.

Writers and callers, classified by task:

| Caller | Task | Now |
|---|---|---|
| Demodulators, RDS block, `shape_audio_sample`, `queue_audio_samples`, `flush_audio_play_batch`, `rds_publish_state` | rtl_dsp | owner |
| FM/AM/SW dashboard actions, `scan_retune` (AM presets), driver hot-tune (`rtl_audio_reset_demod_filters`) | UI loop / driver app task | request `demod` |
| `apply_cb_mode` | UI loop | request `demod` (restarts the BFO too) |
| CB clarifier step | UI loop | request `ssb_bfo` |
| Stream start (`rtl_audio = {}` before) | driver app task | request `full` |
| `rds_replay` | command context | immediate `rtl_rds_reset_now()`; refused while the radio streams |
| Legacy `run_rtl_capture` (`RTL_USE_LEGACY_USB`, compiled out) | its own task, which also demodulates | immediate `_now()` |

Reset behavior by state class:

| Class | Fields (examples) | Demod reset (retune / mode) | Full reset (stream start) |
|---|---|---|---|
| 1. Continuity-critical filter / NCO | IQ LPFs, boxcar sums and phase, discriminator previous sample, channel filter, audio decimator, pilot/sub resonators, de-emphasis, DC, envelope, SSB BFO, RDS band-pass/NCO/LPF/timing tracks | zeroed / nominal | zeroed |
| 2. Audio conditioning | `agc_gain`, `agc_level`, `fade_in`, `last_out` | kept; `fade_in` clamped to ≤ 48 samples (short fade, no AGC re-acquire) | zeroed |
| 3. Telemetry / meters | `peak`, `square_sum`, `samples`, chip counts, signal dBFS, clipping, queued/dropped chunks | per-block values recomputed every block; counters kept | cleared |
| 4. Published UI | RDS PS/RT/PI, stereo lock, RDS carrier | RDS text cleared when the RDS reset is applied | cleared |

Remaining races (documented, accepted):
- **Old-LO blocks after a retune:** up to the queue depth (2 filled + 1 in
  flight) of blocks captured before the retune can be processed after the reset
  is applied. This existed before Stage 1. Only the Stage 4 transition marker in
  `RtlIqBlock` (§11) can fix it, by resetting on the first block after the LO
  actually changed.
- **Deferred application:** a request is applied at the next block, about 7 ms
  while streaming. With the radio stopped it stays pending until the next
  stream start, where the full reset supersedes it. The RDS text clear is
  deferred the same way.
- **Telemetry readers:** UI tasks read class-3/4 fields without locks. These are
  aligned 32-bit words on RV32, so there is no tearing, only one-block
  staleness. The RDS text strings were not re-audited in Stage 1.
- **`rds_replay`:** it runs only when `rtl_capture_state != running`. A block
  still in flight during `stopping` could overlap. This is a developer command,
  and the window was not widened by Stage 1.

## 14. Stage 2 notes (benchmark lab first; no live integration before the gate)

- **Candidate B, exact ratios:** ÷2 ÷2 halfbands, then rational L/M to 240k:
  2.40 → 600k → **2/5**; 2.56 → 640k → **3/8**; 2.88 → 720k → **1/3**;
  3.20 → 800k → **3/10**.
- **Halfbands:** short (7-11 tap) designs that exploit the zero taps and
  symmetry, and compute only the surviving output (decimate in the filter, never
  filter then discard). Integer in, int16 out.
- **The rational polyphase is also the channel filter.** Its prototype sets the
  240k passband for WFM, so no separate channel FIR runs at 240k.
- **Keep 240k only for WFM initially.** AM/SSB/NFM move to their own channel
  rates in later stages.
- **Spectrum and demod rate domains stay separate.** The spectrum keeps full
  device-rate IQ; only the demod path is decimated.
- **PIE SIMD** is a candidate for the halfbands, benchmarked separately from the
  **hardware-loop (`esp.lp.setup`) risk**: rev 1.3 has reported HWLOOP state loss
  across context switches (§11). Any PIE kernel is tested with and without
  HWLOOP, against a scalar oracle, under forced preemption (Wi-Fi/UI/USB), with
  a soak.
- **Q15 numeric requirements:**
  - coefficients quantized with the passband ripple and stopband attenuation
    re-checked after quantization;
  - accumulators ≥ 32-bit with no overflow at full-scale CU8 (±127 × Σ|h|);
  - rounding (not truncation) on output;
  - measured SNR / spur floor versus a float reference on synthetic tones and
    saved IQ.
- **Optional experiment:** a CIC ÷4 (with a short compensator) versus two
  halfbands, measured for cost versus droop and aliasing.
- **New modules live in `dsp/`, not `main.cpp`** (layout in §9). Host tests run
  the same sources.
- **Stage-2 benchmark gate** (on the Tab5; results reviewed before any frontend
  is chosen or connected):

| Device rate | Frontend cycles/sample (target) | ms per 16K block | Passband ripple / stopband | vs float oracle (SNR, max err) | Preemption stress + soak |
|---|---|---|---|---|---|
| 2.40 MS/s | measure | measure | spec | measure | pass/fail |
| 2.56 MS/s | measure | measure | spec | measure | pass/fail |
| 2.88 MS/s | measure | measure | spec | measure | pass/fail |
| 3.20 MS/s | measure | measure | spec | measure | pass/fail |

Live multirate FM integration, ESP-IDF migration and any esp-rtl-sdr change
are out of scope until the gate is reviewed and explicitly authorized.
