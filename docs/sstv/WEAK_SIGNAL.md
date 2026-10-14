# SSTV weak-signal benchmark and receive-chain notes

Goal: a complete, clean Robot 36 decode at about -113 dBm at the RTL-SDR input, with zero missed lines and no audio gaps,
repeatedly. This file keeps three things apart: **what the code does** (read from the source), **what the bench measured**,
and **hypotheses** that no test has confirmed yet.

## The benchmark (fixed)

| Item | Value |
|---|---|
| Signal | Robot 36, `gambit/robot_36.wav` (the full colour test pattern), FM, 2.5 kHz deviation |
| Frequency | 145.500 MHz, Tab5 on NFM |
| Pluto | TX1, 89.75 dB programmed attenuation, drive 0.125 |
| External pad | 30 dB, then cable, then the RTL-SDR Blog V4L |
| Level at the dongle | **about -112.8 dBm**, measured with a tinySA Ultra in place of the dongle (see below) |
| Tuner gain | fixed manual, recorded per run (`gain_tenth_db` in every result line) |
| Previous best result | about 117 sync pulses locked, 19 missed, 135 of 240 rows, noisy picture, zero audio gaps |

### Level calibration (tinySA Ultra, LOW input, 200 Hz RBW, floor about -122 dBm)

The Pluto tool's own "nominal dBm" labels were **4.8 dB low** (its PA figure is too small). Measured with the pad in line:

| Programmed attenuation / drive | Measured at the pad output |
|---|---|
| 70 dB / 0.5 | -82.2 dBm |
| 80 / 0.5 | -92.2 |
| 84 / 0.5 | -95.8 |
| 88 / 0.5 | -99.8 |
| 89.75 / 0.5 | -101.8 |
| 89.75 / 0.125 | -112.8 |
| 89.75 / 0.0625 | about -119 (within 3 dB of the tinySA floor, so uncertain by a couple of dB) |

The attenuator and the digital drive both track 1 dB per dB to within about 1 dB. The tinySA reads to about 1-2 dB.
Not yet explained: an intermittent short burst of about -41 dBm at the dongle around transmit start or stop, seen on some runs
only (its timing is not measured). The firmware also muted the TX stream twice about 12 s into a run (250 ms starve guard).

## What the code does (read from `main.cpp`, `sstv_receiver.cpp`)

Receive path for SSTV (Browse/NFM, 240 kS/s from the V4L):

1. **IF channel filter** - a one-pole complex low-pass on I and Q, `alpha = 1 - exp(-pi * BW / fs)`. That puts the -3 dB
   points at +-BW/2 (a two-sided width of BW), with only 6 dB per octave of roll-off. Its equivalent noise bandwidth is about
   **1.57 x BW**. The default BW on the Browse path is **25 kHz** (`kRtlWxFilterDefaultHz`), so the noise bandwidth is about 39 kHz.
   (An earlier note in this work claimed 100 kHz; that was the clamp's upper limit, not the value in use.) An FM SSTV signal at
   2.5 kHz deviation and 2.3 kHz top audio needs about 9.6 kHz by Carson's rule.
2. Boxcar decimation to the discriminator rate, then the **FM discriminator** (phase difference of successive samples).
3. A one-pole **audio low-pass** (coefficient 0.55 at 240 kS/s, corner about 30 kHz), a boxcar to **48 kHz**, then **de-emphasis**
   (one pole, corner about 640 Hz), a slow DC blocker, and `shape_audio_sample`: an audio **AGC** (target level 4800, gain
   limited to 0.15 - 2.8) and a **soft limiter** at +-12000, then a 0.9/0.1 smoother.
4. The SSTV decoder takes the 48 kHz PCM, averages groups of 4 to **12 kHz** (a plain boxcar, so weak anti-aliasing), and
   works from there (VIS, line sync, tone estimation).

The software IF filter above is **not** the RTL-SDR's hardware tuner bandwidth. Hardware tuner-bandwidth control depends on the
receiver, driver and sample rate and is not changed here.

The TUNE tab's IF chip overrides only the software width, only while SSTV is open, and puts the radio's own value back when SSTV
closes. Choices: AUTO (the radio's width, 25 kHz), 100, 50, 25, 16, 12, 8 kHz. Gain, sample rate and AGC are never touched by it.

## Hypotheses (unconfirmed)

- A tighter IF width lowers the noise bandwidth only a little (about 3 dB from 25 to 12 kHz for this one-pole filter); a sharper
  filter may do more but costs CPU on the P4.
- The 48 -> 12 kHz boxcar folds FM discriminator noise (which rises with frequency) into the 0-6 kHz tone band.
- The audio AGC and soft limiter act on noise bursts near threshold and may disturb tone estimation.
- The sync detector and VIS recovery may be less tolerant of noise than the signal allows.

## Test harness

`PlutoSDR/sstv/gambit/auto_threshold.py` drives the Pluto and the Tab5 (serial, authenticated) with no one watching the screen.
For each run it sets tune, mode, IF width and gain, transmits one frame, polls `SSTV STATUS` during the transmission and reads
`SSTV LAST` afterwards. One JSON line per run goes to `runs/auto-*/results.jsonl`. Serial controls used: `SSTV OPEN`,
`SSTV TUNE <hz>`, `SSTV MODE`, `SSTV IFBW <hz>|DEFAULT`, `SSTV LAST`, `SSTV STATUS`, `RTL_GAIN MANUAL <tenth dB>`.

## Results

(See below; filled in from the harness output.)
