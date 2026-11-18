# OrcSDR Cardputer Zero — Airband AM (staged)

**Selector:** `airband` / `Airband AM`
**Product:** OrcSDR · Oregon Radio Company
**State:** native Linux **staged** manual AM receive path; **not yet connected to the production selector or physically qualified**.

This C++17 module converts actual CU8 IQ to 48,000 Hz mono signed 16-bit PCM. It is **not** the FM demodulator relabeled. It does not own the RTL-SDR, ALSA, LCD, global input dispatcher, palette selection, or settings file. The orchestrating application owns the single receiver and the current AudioSink. There are no external library dependencies for this staged module.

## Supported radio behavior

- **VHF airband:** 118.000000–136.975000 MHz inclusive, aligned to `apps/orcsdr-tab5/ui/airband_scanner.hpp` (guard: 121.500000 MHz). This is a receive range, **not** a claim that every RF tuner supports its endpoints.
- **Channel stepping:** real +/−25,000 Hz with saturated band edges. Exact six-digit MHz manual entry can choose a non-25-kHz carrier, marked as a direct frequency, not a fabricated 8.33 channel name. **8.33-kHz aviation channel-designator mode is NOT implemented.** A 25 kHz step after a manually entered off-grid carrier remains a 25 kHz increment, not an implicit snap.
- **Input:** 2,560,000 CU8 complex samples/s preferred; 2,400,000 supported if the host confirms actual readback. Decimation is 32:1 or 30:1 to 80 kS/s complex. The current host request is 2.56 MS/s, not a fake resampled rate.
- **Frequency placement:** tune the RTL center 125 kHz away from the selected channel, normally +125 kHz; use −125 kHz close to the upper edge. The selected channel is mixed to baseband with the signed `selected_hz - applied_center_hz` offset. This avoids the center-DC image.
- **Actual selected-channel DSP:** signed complex NCO → modulo-safe, 3-stage CIC decimator to 80 kS/s → 97-tap Blackman-windowed, 3-phase 80:48 kS/s narrow-channel FIR → AM envelope detector → adaptive envelope DC blocker → ~250 Hz high-pass + two ~3.6 kHz low-pass stages → fixed safe level, user volume and squelch gate → S16 mono PCM. The interpolation is causal and consistent across CU8 block boundaries. **RF overview width is not AM voice bandwidth.**
- **Plot:** honest 640 kHz zoom window centered on the selected station, sampled from an actual 1,024-point FFT of the 2.4/2.56 MHz IQ stream. FFT labels/marker reference applied RF-center metadata. The input stream spans 2.4 or 2.56 MHz; the **display zoom is 640 kHz**, so the ±5 kHz selected passband is visibly marked.
- **Audio safety:** PCM ≤70% signed full scale (about 6 dB headroom); no dynamic audio AGC/no noise amplification. Mute, volume 0, disconnected, no sink, closed squelch yield exact zero PCM. An AudioSink that fails should be closed by the host, not retried in a hot loop.
- **Squelch:** `0` is labeled `SQL BYPASS` (diagnostic mode); default threshold is `6` **relative dB**, **not calibrated dBm or a standards-compliant receiver sensitivity**. Gate has a ~200 ms warmup, ±1.5 dB hysteresis and ~500 ms hang after last valid activity. The level/floor is derived from in-channel envelope, with a normalized full-carrier stability heuristic for transmissions already present when tuning. Weak/noisy on-air transmissions and start-on-signal corner cases require physical qualification. User mute is independent of this gate.

## Controls (host action equivalents are mandatory)

| Key | Action | Host call |
|---|---|---|
| F | Exact MHz frequency entry | `action(frequency_entry)`, `action(entry_digit, char)`, `action(entry_point)`, `action(entry_commit)` |
| Left / Right | Tune one 25 kHz step | `action(tune_down)` / `action(tune_up)` |
| S / D | Volume −5 / +5 | `action(volume_down)` / `action(volume_up)` |
| M | User mute | `action(mute)` |
| G | Toggle requested tuner auto gain | `action(gain_auto_toggle)` |
| Gain UI left / right | Adjust requested manual gain by 1 dB | `action(gain_down)` / `action(gain_up)` |
| `[` / `]` | Relative squelch −1 / +1 dB | `action(squelch_down)` / `action(squelch_up)` |
| T | Fixed 25 kHz step (no other steps implemented) | host shows `25 KHZ ONLY` |
| Enter / Backspace / Escape | Submit/edit/cancel frequency entry | `action(entry_commit/entry_backspace/entry_cancel)` |
| Space / P / Tab / Q | **Global selector/palette/menu/quit**, NOT owned by airband | handled by coordinator |

`keyboard_action()` handles printable keys, return, backspace and Escape **only while entry is active**; the coordinator remains responsible for translating real Cardputer keyboard codes, arrows and the global keys.

## Standalone configure / build / test

From the full OrcSDR repository root:

```sh
cmake -S apps/orcsdr-cardputer-zero/staged-dashboards/airband -B build/staged-airband -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/staged-airband --parallel 2
ctest --test-dir build/staged-airband --output-on-failure
build/staged-airband/orc_zero_airband_preview --demo --screenshot build/staged-airband/airband-demo.ppm
```

On Windows, adapt the executable suffix and the generator-specific configuration directory, e.g. `build/staged-airband/Debug/orc_zero_airband_preview.exe`.

Open the generated `airband-demo.ppm` with an image viewer. This is a **DEMO** image from deterministic synthetic AM IQ, **never LIVE RF**. Audio testing is optional and does not assume ALSA or a speaker:

```sh
build/staged-airband/orc_zero_airband_preview --demo --screenshot build/staged-airband/airband-demo.ppm --pcm build/staged-airband/demo-audio.s16le
# optional: play the raw file only on a machine with explicitly configured audio output
# ffplay -f s16le -ar 48000 -ac 1 build/staged-airband/demo-audio.s16le
```

This PCM preview is explicitly synthetic; no audio can be heard unless the operator opts to play it. The standalone preview writes output under `build/` and has no access to the physical device.

### What the checks prove

The test harness generates deterministic complex AM with the carrier **offset from the RTL center**, plus a +25 kHz interferer. It checks tone and 48 kHz rate at both input rates, adjacent-channel isolation, chunk-invariant PCM, exact muting/volume-zero, band/entry/step controls, readback failure, unsupported-rate and stale-generation rejection, queue discontinuity, squelch, frame safety and changed-region drawing. The rendering regression demands that a changed volume modifies framebuffer pixels and redraws **only** the 49–68 pixel status strip, without incrementing the full-frame clear count.

**Not proven:** on-device capture throughput, ALSA routing, actual audio loudness, weak-signal performance, antenna correctness, LCD flicker or Cardputer keyboard mapping. See `INTEGRATION.md` and `HANDOFF.md`.
