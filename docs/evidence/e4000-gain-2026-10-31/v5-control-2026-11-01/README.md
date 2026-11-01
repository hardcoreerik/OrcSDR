# Nooelec V5 control, 2026-11-01

User connected a spare V5 to COM39 and transferred the same tinySA RF ->
30 dB attenuator cable. COM17/COM8 unchanged. Firmware unchanged; no flash.
Preflight profile=3 nooelec_smart_v5_r820t2, receiver Nooelec V5, streaming
2.4 MS/s. Supported nearest comparison steps: 14.4/16.6/19.7 dB.

CW 100.1 MHz nominal -40 dBm; receiver 100 MHz. Sequence:
14.4 -> 16.6 -> 19.7 -> 16.6 -> 14.4. Same stop/retrieve/start path as XTR.
Six captures (28,800,000 bytes) matched SHA-256 hashes, zero rail clipping,
source tone passed script frequency/amplitude checks. Script exit 0;
finally output OFF and pause acknowledged. Final receiver request manual 0 dB.

Integrated tone results, same +/-4-bin Hann power method as XTR repeat:
-55.223, -52.807, -50.279, -52.899, -55.252 dBFS.
Returning to 14.4 differs -0.029 dB; returning to 16.6 differs -0.092 dB.
Both are within 0.10 dB. The XTR's ~1.9 dB repeat shift was not reproduced
on this V5 under the shared acquisition path. This is one control run, not
proof that all shared software/instrument behavior is excluded. Absolute
cross-tuner dBFS values are not calibrated gain comparisons.

Archived script retains local paths; follow parent controlled-rf setup and
choose a fresh output directory to repeat. Raw CU8 stays in ignored
build-offset/gain-candidate/v5-gain-control-20261101. events.json records
commands and device hashes. No authentication secrets are included.

Next discriminator: put XTR on PC with unchanged RF fixture and repeat
same-gain returns while recording USB transactions and initialization.
