# Retune versus full initialization, 2026-11-01

Current a00e130 COM39 firmware; no build/flash. Same XTR/tinySA100.1MHz
nominal-40dBm/30dB fixture. Receiver100MHz/2.4MS/s. Existing RTL_SIGNAL,
10 observations per point,4s settle,0.3s intervals. Smoothed/rounded telemetry,
not calibrated RF or a raw-IQ confirmation of each retune case.

| Operation | Mean dBFS |
| --- | ---: |
| Initial -1 then14 gain | -31.80 |
| Same-frequency driver retune1 | -31.79 |
| Same-frequency driver retune2 | -31.78 |
| Reapply manual14 | -31.79 |
| Gain-1, driver retune, then14 | -31.80 |
| Full stop/start at14 | -33.50 |

Gain shadow and STREAMING checked; no loss of the ~1.7dB until full restart.
This narrows the boundary to full initialization versus normal retune/gain.
Full restart invokes run_init_table, shared demod prefix, E4000 initialize,
sample-rate restore and tune; existing driver retune avoids the full init.
Static check shows common demod gain/AGC setup matches first-party PC trace
(p0r19=05,p1r93=f0,r94=0f,r11=00,r04=00,B1=1b), so do not blindly change
them. Actual wider register snapshots before/after initialization are still
needed. No exact root cause/fix established.

First script attempt incorrectly requested gain0 then waited for exact0;
E4000 nearest step is-1. It stopped before RF ON; OFF/pause acknowledged.
Corrected archived runner uses-1. Valid run exited0, final sourceOFF/pause
acknowledged. Leaves receiver streaming manual14. COM17/COM8 untouched.
Actual local paths retained in script; fresh output directory required.
