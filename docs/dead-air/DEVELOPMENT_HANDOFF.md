# Dead Air — native Tab5 development handoff


> **Historical design/integration handoff, not current support status.** Its milestone-one "not hardware tested" statement and "ENV III not present" tasks describe the original development period. The current mainline dashboard is implemented; the maintainer confirms working ENV III temperature/humidity/pressure readings on Tab5 (2026-11-04). See [current field acceptance](../MAINTAINER_FIELD_ACCEPTANCE_2026-11-04.md) and [current project status](../../PROJECT_STATUS.md). Historical steps below are retained for engineering provenance.

Status: implementation milestone 1; **not** a hardware-tested release.
Work branch: chatgpt/dead-air-native-ui-20261018.
Target hardware: M5Stack Tab5 (ESP32-P4, 1280 x 720, 5-inch touchscreen).

## Design contract

- The approved design is Proposal A: science-first, grounded in observations. Avoid ghost/spirit/entity detector claims. No fabricated numerical data, spectra, waterfalls, recordings or probability scores.
- Prefer a 5-inch-usable hierarchy: large content, readable default fonts, a 52 px minimum tab/action height. Six tabs: Overview / Spectrum / Audio / Environment / Events / Review.
- Native ESP-IDF + M5GFX / M5Unified, using the existing shared dashboard header. Avoid HTML and avoid drawing from the DSP/USB callback.
- ENV III (SHT30 + QMP6988 temperature, RH and pressure) is optional. Not present, warming-up, stale or invalid readings show "--" and cannot produce valid detector events.
- Display RF power as **dBFS** until a rigorously supported calibrated dBm path exists. Never call a tuned frequency a simultaneously monitored sweep window.

## Added files and behavior

- apps/orcsdr-tab5/ui/dead_air_model.hpp and .cpp: observation data model, 48-event bounded ring, three RF anomaly sensitivity presets (+12 / +8 / +5 dB relative to valid frequency-local baseline), 12 valid sample warm-up, 200 ms minimum persistence, 3 s cooldown, retune/config revision reset, manual event markers, source names, and no stale readings represented as live.
- apps/orcsdr-tab5/ui/dead_air_dashboard.hpp and .cpp: six M5GFX-native panels, shared OrcSDR header and bottom tabs, normal receiver and sensor unavailable states, plots only when snapshot validity flags are asserted, Event Model threshold buttons and manual event marker action.
- tests/dead_air_model_tests.cpp: host tests for reading freshness, sensitivity, event debounce, retune/sample-gap resets, bounded ring and manual markers.
- ui/dead_air_journal.hpp/.cpp: fixed-capacity SPSC event queue, versioned JSONL journal, lost-sequence records, per-batch sync and explicit failed state. EventModel::set_listener(enqueue_event, &queue) feeds the queue without storage access.
- ui/dead_air_sd_journal.hpp/.cpp: SD adapter using OrcSDR FileSystem with unique session paths beneath /orcsdr/dead_air, exact-write checks, sync, and partial-write poisoning.
- tests/dead_air_journal_tests.cpp, tests/dead_air_sd_sink_tests.cpp, tests/dead_air_render_tests.cpp: independent additional host suites.
- Existing dashboard registry / screen controller / native component CMake configuration updated to include Dead Air screen identity and compile the new sources.

## Known integration blocker

The GitHub connector returned an empty main.cpp file, including small line-range reads, because the source is extremely large. Avoiding unsafe blind edits, this branch **does not modify apps/orcsdr-tab5/ui/main.cpp**. The descriptor appears in the dashboard registry, but the screen will NOT open through Home until a local adapter handles the application-level transition and touch/receiver events.

Not yet implemented or tested:
1. Live receiver/IQ/audio snapshot source or tuner-settled signals; the screen intentionally renders no synthetic waterfall.
2. Real audio recording, replay, clip collection, hashes, or exports.
3. SD event journal queue/encoder/filesystem sink are implemented and host-tested but NOT attached to a FreeRTOS storage worker or the running dashboard. Events remain in RAM until application integration; media archives and crash recovery are still unfinished.
4. ENV III I2C probe, external power control, data freshness, per-sensor detector policies.
5. Build with the pinned ESP-IDF/managed M5GFX dependency and physical Tab5 checks.

## Next local integration, staged

1. Review the branch against the actively changing main worktree and reconcile registry/screen-controller edits before merge.
2. Inspect ui/main.cpp and the current Home action dispatch. Add an application-level Dead Air owner (model, snapshot adapter and lifecycle). Under ScreenController: call begin_transition(screens::Id::dead_air), then dead_air::attach_events(&owner.model), dead_air::enter(owner.snapshot), then finish_transition(). Return via the existing Home navigation path.
3. On the existing UI loop, and only when screens::owns(screens::Id::dead_air), call dead_air::update(snapshot) when the snapshot revision changes. Route dead_air::handle_touch results to the real app action dispatcher.
4. Construct real Snapshot values from existing RTL-SDR and audio APIs. Only set spectrum_valid/audio_envelope_valid when actual samples are supplied. Set monotonic time and receiver configuration revisions on RF observations, and observe_rf from the UI task. All retune and settle gaps must invalidate detection.
5. Allocate EventQueue with PSRAM-backed application ownership; wire EventModel::set_listener(enqueue_event, &queue) on UI task. Create a dedicated low-priority storage worker that opens SdJournalSink, starts EventJournal with sink.session_id(), drains queue in bounded batches, and closes via journal.finish(queue, sink) followed by sink.close(). Display dropped(), missing(), failed() states. This writes versioned JSONL events with sequence, timestamps, RF source, units, thresholds, baselines and gaps; raw media links/clock-quality fields require later schema changes. Never read EventModel's ring directly from storage task; never write SD on IQ/DSP/UI callbacks.
6. When ENV III hardware arrives: verify I2C wiring, external power/voltage and device presence, sample all three channels, propagate present/valid/age to the Snapshot. Keep normal RF operations and Event Log fully functional when module is absent.
7. Add proper clip capture and replay with original audio and optional IQ, pre/post-trigger context, event-linked notes, and processing provenance.

## Host test instructions

From repository root on Linux/macOS with g++:

    g++ -std=c++17 -O2 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui \
      tests/dead_air_model_tests.cpp apps/orcsdr-tab5/ui/dead_air_model.cpp \
      -o /tmp/dead-air-model-tests
    /tmp/dead-air-model-tests

On Windows in a PowerShell session with g++ on PATH:

    g++ -std=c++17 -O2 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui `
      tests/dead_air_model_tests.cpp apps/orcsdr-tab5/ui/dead_air_model.cpp `
      -o ./dead-air-model-tests.exe
    ./dead-air-model-tests.exe

Native Tab5 firmware must use the pinned native build script, not PlatformIO:

    Set-Location apps/orcsdr-tab5
    ./tools/build-tab5-idf.ps1

Do not flash any device until the user explicitly authorizes flashing.

## On-device acceptance checklist

- Overview/Spectrum/Audio/Environment/Events/Review all readable at 1280x720 on the physical 5-inch display, all touch targets usable.
- App navigates in and out without drawing after screen ownership passes to another dashboard. Header status region stays clear and global Home/volume/settings work.
- Unplug SDR: no fake spectrum or 0 dBm. Reconnect: actual frequency and relative dBFS metrics only; no phantom data during tuner stabilization.
- ENV III missing: temperature/RH/pressure -- and normal core operation. Present: real current readings; unplug: old values suppressed; stale and warming-up distinguished.
- Test sustained valid RF increases, manual event marking, sensitivity presets, retuning, gain revision changes, cooldown and bounded event overflow.
- After SD worker integration, confirm event files contain durable complete lines after interrupted writes and USB audio is smooth. Partially written final JSONL lines require offline recovery and must not be blindly appended.
- Record exact branch/commit, toolchain output, test harness result, hardware port, and all deviations in the release notes.

## Verification status

GitHub source review and Git tree generation: performed in web sandbox.
Local native IDF build: NOT RUN (SDK/toolchain unavailable).
Host CI: C++ model, JSONL queue/journal, native M5GFX renderer stand-in, and SD FileSystem adapter tests PASS through GitHub Actions. Native ESP-IDF device build NOT RUN in this sandbox.
Physical touchscreen, RTL-SDR, optional ENV III, microphone and SD validation: NOT RUN.
No claim of hardware readiness is made.

## CI and local integration verification

Draft integration PR: https://github.com/hardcoreerik/OrcSDR/pull/203
GitHub Actions workflow .github/workflows/dead-air-host-tests.yml compiles
and executes four independent host binaries: RF event model, JSONL/queue,
six-tab M5GFX renderer through the recording stand-in, and OrcSDR SD adapter.
Also require Documentation Truth and Tab5 cadence/history checks.

The host renderer checks no fake disconnected ENV III readings, six-page touch
navigation, event marking, and ScreenController ownership. It does not
measure actual physical readability or native M5GFX page-flip behavior.
Use the repo's pinned ESP-IDF build script locally and capture the full
build log and device serial observations with branch and SHA before merging.

## Local integration status (branch claude/dead-air-dashboard-integrate-7d15aa)

Added `ui/dead_air_app.{hpp,cpp}`, the glue module (the Dead Air equivalent of `acars_app`).
`main.cpp` only provides hooks and forwards open/close/tick/draw/touch/feed_spectrum.

Connected to real data:
- Receiver: ready/streaming state, tuned frequency, demodulator name (BROWSE band = NFM), IQ sample rate.
- RF level: `rtl_signal_dbfs` (CU8 IQ power, dBFS), observed only when new IQ was processed. Retune, gain,
  sample-rate or receiver-restart changes bump the configuration revision and open an 800 ms settling window
  during which no detection or baseline sampling happens.
- Spectrum: the receiver's FFT (`draw_spectrum`) is max-pooled to 96 bins and offset to the same full-scale
  reference as the IQ meter. It is computed only while the Spectrum tab is visible. The offset assumes a
  Hann window; treat absolute values as approximate, relative shape as real.
- Tuning: Spectrum tab -/+ STEP buttons use the existing band-aware `rtl_step_frequency`.
- Event journal: `EventModel` -> `EventQueue` (PSRAM) -> dedicated worker task "dead_air_sd" (priority 1,
  stack in PSRAM) -> `SdJournalSink`/`EventJournal`. Health (state, session, saved, lost, dropped) is shown
  on Overview and Review. The worker closes and syncs the file when the screen is left.

Since this section was written (see `CHANGELOG.md` for the details): the Spectrum waterfall, the Audio tab's
level meter and detector, receiver and microphone clips with hash check and replay, and the sweep controls are
connected. The Overview shows a one-minute RF strip, the orb and a session card; the Events tab is tappable (detail with
band-plan context, paging, VIEW CLIP, SCRUB, CONTROL RUN, CHIME); sweeps fill a band map; the Spectrum tab has a peak
readout and tap-to-tune. Per-event traces, the control run and the session counters live in the app glue
(`dead_air_app.cpp`) and are handed to the dashboard through `Snapshot`.

ENV III (SHT30 + QMP6988 on Port A) is read by `dead_air_env` (pure logic, host-tested) through `dead_air_env_m5`
(M5Unified `Ex_I2C`, which also switches the Port A 5 V rail on). Temperature, humidity and pressure appear on the
Environment tab and the Overview card; `RTL_DEAD_AIR ENV` reports it. Not verified on a Tab5 yet.

Environment events (`dead_air_envevents`, host-tested, fed once per sensor sample from `tick()`) log a temperature, humidity or
pressure departure from the channel's own baseline. They do not yet trigger an RF measurement, scan or clip, and are not counted
on the session card.

Still NOT connected: device-motion reading (`motion_g` is never
filled), continuous audio/IQ recording, session export. `ActionKind::start_recording`, `stop_recording`,
`save_clip` and `export_session` exist but have no buttons.

Serial: `RTL_UI_OPEN DEAD_AIR`, `RTL_DEAD_AIR STATUS`.
