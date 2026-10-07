# Weak-Signal 12 kHz USB Analysis Tap Proposal

Status: **PROPOSAL ONLY — do not implement before owner approval**

Branch: `codex/ft8-native-decoder-research`

Baseline: `claude/ft8-ui` at `804268c71af127490364c325a76e61383042f137`

## Problem statement

The decoder backend contract expects mono signed 12 kHz PCM representing a USB-style receive channel with weak-signal tones in approximately 200–3000 Hz.

The current FT8 screen does not provide that signal.

Current FT8 tuning in `main.cpp` calls `ft8_select_band()`, which routes the requested FT8 dial frequency through `RtlBand::shortwave`. Current shortwave demodulation is AM:

```text
ft8_select_band()
    |
    v
RtlBand::shortwave
    |
    v
band_demod(shortwave)
    |
    v
AM envelope demod
    |
    v
48 kHz post-AGC/post-limiter speaker audio
```

That output is unsuitable as the decoder analysis source.

## Findings from the current receive path

Repository inspection of the UI baseline shows:

- wide dashboard bands, including shortwave, normally acquire at 2.4 MS/s;
- `kRtlDemodRateSps` is 240 kS/s;
- `rtl_rf_decimation(2.4 MS/s)` is 10;
- `demodulate_am()` performs complex-I/Q low-pass/decimation work, envelope detection, then further decimation to 48 kHz;
- `demodulate_ssb()` exists for CB/Home and uses a +/-1500 Hz BFO, real projection, 48 kHz output, DC removal and the shared audio shaper;
- `shape_audio_sample()` applies level-dependent AGC, soft limiting, fade-in and output smoothing;
- `queue_audio_samples()` feeds web audio, visualizer, recorder and speaker;
- normal demodulation is skipped when there is no speaker/web/recorder audio demand.

Therefore the weak-signal decoder must not consume `queue_audio_samples()`.

Reasons:
1. it is 48 kHz, not the backend's 12 kHz contract;
2. it is post-AGC/limiter audio, which changes relative tone amplitudes and noise statistics;
3. it may not exist when sound/web recording is disabled;
4. the current shortwave route is AM, which destroys the phase/sideband information required to isolate USB;
5. sharing mutable `rtl_audio` demod state would create a risk that decoder visual/analysis features change speaker audio.

## Design rule

**The decoder analysis tap is a sidecar. It reads the same immutable raw CU8 receive block but owns all of its state and buffers.**

When disabled, it performs no writes to existing demodulator/audio state.

```text
                           +---------------- existing path ----------------+
                           |                                                |
RTL CU8 block ------------+--> level/spectrum --> existing demod --> speaker/web
       |
       +---------------- weak-signal sidecar -----------------------------+
                           |
                           v
                  narrow complex channelizer
                           |
                           v
                    USB sideband selection
                           |
                           v
                     12 kHz int16 PCM
                           |
                           v
                     DecoderBackend
```

No existing DSP, demodulation, filter or sound function is replaced.

## Proposed module boundary

Suggested decoder-owned firmware module:

- `ui/ft8_audio_tap.hpp`
- `ui/ft8_audio_tap.cpp`

A broader future name such as `weak_signal_audio_tap` may be preferable once FT4/JS8 are enabled.

Pure-DSP portions should be isolated enough to host-test without ESP-IDF.

Proposed high-level API:

```cpp
namespace orcsdr::ftx::audio_tap {

struct Config {
  uint32_t input_sample_rate_hz;
  uint32_t output_sample_rate_hz;   // 12000
  uint16_t low_audio_hz;            // normally 200
  uint16_t high_audio_hz;           // normally 3000
  bool usb;                          // true for current family
};

struct Metrics {
  uint64_t input_complex_samples;
  uint64_t output_samples;
  uint32_t blocks;
  uint32_t max_block_us;
  uint32_t dropped_output_samples;
};

bool begin(const Config& config, /* preallocated storage */);
void reset();
size_t process_cu8(const uint8_t* iq, size_t bytes,
                   int16_t* output, size_t capacity);
Metrics metrics();

}
```

The final firmware API may use a bounded ring rather than a caller output buffer, but the signal-processing core should remain deterministic and allocation-free per block.

## Recommended signal path to benchmark

Do not adopt the existing voice SSB demodulator as the decoder frontend.

The initial benchmark candidates should be:

### Candidate A — independent multistage complex decimator + analytic USB filter

1. CU8 -> centered signed I/Q.
2. multistage complex low-pass/decimation from 2.4 MHz toward a modest complex intermediate rate;
3. complex frequency translation/filtering that selects only the +200..+3000 Hz USB sideband;
4. final decimation to 12 kHz;
5. real projection to int16, with a fixed/calibrated scale and **no audio AGC, no limiter, no deemphasis, no speaker filter**.

This is the cleanest signal model.

### Candidate B — coarse decimation then translate/filter/retranslate

A potentially cheaper implementation:

1. 2.4 MHz -> 240 kHz or lower complex stream using a cheap bounded decimator;
2. shift the desired USB passband center (roughly +1.6 kHz) to DC at the lower rate;
3. real/complex low-pass around roughly 1.4 kHz half-width;
4. decimate to 12 kHz;
5. restore the center offset so output tones remain at their original 200–3000 Hz audio positions;
6. emit real PCM.

This avoids a high-rate NCO/filter for the narrow final channel.

### Candidate C — polyphase channelization

Only investigate if A/B are too expensive. A small polyphase/FIR decimator may reduce multiply cost but increases implementation complexity and coefficient/state memory.

## Important frequency-sign test

RTL-SDR I/Q orientation, direct-sampling behavior, and the project's tuner routes can make an assumed "positive frequency is USB" sign wrong if not tested.

Do not encode the sign from memory.

The host test corpus must inject one known complex tone:
- +1000 Hz from the dial frequency;
- -1000 Hz from the dial frequency.

The approved USB tap must pass the expected +side tone and strongly reject the opposite side. Repeat on a real saved capture where a known USB signal is available.

## V3 direct-sampling consequence

The project notes that the V3-class HF path uses direct sampling. The analysis tap must not contain tuner-model-specific gain or frequency assumptions.

The input contract is simply the CU8 complex stream delivered by the existing driver plus its reported sample rate.

Tests must cover captures from each supported dongle profile where practical:
- RTL-SDR Blog V4;
- V4L profile;
- V3/R820T2/direct-sampling HF path;
- other supported devices as recordings become available.

No gain is hard-coded by the tap.

## Output scaling

The decoder needs stable relative tone/noise metrics, not loud speaker audio.

Recommended initial scaling:
- remove only fixed numeric conversion/known filter gain;
- saturate safely to int16;
- no per-slot AGC;
- no peak normalizer;
- no dynamic compressor.

The demod/sync layer should estimate local noise and candidate amplitude itself.

If an automatic numeric scale is needed to prevent underflow, it must be a transparent block-floating or explicitly reported scale that does not alter relative energy within the analysis region.

## Where to hook the sidecar

The current `rtl_dsp_task` receives each immutable raw CU8 block and already dispatches protocol-specific consumers before normal audio demodulation.

Proposed integration location:

```text
rtl_dsp_task
  update signal power
  existing protocol consumers
  diagnostic recorder
  spectrum
  ...
  if weak_signal_tap_active:
      ftx_audio_tap::process_cu8(block.data, block.bytes, ...)
  existing demodulate_home / CB audio
```

The exact ordering should be selected from timing measurements. The important properties are:

- the tap never edits `block.data`;
- no use of `rtl_audio` filter/NCO/AGC members;
- no writes to speaker/web/recorder buffers;
- no dependency on `rtl_audio_enabled` or web-audio demand;
- the tap is active only while the weak-signal decoder owns the appropriate receive session;
- optional work is dropped/yielded before raw receive/audio is allowed to starve.

## Data handoff to DecoderBackend

Preferred architecture:

- the tap performs only channelization/demod/12 kHz resampling;
- a bounded preallocated PCM ring or double-buffer separates the high-rate DSP task from the low-priority decoder task;
- the high-rate path performs no LDPC/sync search;
- the low-priority decoder drains chunks and calls `offer_audio()`.

When the ring is full:
- increment an explicit discontinuity/drop counter;
- mark the slot incomplete;
- do not call that slot a normal "decode failure";
- reset incremental decoder continuity as required.

An incomplete slot is a transport/scheduling condition, not evidence of RF silence.

## Tap-off bit-identity gate

The owner requires proof that the feature cannot alter existing sound when disabled.

### Gate 1 — source/state isolation

Host/unit checks verify:
- process function writes only to its own Context and output memory;
- no reference to `rtl_audio`, `queue_audio_samples`, speaker, visualizer, web-audio or existing filter state inside the tap module;
- disabled dispatch executes no tap process call.

### Gate 2 — deterministic saved-IQ comparison

Use the same CU8 input and existing audio configuration for two runs:

A. firmware/code with tap dispatch compiled but runtime-disabled;
B. baseline without tap dispatch.

Capture the existing post-DSP PCM at the audio-recorder boundary.

Pass requirement:
- identical sample count;
- bit-identical PCM / identical SHA-256;
- same receiver configuration.

If a host-extractable replay harness cannot represent the exact production demod path, perform this comparison on-device using a deterministic saved-IQ replay path.

### Gate 3 — on-device live A/B

With the same station/settings:
- tap OFF;
- tap ON but decoder not doing deep work;
- tap ON with live incremental decode.

Record:
- audio ring overruns;
- audio submit failures/drops;
- RTL USB overruns/consumer drops;
- DSP block average/max;
- effective sample rate;
- speaker/recorded audio behavior.

Tap OFF must remain behaviorally and bitwise identical to baseline. Tap ON is allowed to consume CPU but must not create receiver/audio drops.

## CPU budget gate

No cost estimate in this proposal is a measurement.

Before production binding, capture on the actual Tab5:
- microseconds per 32 KiB or production IQ block;
- average and worst-case tap cost;
- percentage of existing DSP gate budget;
- decoder-ring copy cost;
- core idle/runtime impact;
- PSRAM/internal-memory traffic if measurable.

The tap is rejected or redesigned if it pushes existing production DSP over its safe budget.

Optimization order:
1. reduce arithmetic / multistage decimation;
2. use esp-dsp supported P4 kernels;
3. fixed-point where sensitivity tests permit;
4. P4 PIE/custom assembly only after profiling proves it necessary.

## Memory policy

- coefficients and immutable tables in flash/const memory;
- large PCM/residual buffers in PSRAM;
- small hot decimator/NCO/filter state in internal RAM;
- allocate all buffers at begin/enable time;
- no heap allocation from the per-IQ-block process path;
- fixed capacity with explicit overflow metrics.

## Filter requirements

The initial analysis passband should preserve at least 200–3000 Hz at 12 kHz output.

Stopband targets are not fixed by this document. They must be derived from:
- rejection needed for a strong opposite-sideband signal;
- alias rejection through the decimation chain;
- CPU cost on P4;
- sensitivity on mixed-signal test cases.

Host benchmark should sweep adjacent interferers at increasing amplitude and frequency separation and report weak target decode probability.

## Visual features remain downstream only

The dashboard waterfall/spectrum must never change this signal path.

The tap feeds decoder analysis data. The dashboard consumes bounded snapshots/results from the decoder.

No waterfall preference, color palette, zoom, map, tab, or visualizer setting may alter channelizer coefficients, gain, soft metrics, or decode thresholds.

## Saved-IQ host proof plan

Build a pure C++ test harness around the tap core:

```text
CU8 file
  |
  +--> baseline diagnostic statistics
  |
  +--> USB analysis tap
         |
         +--> 12 kHz PCM/WAV fixture
         +--> tone amplitude/rejection measurements
         +--> later FT8/FT4/JS8 decoder
```

Synthetic CU8 cases:
- one USB tone;
- one LSB tone;
- target + strong opposite-sideband tone;
- target + adjacent carrier;
- frequency offset;
- amplitude clipping/8-bit quantization;
- deterministic AWGN.

Real CU8 cases come from owner-owned captures with provenance recorded before commit.

## Proposed integration changes

No existing DSP function is modified.

The only future `main.cpp` glue requested after approval is conceptually:

```cpp
if (weak_signal_tap_active()) {
  weak_signal_tap_offer(block.data, block.bytes, block.sample_rate_sps);
}
```

Everything else lives outside `main.cpp`.

Activation/deactivation should follow receiver ownership/screen lifetime and must reset tap continuity on:
- tune/retune;
- sample-rate change;
- dropped input block;
- mode/profile change;
- receiver stop/disconnect.

## Why not simply force Home/shortwave USB?

Changing the active dashboard's normal demodulator would violate the owner's constraint that existing DSP/sound behavior remain untouched and would couple decoder correctness to speaker mode.

Even if Home's current USB voice path were selected:
- its filter is voice-oriented;
- it uses a 1500 Hz BFO and existing mutable audio state;
- it ends in dynamic shaping;
- it is gated by audio demand;
- it outputs 48 kHz.

It is useful as a comparison/reference path, not as the decoder's promised analysis contract.

## Decisions requested

1. Approve a parallel raw-CU8 sidecar rather than reusing post-speaker audio.
2. Approve an independent 12 kHz USB analysis path with no dynamic AGC/limiter.
3. Approve a bounded PCM ring between the high-rate DSP task and low-priority decoder.
4. Approve tap-off bit-identity as a release gate.
5. Approve an initial A/B/C frontend benchmark before choosing final decimation/filter architecture.
6. Approve the rule that any slot with tap/ring discontinuity is marked incomplete rather than "quiet" or "decode failed."

No audio-tap implementation will be committed until this proposal is approved.
