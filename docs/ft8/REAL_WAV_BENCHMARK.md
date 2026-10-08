# FT8 Real-WAV Reference Benchmark

Status: baseline measurement before decoder-coverage optimization.

Sample: `210703_133430.wav`

Official sample source:
https://sourceforge.net/projects/wsjt/files/samples/FT8/210703_133430.wav

SHA-256:
`9feb99c275770a6618538026da7decc6b09eb6cf63121e5168fa86dcdf00c2f5`

Reference: the WSJT-X `jt9` executable from Ubuntu's
`wsjtx 2.7.0~rc3+repack-1build2` package, run externally as
`jt9 -8 -F 200 -f 1500` on the same WAV. No WSJT-X implementation code is
copied or linked into OrcSDR.

Reference/OrcSDR comparison baseline was established with the same external
`jt9` executable and OrcSDR's default geometry
`rows_per_symbol=2`, `bins_per_tone=1` (80 ms / 6.25 Hz). The later
FT8/FT4 survey run is GitHub Actions `37722860388`.

| Reference message | Ref SNR dB | Ref DT s | Ref Hz | OrcSDR | Orc start s | Orc Hz | Orc SNR |
|---|---:|---:|---:|---|---:|---:|---|
| W1FC F5BZB -08 | +15 | +0.3 | 2571 | MISSED | — | — | not implemented |
| CQ F5RXL IN94 | -2 | -0.8 | 1197 | MISSED | — | — | not implemented |
| WM3PEN EA6VQ -09 | +13 | -0.1 | 2157 | **DECODED** | 0.480 | 2156.250 | not implemented |
| K1JT HA0DU KN07 | -13 | +0.3 | 590 | MISSED | — | — | not implemented |
| A92EE F5PSR -14 | -7 | +0.1 | 723 | MISSED | — | — | not implemented |
| K1BZM EA3GP -09 | -3 | -0.1 | 2695 | MISSED | — | — | not implemented |
| N1JFU EA6EE R-07 | -13 | +0.3 | 641 | MISSED | — | — | not implemented |
| N1PJT HB9CQK -10 | -3 | +0.2 | 466 | MISSED | — | — | not implemented |
| W1DIG SV9CVY -14 | -7 | +0.4 | 2734 | MISSED | — | — | not implemented |
| K1JT EA3AGB -15 | -16 | +0.1 | 1649 | MISSED | — | — | not implemented |
| W0RSJ EA3BMU RR73 | -16 | +0.3 | 400 | MISSED | — | — | not implemented |
| XE2X HA2NP RR73 | -11 | +0.2 | 2853 | MISSED | — | — | not implemented |
| KD2UGC F6GCP R-23 | -6 | +0.4 | 472 | MISSED | — | — | not implemented |
| K1BZM EA3CJ JN01 | -7 | +0.2 | 2522 | MISSED | — | — | not implemented |

## Baseline score

- Reference decodes: **14**
- OrcSDR accepted decodes: **1**
- Coverage: **1/14 = 7.1%**
- Accepted OrcSDR messages not present in the 14-message reference list: **0**
- Current observed false-decode count on this recording: **0**
- OrcSDR host wall time: **4.05 s** in regression-floor run `37723100513`
  (earlier equivalent hosted runs measured 5.15 s and 5.94 s, so host timing has normal runner variance)
- OrcSDR SNR estimator: **not implemented**, so no SNR is fabricated.

The single accepted OrcSDR message is present in the reference output and its
audio-frequency estimate differs by 0.75 Hz from the published integer-Hz
reference value.

This baseline is intentionally poor compared with WSJT-X Deep. It is recorded
before adding finer search, improved soft metrics, subtraction, or additional
passes so later improvements have an honest before/after comparison.

## FT4 real-WAV baseline

The older user-guide tutorial filename `200514_182053.wav` currently returns
HTTP 404 from the public SourceForge sample path, so the benchmark surveyed the
three FT4 WAVs currently published by the WSJT Project. The useful coverage
fixture is `000000_000002.wav`.

Official SourceForge sample SHA-256:
`d9e91fa04ba138a7b9f41b4103823c77ca1c3a9775101f6b14d60935bcd3813b`

Reference: the same installed WSJT-X `jt9` executable, run as
`jt9 --ft4 -d 3`. OrcSDR uses `rows_per_symbol=2`,
`bins_per_tone=1` (24 ms / 20.833 Hz for FT4).

| Reference message | Ref SNR dB | Ref DT s | Ref Hz | OrcSDR | Orc start s | Orc Hz | Orc SNR |
|---|---:|---:|---:|---|---:|---:|---|
| N1TRK N4FKH 569 VA | -10 | -0.2 | 296 | MISSED | — | — | not implemented |
| N1TRK KB7RUQ RR73 | -9 | -0.4 | 422 | MISSED | — | — | not implemented |
| CQ RU AB5XS EM12 | -7 | -0.1 | 560 | MISSED | — | — | not implemented |
| NZ7P WA7JAY 589 CA | -12 | +0.1 | 727 | MISSED | — | — | not implemented |
| KB0VHA KA1YQC R 539 MA | +16 | +0.3 | 1149 | MISSED | — | — | not implemented |
| CQ RU N9OY EN43 | -3 | -0.2 | 1640 | MISSED | — | — | not implemented |
| K1JT WB4HXE 559 GA | -11 | +0.2 | 1910 | MISSED | — | — | not implemented |
| VE3LON K7RL R 549 WA | +5 | +0.3 | 2067 | MISSED | — | — | not implemented |
| WD9IGY KX1X 73 | -1 | +0.2 | 2310 | **DECODED** | 0.672 | 2304.133 | not implemented |
| W7BOB KJ7G RR73 | -17 | -0.4 | 2413 | MISSED | — | — | not implemented |
| NI6G W7DRW 569 AZ | -7 | -0.3 | 2566 | MISSED | — | — | not implemented |
| K4SQC VE3RX RR73 | +2 | -0.3 | 2725 | **DECODED** | 0.192 | 2720.793 | not implemented |
| CQ RU W1QA FN32 | -6 | -0.4 | 2813 | MISSED | — | — | not implemented |
| CQ RU WS4WW FM17 | 0 | 0.0 | 2995 | MISSED | — | — | not implemented |
| HB9BUN KG4W R 549 VA | +13 | +0.2 | 3159 | MISSED | — | — | not implemented |
| W9TO KN3ILZ 529 PA | -8 | +0.4 | 3337 | MISSED | — | — | not implemented |
| W9JA PY2APK RRR | -10 | -0.3 | 520 | MISSED | — | — | not implemented |
| AC6BW KR9A R 559 WI | -15 | +0.2 | 2299 | MISSED | — | — | not implemented |
| CQ RU W0FRC DM79 | -13 | -0.3 | 2560 | MISSED | — | — | not implemented |

FT4 baseline:
- Reference decodes: **19**
- OrcSDR accepted decodes: **2**
- Coverage: **2/19 = 10.5%**
- Accepted OrcSDR messages not present in the reference list: **0**
- Current observed false-decode count on this recording: **0**
- OrcSDR host wall time: **1.90 s** in regression-floor run `37723100513` (the prior survey measured 2.29 s)
- OrcSDR SNR estimator: **not implemented**.

The other current official FT4 files measured in the same job,
`190106_000112.wav` and `190106_000115.wav`, produced zero reference
decodes with this `jt9` configuration and zero OrcSDR decodes, so they are
not useful coverage fixtures.

Many missed FT4 lines use FT Roundup contest message families that the current
conservative source-message parser does not yet implement. Those parser misses
must not be confused with synchronization/FEC sensitivity misses.

## Regression floor

External-WAV CI now requires:
- FT8: at least 1 accepted decode and exact message `WM3PEN EA6VQ -09`;
- FT4: at least 2 accepted decodes on `000000_000002.wav`, including
  `K4SQC VE3RX RR73` and `WD9IGY KX1X 73`.

These are floors, not quality targets. Task 3 should raise them only after
repeatable before/after measurements establish additional stable decodes
without increasing false accepts.

## Task 3, experiment 1: search geometry (no decoder math changed)

Purpose: find out where the missed reference signals are lost before changing any algorithm, and measure whether a finer
spectral search geometry alone recovers more of them. Nothing in the decoder's acceptance rule changed; the tool
`tools/ft8-wav-diagnose.cpp` (run with `bash tools/diagnose-ft8-wav.sh`) drives the same module functions as the
production path (`sync::search`, `pipeline::try_candidate`, which `decode_grid` now also uses) and classifies every
reference signal by where it was lost. Reference lists are in `tools/ft8-reference/`.

Method: the same two official recordings, the same WSJT-X references (14 FT8, 19 FT4), the production candidate limit of 16
and sync threshold 0.10, only `rows_per_symbol,bins_per_tone` varied. Host measurements: WSL2 g++ -O3 on the project
workstation, one run each; single-run wall times vary by roughly 10 percent. Nothing here is an ESP32-P4 number.

### FT8 `210703_133430.wav` (14 reference decodes, 13 in scope)

| grid (rows,bins) | spectral ms | sync ms | gate ms (all candidates) | positions >= threshold | after NMS | LDPC attempts cap/all | LDPC converged cap/all | CRC pass cap/all | plausible cap/all | accepted in cap | accepted if cap unlimited | false accepts |
|---|---:|---:|---:|---:|---:|---|---|---|---|---:|---:|---:|
| 2,1 (baseline, 80 ms / 6.25 Hz) | 281 | 5.7 | 56.6 | 2,822 | 861 | 16 / 861 | 2 / 4 | 2 / 4 | 1 / 1 | **1** | 1 | 0 |
| 4,1 (40 ms / 6.25 Hz) | 537 | 11.2 | 63.1 | 5,617 | 1,008 | 16 / 1,008 | 3 / 6 | 3 / 6 | 2 / 2 | **2** | 2 | 0 |
| 4,2 (40 ms / 3.125 Hz) | 1,063 | 49.2 | 87.3 | 11,230 | 1,024 (saturated) | 16 / 1,024 | 6 / 9 | 6 / 9 | 5 / 6 | **5** | 6 | 0 |

Exact messages: baseline `WM3PEN EA6VQ -09`. 4,1 adds `W1FC F5BZB -08`. 4,2 adds `K1JT HA0DU KN07`, `N1JFU EA6EE R-07` and
`W1DIG SV9CVY -14`, and one more (`XE2X HA2NP RR73`, ranked 17th, LDPC 15 iterations) that the 16-candidate limit hides.
Every accepted message is in the WSJT-X reference list. The 4,2 grid's candidate list hit the diagnostic limit of 1,024.

### FT4 `000000_000002.wav` (19 reference decodes, 16 in scope)

| grid | spectral ms | sync ms | gate ms | positions >= threshold | after NMS | LDPC converged cap/all | CRC pass cap/all | plausible cap/all | accepted in cap | accepted if cap unlimited | false accepts |
|---|---:|---:|---:|---:|---:|---|---|---|---:|---:|---:|
| 2,1 (baseline) | 33 | 1.6 | 23.8 | 1,457 | 379 | 8 / 16 | 8 / 16 | 2 / 2 | **2** | 2 | 0 |
| 4,1 | 65 | 3.1 | 26.0 | 2,884 | 427 | 8 / 17 | 8 / 17 | 2 / 3 | **2** | 3 | 0 |
| 4,2 | 130 | 6.2 | 33.0 | 5,873 | 526 | 9 / 15 | 9 / 15 | 3 / 3 | **3** | 3 | 0 |

4,2 adds `N1TRK KB7RUQ RR73` (rank 14). At 4,1 the same message exists at a rank beyond the 16 limit.

### Noise-only check

Eight deterministic Gaussian-noise recordings per mode (15 s for FT8, 6 s for FT4), each run at all three grids, 48 runs: zero CRC
passes, zero accepted messages. A few LDPC runs converged to a wrong codeword and were rejected by CRC-14, as the gates intend.

### Where the misses are (classification of every reference signal)

FT8, baseline 2,1 (13 in scope): 1 decoded; 3 sync candidates with strong sync (rank 5 to 8, sync 0.75 to 0.81) whose LDPC
did not converge; 8 candidates ranked 23 to 554 (sync 0.18 to 0.63) whose LDPC did not converge; 1 candidate (`A92EE F5PSR -14`,
rank 1) that reached a CRC-valid codeword but could not be unpacked, see below. One more reference signal (`CQ F5RXL IN94`,
dt -0.8 s) starts 0.3 s before the recording begins and cannot be found as a full frame.

FT8, 4,2: 5 decoded, 1 more beyond the candidate limit, 1 CRC-valid but unpackable (`A92EE`), 3 strong-sync LDPC failures
(`K1BZM EA3GP -09` rank 11, `N1PJT HB9CQK -10` rank 12, `K1JT EA3AGB -15` rank 15), 3 weaker LDPC failures beyond the limit.

FT4, baseline: 3 reference signals (2995, 3159, 3337 Hz) lie partly or wholly above the 3 kHz analysis band and cannot be found by
design; 3 are CRC-valid but of message types the parser does not implement; 8 are ranked beyond the limit with LDPC failures;
2 were suppressed by non-maximum suppression because a stronger signal sits 11 Hz away. At 4,2: 3 decoded, 6 CRC-valid but
unpackable within the limit plus 2 more beyond it, 3 weak LDPC failures, 2 suppressed neighbours, 3 out of band.

### Findings

1. The dominant loss at the baseline grid is soft-demodulation quality, not sync or ranking. Strong sync candidates exist and LDPC
   fails; a finer time and, above all, frequency grid recovers four of them. Time resolution alone (4,1) recovers one; adding half-bin
   frequency resolution (4,2) recovers three more. Misalignment of a signal against the grid (up to a quarter symbol in time and half a tone
   spacing in frequency) is the most likely cause, and it is exactly what a candidate-local refinement would remove.
2. The gates are cheap and the spectral stage is not. Attempting all 861 to 1,024 candidates through demod, LDPC and CRC costs
   56 to 87 ms on the host (about 0.07 ms per candidate); the spectral stage grows 3.8 times from 2,1 to 4,2 (281 to 1,063 ms) and the
   exact-correlation implementation is a correctness oracle, not the production algorithm.
3. Candidate-limit pressure is real only at the finer grid: at 2,1 attempting every candidate recovers nothing extra, at 4,2 it recovers one.
   The cost of a larger limit is about one millisecond.
4. Two message-layer findings that need no DSP change:
   - `encode_standard_callsign` rejects any callsign with more than one digit, so valid prefixes such as `A92EE` (`A9` plus area digit `2`) never round-trip
     and a CRC-valid FT8 decode is thrown away. This is a genuine parser bug.
   - FT4's contest message types (ARRL RTTY Roundup exchanges such as `N1TRK N4FKH 569 VA` and directed-CQ tokens such as `CQ RU AB5XS EM12`)
     are not parsed. At 4,2, six CRC-valid FT4 candidates inside the limit and two beyond it are lost only to this, so they are not RF or DSP misses.
5. No false accepts were observed on either recording or on the noise corpus at any grid.

### Recommended next single decoder improvement

Candidate-local time and frequency refinement before soft demodulation (a two-stage search): keep the cheap coarse whole-band search,
then re-evaluate only the top candidates at finer time and frequency offsets and demodulate at the best refined position. Measure it first with
an exact-correlation refinement on the host to prove the coverage gain independently of DSP cost, targeting roughly the 4,2 coverage (5 to 6 of 13 on
FT8) at a small fraction of the 4,2 spectral cost; then design the efficient P4 implementation (per-candidate down-conversion to a low sample rate).
The callsign-encoder fix, the candidate limit and the FT4 contest-message parser are independent, low-risk items that should be measured separately.

## Task 3, experiment 2: candidate-local time and frequency refinement (host prototype)

Change under test (one change): keep the coarse 2,1 whole-band search, take the top-K sync candidates, and for each run a short coordinate search
over sample-accurate start time (+-1 coarse hop) and sub-bin frequency (+-1 coarse bin), scoring only the protocol sync symbols with exact
single-frequency correlation; then build a candidate-local energy grid (one row per channel symbol, one bin per tone) at the refined position and feed it
to the unchanged `pipeline::try_candidate`. Tool: `tools/ft8-wav-refine.cpp`. Control: the same tool with `--no-refine` (same K, same gates). Exact
correlation is a measurement vehicle; it is not the P4 implementation. Host wall times, WSL2 g++ -O3, single runs (about 10 percent noise).

| recording | variant | accepted | spectral ms | refine ms | gates ms | total ms | false accepts |
|---|---|---:|---:|---:|---:|---:|---:|
| FT8 (14 ref) | 2,1 coarse, K=32, no refine (control) | 1 | 262 | 0 | 65 | 330 | 0 |
| FT8 | 2,1 coarse, K=32, refine | 5 | 279 | 431 | 66 | 778 | 0 |
| FT8 | 2,1 coarse, K=64, no refine | 1 | 262 | 0 | 138 | 402 | 0 |
| FT8 | **2,1 coarse, K=64, refine** | **6** | 270 | 847 | 132 | 1,250 | 0 |
| FT8 | 2,1 coarse, K=16, refine | 3 | 271 | 219 | 33 | 525 | 0 |
| FT8 | 4,2 coarse, K=64, no refine (experiment 1 best grid) | 6 | 1,053 | 0 | 134 | 1,197 | 0 |
| FT4 (19 ref) | 2,1 coarse, K=32, no refine | 2 | 32 | 0 | 14 | 47 | 0 |
| FT4 | 2,1 coarse, K=32, refine | 3 | 31 | 48 | 14 | 94 | 0 |
| FT4 | 2,1 coarse, K=64, refine | 3 | 32 | 98 | 28 | 159 | 0 |
| FT4 | 4,2 coarse, K=64, no refine | 3 | 125 | 0 | 28 | 155 | 0 |

FT8 refined K=64 accepts `K1JT EA3AGB -15`, `K1JT HA0DU KN07`, `N1JFU EA6EE R-07`, `W1DIG SV9CVY -14`, `W1FC F5BZB -08`, `WM3PEN EA6VQ -09`: all six are in the
WSJT-X reference, and `K1JT EA3AGB -15` (a strong-sync LDPC failure at every geometry of experiment 1) is newly recovered. FT4 refined accepts
`K4SQC VE3RX RR73`, `N1TRK KB7RUQ RR73`, `WD9IGY KX1X 73`. Noise-only: 16 deterministic Gaussian recordings (8 FT8, 8 FT4), refine on, K=64: zero LDPC convergences, zero CRC passes,
zero accepts. No synthetic-signal corpus was run through this host tool; the synthetic end-to-end tests exercise the production path, which this experiment did not change.

Findings: refinement from the cheap 2,1 grid matches the best experiment-1 coverage (6 of 14 FT8, 3 of 19 FT4) while the coarse spectral stage stays at the 2,1 cost (about one quarter of
4,2). Coverage depends on K: K=16 gives 3 FT8, K=32 gives 5, K=64 gives 6, so candidate capacity (experiment 3) interacts with this and refinement is the larger lever. The
refinement cost shown (about 13 ms per candidate on the host) is exact correlation and is the number the efficient implementation must beat; a per-candidate down-conversion to a few hundred hertz
of bandwidth is expected to be well over an order of magnitude cheaper, but that is a design expectation, not a measurement.

Recommended next single change: candidate capacity and ranking (experiment 3 as ordered): measure K=96/128 with refinement, and rank by refined sync score instead of coarse score before cutting to the gate budget.
