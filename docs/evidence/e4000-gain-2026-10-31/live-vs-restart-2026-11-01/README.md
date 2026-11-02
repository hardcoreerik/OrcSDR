# Live gain versus restart, 2026-11-01

Current COM39 image/driver a00e130, no new build/flash. XTR same fixture,
tinySA 100.1 MHz nominal -40 dBm -> 30 dB attenuator, receiver100 MHz/2.4 MS/s.
COM17/COM8 untouched. Both scripts completed exit0; source OFF/pause
acknowledged at each finish. Raw files retained ignored build directory.

## Existing signal telemetry

RTL_SIGNAL smoothed/rounded 0.1 dB measurements (10 observations per point,
4 s settling, 0.3 s spacing): live 14/16.5/14/16.5/14 means
-31.6/-30.5/-31.6/-30.5/-31.6 dBFS. At fixed14, three stop/start means
-33.30/-33.29/-33.30. First restart lowered level about1.7 dB; later restarts
stayed lower. Not calibrated tone power, and strided IQ power may introduce
bias. See telemetry-events.json. This suggests the restart path, not proof
of which tuner/demod field is responsible.

## Raw live capture confirmation

Code inspection confirms iq_get_begin reads an immutable completed buffer
without requiring RTL_STOP. The previous lab runner inserted a stop itself.
This runner removes that stop and keeps the stream running between the three
source-on captures. Driver STREAMING and requested gain checked before/after.
Baseline is captured first; source-on sequence has a single initial start.
No RTL_STOP or RTL_TUNE between its 14/16.5/14 capture points. Source output
is fixed during the sequence. Capture/retrieval could still affect scheduling.

Four 4,800,000-byte CU8 captures matched device SHA-256; no rail clipping.
Integrated Hann +/-4-bin tone power, same analysis convention as previous runs:
14=-27.45307, 16.5=-26.37244, return14=-27.55953 dBFS.
Step +1.08062 dB; return delta -0.10647 dB. Gain step direction now agrees
with PC reference, unlike restarted captures. Return differs ~0.11 dB over
long serial transfers; PC short-read timing is different. Do not claim
calibrated gain parity, zero drift, or that firmware is fixed.

## Next boundary

Treat the earlier stopped-capture gain dip as confounded by receiver startup.
Investigate demod/tuner initialization versus retained state across stop/start,
including calibration and sample-rate restoration, using targeted traces.
Do not change gain codes to compensate. Raw live capture protocol needs no
new firmware API. Scripts retain actual local paths; choose fresh output dirs
and follow parent fixture instructions when repeating.
