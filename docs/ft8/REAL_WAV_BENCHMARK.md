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

Reference/OrcSDR comparison run: GitHub Actions `37722089040`,
decoder commit `f1e788ff0dfa8f80e22efe13eb77df0ce864b352`, default OrcSDR
geometry `rows_per_symbol=2`, `bins_per_tone=1` (80 ms / 6.25 Hz).

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
- OrcSDR host wall time: **5.15 s** on that GitHub Ubuntu runner
- OrcSDR SNR estimator: **not implemented**, so no SNR is fabricated.

The single accepted OrcSDR message is present in the reference output and its
audio-frequency estimate differs by 0.75 Hz from the published integer-Hz
reference value.

This baseline is intentionally poor compared with WSJT-X Deep. It is recorded
before adding finer search, improved soft metrics, subtraction, or additional
passes so later improvements have an honest before/after comparison.

## FT4 status

The WSJT-X user guide names tutorial sample `200514_182053.wav`, but that
filename currently returns HTTP 404 from the public SourceForge sample path.
Rather than invent another mirror, the benchmark uses the currently published
WSJT Project FT4 sample `190106_000115.wav` from the official SourceForge
FT4 directory. The native pipeline contains the QEX-defined receive-side
payload XOR restoration and a synthetic FT4 end-to-end standard-message
regression. External CI runs `jt9 --ft4 -d 3` and OrcSDR against the exact
same downloaded bytes.

## Regression floor

The external-WAV CI must, at minimum:
- produce at least one plausibility-valid decode;
- include exactly `WM3PEN EA6VQ -09`.

This is a floor, not a quality target. Task 3 should raise it only after
repeatable before/after measurements establish additional stable decodes.
