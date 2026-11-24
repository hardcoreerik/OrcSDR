# Digital Voice scanner benchmarks (2026-11-23)

What is measured, on what, and what it does and does not prove. Raw data sits next to this file (`*.jsonl`, `weak_signal_table.md`);
the host tools are `tests/digital_voice_live/dv_bench.cpp` and `run_host_benchmarks.sh`, the device scripts are `tab5_device_bench.py` and `tab5_bench_lib.py`.

## 1. CPU (Tab5, firmware as flashed, measured by the firmware's own counters over 30 s windows, live antenna on 152.1575 MHz)

Two cores matter: the DSP task (core 1, owns the USB stream and the 240 -> 48 kS/s channelizer) and the decode worker (core 0, one decoder set).

| Decoders running | DSP channelizer (core 1) | DMR | NXDN48 | NXDN96 | Worker total (core 0) | Ring drops in window | USB sample loss |
|---|---|---|---|---|---|---|---|
| DMR only | 20.3 % | 49.3 % | - | - | 49.3 % | 0 | 0 |
| NXDN48 only | 20.4 % | - | 35.7 % | - | 35.7 % | 0 | 0 |
| NXDN96 only | 20.4 % | - | - | 71.1 % | 71.1 % | 0 | 0 |
| all three forced | 21.4 % | 33.4 % | 24.1 % | 44.8 % | 102 % (saturated) | 31 | 0 |

Notes
- The decoders search for sync continuously when no signal is present, which is the expensive state; DMR measured about 0.26 of a core in the earlier offline test on a
  decoded stream and 0.49 here on noise. The scan planner therefore runs one decoder set per channel (an `auto` entry rotates them), never all three at once.
- Forcing all three overloads the worker: decoded data is dropped at the ring (31 blocks in 32 s) but the DSP task and the USB stream are untouched (0 lost samples).
- DSP block time: 14.2 ms average, 16.7 ms worst seen, against a 68.3 ms block period (16384 samples at 240 kS/s).

## 2. Memory (Tab5)

| State | PSRAM free | Internal RAM free (largest block) |
|---|---|---|
| Dashboard open, idle (receiver not started) | 21,269,676 B | 36,103 B (19,456 B) |
| Receiver running, decoders active, any mask | 20,956,316 B (about 313 KB used by ring, decoder state and worker stack) | 35,691 B (18,432 B) |

Decoder state is 36,264 B. Dashboard state, discovery tables and the SD write buffer are in PSRAM; the discovery database text is at most 24 KB (worst case measured 20.6 KB).
Internal RAM is the scarce resource on the Tab5; this build costs about 410 B of it while decoding, which is why decoder state and the database live in PSRAM.

## 3. Acquisition time

Host, real captures, time from the start of the stream (retune settle excluded) to the first validated event:
- Terra control channel 152.1575 MHz: 0.10 s clean, 0.20 s on the control-only capture.
- Terra 152.685 MHz (weaker): 1.13 s.
- First identity-validated event is when the call itself happens (7.9 s into the 152.1575 capture): identity needs a voice header or terminator, which exists only during a call.

Tab5, SD replay of the same recordings (decoder acquisition on the device; includes about 4 s of file open and receiver start, so the offset between the two columns is the useful number):

| Replay | First DMR event (wall) | First identity (wall) | Events | Identity events | Matches host |
|---|---|---|---|---|---|
| call, clean | 4.4 s | 12.9 s | 186 | 11 | yes, exactly |
| call + noise 3 dB below capture power | 4.6 s | 13.1 s | 141 | 10 | yes, exactly |

(A third replay, with 6 dB noise, could not be completed: the 6.7 MB file never finished copying to the SD card, so no device number is claimed for it. The host figure is 185 events / 11 identity.)

## 4. Weak-signal performance (host, real captures + added white Gaussian noise, 3 seeds)

"Added noise" is relative to the capture's own RMS (0 dB = added noise as strong as everything already in the capture), so it is a degradation, not an absolute SNR.
The captures' own SNR was not measured with a calibrated instrument.

| Capture | Added noise vs capture RMS | Events (3 seeds) | Identity events | Calls found | First valid burst |
|---|---|---|---|---|---|
| terra_152p1575_call_cc13 | 0.0 dB | [19, 28, 28] | [2, 1, 3] | [1, 1, 1] of 2 | 1.18 s |
| terra_152p1575_call_cc13 | -3.0 dB | [147, 153, 145] | [10, 9, 9] | [2, 2, 2] of 2 | 0.13 s |
| terra_152p1575_call_cc13 | -6.0 dB | [177, 182, 183] | [10, 11, 11] | [2, 2, 2] of 2 | 0.10 s |
| terra_152p1575_call_cc13 | -9.0 dB | [186, 186, 185] | [11, 11, 11] | [2, 2, 2] of 2 | 0.10 s |
| terra_152p1575_call_cc13 | -12.0 dB | [186, 186, 186] | [11, 11, 11] | [2, 2, 2] of 2 | 0.10 s |
| terra_152p1575_call_cc13 | -15.0 dB | [186, 186, 186] | [11, 11, 11] | [2, 2, 2] of 2 | 0.10 s |
| terra_152p1575_call_cc13 | -18.0 dB | [186, 186, 186] | [11, 11, 11] | [2, 2, 2] of 2 | 0.10 s |
| terra_152p1575_call_cc13 | none | [186] | [11] | [2] of 2 | 0.10 s |
| terra_152p1575_control_only | 0.0 dB | [2, 2, 2] | [0, 0, 0] | [0, 0, 0] of 0 | 2.06 s |
| terra_152p1575_control_only | -3.0 dB | [40, 36, 35] | [0, 0, 0] | [0, 0, 0] of 0 | 0.20 s |
| terra_152p1575_control_only | -6.0 dB | [79, 80, 78] | [0, 0, 0] | [0, 0, 0] of 0 | 0.20 s |
| terra_152p1575_control_only | -9.0 dB | [91, 96, 89] | [0, 0, 0] | [0, 0, 0] of 0 | 0.20 s |
| terra_152p1575_control_only | -12.0 dB | [97, 97, 94] | [0, 0, 0] | [0, 0, 0] of 0 | 0.20 s |
| terra_152p1575_control_only | -15.0 dB | [96, 98, 94] | [0, 0, 0] | [0, 0, 0] of 0 | 0.20 s |
| terra_152p1575_control_only | -18.0 dB | [96, 99, 97] | [0, 0, 0] | [0, 0, 0] of 0 | 0.20 s |
| terra_152p1575_control_only | none | [98] | [0] | [0] of 0 | 0.20 s |
| terra_152p685_call_cc8 | 0.0 dB | [3, 3, 1] | [0, 1, 0] | [0, 1, 0] of 1 | 7.41 s |
| terra_152p685_call_cc8 | -3.0 dB | [67, 58, 64] | [10, 4, 10] | [1, 1, 1] of 1 | 2.11 s |
| terra_152p685_call_cc8 | -6.0 dB | [115, 111, 111] | [12, 12, 13] | [1, 1, 1] of 1 | 1.19 s |
| terra_152p685_call_cc8 | -9.0 dB | [129, 122, 125] | [15, 13, 14] | [1, 1, 1] of 1 | 1.13 s |
| terra_152p685_call_cc8 | -12.0 dB | [133, 134, 131] | [15, 15, 15] | [1, 1, 1] of 1 | 1.13 s |
| terra_152p685_call_cc8 | -15.0 dB | [137, 135, 134] | [16, 15, 15] | [1, 1, 1] of 1 | 1.13 s |
| terra_152p685_call_cc8 | -18.0 dB | [137, 137, 136] | [16, 15, 14] | [1, 1, 1] of 1 | 1.13 s |
| terra_152p685_call_cc8 | none | [136] | [13] | [1] of 1 | 1.13 s |

Reading it: decoding is unaffected up to 9-12 dB of added noise, gives up about a quarter of the events at -3 dB (both calls still found), and collapses at 0 dB, where
only one or two bursts get through and calls are found in some runs only. At 0 dB the weaker 152.685 MHz capture still produced a correct identity in 1 of 3 seeds.
Adding a little noise sometimes yields slightly more events than none (8-bit quantisation dither); that is a measurement artefact, not a gain.

Tab5 live reception (not a replay): on the Tab5's own antenna the Terra control channels are only about 1 dB above the receiver noise at maximum tuner gain
(level 162 vs 139 on an empty channel), which is below what the table shows the decoder needs. See section 6.

## 5. False identification

- Pure Gaussian noise at a receiver-like level through all four decoder masks, 2 seeds each, 900 s per run: **7,200 s, 0 events, 0 identity events** (`false_id.jsonl`).
- Weak-signal runs above (63 runs): **0 identities that were not in the true set**.
- Real idle channels in the regression corpus (idle NXDN voice channel, all decoders enabled): 0 events.
- Statistical context: an identity needs sync, slot-type Golay, BPTC and an RS(12,9) check to all pass, so 0 in two hours means an upper bound of about 1.5 per hour at 95 %
  (rule of three), not a measured rate of zero. Voice identities are RS-validated, never inferred.

## 6. Tab5 direct live reception (no PC, no replay) - status

The Tab5 receive chain (own dongle, own antenna) runs cleanly live: 0 lost samples, decoders running, level readings tracking gain. It has **not** decoded a DMR burst
off the air yet, because the antenna it has hears the 152 MHz Terra signals at about 1 dB above its noise (gain sweep in `rf/tab5live/gain_sweep_2026-11-23.json`),
whereas the PC dongle with its LNA hears them well above it. The unattended Tab5-only hunter (`rf/tab5_live_hunt.py`) keeps trying on the known channels with its results
in `rf/tab5live/tab5live.jsonl`.
