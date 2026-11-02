# Fixed-gain raw restart test, 2026-11-01

Existing a00e130 firmware COM39; no rebuild/flash. Same XTR/tinySA fixture:
100.1 MHz nominal -40 dBm -> 30 dB attenuator; receiver100 MHz/2.4 MS/s.
Source-off baseline, then source ON; initial start/gain14 before first signal
capture. No gain command between signal points. Stop then RTL_TUNE FM100MHz
between points, wait STREAMING/manual140 plus4s. Offset and gain statuses
recorded. This tests application stop/start, not an isolated single register.
Source state constant during all three source-on captures.

Four 4.8MB CU8 captures independently matched device SHA-256. No rail clipping.
Retrieval occurs while streaming, removing the old harness stop-before-transfer.
Integrated Hann +/-4-bin tone powers:

| Condition | dBFS |
| --- | ---: |
| Before restart, gain14 | -27.45040 |
| After first stop/start, gain14 | -29.30431 |
| After second stop/start, gain14 | -29.31976 |

First shift -1.85391dB; second -0.01545dB. Confirms real raw-IQ level change
associated with first restart at unchanged requested gain and source. It is
not a ~1.8dB loss on every restart. Combined with live-gain stability,
prioritizes initial versus restarted state. Does not identify exact tuner,
demod, calibration or application cause; absolute gain remains uncalibrated.
Do not change nominal gain codes to compensate. Source-off capture alone
is not a matched per-point source power calibration.

Script exited0. Final OFF/pause acknowledged; manual -1dB restored after
source-off. COM17/COM8 unchanged. Raw files stay ignored build directory;
events/results preserve hashes and state. Archived script retains local paths;
repeat with same fixture and fresh output directory.
