# FM DSP capture lab

Updated: **2026-08-09**  
Authority: [`../PROJECT_STATUS.md`](../PROJECT_STATUS.md)

## Purpose

Build FM filters from repeatable RF evidence instead of tuning by ear. A lab
capture pairs the original RTL-SDR IQ stream with the exact PCM produced by the
current demodulator, plus enough metadata to replay alternative filters on a PC.

This document separates three evidence boundaries:

- A post-DSP WAV measures the demodulator's audio output.
- Raw IQ is required to redesign or compare RF/channel filters.
- An acoustic or electrical loopback is required to measure the Tab5 speaker,
  amplifier, DMA, and queue path.

## Evidence recovered from the SD card

Ten recordings under `G:\orcsdr` were inspected on 2026-08-09. The card's file
timestamps report 2081 and are not trusted as capture times.

| Recording | Duration | Peak | RMS | Clipped samples | Longest digital-zero run |
|---|---:|---:|---:|---:|---:|
| `rec_001_FM_91900000.wav` | 12.000 s | -10.7 dBFS | -20.6 dBFS | 0 | 0.00 ms |
| `rec_001_FM_94500000.wav` | 12.000 s | -10.0 dBFS | -19.4 dBFS | 0 | 0.00 ms |
| `rec_001_FM_96095000.wav` | 12.000 s | -10.3 dBFS | -18.7 dBFS | 0 | 0.00 ms |
| `rec_001_FM_96113000.wav` | 12.000 s | -10.4 dBFS | -19.3 dBFS | 0 | 0.00 ms |
| `rec_001_FM_96146000.wav` | 0.853 s | -11.3 dBFS | -19.0 dBFS | 0 | 0.00 ms |
| `rec_002_FM_94500000.wav` | 0.819 s | -10.4 dBFS | -18.3 dBFS | 0 | 0.00 ms |
| `rec_002_FM_95975000.wav` | 12.000 s | -9.4 dBFS | -16.5 dBFS | 0 | 0.00 ms |
| `rec_002_FM_96113000.wav` | 12.000 s | -10.6 dBFS | -19.3 dBFS | 0 | 0.00 ms |
| `rec_002_FM_96136000.wav` | 5.495 s | -10.2 dBFS | -18.8 dBFS | 0 | 0.00 ms |
| `rec_003_FM_96113000.wav` | 10.786 s | -10.1 dBFS | -18.9 dBFS | 0 | 0.00 ms |

All files are 48 kHz, mono, signed 16-bit PCM. The consistent headroom, lack
of clipping, and absence of digital-zero gaps show that the demodulator can
produce stable PCM even when live playback sounds substantially worse.

`rec_001_FM_96095000.wav` was identified from its 12-second fingerprint as
**“La Grange” by ZZ Top**. Recognition is supporting evidence that the captured
audio retained coherent musical structure, not a fidelity specification.

### Bandwidth finding

For `rec_001_FM_96095000.wav`, approximately 95% of spectral energy is below
2.7 kHz and only 3.9% lies above 3 kHz. Across the ten files, the 95% roll-off
ranges from about 1.0 to 4.0 kHz. The current FM output is therefore clean but
strongly narrowed compared with full broadcast-FM audio. Paired raw IQ is
needed to determine how much bandwidth can be restored without reintroducing
static or adjacent-channel interference.

## Why the WAV can be clean while live audio is poor

The current firmware appends each post-AGC/limiter PCM batch to the recorder
immediately before calling `M5.Speaker.playRaw()`. A batch is therefore retained
in PSRAM even when `playRaw` rejects it or the downstream DMA, codec, amplifier,
or speaker reproduces it poorly.

The observed split means the FM demodulator is no longer the first suspect.
The next live-audio measurements must include:

- accepted and rejected `playRaw` batch counts;
- maximum time between accepted batches and queue starvation events;
- DSP block time and USB delivery gaps;
- a microphone or line-level recording of the Tab5 output synchronized with
  the internal PCM reference;
- the same test at multiple volume levels to expose amplifier/speaker limits.

## Capture format

Use one sequence identifier for all files from a capture:

```text
fm_96095000_0001.sigmf-data
fm_96095000_0001.sigmf-meta
fm_96095000_0001.wav
```

`sigmf-data` contains unsigned 8-bit interleaved complex IQ (`cu8`) at
960 ksample/s. `sigmf-meta` records at minimum:

- center frequency and sample rate;
- capture start sample and shared sequence identifier;
- demodulation mode and channel/filter bandwidth;
- tuner gain/AGC state and measured signal/noise level;
- de-emphasis constant, audio sample rate, and volume;
- firmware commit/build identity;
- IQ and WAV byte counts plus SHA-256 values;
- dropped USB blocks and rejected speaker batches during the window.

The WAV remains 48 kHz mono signed 16-bit PCM and begins from the same capture
epoch. If DSP latency shifts the first PCM sample, metadata records the IQ-to-
PCM sample offset rather than silently trimming either stream.

## Resource budget

At the current rates:

| Stream | Rate | Five seconds | Eight seconds |
|---|---:|---:|---:|
| CU8 IQ, 960 ksample/s | 1.92 MB/s | 9.60 MB | 15.36 MB |
| PCM S16 mono, 48 kHz | 0.096 MB/s | 0.48 MB | 0.768 MB |
| Combined payload | 2.016 MB/s | 10.08 MB | 16.128 MB |

Start at five seconds. Extend to eight only after measured PSRAM headroom
includes the USB ring, spectrum buffers, display allocations, and filesystem
overhead. Do not allocate a parallel permanent capture framework: reuse the
existing IQ and WAV recorder buffers, with LoRa and FM lab capture mutually
exclusive.

## Implementation plan

### Phase 1 — synchronized in-memory capture

1. Add an `FM DSP CAPTURE` action available only while the FM stream is active.
2. Reserve one reusable PSRAM IQ buffer and the existing PCM buffer.
3. Start both counters from one capture sequence/sample epoch.
4. Copy each incoming CU8 block before demodulation and append the resulting
   post-DSP PCM after demodulation.
5. Stop after five seconds without writing to SD in the receive callback.

### Phase 2 — post-capture persistence

1. Pause or stop streaming after the buffers are complete.
2. Mount the Tab5 card through the shared SDMMC-first filesystem path.
3. Write SigMF data, SigMF metadata, and WAV to temporary unique filenames.
4. Flush, close, calculate SHA-256, and atomically rename each completed file.
5. Preserve earlier captures; never reuse a sequence name after reboot.
6. Resume the same station only after persistence completes.

### Phase 3 — offline replay and scoring

1. Add a host tool that reads the SigMF pair and reproduces the current FM
   demodulator as the baseline.
2. Run at least three channel/audio-filter variants against the identical IQ.
3. Write a WAV and compact metrics JSON for every variant.
4. Compare SNR, occupied audio bandwidth, clipping, DC offset, discontinuities,
   adjacent-channel rejection, and processing time.
5. Promote a filter only when it improves measured quality and passes operator
   listening on the same source capture.

### Phase 4 — live-output isolation

1. Play the winning internal PCM through the Tab5 speaker without live RF or
   spectrum work. If it still sounds poor, the defect is in the output chain.
2. Repeat with live RF but graphics disabled, then graphics enabled.
3. Record an external microphone/line reference and align it with the internal
   WAV to detect missing batches and frequency-response coloration.
4. Change DMA depth, batch size, or speaker configuration one variable at a
   time and retain before/after telemetry.

## Definition of done

- One button produces a valid, synchronized IQ/metadata/WAV triplet on SD.
- Device and host SHA-256 values match for every file.
- Capture adds no SD write latency to the real-time receive callback.
- The baseline host replay is recognizably equivalent to the device WAV.
- At least three filter variants are scored from the same IQ.
- The selected filter restores useful bandwidth without measurable clipping,
  dropouts, or unacceptable adjacent-channel/static increase.
- Internal PCM and external speaker-loopback evidence identify whether any
  remaining live-audio defect is queue/DMA loss or physical output coloration.
