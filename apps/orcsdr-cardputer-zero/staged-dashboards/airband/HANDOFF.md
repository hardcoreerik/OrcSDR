# Airband AM sandbox implementer handoff — 2026-11-17

**Repository:** `hardcoreerik/OrcSDR`
**Target integration branch:** `codex/cardputer-zero-ui-audio-controls`
**Verified baseline via connected GitHub API:** `73736f9e784dd8fcc20c935620740a00dcbca301` (Nov 17 inspection). Remote branch state must be fetched/reverified by coordinator just before application.
**Sandbox work lane:** `sandbox/cardputer-zero-airband-dashboard`
**Strict owned path:** `apps/orcsdr-cardputer-zero/staged-dashboards/airband/`
**Initial risk:** HIGH (no existing Linux AM session; real hardware not owned by sandbox).
**Approval:** Task authorizes implementation/tests and a folder-scoped delivery to integration branch, NOT changes to `main`, a release, device access or cross-folder integrations.

## Delivery inventory

- `airband.hpp`, `airband.cpp`: portable controller, AM DSP, input/parser/step, relative squelch, gain/volume, bounded PCM and telemetry; references the EXISTING host metadata header.
- `airband_render.cpp`: 320x170 framebuffer with dirty-rect reporting and real FFT zoom/waterfall; includes same-project OrcSDR font patterns.
- `demo.cpp`: explicitly marked DEMO synthetic AM preview, optional raw S16LE PCM file, no devices.
- `tests/airband_tests.cpp`: deterministic source IQ and safety/regression checks.
- `CMakeLists.txt`: independent C++17 staged build and CTest executable.
- `README.md`, `INTEGRATION.md`, `RESEARCH.md`, `HANDOFF.md`: operator commands, coordinator seams, source/license credits and acceptance constraints.

## Exact sandbox verification

Executed on **Linux x86_64, Intel Xeon Platinum 8573C, GCC 14.2, CMake 3.**/CTest. No Cardputer hardware or radio was used.

```sh
cmake -S apps/orcsdr-cardputer-zero/staged-dashboards/airband -B build/staged-airband -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/staged-airband --parallel 2
ctest --test-dir build/staged-airband --output-on-failure
# observed: 1/1 CTest PASS (airband_regression), 100%, about 1.1 seconds

build/staged-airband/orc_zero_airband_preview --demo --screenshot build/staged-airband/airband-demo.ppm --pcm build/staged-airband/demo-audio.s16le
# observed: P6 320x170 RGB preview; exactly 24,548 generated PCM frames @ 48k Hz

cmake -S apps/orcsdr-cardputer-zero/staged-dashboards/airband -B build/staged-airband-release -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/staged-airband-release --parallel 2
ctest --test-dir build/staged-airband-release --output-on-failure
# observed: 1/1 CTest PASS, about 0.36 seconds

/usr/bin/time -f 'wall=%e user=%U sys=%S maxRSS=%M KiB' build/staged-airband-release/orc_zero_airband_preview --demo --screenshot build/staged-airband-release/airband-demo.ppm
# measured: wall 0.06s, user 0.06s, sys 0.00s, maxRSS 2048 KiB
```

**Bounded performance sample:** preview generated and processed 160 × 8,192 complex CU8 samples = 1,310,720 samples, approximately **0.512 seconds** of 2.56 MS/s input. On the Xeon x86_64 host only, 0.06 s wall time is ~8.5× faster than synthetic real time, and includes preview generation. This is NOT a measured Cardputer Zero processing margin. `maxRSS` is whole preview process and not a measured module footprint. Fixed framebuffer is 320×170×2 = 108,800 bytes; waterfall stores 32×256 = 8,192 bytes per snapshot; IQ payload per call ≤32,768 bytes; PCM callback at most 256 frames. Renderer retains an internal previous snapshot. Its dirty rectangle list holds at most 6 entries. An IQ queue, if used, is bounded separately by the host.

## Negative/positive controls

- Real AM tone recovered from *offset, mixed and channel-filtered* CU8 at both 2.4 MS/s and 2.56 MS/s; 48 kHz output, tone check.
- Desired + adjacent 25-kHz AM: synthetic 1,000 Hz desired PCM amplitude ~5950, +25-kHz interferer's 1,800 Hz component ~461 in the combined-channel test (approx −22 dB **at this exact synthetic setup**, not a calibrated adjacent-channel standard). An interferer-only test yields further rejection.
- Chunk-size equivalence, invalid frequency/rate/IQ, true 25 kHz stepping, exact user mute and volume=0 silence, gain state/settings validation, audio sink failure, strong/DC inputs, queue discontinuities, stale-generation rejection, tune-pending and error restoration.
- UI regression: volume-only change produces actual changed pixels while dirty list is just the y=49..67 status strip and full-screen-clear count is unchanged.
- Generated preview was visually inspected in sandbox; compact `OrcSDR`/`AIRBAND AM` header, exact selected frequency, `DEMO` marker, spectrum peak, target marker and span label fit 320x170 without clipping. Physical LCD appearance remains unverified.

## Known issues and integration requests

1. **Not production-integrated.** The coordinator must own and connect the session/selector, host queue, RtlReceiver configuration/readback, ALSA lifecycle, shared waterfall palette, real Cardputer keycodes, region-blit calls and persistence. See `INTEGRATION.md` for exact filenames and calls. This request does **not** authorize editing outside the staged folder.
2. **Squelch not RF-qualified.** Signal/floor are relative, 200 ms warmup, ±1.5 dB hysteresis, ~500 ms activity hang; the carrier-presence heuristic is imperfect under oscillator error/weak fading. In a synthetic signal-to-silence transition, closing can take longer than 500 ms because energy settling delays the start of the hang. Physical aviation reception is needed before default threshold optimization.
3. **No 8.33 channel names.** Only 25 kHz steps and exact direct Hz are implemented. Manual decimal entry may land off raster intentionally. Do not advertise 8.33 support.
4. **Audio and USB availability are separate.** The reusable module has no ALSA device selection. The coordinator's `default` ALSA may be broken while `plughw:ES8389Audio,0` works. Its card index is NOT a product default. No hot reconnect loop is included.
5. **Render dirty rectangles are reported** but cannot guarantee physical LCD flicker until the host forwards only those regions to its actual framebuffer transfer layer. No shared global UI file was modified.
6. **Host intake stub:** local sandbox lacked network/DNS for `git clone`; build verification used an untracked **metadata-equivalent shadow copy** of `src/iq_queue.hpp`, with the same publicly fetched `IqMetadata` fields, strictly outside this assigned folder. The shipped stage references the real `../../src/iq_queue.hpp` and will use the actual header in a full OrcSDR checkout. Coordinator should run the stated commands on the actual fetched branch before treating it as integrated acceptance.
7. **Git publication:** direct sandbox `git clone` was blocked by DNS. Source was read via connected GitHub tools, and the commit may be published using GitHub Git-data API with a branch-head compare-and-swap (no force). The final response must report the VERIFIED remote SHA separately from the local isolated patch SHA; never infer publication from local success.

## Hardware gate checklist (coordinator only)

- [ ] Pull current integration branch; ensure no unpushed Home/FM changes lost; verify folder diff paths.
- [ ] Wire host single-receiver adapter and `AudioSink`, use generation readback/queue flush/discard; choose actual ALSA output explicitly.
- [ ] Confirm hardware 2.56 MS/s readback, continuous CPU load/drop figures; attempt 2.4 MS/s fallback only with matching metadata.
- [ ] Tune a real authorized 118–136.975 MHz AM station using an adequate airband antenna, verify intelligible speech, RSS/noise floor, squelch and RF center offset.
- [ ] Validate audio volume, mute, silence after retune, speaker/headphone routing, and sink-failure indication.
- [ ] Validate physical keys, selector/P/Tab ownership, exact frequency entry, dirty-rect blits, palette cycling and LCD readability without full clears.
- [ ] Test USB disconnect/reconnect, receiver open errors, stopping playback on app exit, and FM↔Airband ownership handover.

**Not checked:** physical Cardputer Zero, live RF, native ALSA, real display driver, or branch push authentication.
**Material remaining risk:** a synthetic-successful AM DSP may still exceed real-device CPU/USB budgets or have weak-airband squelch/audio quality problems, and it remains unintegrated until coordinator wiring is tested.
