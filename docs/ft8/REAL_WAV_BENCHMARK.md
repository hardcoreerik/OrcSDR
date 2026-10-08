# FT8 Real-WAV Reference Benchmark

Status: baseline measurement before decoder-coverage optimization.

Sample: `210703_133430.wav`

Official sample source:
https://sourceforge.net/projects/wsjt/files/samples/FT8/210703_133430.wav

SHA-256:
`9feb99c275770a6618538026da7decc6b09eb6cf63121e5168fa86dcdf00c2f5`

Reference: WSJT-X/jt9 published decode output for this sample. The WSJT-X user
guide identifies this file as its FT8 tutorial sample. A WSJT development-list
example publishes 11 jt9 FT8 decodes for the same file.

OrcSDR measurement: commit `cc6fafc3f6bf3114da122355cc8b96c62b597756`,
GitHub Actions run `37684046948`, default benchmark geometry
`rows_per_symbol=2`, `bins_per_tone=1` (80 ms / 6.25 Hz).

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

## Baseline score

- Reference decodes: **11**
- OrcSDR accepted decodes: **1**
- Coverage: **1/11 = 9.1%**
- Accepted OrcSDR messages not present in the 11-message reference list: **0**
- Current observed false-decode count on this recording: **0**
- Host wall time: **4.45 s** on GitHub's Ubuntu runner
- OrcSDR SNR estimator: **not implemented**, so no SNR is fabricated.

The single accepted OrcSDR message is present in the reference output and its
audio-frequency estimate differs by 0.75 Hz from the published integer-Hz
reference value.

This baseline is intentionally poor compared with WSJT-X Deep. It is recorded
before adding finer search, improved soft metrics, subtraction, or additional
passes so later improvements have an honest before/after comparison.

## FT4 status

The official WSJT-X FT4 tutorial sample is `200514_182053.wav`. The current
native receive pipeline still refuses FT4 at its final acceptance layer because
the protocol-defined FT4 payload XOR restoration has not yet been implemented
and tested there. The FT4 real-WAV benchmark is therefore **blocked**, not
reported as a zero-decode result. The next FT4-specific step is to finish and
test that transform gate before measuring the official sample.

## Regression floor

The external-WAV CI must, at minimum:
- produce at least one plausibility-valid decode;
- include exactly `WM3PEN EA6VQ -09`.

This is a floor, not a quality target. Task 3 should raise it only after
repeatable before/after measurements establish additional stable decodes.
