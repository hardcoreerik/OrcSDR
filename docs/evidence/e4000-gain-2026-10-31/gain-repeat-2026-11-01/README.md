# Focused E4000 gain repeat, 2026-11-01

Same COM39 firmware/driver and tinySA fixture as the parent report. User
confirmed the fixture unchanged. Source CW 100.1 MHz, nominal -40 dBm,
30 dB attenuator; receiver 100 MHz, 2.4 MS/s. No flash, restart command or
access to COM17/COM8. Each capture stops reception for raw IQ retrieval;
therefore these results do not isolate gain changes from start/stop history.

## Result

The script completed (exit 0). Nine 4,800,000-byte captures were retrieved
and independently SHA-256 verified (43,200,000 bytes). Source tone and
rail-clipping checks passed; all captures had zero raw rail clipping.
Manual gain/state was checked before each signal capture, with a one-second
settling delay. This verifies reported software state, not a tuner-register
readback after the capture. Output OFF and pause were acknowledged at exit.

Integrated tone results, in capture order:

| Requested dB | Measured relative tone dBFS |
| ---: | ---: |
| 14 | -25.372 |
| 16.5 | -26.012 |
| 19 | -22.494 |
| 16.5 | -26.075 |
| 14 | -27.252 |
| 29 | -15.531 |
| 34 | -12.916 |
| 29 | -15.559 |

The first 14 -> 16.5 step fell 0.640 dB. Both 16.5 captures agree within
0.063 dB. Returning to 14 gave 1.880 dB less signal than the first 14 capture.
The two 29 captures agree within 0.028 dB; 34 is about 2.62 dB higher.
This is evidence of an unresolved repeatability/state or test-condition
issue, not proof of its cause. Gain table changes are premature.

## Measurement correction

The earlier single-FFT-bin endpoint metric was sensitive to tone energy
spreading into adjacent bins. Applying the same integrated-bin method to
both original datasets gives ESP span 34.709 dB and PC span 36.707 dB,
a 1.998 dB difference. These are uncalibrated comparative values; neither
establishes a calibrated 43 dB hardware range. Preserve the original report
as its original measurement; use this correction when comparing spans.

The archived analyzer sums FFT bins round(100000/2400000*65536) +/-4 for
every complete 65,536-sample block, using a Hann window normalized by its
sum, then averages power in linear units. This is a consistent relative
power convention, not a newly calibrated absolute dBFS standard.

## Repeat and next discriminator

The archived scripts retain the actual local paths used. Run from repository
root with numpy/pyserial, a fresh output directory and matching fixture.
Adjust the source-helper and authentication-key paths as described in the
parent controlled-rf README. Do not run competing serial clients or flash.
Raw IQ remains in the ignored build directory; JSON contains its hashes.

Next isolate offset state with paired OFF/ON measurements, then compare
initialization and tuner gain/mode register readback against first-party PC
controls. A live-gain test without intervening receiver starts would also
separate gain history from start/stop history. No driver fix is claimed.
