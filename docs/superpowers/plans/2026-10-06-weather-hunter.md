# Weather Hunter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add truthful, on-demand Weather Hunter discovery across NOAA Weather Radio, supported personal weather sensors, and 400.15–405.99 MHz radiosondes, including explicit SAME watch/decoding and Balloon Hunter tracking, while keeping automatic jobs non-preemptive and explicit user Hunter actions as normal foreground receiver takeovers.

**Architecture:** Build a pure RF job scheduler above the Foundation's explicit-owner and non-preemptive claim APIs. Pressing START/Watch/Balloon Hunter is an explicit foreground action and may intentionally replace the previous receiver job through the reviewed Weather start seam; scheduled/opportunistic/automatic jobs must `try_acquire` only an idle receiver and pass that claimed token intact into the start path. Protocol decoders are independently authored, fixture-driven modules that normalize into the shared Weather observation/report model; Balloon Hunter is a dedicated foreground tracking submode after a supported sonde lock.

**Tech Stack:** C++17, existing RTL-SDR/WX/Browse receive paths, `weather_model`, `weather_runtime`, `weather_report_store`, shared scan/spectrum measurements, M5GFX Weather UI, host fixture tests, and physical RF captures.

**Spec:** `docs/superpowers/specs/2026-10-06-weather-dashboard-design.md`

## Global Constraints

- This plan starts only after the Weather Dashboard Foundation is reviewed and accepted.
- No scheduled/opportunistic/automatic job may preempt a foreground receiver owner. `IDLE ONLY` is the default Weather background policy; `OFF` guarantees no non-user Weather retunes. Explicit user START/Watch/Balloon actions are foreground requests and may intentionally take over the receiver.
- Weather Hunter is sequential. It never displays multiple frequency ranges as simultaneously monitored.
- A short NOAA sample may say `NO ALERT DECODED DURING SAMPLE`; only explicit continuous NOAA RF Watch may claim to monitor SAME alerts.
- Do not infer owner identity/address from a consumer weather-sensor device ID.
- Decoder source must be independently authored from public protocol material and/or owned/reproducible captures; every decoder gets provenance notes. Do not copy third-party decoder source.
- Initial sensor support is deliberately bounded to protocols for which the project has clean fixtures and a documented implementation basis; unsupported packets remain `UNKNOWN`, not guessed.
- Radiosonde discovery range is 400.15–405.99 MHz. A frequency/energy hit is a candidate until a supported decoder validates frames.
- Balloon Hunter owns the tuner only while explicitly active and releases it on Stop/Home/lost-lock timeout.
- Every RF result stored or displayed carries frequency, timestamp/age, receiver-relative signal evidence where available, source kind, and decoder confidence/state.

## Review Focus

- **Foreground receiver becomes active during an idle Weather poll:** current Weather job aborts/releases without retuning back over the user. Covered in Task 1 scheduler race tests.
- **Corrupt/partial SAME header:** no alert is emitted until validation passes; raw normalized evidence is retained for diagnostics. Covered in Task 3 fixture tests.
- **Unsupported/ambiguous ISM packet:** it stays unknown and never becomes a temperature/humidity observation. Covered in Task 4 decoder-registry tests.
- **Radiosonde carrier without valid frames:** UI says candidate/no lock, not a balloon identity/position. Covered in Task 5 tests.
- **Leaving Weather Hunter/Balloon Hunter during an active dwell:** receiver token/job is cancelled exactly once and cached prior results remain readable. Covered in Tasks 1, 2, and 5.

---

### Task 1: Implement the non-preemptive Weather RF job scheduler

**Files:**
- Create: `apps/orcsdr-tab5/ui/weather_rf_scheduler.hpp`
- Create: `apps/orcsdr-tab5/ui/weather_rf_scheduler.cpp`
- Create: `tests/weather_rf_scheduler_tests.cpp`
- Modify: `apps/orcsdr-tab5/main/CMakeLists.txt`

**Interfaces:**
- Consumes: Foundation explicit Weather-owner start seam, `radio::Session::try_acquire/release`, Weather model time/source state, and injected claimed-start/tune/sample/cancel hooks.
- Produces: fixed-capacity `Job`, `JobKind`, `Priority`, `Policy`, scheduler `submit/cancel/service/snapshot`, and explicit finish reasons used by Weather Hunter and future Satellite jobs.

- [ ] **Step 1: Write deterministic scheduler tests with a fake receiver**

  Cover priority ordering; an explicit foreground Hunter start being allowed to take over through the foreground Weather start seam; automatic jobs refusing a busy owner; a successful `try_acquire` token being passed unchanged into claimed-start; stale claimed-token rejection; foreground request cancelling an opportunistic dwell; `OFF` rejecting background jobs; `IDLE ONLY` waiting for free owner; no starvation loop after repeated busy results; cancel/release exactly once; and leaving Weather cancelling Weather-owned foreground/tracking jobs.

- [ ] **Step 2: Implement fixed-capacity job state**

  Required enums include `foreground`, `tracking_watch`, `scheduled_armed`, `opportunistic`, and `discovery` priority classes. Do not allocate from the RF service path. Scheduler state must expose current job, requested/current frequency, dwell deadline, completion reason, and whether the receiver token is held.

- [ ] **Step 3: Integrate with the Foundation runtime through hooks only**

  `weather_runtime` owns high-level requests; scheduler owns dwell/tune progression. Explicit user jobs call the foreground Weather start hook. Automatic jobs call `try_acquire`, then pass the returned token directly to the Foundation claimed-start hook; they must never call a generic start path that reacquires ownership. `main.cpp` exposes only receiver hooks and does not contain job state machines.

- [ ] **Step 4: Run race/policy tests and existing radio-session regressions**

- [ ] **Step 5: Commit Task 1**

  ```bash
  git add apps/orcsdr-tab5/ui/weather_rf_scheduler.* apps/orcsdr-tab5/ui/weather_runtime.* tests/weather_rf_scheduler_tests.cpp apps/orcsdr-tab5/main/CMakeLists.txt
  git commit -m "feat(weather): add non-preemptive RF scheduler"
  ```

### Task 2: Turn Weather Hunter into a real sequential discovery workflow

**Files:**
- Modify: `apps/orcsdr-tab5/ui/weather_dashboard.hpp`
- Modify: `apps/orcsdr-tab5/ui/weather_dashboard.cpp`
- Modify: `apps/orcsdr-tab5/ui/weather_runtime.hpp`
- Modify: `apps/orcsdr-tab5/ui/weather_runtime.cpp`
- Create: `tests/weather_hunter_tests.cpp`

**Interfaces:**
- Consumes: Task 1 scheduler and Foundation NOAA scan primitives.
- Produces: user-selectable `HunterPlan`, progress/results snapshot, `START/CANCEL`, and bounded result list that records each sequential band/job separately.

- [ ] **Step 1: Write Hunter-plan tests**

  Assert default U.S. plan order is NOAA channels, supported sensor-band jobs, then 400.15–405.99 MHz radiosonde discovery; disabled job classes are skipped; progress counts only completed/active jobs; cancellation preserves completed results; no satellite dwell appears in the generic Hunter plan; and pressing START emits one explicit foreground Weather takeover request before the first dwell rather than attempting an idle-only claim.

- [ ] **Step 2: Implement the Hunter setup popup and progress screen**

  The setup surface shows enabled job classes and states explicitly `SEQUENTIAL RF JOBS`. During execution show current band/frequency, elapsed/dwell, decoded count, candidate count, and completed jobs. Never render inactive bands as live.

- [ ] **Step 3: Cache Hunter results back into NOW/RF WEATHER with age**

  A completed result updates the shared Weather snapshot/report session. A miss updates only the scan timestamp/state; it does not erase a prior observation as if disproved.

- [ ] **Step 4: Run UI/self-check and scheduler tests**

- [ ] **Step 5: Commit Task 2**

  ```bash
  git add apps/orcsdr-tab5/ui/weather_dashboard.* apps/orcsdr-tab5/ui/weather_runtime.* tests/weather_hunter_tests.cpp
  git commit -m "feat(weather): add sequential Weather Hunter"
  ```

### Task 3: Add NOAA SAME receive/watch decoding with explicit coverage semantics

**Files:**
- Create: `apps/orcsdr-tab5/ui/weather_same_decoder.hpp`
- Create: `apps/orcsdr-tab5/ui/weather_same_decoder.cpp`
- Create: `tests/weather_same_decoder_tests.cpp`
- Create: `tests/fixtures/weather_same/README.md`
- Add only captured/redistributable fixture files under: `tests/fixtures/weather_same/`
- Modify: `apps/orcsdr-tab5/ui/weather_runtime.hpp` and `apps/orcsdr-tab5/ui/weather_runtime.cpp`
- Modify: `apps/orcsdr-tab5/ui/weather_dashboard.hpp` and `apps/orcsdr-tab5/ui/weather_dashboard.cpp`
- Modify: `apps/orcsdr-tab5/main/CMakeLists.txt`

**Interfaces:**
- Consumes: demodulated WX audio/sample stream through a bounded decoder hook; official SAME/EAS field definitions/event-code tables independently transcribed as data where licensing permits.
- Produces: `SameHeader`, validation state, raw normalized header, event/location/originator fields, and explicit `RF WATCH` alert records.

- [ ] **Step 1: Record fixture provenance before decoder code**

  `tests/fixtures/weather_same/README.md` must list source/capture method, date, sample format, redistribution basis, expected header fields, and SHA-256. If no usable fixture exists, stop this task at the fixture gate rather than inventing decoder acceptance.

- [ ] **Step 2: Write parser/frame tests for valid, repeated, corrupted, truncated, and unknown-event headers**

  Preserve original event/location codes even when unknown locally. Corruption/truncation yields no accepted alert.

- [ ] **Step 3: Implement independently authored signal/header decoding**

  Keep the DSP/frame decoder bounded and callback-safe. Do not perform SD writes, map work, or display calls from the audio/RF callback. Queue validated headers to Weather runtime.

- [ ] **Step 4: Add explicit `NOAA RF WATCH` mode**

  Starting Watch acquires Weather foreground/tracking ownership and remains on the selected NOAA channel until Stop/Home. Opportunistic NOAA samples are still labeled samples and never inherit Watch semantics.

- [ ] **Step 5: Save SAME events into Weather reports/history with source `direct_rf`**

  Include frequency, raw normalized header, parsed fields, validation result, UTC/relative timestamp, and receiver-relative signal metric where available.

- [ ] **Step 6: Run fixture, runtime, callback-budget, and native tests**

- [ ] **Step 7: Commit Task 3**

  ```bash
  git add apps/orcsdr-tab5/ui/weather_same_decoder.* apps/orcsdr-tab5/ui/weather_runtime.* apps/orcsdr-tab5/ui/weather_dashboard.* tests/weather_same_decoder_tests.cpp tests/fixtures/weather_same apps/orcsdr-tab5/main/CMakeLists.txt
  git commit -m "feat(weather): decode NOAA SAME RF alerts"
  ```

### Task 4: Add a fixture-gated personal weather-sensor decoder registry

**Files:**
- Create: `apps/orcsdr-tab5/ui/weather_sensor_decoder.hpp`
- Create: `apps/orcsdr-tab5/ui/weather_sensor_decoder.cpp`
- Create: `tests/weather_sensor_decoder_tests.cpp`
- Create: `tests/fixtures/weather_sensors/README.md`
- Add protocol fixtures only under: `tests/fixtures/weather_sensors/<protocol>/`
- Modify: `apps/orcsdr-tab5/ui/weather_runtime.hpp` and `apps/orcsdr-tab5/ui/weather_runtime.cpp`
- Modify: `apps/orcsdr-tab5/main/CMakeLists.txt`

**Interfaces:**
- Consumes: bounded demod/pulse/bit observations from an injected RF capture frontend and Task 1 regional frequency profile.
- Produces: protocol-specific validated packets normalized into `weather::Observation`, opaque device ID, battery/status fields, RF frequency, and decoder name/version.

- [ ] **Step 1: Establish the clean-room fixture gate and select at most two initial protocols**

  Select protocols only when the project has reproducible fixtures and a public, legally usable description sufficient for independent implementation. Record protocol/source links, capture hardware, antenna, sample format, hashes, and expected fields. `rtl_433` may be cited as interoperability/coverage reference but its decoder source is not copied.

- [ ] **Step 2: Write registry tests before protocol implementations**

  Unknown packets return `unsupported`; a decoder cannot emit a measurement without checksum/frame validation where the protocol provides one; duplicate device IDs across bands remain keyed by protocol+ID+frequency family; malformed values are rejected rather than clamped.

- [ ] **Step 3: Implement the registry and first fixture-backed decoder**

  Keep protocol modules or tables isolated so future decoders do not create a monolithic switch. Normalize units in the pure Weather model and retain raw protocol values where useful for reports.

- [ ] **Step 4: Add the second decoder only after the first passes fixture and hardware replay tests**

  If the second protocol lacks fixture/provenance quality, ship one protocol and document the limitation.

- [ ] **Step 5: Integrate decoded devices into RF WEATHER and reports**

  Show protocol/model label, opaque ID, band, last-heard age, measurements, and battery/status only if actually decoded. Do not display owner/address assumptions.

- [ ] **Step 6: Commit Task 4**

  ```bash
  git add apps/orcsdr-tab5/ui/weather_sensor_decoder.* apps/orcsdr-tab5/ui/weather_runtime.* tests/weather_sensor_decoder_tests.cpp tests/fixtures/weather_sensors apps/orcsdr-tab5/main/CMakeLists.txt
  git commit -m "feat(weather): decode fixture-backed weather sensors"
  ```

### Task 5: Add radiosonde discovery and Balloon Hunter tracking

**Files:**
- Create: `apps/orcsdr-tab5/ui/weather_radiosonde.hpp`
- Create: `apps/orcsdr-tab5/ui/weather_radiosonde.cpp`
- Create: `tests/weather_radiosonde_tests.cpp`
- Create: `tests/fixtures/radiosonde/README.md`
- Add supported sonde fixtures under: `tests/fixtures/radiosonde/<type>/`
- Modify: `apps/orcsdr-tab5/ui/weather_dashboard.hpp` and `apps/orcsdr-tab5/ui/weather_dashboard.cpp`
- Modify: `apps/orcsdr-tab5/ui/weather_runtime.hpp` and `apps/orcsdr-tab5/ui/weather_runtime.cpp`
- Modify: `apps/orcsdr-tab5/main/CMakeLists.txt`

**Interfaces:**
- Consumes: scheduler sweeps of 400.15–405.99 MHz, receiver-relative energy/candidate observations, and fixture-backed sonde frames.
- Produces: `SondeCandidate`, validated `SondeTelemetry`, track points, lock state, and `Balloon Hunter` foreground mode.

- [ ] **Step 1: Add discovery tests that separate RF candidate from decoder lock**

  A spectral/energy peak becomes only `candidate`. A supported decoder must validate frames before type/ID/position/telemetry appear. Multiple nearby peaks remain independently ranked with age/frequency.

- [ ] **Step 2: Establish an initial decoder fixture gate**

  Prefer an RS41 implementation only if clean public format references and reproducible owned/redistributable captures are available. Otherwise ship discovery/candidate UI first and keep decoder state `unsupported`.

- [ ] **Step 3: Implement bounded discovery and decoder state**

  Do not scan continuously. Weather Hunter sweeps/dwells through the range, ranks candidates, and optionally attempts supported decoders. No candidate is labeled a specific balloon model before validation.

- [ ] **Step 4: Implement Balloon Hunter as explicit foreground tracking**

  On a decoder lock, `OPEN BALLOON HUNTER` acquires tracking ownership, follows the transmitter, renders frequency/signal/altitude/ascent/temperature/humidity/GPS fields only when valid, and projects validated positions through `offline_map`. Stop/Home/lost-lock timeout releases the receiver.

- [ ] **Step 5: Extend report bundles with `telemetry.csv` and `track.csv`**

  Include decoder/provenance metadata and retain gaps/invalid frames rather than interpolating undocumented values.

- [ ] **Step 6: Run fixture tests, map projection tests, native build, and real-sonde hardware acceptance**

  Hardware acceptance records antenna dimensions/type, receiver, scan range, detected frequency, decoder lock evidence, and comparison against an independent public track only as a separate verification source.

- [ ] **Step 7: Commit Task 5**

  ```bash
  git add apps/orcsdr-tab5/ui/weather_radiosonde.* apps/orcsdr-tab5/ui/weather_dashboard.* apps/orcsdr-tab5/ui/weather_runtime.* tests/weather_radiosonde_tests.cpp tests/fixtures/radiosonde apps/orcsdr-tab5/main/CMakeLists.txt
  git commit -m "feat(weather): add radiosonde Balloon Hunter"
  ```

### Task 6: Enable truthful idle polling and close out Hunter documentation

**Files:**
- Modify: `apps/orcsdr-tab5/ui/weather_runtime.hpp` and `apps/orcsdr-tab5/ui/weather_runtime.cpp`
- Modify: `apps/orcsdr-tab5/ui/weather_rf_scheduler.hpp` and `apps/orcsdr-tab5/ui/weather_rf_scheduler.cpp`
- Modify: `apps/orcsdr-tab5/ui/settings_app.hpp` and `apps/orcsdr-tab5/ui/settings_app.cpp`
- Modify: `orcdial/src/controller.hpp`
- Modify: `docs/ORCDIAL_CONTROL_MATRIX.md`
- Modify: `docs/user-guide/dashboards/weather.md`
- Modify: `PROJECT_STATUS.md`
- Create: `docs/testing/weather-hunter-acceptance.md`
- Modify: `apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1`

**Interfaces:**
- Consumes: completed Hunter scheduler/decoders and existing Weather `Background Policy` UI.
- Produces: functional `OFF`/`IDLE ONLY` policy behavior, bounded cached poll results, authoritative OrcDial Hunter controls that actually exist, docs/evidence.

- [ ] **Step 1: Add idle-poll integration tests**

  Prove automatic Weather polling can borrow only an idle receiver, hands the exact claimed token into the Foundation start seam, aborts/releases if that token becomes stale before start, yields when a foreground owner appears, never calls Wi-Fi, never claims continuous SAME coverage, and caches result ages after release.

- [ ] **Step 2: Enable only cheap, bounded default idle jobs**

  Start conservatively: known NOAA channel/sample and previously discovered supported sensor frequencies. Do not continuously sweep 400–406 MHz by default. Radiosonde discovery remains user-initiated or future time-window scheduled work.

- [ ] **Step 3: Wire only real Hunter/Balloon controls to OrcDial**

- [ ] **Step 4: Run Wi-Fi-off, tuner-busy, Hunter-cancel, SAME Watch, sensor, and sonde acceptance matrix**

- [ ] **Step 5: Record exact verified vs experimental capabilities in docs/status**

- [ ] **Step 6: Commit Task 6**

  ```bash
  git add apps/orcsdr-tab5/ui/weather_runtime.* apps/orcsdr-tab5/ui/weather_rf_scheduler.* apps/orcsdr-tab5/ui/settings_app.* orcdial/src/controller.hpp docs/ORCDIAL_CONTROL_MATRIX.md docs/user-guide/dashboards/weather.md PROJECT_STATUS.md docs/testing/weather-hunter-acceptance.md apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1
  git commit -m "docs(weather): close out Weather Hunter acceptance"
  ```
