# OrcSDR Native FT8 Decoder — Phase 0 Research

Status: **PHASE 0 APPROVED 2026-10-07 — Phase 1 implementation is in progress on this branch**

Research date: 2026-10-07

Research branch: **codex/ft8-native-decoder-research**

Base: OrcSDR main at **0b8be74ecfa58a315d07210f953f0fd5626943e2**

Scope: receive-only FT8 decoding on the M5Stack Tab5 / ESP32-P4 under ESP-IDF 5.5.4.

No decoder code is introduced by this phase.

---

## 1. Executive recommendation

OrcSDR should build an independent FT8 decoder optimized around its actual product constraints rather than port WSJT-X or vendor an existing embedded decoder.

The recommended starting architecture is:

1. **Incremental 12 kHz analysis** during the receive slot, so most spectral work is completed while samples arrive rather than after the slot.
2. **Two-stage Costas synchronization search**: a cheap broad search for plausible time/frequency candidates followed by candidate-local refinement of DT, tone frequency, and optionally linear drift.
3. **Candidate-local soft demodulation** using normalized per-tone energy/noise estimates, with max-log metrics as the baseline and a more probabilistic metric retained as a benchmark alternative.
4. **An independently written normalized-min-sum LDPC(174,91) decoder with early syndrome termination** as the bounded embedded baseline.
5. **A strictly budgeted fallback tier** for near-codewords: OSD-lite or another reliability-ordered reprocessing method only when the candidate is close enough to justify the cost.
6. **At most one initially optional subtraction/re-search pass**, triggered only after a valid decode and only if CPU budget remains.
7. **Static working memory**: large slot/spectrogram storage in PSRAM; hot LLR, LDPC graph/message, and candidate scratch in internal RAM; no allocation in the per-slot hot path.
8. **A low-priority decoder task whose affinity is chosen only after measuring OrcSDR's existing task/core load.** The receiver/audio path always wins.
9. **No a-priori (AP) decode in the first production decoder** unless the Decode contract can explicitly mark assisted results. An AP result must never look identical to an ordinary over-the-air decode.
10. **CRC plus protocol/message plausibility validation** before any Decode record is emitted. Energy or sync evidence alone is never a decode.

The primary optimization goal should be **bounded real-time behavior on the P4 without starving SDR/audio/UI**, not matching a desktop decoder's unlimited search depth. Sensitivity and decode count are then improved experimentally inside that hard budget.

---

## 2. Scope and non-goals

### Phase 0 scope

This document covers protocol facts, existing decoder strategies, embedded constraints, ESP32-P4 capabilities, clean-room provenance, proposed architecture, risks, and a benchmark plan.

### Explicit non-goals

- No transmit support.
- No FT8 waveform transmitter, PTT, CAT transmit, or RF output path.
- No decoder implementation in this phase.
- No changes to OrcSDR's existing RTL-SDR DSP, demodulation, or filters.
- No claim of Tab5 hardware verification.
- No imported WSJT-X, ft8_lib, ft8mon, mfsk-core, or other decoder source.
- No third-party recordings committed without a clear redistribution license.

The Phase 1 test encoder requested by the project plan is **host-test-only**. It exists only to synthesize deterministic receive fixtures. It must not be compiled into the Tab5 firmware and must have no radio/TX integration.

---

## 3. Integration boundary that exists today

Current main does **not** yet contain the FT8 UI/backend files. They live on the separate UI sandbox branch. This decoder branch remains based on current main and does not copy that UI work.

For integration planning only, the read-only UI branch currently defines DecoderBackend with this contract:

- begin(context, sample_rate_hz)
- reset(context)
- begin_slot(context, slot_epoch)
- offer_audio(context, int16 samples, count)
- finish_slot(context, Decode output, capacity)

Its documented expected input is mono signed 12 kHz PCM, already tuned/demodulated as USB audio with conventional FT8 energy in roughly the 200–3000 Hz audio passband.

The Decode record currently carries:

- UTC epoch
- SNR dB
- DT milliseconds
- audio-frequency offset
- synchronization score
- message text
- callsign
- grid
- decode kind

**Recommendation:** preserve this seam for the baseline decoder. It is adequate for streaming/incremental analysis because offer_audio can process chunks instead of merely buffering them. A future seam change is justified only if a measured requirement cannot be expressed through it.

One likely future integration pressure is AP-assisted decode provenance: the current Decode record has no explicit "assisted" flag. The safe Phase 1 decision is therefore to **defer AP decoding** rather than silently mix assisted and ordinary results.

---

## 4. FT8 protocol facts — verified

Primary protocol authority for this design is Franke, Somerville, and Taylor, "The FT4 and FT8 Communication Protocols," QEX, July/August 2020 [P1]. The WSJT-X user documentation is a secondary cross-check [P2].

### 4.1 Timing and modulation

| Property | FT8 value | Basis |
|---|---:|---|
| T/R sequence | 15 s | [P1], [P2] |
| Symbol interval | 0.160 s | [P1] |
| Symbol/keying rate | 6.25 baud | 1 / 0.160 s; also stated by [P2] |
| Modulation | 8-tone continuous-phase FSK with Gaussian smoothing, BT=2 | [P1] |
| Tone spacing | 6.25 Hz | reciprocal of symbol interval / orthogonal tone spacing |
| Information payload | 77 bits | [P1] |
| CRC | 14 bits, polynomial 0x6757, initial zero | [P1] |
| Message + CRC | 91 bits | [P1] |
| FEC | LDPC(174,91), 83 parity bits | [P1] |
| Coded data symbols | 58 symbols, 3 coded bits per 8-FSK symbol | [P1] |
| Sync | three 7-symbol Costas blocks using permutation 3,1,4,0,6,5,2 | [P1] |
| Total symbols | 79 = 58 data + 21 sync | [P1], [P2] |
| On-air frame duration | 12.64 s | 79 x 0.160 s |
| Nominal occupied bandwidth | about 50 Hz | [P2] |

The phrase "three 7x7 Costas arrays" is often used informally. More precisely for implementation: FT8 inserts **three 7-symbol synchronization blocks** using a seven-tone Costas permutation.

### 4.2 Source/message encoding

FT8 carries exactly 77 source bits. Message type fields define standard calls, grids, reports, acknowledgements, free text, telemetry, and other structured forms [P1].

Standard and non-standard call handling includes hashed-call forms. The current specification uses 10-, 12-, and 22-bit hashes in different message types. A hash is not a unique callsign identity: collisions are possible, and resolution depends on callsigns previously learned by the receiver [P1].

**OrcSDR implication:** a hash-resolved callsign must be treated as a mapping/inference from the receiver's known-call table, not as fresh literal information sent in full by that frame. The decoder core should retain enough metadata internally to distinguish literal/source fields from hash resolution even if the initial UI seam only exposes text.

### 4.3 CRC and LDPC

The 77 source bits receive a CRC-14 to form 91 bits. The published CRC process zero-extends the source message for the calculation and uses polynomial 0x6757 with zero initialization [P1].

The 91-bit word is encoded into the FT8-specific LDPC(174,91) codeword by adding 83 parity bits. The protocol is fully defined by its generator and sparse parity-check matrices [P1].

A decode must satisfy more than "some FT8-looking energy exists." At minimum the production acceptance path should require:

1. synchronization/candidate criteria;
2. a valid LDPC codeword or bounded fallback result;
3. correct CRC;
4. source-message unpacking that obeys the assigned type/range rules;
5. duplicate suppression so repeated search paths do not create duplicate records.

CRC alone is not a sufficient *search* discriminator: arbitrary garbage hypotheses must not be sprayed through the CRC until one happens to pass.

### 4.4 Protocol-version warning

Older 2017 FT8 material describes the original protocol version with different payload/FEC parameters. It is not normative for current FT8. OrcSDR must use the 77-bit / CRC-14 / LDPC(174,91) protocol documented in the 2020 QEX paper and current WSJT-X documentation.

---

## 5. What modern FT8 decoders do

### 5.1 WSJT-X reference architecture

WSJT-X is the protocol's reference implementation and is GPLv3-or-later. This Phase 0 study uses the QEX paper's published algorithm description as the primary source rather than transposing WSJT-X source.

The QEX receive pipeline can be summarized as:

1. spectral analysis identifies likely signals/candidates;
2. each candidate is synchronized/refined in time and frequency;
3. soft bit information is estimated, including noncoherent block metrics over several symbol-block lengths;
4. belief propagation (BP) attempts LDPC decoding;
5. OSD is used as a more expensive fallback when BP fails;
6. successfully decoded waveforms are reconstructed and subtracted;
7. the residual is searched again, with second and sometimes third decode passes;
8. optional a-priori information can constrain expected message bits.

The paper reports that most successful codewords need only a few BP iterations, while OSD improves difficult cases at higher computational cost. For FT8 in the paper's AWGN simulation, the 50% thresholds in a 2500 Hz reference bandwidth were approximately -19.6 dB for single-symbol BP, -20.3 dB after the multi-symbol/block-detection improvement, and -20.8 dB with BP+OSD [P1]. These are **reference-algorithm simulation results**, not OrcSDR performance claims.

Signal subtraction is particularly valuable on busy FT8 channels because a strong decoded signal can mask a weaker overlapping one. The reference implementation estimates a channel-modified version of the decoded waveform, subtracts it, and re-runs candidate analysis [P1].

AP decoding can gain several dB in favorable cases, but the QEX authors explicitly note an increased false-decode rate [P1].

Historical WSJT-X profiling exposes function families such as sync8, ft8b, bpd174, and osd174, confirming that candidate processing and FEC dominate a large fraction of decode work in that generation [D1]. The historical timing numbers are not used as modern performance targets.

**Fit for OrcSDR:** excellent algorithmic reference, poor direct implementation fit. Desktop WSJT-X can spend more CPU and memory, use deeper fallback search, and is not organized around OrcSDR's real-time ESP32-P4 task/memory constraints.

### 5.2 kgoba/ft8_lib

ft8_lib is MIT-licensed C intended specifically for experimental embedded FT8/FT4 use [D2].

Verified characteristics:

- lightweight decoder/encoder;
- demonstrated on fast microcontrollers including STM32F7-class systems;
- example FT8 decoder uses slightly under 200 KB RAM;
- needs access to essentially the whole receive window in spectral-magnitude form;
- candidate search and decode are bulk/late-slot rather than genuinely incremental;
- incremental decoding is listed as a future idea, not an implemented feature;
- its example SNR handling is incomplete enough that an open issue explicitly calls out SNR computation as TODO [D3].

**Strength:** it proves that FT8 decode can fit in MCU-scale memory and offers a simple architecture to benchmark.

**Weakness for OrcSDR:** the whole-slot/bulk model leaves CPU idle early and creates a post-slot burst exactly when OrcSDR also needs to remain responsive. Its lightweight design also does not attempt the full desktop-style OSD/subtraction depth.

**Use policy:** external benchmark/reference only. Although MIT permits reuse with attribution, the recommended OrcSDR path is not to vendor or port it.

### 5.3 Robert Morris / AB1HL ft8mon

ft8mon is an MIT-licensed desktop C++ FT8 receiver using FFTW [D4]. It is substantially more aggressive than ft8_lib.

Its current source exposes configurable:

- coarse candidate search and multi-stage time/frequency refinement;
- 25 LDPC iterations by default;
- OSD depth and eligibility thresholds;
- several spectral-subtraction passes;
- a-priori/hint behavior;
- multiple threads;
- candidate-overlap and search-window tuning.

It uses a 1920-sample FFT at 12 kHz in its current code, which naturally produces 6.25 Hz bins for one FT8 symbol [D5].

**Strength:** useful evidence for the value of refined synchronization, OSD, and repeated interference cancellation.

**Weakness for OrcSDR:** it is desktop-oriented, uses FFTW and multiple threads, and exposes a large search/complexity surface. Blindly matching its pass depth on a microcontroller would make worst-case latency difficult to bound.

**Use policy:** algorithmic comparison and external benchmark only; no source copied.

### 5.4 mfsk-core

mfsk-core is a modern Rust WSJT-family implementation with a serious embedded path [D6]. It is GPL-3.0-or-later and explicitly states that its algorithms are derived from WSJT-X, so it is **not a clean-room source for OrcSDR implementation**.

It is still useful as feasibility evidence:

- FT8 embedded decode can run with roughly 150 KB usable RAM when paired with an external FFT backend;
- its embedded path uses a fixed-point spectrogram and DFT while retaining floating-point LLR/BP by default, with an optional fixed-point LLR path;
- published reference working-set figures include hundreds of KB of PSRAM spectrogram storage and much smaller BP scratch;
- its documentation reports on-device, off-air embedded FT8 decode and post-slot timings on ESP32-S3-class M5 hardware.

**Lesson, not code:** a hybrid representation can be attractive — compact/fixed spectral storage, but float or wider fixed point where LDPC soft-information resolution matters.

### 5.5 QMX Panadapter and Mini-FT8

QMX Panadapter is especially relevant feasibility evidence because it runs FT8 decoding on the same M5Stack Tab5 / ESP32-P4 family. Its published documentation reports continuous on-device FT8 reception and busy-slot results in the tens of callsigns [D7].

However, it consumes low-rate USB UAC I/Q from a QMX radio. OrcSDR's RF path and scheduler are different. Therefore this is **proof that the P4 has enough general compute for serious FT8 work**, not proof that its architecture or performance transfers directly.

Mini-FT8 runs FT8 on M5 ESP32-S3-class hardware and is built on ft8_lib [D8]. It reinforces MCU feasibility but is not an independent decoder design.

---

## 6. ESP32-P4 / Tab5 facts we can rely on

### 6.1 Verified hardware/software facts

| Fact | Status | Source / repository evidence | Design consequence |
|---|---|---|---|
| Tab5 main SoC is ESP32-P4NRW32 | VERIFIED | M5Stack Tab5 docs [H1] | Native P4 optimization is relevant |
| HP CPU is dual-core RISC-V | VERIFIED | M5Stack [H1], Espressif v1.3 datasheet [H2] | Decoder can be separated from critical receiver work |
| Tab5/P4 v1.3 HP max clock is 360 MHz | VERIFIED | M5Stack [H1], v1.3 datasheet [H2] | Do not budget against old 400 MHz marketing figure |
| ISA includes single-precision floating point ("F" in RV32IMAFC) | VERIFIED | Espressif [H2]; Espressif product docs also state single-precision FPU | f32 is a legitimate benchmark candidate |
| P4 has custom XespV/PIE DSP/SIMD features and hardware loops | VERIFIED | v1.3 datasheet [H2], P4 TRM [H3] | Optimized fixed/float kernels are possible |
| PIE architecture includes 128-bit vector registers and 8/16/32-bit SIMD forms | VERIFIED in Espressif TRM | [H3] | Potential acceleration for fixed-point spectral/LLR loops |
| esp-dsp has P4-optimized FFT implementations callable from C/C++ | VERIFIED | esp-dsp [H4], source headers [H5] | Prefer supported esp-dsp kernels before custom assembly |
| Tab5 has 32 MB Octal PSRAM | VERIFIED | M5Stack [H1] | Whole-slot/spectrogram storage is practical |
| OrcSDR enables PSRAM at 200 MHz | VERIFIED in main sdkconfig.defaults | CONFIG_SPIRAM_SPEED_200M=y | Plenty of capacity, but bandwidth contention matters |
| OrcSDR enables PSRAM XIP | VERIFIED in main sdkconfig.defaults | CONFIG_SPIRAM_XIP_FROM_PSRAM=y | Decoder must not assume exclusive PSRAM bandwidth |
| OrcSDR sets CONFIG_DSP_MAX_FFT_SIZE=32768 | VERIFIED in main sdkconfig.defaults | repository | Large supported table ceiling is not the limiting issue |
| Exact sustained PSRAM bandwidth under OrcSDR workload | **UNVERIFIED** | must benchmark on device | Do not budget from theoretical bus bandwidth |
| Best P4 FFT length/kernel for FT8 on current IDF/component revision | **UNVERIFIED** | must benchmark | Keep spectral front end swappable |
| Spare CPU time/core occupancy during live RTL RX | **UNVERIFIED** | must profile current firmware | Do not hard-code task affinity yet |

### 6.2 PIE/XespV use from normal C/C++

The P4 TRM describes PIE/XespV as custom SIMD/DSP instructions. Direct hand-written PIE requires architecture-specific handling and is not the first recommendation.

Espressif's esp-dsp library already contains P4-specific assembly paths, including P4 float and fixed-point FFT entry points, and selects optimized variants through configuration [H4, H5]. This gives OrcSDR a maintainable C/C++ entry point to P4 acceleration.

**Recommendation:** Phase 1 core remains plain host-buildable C++. The firmware adapter may substitute an esp-dsp backend behind a narrow spectral/FFT interface. Only write custom PIE assembly if profiling proves the official kernels cannot meet the budget.

### 6.3 The 1920-point FFT problem

At 12 kHz, exactly one FT8 symbol is 1920 samples, and a 1920-point DFT gives 6.25 Hz bin spacing. ft8mon uses that convenient geometry [D5].

Espressif's common optimized radix-2 FFT path requires a power-of-two length; the source rejects invalid lengths [H6]. Therefore OrcSDR must **not assume it can simply request a P4-optimized 1920-point esp-dsp FFT**.

Phase 1 should benchmark at least these alternatives:

1. 2048-point window/zero-padding plus local frequency interpolation;
2. power-of-two STFT with candidate-local exact-tone correlation;
3. a pruned DFT/Goertzel-style tone bank for only the frequency cells needed;
4. a mixed approach: cheap power-of-two broad search, then precise 6.25 Hz candidate-local correlations.

The mixed approach is the current recommendation because it lets broad search exploit esp-dsp while preserving exact FT8 tone geometry only where it matters.

---

## 7. OrcSDR-specific receive realities

Existing desktop examples usually assume clean sound-card audio. OrcSDR's decoder must be evaluated against its own RF chain.

### 7.1 RTL-SDR frequency error and drift

The decoder receives 12 kHz USB audio, so RF tuning error appears as audio-frequency offset. Dongle oscillator ppm, temperature drift, tuner state, and receiver retunes can push FT8 energy away from its expected location.

**Hypothesis:** use a deliberately generous coarse audio-frequency search followed by candidate-local refinement instead of requiring tones to sit exactly on nominal bins.

**Measure:** inject known frequency offsets and linear drift; record recall and CPU as search width expands. Repeat using real RTL-SDR captures at cold start and thermal equilibrium.

### 7.2 8-bit front-end and strong adjacent signals

RTL-SDR CU8 front ends have limited instantaneous dynamic range. Strong nearby carriers or broadcast/interference can raise the effective noise floor or clip before the FT8 decoder ever sees 12 kHz PCM.

**Hypothesis:** candidate metrics normalized to a local noise/neighbor estimate will outperform raw magnitude thresholds. Strong decoded FT8 signals should optionally be subtracted before a second search pass.

**Measure:** synthetic quantization/clipping sweeps plus real captures with strong adjacent/intersecting signals. Track false candidates, valid decodes, and weak-signal recovery.

### 7.3 OrcSDR-owned 12 kHz USB audio

The project requirement is explicit: the FT8 decoder consumes the existing mono 12 kHz USB audio and does not change the receiver DSP/filter path.

That constraint is desirable for clean ownership. The decoder can characterize its input, but Phase 1 must not "fix" the upstream demodulator inside FT8 code.

**Measure:** preserve 12 kHz PCM test captures at the exact DecoderBackend boundary. This lets us distinguish RF/frontend problems from decoder regressions.

### 7.4 Time accuracy / DT

FT8 uses synchronized 15-second periods but real stations do not all start at exactly the same offset. The QEX paper explicitly studies tolerance to late starts.

OrcSDR main already provides time_service::Snapshot with UTC and wallclock_valid. The production FT8 runtime must refuse to start normal slot decoding when wallclock_valid is false.

**Hypothesis:** search DT across a practical window around the nominal start instead of trusting RTC/NTP to sample accuracy. Clock validity selects the slot; Costas synchronization estimates fine DT.

**Measure:** inject +/- timing offsets and measure recall/DT accuracy. Separately test RTC-only, NTP-resynced, and intentionally invalid-clock device cases.

---

## 8. Concrete improvement hypotheses

These are hypotheses, not promises. Every one has a required measurement before it becomes production policy.

| Stage | Hypothesis | Expected benefit | Cost/risk | Measurement |
|---|---|---|---|---|
| Slot acquisition | Accumulate analysis incrementally as PCM arrives | moves work out of post-slot burst; earlier result | more stateful code | acquisition CPU per 160 ms chunk; post-frame latency |
| Spectral front end | Power-of-two coarse FFT + candidate-local exact correlation | uses P4 optimized FFT while retaining FT8 resolution | two representations | recall/CPU vs 1920-point host reference |
| Spectrogram storage | u16/log-power or other compact cells in PSRAM | lower bandwidth/capacity than float grid | quantization can hurt weak signals | SNR-threshold delta vs float |
| Coarse sync | Costas matched score with local noise normalization | fewer false candidates under uneven RF | threshold tuning | ROC: candidate recall vs false-candidate count |
| Search grid | Coarse time/frequency grid then local refinement | much cheaper than dense full-band oversampling | can miss between-grid weak signals | recall/CPU over DT/frequency sweep |
| Drift | Estimate small linear frequency slope only for strong/credible candidates | handles dongle drift and some propagation | extra candidate cost | recall under injected Hz/s |
| Soft metrics | noise-normalized max-log baseline | cheap LLR generation | may leave sensitivity on table | 50% decode SNR |
| Soft metrics | probability/log-sum-exp variant as benchmark | potentially better reliability ordering | float/log cost | sensitivity delta per CPU ms |
| LDPC | normalized min-sum with early syndrome exit | bounded, add/compare-heavy embedded loop | normalization must be tuned | FER/SNR vs BP reference, cycles |
| LDPC representation | f32 baseline versus Q-format LLR/messages | fixed point may exploit P4 SIMD and shrink scratch | quantization can cap recall | sensitivity and runtime on P4 |
| Candidate ordering | strongest/highest-sync first with dedupe | useful results sooner; bounded queue | weak candidates delayed | time-to-first/last decode |
| Fallback | OSD-lite only for near-codewords | recover difficult frames | combinatorial worst case | gain vs CPU and false-decode rate |
| Interference | one bounded subtraction pass after valid decodes | recover overlapped weaker signals | waveform/channel estimate cost | added valid decodes per CPU ms |
| AP | own/recent calls as optional priors | several dB possible in targeted cases | elevated false-decode risk | separate assisted FER/false rate |
| Message validation | CRC + legal field/range checks | suppress accidental acceptances | too-strict parser could reject legal messages | conformance corpus |
| Scheduling | decoder low priority, chunks/yields between candidates | protects SDR/audio/UI | longer decode tail | dropped IQ/audio blocks and deadline |

### Recommended initial complexity ladder

Do not implement every optimization at once.

**Baseline A**
- incremental spectrogram;
- two-stage Costas search;
- float soft metrics;
- normalized min-sum;
- no OSD;
- no subtraction;
- no AP.

**Baseline B**
- compact/fixed spectrogram benchmark;
- candidate-local refined metric;
- bounded OSD-lite.

**Baseline C**
- one subtraction/re-search pass.

Only advance if the prior baseline has deterministic host tests and measured Tab5 headroom.

---

## 9. Recommended module architecture for Phase 1

All protocol/DSP core modules must compile with plain g++ and include no ESP-IDF headers.

### Pure C++ core

**ft8_codec**
- 77-bit source packing/unpacking;
- callsign/grid/report encoding rules;
- callsign hash functions/table behavior;
- CRC-14;
- LDPC generator/parity representation;
- host-test-only encoder;
- production decoder FEC.

**ft8_sync**
- Costas scoring;
- coarse time/frequency candidate discovery;
- candidate dedupe/non-max suppression;
- local DT/frequency refinement;
- optional drift estimate.

**ft8_demod**
- candidate-local tone energies;
- noise estimation;
- soft bit/LLR generation;
- SNR estimator, but only after it is calibrated and validated.

**ft8_pipeline**
- incremental PCM ingestion;
- spectral history management;
- candidate queue;
- decode budget;
- duplicate suppression;
- CRC/plausibility gate;
- optional bounded second pass.

### Platform adapter

**ft8_decoder_backend_impl**
- implements the existing DecoderBackend seam once that seam lands in main/integration;
- allocates large buffers once at begin;
- bridges time/runtime and pure core;
- owns no UI.

### ESP-IDF runtime wrapper

- low-priority FreeRTOS task;
- task/core affinity selected from profiling, not assumption;
- esp_timer only for measurement/deadlines;
- NvsStore only for approved configuration;
- esp-dsp backend optional behind pure C++ interface;
- no Arduino APIs;
- no feature logic in main.cpp.

main.cpp should only register/route the decoder.

---

## 10. Incremental decode timing strategy

A common embedded implementation stores a complete slot and starts expensive work only at the end. OrcSDR should instead overlap capture and analysis.

A nominal FT8 waveform lasts 12.64 s inside a 15 s period. Once the final required symbols have arrived, the remaining part of the 15 s period is usable decode time.

Proposed sequence:

1. **During audio arrival:** maintain overlapping spectral columns and a cheap running Costas activity map.
2. **After enough first/middle sync is present:** maintain provisional candidate tracks, but do not emit a decode.
3. **After complete frame data is present:** finalize candidate refinement and FEC immediately; results can potentially appear before the next 15 s boundary.
4. **Before next slot:** stop optional deep work at a hard deadline and preserve receiver continuity.

There is no claim that the final message can be trusted before all required coded information is present. Early work means **early analysis**, not premature user-visible decodes.

---

## 11. LDPC recommendation

### 11.1 Baseline

Use an independently written **normalized min-sum (NMS)** implementation for LDPC(174,91) with:

- sparse static adjacency tables generated from the protocol parity matrix;
- fixed maximum iterations;
- syndrome check every iteration or at a measured useful cadence;
- immediate termination on valid parity;
- no dynamic allocation;
- instrumentation for iterations used and final unsatisfied-check count.

NMS is a standard reduced-complexity BP family described in the coding literature [A1]. It is attractive on the P4 because the hot loop is primarily compare/sign/add/scale rather than transcendental math.

This is a design hypothesis. Phase 1 must benchmark NMS against a straightforward sum-product/BP host reference written independently from generic coding literature.

### 11.2 Float versus fixed point

Do not assume fixed point is automatically better.

Start with f32 for correctness because the P4 supports single-precision floating point. Then benchmark a Q-format representation for LLR/check messages. P4 SIMD could make a carefully aligned i16 implementation attractive, but poor quantization can lose weak-signal recall.

Gate fixed-point adoption on:
- no meaningful sensitivity regression at the target operating point;
- measurable P4 speed/internal-RAM benefit;
- deterministic saturation behavior.

### 11.3 OSD-lite

OSD is valuable but can explode in cost. The WSJT-X paper uses OSD after BP failure and reports measurable sensitivity gains [P1]. Generic OSD literature supplies the algorithmic basis [A2].

For OrcSDR, fallback eligibility should require all of:
- credible FT8 synchronization;
- good enough soft confidence;
- LDPC result near a valid codeword (bounded unsatisfied checks);
- remaining per-slot CPU budget.

Depth/order must have an absolute small cap. The benchmark must report both additional valid decodes and CPU milliseconds consumed. "More decodes" is not sufficient if the next audio slot is harmed.

---

## 12. Signal subtraction recommendation

Subtraction is one of the most valuable "deep" features for a crowded FT8 watering hole, but it should be added only after baseline decode is stable.

Phase 1/2 design:

1. decode strongest credible candidates;
2. reconstruct only successfully CRC-validated frames;
3. estimate a simple candidate-local complex amplitude/phase or equivalent audio-domain fit;
4. subtract from an analysis copy, never from the actual receiver/audio stream;
5. run one additional search pass;
6. compare added decode count against cost and false-decode behavior.

Initial production should cap subtraction at **one pass**. A second/third pass can be studied later if P4 measurements show clear spare time.

---

## 13. AP / hint decoding policy

The QEX paper shows that AP information can improve sensitivity by several dB, but it also raises false-decoding risk [P1].

Useful OrcSDR priors could eventually include:
- the user's own callsign;
- a selected station;
- recently decoded callsigns;
- expected standard message class.

However, the current Decode model does not expose assisted provenance.

**Recommendation: AP OFF for the initial production decoder.**

If AP is later enabled:
- assisted results must carry explicit internal/output provenance;
- the UI must visibly distinguish them;
- ordinary and AP false-decode rates must be benchmarked separately;
- AP can never bypass CRC and legal-message validation.

---

## 14. Memory plan

### 14.1 Principles

- Allocate once in begin.
- No heap allocation in offer_audio or the candidate/FEC loops.
- Keep large sequential/cold data in PSRAM.
- Keep small hot random-access structures internal where possible.
- Use explicit capacities everywhere; no unbounded vectors/maps in firmware hot paths.
- Measure cache/PSRAM contention under live RTL operation.

### 14.2 Provisional budget categories

**PSRAM**
- slot or rolling PCM only if required;
- compact spectrogram/history;
- analysis copy used for subtraction;
- candidate history/logging buffers.

**Internal RAM**
- active candidate structure;
- LDPC LLRs;
- sparse graph/message scratch;
- FFT/correlation scratch that profiling proves latency-sensitive;
- task stack where practical.

Exact byte budgets are intentionally **UNVERIFIED** until the representation is selected. The first Phase 1 benchmark should print peak internal RAM, peak PSRAM, and allocation count after begin.

OrcSDR already uses PSRAM aggressively and enables PSRAM XIP, so "32 MB available" does not mean bandwidth is free.

---

## 15. CPU and task scheduling

The Tab5 has two 360 MHz HP cores, but OrcSDR already uses pinned workers and high-rate receive/DSP work.

Repository inspection shows several low-priority analysis/storage workers pinned to core 1, while other work has its own affinities. That is not enough to declare core 1 "the decoder core."

Before pinning FT8:

1. record current task list, priorities, and affinities during active 2.4 MS/s RTL reception;
2. collect per-core idle/runtime statistics if available;
3. run a synthetic decoder workload unpinned/low-priority;
4. measure audio/IQ drops, UI responsiveness, watchdog margin, and decoder latency;
5. only then choose an affinity.

Hard rule: **the decoder yields or abandons optional deep work before it can starve receive/audio.**

---

## 16. SNR and truthfulness

A decoder score is not automatically an SNR measurement. ft8_lib's own issue history is a useful warning here [D3].

OrcSDR should expose SNR only after an estimator is calibrated against known synthetic signals and checked on shared real recordings against an external reference.

The decoder should internally separate:

- energy estimate;
- synchronization score;
- soft-decode confidence;
- CRC/codeword validity;
- SNR estimate.

Only the last one is labeled SNR.

Likewise:
- energy is not FT8;
- a Costas match is not a decoded message;
- a CRC-valid, legally unpacked message is the minimum normal decode;
- a hash-resolved callsign is not the same as a fully transmitted callsign;
- an AP-assisted result is not an ordinary decode.

---

## 17. Phase 1 test corpus design

### 17.1 Spec-derived encoder

Build a host-only test encoder from the published protocol definition. It should produce deterministic FT8 frames and PCM fixtures for receive testing.

It must support controlled:

- payload/message type;
- audio frequency;
- DT;
- SNR;
- frequency offset;
- linear frequency drift;
- Gaussian noise seed;
- amplitude/quantization;
- multiple simultaneous signals;
- relative phase;
- clipping;
- adjacent continuous carriers/interference.

This encoder is test infrastructure only and is excluded from Tab5 firmware source lists.

### 17.2 Protocol unit vectors

Create deterministic vectors for:
- message pack/unpack;
- CRC;
- LDPC encode/parity;
- Gray mapping;
- Costas positions;
- callsign hashes;
- locator/report fields.

Prefer vectors calculated from the public protocol resources described by the QEX authors. Record the provenance of every vector.

### 17.3 Noise and false-decode corpus

Run large deterministic sets of:
- AWGN-only slots;
- colored/noise-shaped slots;
- strong non-FT8 tones;
- chirps/drift;
- multi-tone non-FT8 interference.

A pure-noise accepted-message count should be zero in the regression corpus, but reporting "0" is not statistically enough. For N independent no-false-positive trials, report the confidence upper bound on the underlying false-accept probability (approximately 3/N for a 95% zero-event upper bound).

### 17.4 Real recordings

Use owner-supplied recordings from the actual RTL-SDR/OrcSDR path.

Before committing any file:
- record who owns it;
- record whether redistribution is allowed;
- record capture parameters and expected reference decodes.

Unlicensed third-party WAV files may be used locally as external benchmarks but must not enter the repository.

---

## 18. Benchmark plan

Every optimization must be tested on the **same corpus**.

### 18.1 Correctness

- injected messages found / injected messages;
- duplicate outputs;
- message text correctness;
- DT error;
- audio-frequency error;
- legal-message unpack success;
- CRC false accepts;
- hash-resolution behavior.

### 18.2 Sensitivity

For each SNR point:
- fixed random seeds;
- enough independent trials to form useful confidence intervals;
- decode probability;
- 50% threshold interpolation.

Report SNR in the same declared reference bandwidth. Do not compare mismatched SNR conventions.

### 18.3 Complexity

Host:
- wall time;
- CPU time;
- time by stage;
- candidates searched;
- LDPC iterations;
- OSD attempts;
- subtraction work.

Tab5:
- cycles/time per stage using esp_timer or appropriate counters;
- post-frame decode latency;
- hard-deadline misses;
- internal RAM peak;
- PSRAM peak;
- stack high-water mark;
- allocation count after begin;
- binary-size delta;
- IQ/audio drops;
- watchdog events;
- UI responsiveness.

### 18.4 External references

Run WSJT-X and/or another reference decoder as **external executables on the same files**. Compare outputs only.

Do not use their source to "fix" OrcSDR implementation.

Report honestly:
- cases they decode and OrcSDR misses;
- cases OrcSDR decodes and they miss;
- latency/memory tradeoffs;
- any reduced search depth made to protect real-time behavior.

### 18.5 Initial performance gates proposed for owner approval

These are proposed gates, not current measurements:

- zero decoder-caused IQ/audio drops in the named Tab5 test;
- no watchdog reset;
- primary decode pass finishes before the next 15 s slot;
- stretch target: typical busy-slot primary results within 1.5 s of complete-frame availability;
- optional OSD/subtraction automatically stops before the next-slot safety margin;
- no allocation in the per-slot hot path;
- binary size delta explicitly recorded on every firmware integration build.

The prompt notes the release app is already roughly 13 KiB above a 256 KiB guard. That number has **not been re-measured in this Phase 0 cloud research** and must be re-established from a named Phase 1 build before being treated as current measurement.

---

## 19. Serial/offline test path planned for later

After owner approval and core tests, add an authenticated serial command consistent with OrcSDR's existing ORC command security model to feed a WAV/PCM file from SD through the decoder and print validated decodes.

Requirements:
- no RF required;
- no auto-detected serial ports;
- physical flashing uses COM17 only and warns before reboot;
- COM24 is never touched;
- build/flash procedure uses apps/orcsdr-tab5/tools/build-tab5-idf.ps1 from PowerShell;
- command documented in docs/API_SERIAL_CLI.md;
- test input reaches the same core path as live DecoderBackend audio.

No serial command is implemented in Phase 0.

---

## 20. License and provenance

### 20.1 OrcSDR license

The OrcSDR repository's current root LICENSE is **GNU Affero General Public License v3 (AGPLv3).**

GPLv3 and AGPLv3 have a special compatibility mechanism that allows GPLv3 code and AGPLv3 code to be combined, with the AGPL terms applying to the combined work [L1, L2].

Therefore it would be inaccurate to say "WSJT-X GPLv3 is legally incompatible with OrcSDR AGPLv3."

That does **not** change this project's provenance rule.

### 20.2 Clean-room rule for OrcFT8

For the native decoder:

**Allowed implementation authorities**
- the published FT8 protocol specification/QEX paper;
- the QEX authors' explicitly public-domain protocol resources;
- generic communications/DSP/FEC literature;
- our own measurements and designs.

**Reference/benchmark only**
- WSJT-X GPL source;
- mfsk-core GPL source;
- ft8mon source, despite MIT;
- ft8_lib source, despite MIT;
- any other full decoder implementation.

**Rule:** no source code, control-flow transcription, lookup table copied from an implementation, or line-by-line port from an existing decoder enters OrcFT8. Phase 1 work is written from the protocol, generic algorithms, this design notebook, and our tests.

MIT code could legally be incorporated with attribution and license preservation, but doing so would weaken the "our own decoder" goal. Default policy is therefore **no implementation reuse even from MIT decoders**. If the owner later authorizes an MIT helper, record exact repository, commit, file, license, copied/modified scope, and attribution before merging it.

WSJT-X's own QEX paper explicitly welcomes independent conforming implementations and identifies its reference [14] helper resources as public domain [P1].

### 20.3 Design notebook / provenance ledger

| Idea or fact | Source class | Permitted influence |
|---|---|---|
| FT8 bit fields, CRC, LDPC matrices, Costas, modulation | QEX/public-domain protocol resources | normative implementation |
| BP + OSD hybrid value | QEX published algorithm description | architectural hypothesis |
| signal subtraction and multipass value | QEX published description | architectural hypothesis |
| AP sensitivity/false-decode tradeoff | QEX published measurements | policy/benchmark hypothesis |
| normalized min-sum LDPC | generic LDPC literature [A1] | independently implement |
| ordered-statistics fallback | generic OSD literature [A2] | independently implement bounded variant |
| incremental embedded analysis | our scheduling design; ft8_lib limitation is comparison evidence | independently design |
| compact spectrogram + hot high-resolution LLR | our design; embedded projects show feasibility | independently benchmark |
| P4 optimized FFT/SIMD | Espressif docs/esp-dsp | platform backend |
| ft8_lib, ft8mon, mfsk-core code | external implementations | benchmark/understanding only; no code transfer |

This ledger should remain in the decoder documentation and be expanded whenever a new algorithmic source materially influences the design.

A formal two-person clean-room process would require one researcher to produce a prose specification and a separate implementer who never viewed restricted source. This project has not established that litigation-style separation. The practical repository rule here is stricter source provenance and no code transposition; if the owner wants formal clean-room separation, that is an explicit decision before Phase 1.

---

## 21. Risk register

| Risk | Why it matters | Mitigation / gate |
|---|---|---|
| P4 CPU budget is overestimated | decoder can starve live SDR/audio | on-device stage timing + drop counters before enabling deep modes |
| PSRAM contention | XiP + display/network/radio can share external memory bandwidth | compact sequential buffers, hot state internal, measure under full UI |
| FFT geometry mismatch | exact 1920 sample symbol does not fit common optimized radix-2 path | benchmark mixed FFT + exact local correlation |
| Weak-sync candidate explosion | busy/noisy band causes unbounded decode cost | hard candidate cap, NMS ordering, per-slot deadline |
| NMS loses sensitivity | simplified FEC may underperform BP | host threshold curve and reference comparison |
| OSD complexity explosion | combinatorial fallback can miss deadline | near-codeword eligibility + small fixed order + budget cutoff |
| Subtraction model mismatch | bad subtraction can erase real weak signals/create artifacts | subtract only CRC-valid signals, one pass initially, compare net gain |
| RTL frequency drift/ppm | tones miss narrow search grid | wide coarse search + local refinement/drift test |
| Clock invalid/offset | wrong slot framing | time_service wallclock_valid gate + Costas DT refinement |
| SNR mislabeled | violates OrcSDR truthfulness | calibrated estimator gate; otherwise expose sync/quality only |
| CRC-only false accepts | large search space can eventually hit CRC | LDPC + CRC + legal message parsing/plausibility + bounded search |
| Callsign hash ambiguity | UI could imply identity not literally transmitted | cache provenance and angle-bracket/unknown semantics |
| AP false decodes | priors can force plausible-looking results | defer initially; explicit assisted flag later |
| Binary growth | app already has little/no guard headroom | size delta on every integration build; no test encoder in firmware |
| GPL/source provenance contamination | undermines in-house clean-room goal | design ledger; no source reuse; implementation from spec/literature |
| UI seam not on main yet | decoder integration branch cannot compile adapter today | core remains independent; integrate seam only after UI branch lands |
| Third-party corpus license | cannot redistribute unknown samples | owner/licensed captures only in repo |
| "hardware verified" overclaim | cloud build != device proof | only named COM17/on-device run may claim hardware validation |

---

## 22. Proposed Phase 1 sequence — only after owner approval

1. **Protocol core first**
   - source fields, callsign/grid/hash handling;
   - CRC-14;
   - LDPC generator/parity;
   - host-only test encoder;
   - no ESP-IDF.

2. **Deterministic synthetic receive corpus**
   - exact clean frames;
   - noise/DT/frequency/drift sweeps;
   - collisions and non-FT8 rejection.

3. **Baseline FEC**
   - float NMS;
   - early termination;
   - benchmark against independently written host BP baseline.

4. **Incremental spectral/sync pipeline**
   - abstract spectral backend;
   - host implementation first;
   - coarse Costas + local refinement.

5. **Full host decoder**
   - soft metrics;
   - CRC/plausibility;
   - stable Decode-like result model independent of firmware.

6. **Performance experiments**
   - compact spectrogram;
   - fixed-point LLR/NMS;
   - OSD-lite;
   - one subtraction pass.

7. **Only then ESP32-P4 adapter**
   - esp-dsp backend benchmark;
   - static PSRAM pools;
   - low-priority task;
   - time_service validity gate;
   - DecoderBackend integration after UI seam is available.

8. **Real Tab5 validation**
   - build with PowerShell/ESP-IDF 5.5.4;
   - explicit COM17 only;
   - named test run and logs;
   - binary size, CPU, RAM, drops, watchdog, decode comparison.

---

## 23. Decisions required from the owner before Phase 1

Please approve or change these decisions:

1. **Provenance policy:** implement only from protocol/public-domain resources + generic DSP/FEC literature; existing decoders are benchmark/reference only.  
   **Recommendation: APPROVE.**

2. **What "better fit" means:** optimize first for bounded P4 latency, memory, and zero receiver starvation; then improve sensitivity inside that envelope rather than trying to beat unrestricted desktop WSJT-X at any cost.  
   **Recommendation: APPROVE.**

3. **Baseline FEC:** normalized min-sum + early syndrome termination first; conventional BP retained as a host benchmark, not necessarily production.  
   **Recommendation: APPROVE.**

4. **Search architecture:** incremental spectrogram + two-stage Costas coarse/refine + candidate-local exact tone analysis.  
   **Recommendation: APPROVE.**

5. **Deep decode:** no OSD/subtraction in first baseline; add small OSD-lite then one subtraction pass only after measurements.  
   **Recommendation: APPROVE.**

6. **AP decoding:** defer until the cross-branch integration contract can explicitly label assisted decodes.  
   **Recommendation: DEFER AP.**

7. **Protocol public-domain helper resources:** use them as normative constants/test-vector authorities, but independently implement OrcFT8 code around them rather than importing helper source.  
   **Recommendation: APPROVE.**

8. **Test corpus:** owner supplies OrcSDR/RTL recordings and confirms whether each may be committed; otherwise keep them external.  
   **Recommendation: APPROVE.**

9. **Initial passband:** cover the UI seam's current 200–3000 Hz FT8 audio window rather than a narrower hard-coded subset.  
   **Recommendation: APPROVE.**

10. **Formal clean room:** decide whether the practical source-provenance rule above is sufficient, or whether Phase 1 must be handed to an implementer who has not reviewed existing decoder source.  
    **Recommendation: practical provenance rule is sufficient unless a formal legal clean room is a business requirement.**

11. **Integration timing:** keep this decoder branch core-only until the separate FT8 UI/backend seam lands on main or an integration branch. Do not cherry-pick UI work here during Phase 0.  
    **Recommendation: APPROVE.**

**Owner approval was received on 2026-10-07. Phase 1 is now proceeding in isolated, host-tested slices; see `docs/ft8/PHASE1_IMPLEMENTATION.md`.**

---

## 24. Sources

Accessed 2026-10-07 unless otherwise stated.

### Primary FT8 protocol

**[P1]** Steve Franke, K9AN; Bill Somerville, G4WJS; Joe Taylor, K1JT. "The FT4 and FT8 Communication Protocols." QEX, July/August 2020.  
https://wsjt.sourceforge.io/FT4_FT8_QEX.pdf

**[P2]** WSJT-X User Guide, FT8 protocol description.  
https://wsjt.sourceforge.io/wsjtx-doc/wsjtx-main-2.7.0-rc8.html

### Decoder references

**[D1]** WSJT development-list historical FT8 decoder profiling (function-level architecture only).  
https://sourceforge.net/p/wsjt/mailman/message/35964640/

**[D2]** kgoba/ft8_lib, MIT. README and repository.  
https://github.com/kgoba/ft8_lib

**[D3]** ft8_lib issue #11 (bulk late-slot processing discussion) and issue #25 (SNR TODO).  
https://github.com/kgoba/ft8_lib/issues/11  
https://github.com/kgoba/ft8_lib/issues/25

**[D4]** Robert Morris AB1HL, ft8mon, MIT.  
https://github.com/rtmrtmrtmrtm/ft8mon

**[D5]** ft8mon FT8 receiver source, inspected for architecture/knobs only. No code copied.  
https://github.com/rtmrtmrtmrtm/ft8mon/blob/master/ft8.cc

**[D6]** jl1nie/mfsk-core, GPL-3.0-or-later, embedded reference docs. Used only as external embedded feasibility evidence.  
https://github.com/jl1nie/mfsk-core  
https://github.com/jl1nie/mfsk-core/blob/main/docs/reference/EMBEDDED.md

**[D7]** SteffenLav/qmx-panadapter, Tab5/ESP32-P4 on-device FT8 feasibility.  
https://github.com/SteffenLav/qmx-panadapter

**[D8]** wcheng95/Mini-FT8, ESP32-S3 application built on ft8_lib.  
https://github.com/wcheng95/Mini-FT8

### ESP32-P4 / DSP

**[H1]** M5Stack Tab5 hardware documentation.  
https://docs.m5stack.com/en/core/Tab5

**[H2]** Espressif ESP32-P4 chip revision v1.3 datasheet.  
https://documentation.espressif.com/esp32-p4-chip-revision-v1.3_datasheet_en.html

**[H3]** Espressif ESP32-P4 Technical Reference Manual, Processor Instruction Extensions (PIE).  
https://documentation.espressif.com/esp32-p4_technical_reference_manual_en.pdf

**[H4]** Espressif ESP-DSP.  
https://github.com/espressif/esp-dsp  
https://components.espressif.com/components/espressif/esp-dsp

**[H5]** ESP-DSP P4 FFT platform/API headers.  
https://github.com/espressif/esp-dsp/blob/master/modules/fft/include/dsps_fft4r_platform.h  
https://github.com/espressif/esp-dsp/blob/master/modules/fft/include/dsps_fft4r.h  
https://github.com/espressif/esp-dsp/blob/master/modules/fft/include/dsps_fft2r.h

**[H6]** ESP-DSP radix-2 fixed FFT implementation showing power-of-two length validation.  
https://github.com/espressif/esp-dsp/blob/master/modules/fft/fixed/dsps_fft2r_sc16_ansi.c

### Generic FEC algorithm literature

**[A1]** Jinghu Chen, Marc P. C. Fossorier et al., reduced-complexity / normalized BP-based LDPC decoding family; see also Chen & Fossorier, IEEE Trans. Communications 50(3), 2002, DOI 10.1109/26.990903.

**[A2]** Marc P. C. Fossorier and Shu Lin, "Soft-decision decoding of linear block codes based on ordered statistics," IEEE Trans. Information Theory 41(5), 1995, DOI 10.1109/18.412683.

### Licensing

**[L1]** GNU license compatibility explanation for GPLv3 + AGPLv3 combined programs.  
https://www.gnu.org/licenses/license-compatibility.html

**[L2]** GNU AGPLv3, Section 13.  
https://www.gnu.org/licenses/agpl-3.0.html

---

## 25. Phase 0 stop gate

**Satisfied 2026-10-07.** The owner approved the recommended workflow after Documentation Truth passed on draft PR #172. Phase 1 implementation is tracked in `docs/ft8/PHASE1_IMPLEMENTATION.md` and remains branch-only; `main` is read-only.
