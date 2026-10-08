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
