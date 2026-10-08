# Satellite Weather Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add offline pass prediction and explicit event-driven 137 MHz weather-satellite acquisition to the Weather dashboard, then gate experimental LRPT image decoding behind measured Tab5 performance and real-pass acceptance.

**Architecture:** Keep orbital/pass math and target metadata independent of RF reception. Cached orbital elements and the existing canonical receiver location produce local pass opportunities without Internet; the Weather scheduler may arm a user-approved scheduled job but may never preempt another foreground receiver. Acquisition and LRPT decoding are separate stages so pass prediction/receiver preparation can ship even if full image decoding exceeds Tab5 resource limits.

**Tech Stack:** C++17, Weather Foundation/Hunter scheduler, canonical receiver location, `offline_map`, `time_service`, SD report store, existing RTL-SDR Browse/raw IQ path, M5GFX, host orbital fixtures, PSRAM/performance instrumentation, and real 137 MHz satellite passes.

**Spec:** `docs/superpowers/specs/2026-10-06-weather-dashboard-design.md`

## Global Constraints

- This plan begins only after Weather Foundation is accepted; scheduler integration assumes the Hunter scheduler API or an equivalent reviewed non-preemptive scheduled-job seam that passes a successful `try_acquire` token intact into the Foundation claimed-start path.
- Satellite acquisition is event-driven. No generic periodic 137 MHz background polling.
- Orbital data is cached local data; online TLE/orbital refresh is optional and disabled by default under the same Weather network policy.
- The target catalog must reflect current operational targets and retain source/retrieval date. Do not hard-code retired NOAA POES APT satellites as active targets.
- A predicted pass is not a received signal; a carrier lock is not decoded imagery; a decoded image is not automatically meteorologically interpreted. UI/report states distinguish each stage.
- Scheduled Weather jobs never preempt a foreground receiver. If the receiver is busy, the pass is marked missed/busy unless the user explicitly switches modes.
- LRPT decoding is experimental until CPU/PSRAM/buffer/USB measurements prove it sustainable on Tab5 during a real pass.
- No network imagery is substituted for directly received satellite imagery under an RF source label.
- Use cached/local orbital elements and maps offline after acquisition; network refresh must preserve the last valid local data on failure.

## Review Focus

- **Stale or malformed orbital elements:** pass prediction refuses invalid data and visibly reports source age. Covered in Task 1 tests.
- **No configured receiver location:** target list remains visible but local pass times/ground track are unavailable, not guessed. Covered in Task 2 tests.
- **Receiver busy at AOS:** scheduler does not steal it and records the pass as not acquired/busy. Covered in Task 3 tests.
- **Doppler/retune command failure during acquisition:** last known valid receiver state is preserved and acquisition status becomes degraded/lost, not silently successful. Covered in Task 3 tests.
- **Decoder cannot keep up with incoming IQ:** bounded queues drop/report frames or disable decode; they do not exhaust memory/watchdog the Tab5. Covered in Task 4 performance tests.

---

### Task 1: Add a local weather-satellite target/orbital model

**Files:**
- Create: `apps/orcsdr-tab5/ui/weather_satellite.hpp`
- Create: `apps/orcsdr-tab5/ui/weather_satellite.cpp`
- Create: `tests/weather_satellite_tests.cpp`
- Create: `tests/fixtures/weather_satellite/README.md`
- Add fixed test orbital-element fixtures under: `tests/fixtures/weather_satellite/`
- Modify: `apps/orcsdr-tab5/main/CMakeLists.txt`

**Interfaces:**
- Consumes: pure target records, receiver lat/lon, UTC, cached orbital elements.
- Produces: `SatelliteTarget`, `OrbitalElements`, `Pass`, target validation/source age, next-pass calculation, azimuth/elevation/range samples, and Doppler estimate interfaces. No display or tuner calls.

- [ ] **Step 1: Create fixed dated orbital fixtures with provenance and expected external-reference pass times**

  Include source URL, retrieval date, target identity, fixture hash, and an independently computed reference for representative locations. The host tests must remain deterministic and must not fetch the Internet.

- [ ] **Step 2: Write failing orbital/pass tests**

  Cover parse validation/checksum where applicable, stale-source age, no-location behavior, next AOS/TCA/LOS ordering, max elevation, azimuth/elevation ranges, and deterministic pass results within the documented tolerance of the independent reference.

- [ ] **Step 3: Implement pure orbital/pass math or integrate an acceptable already-reviewed local library**

  Any dependency requires explicit license/footprint review before addition. Prefer a small independently implemented SGP4-compatible module only if the project can validate it rigorously; do not hand-wave orbital accuracy.

- [ ] **Step 4: Add target catalog entries as data, not UI constants**

  Initial catalog may include current compatible Meteor-series LRPT targets only when current source data confirms them. Each target stores display name, downlink candidates, modulation/decoder profile, source date, and enabled/operational state.

- [ ] **Step 5: Run all orbital fixtures and commit**

  ```bash
  git add apps/orcsdr-tab5/ui/weather_satellite.* tests/weather_satellite_tests.cpp tests/fixtures/weather_satellite apps/orcsdr-tab5/main/CMakeLists.txt
  git commit -m "feat(weather): add offline satellite pass model"
  ```

### Task 2: Add Weather satellite opportunities and offline map ground tracks

**Files:**
- Modify: `apps/orcsdr-tab5/ui/weather_dashboard.hpp` and `apps/orcsdr-tab5/ui/weather_dashboard.cpp`
- Modify: `apps/orcsdr-tab5/ui/weather_runtime.hpp` and `apps/orcsdr-tab5/ui/weather_runtime.cpp`
- Modify: `apps/orcsdr-tab5/ui/weather_satellite.hpp` and `apps/orcsdr-tab5/ui/weather_satellite.cpp`
- Create: `tests/weather_satellite_ui_tests.cpp`

**Interfaces:**
- Consumes: Task 1 pass model, canonical receiver location/time, `offline_map` projection.
- Produces: RF WEATHER “Next Satellite Opportunity”, MAP satellite layer/ground-track projection, pass detail popup, and `PREPARE`/`ARM PASS` actions.

- [ ] **Step 1: Write UI-state tests for valid pass, stale orbit, no location, no target, and map-pack-missing states**

- [ ] **Step 2: Render next-pass opportunity without touching the receiver**

  Show target, AOS countdown/time, max elevation, duration, downlink profile, orbital-data age, and recommended antenna note as metadata—not a reception guarantee.

- [ ] **Step 3: Project sampled ground track through existing `offline_map`**

  If no map pack exists, provide list/compass pass details rather than a blank map.

- [ ] **Step 4: Add explicit `PREPARE` and `ARM PASS` semantics**

  `PREPARE` opens receiver/antenna guidance and does not tune. `ARM PASS` schedules a future Weather job; it does not start acquisition early.

- [ ] **Step 5: Commit Task 2**

  ```bash
  git add apps/orcsdr-tab5/ui/weather_dashboard.* apps/orcsdr-tab5/ui/weather_runtime.* apps/orcsdr-tab5/ui/weather_satellite.* tests/weather_satellite_ui_tests.cpp
  git commit -m "feat(weather): show offline satellite opportunities"
  ```

### Task 3: Add scheduled 137 MHz Satellite Hunter acquisition

**Files:**
- Create: `apps/orcsdr-tab5/ui/weather_satellite_rx.hpp`
- Create: `apps/orcsdr-tab5/ui/weather_satellite_rx.cpp`
- Create: `tests/weather_satellite_rx_tests.cpp`
- Modify: `apps/orcsdr-tab5/ui/weather_rf_scheduler.hpp` and `apps/orcsdr-tab5/ui/weather_rf_scheduler.cpp`
- Modify: `apps/orcsdr-tab5/ui/weather_runtime.hpp` and `apps/orcsdr-tab5/ui/weather_runtime.cpp`
- Modify: `apps/orcsdr-tab5/ui/weather_dashboard.hpp` and `apps/orcsdr-tab5/ui/weather_dashboard.cpp`
- Modify: `apps/orcsdr-tab5/main/CMakeLists.txt`

**Interfaces:**
- Consumes: an explicitly armed valid pass, scheduler, target frequencies, existing RTL-SDR acquisition/retune hooks, time/location/pass state.
- Produces: Satellite Hunter state machine (`waiting`, `acquiring`, `locked`, `lost`, `complete`, `busy`, `cancelled`), bounded Doppler retunes, RF metrics, and acquisition event log.

- [ ] **Step 1: Write state-machine tests with a fake clock and tuner**

  Cover pre-AOS waiting, AOS acquisition window, busy receiver at AOS, user cancellation, target-frequency selection, a successful idle claim being handed unchanged into receiver start, stale claimed-token rejection, bounded Doppler retune cadence, failed retune, LOS stop/release, and no receiver ownership after completion.

- [ ] **Step 2: Implement scheduled-job integration without preemption**

  An armed pass gets `scheduled_armed` priority but `try_acquire` still fails when another foreground owner exists. On success, pass that exact token to the Foundation claimed-start hook; do not call the generic foreground start path or `Session::acquire()` again. Never automatically stop FM/Airband/etc. to catch a pass.

- [ ] **Step 3: Implement Satellite Hunter UI**

  Show target, pass phase/countdown, azimuth/elevation, tuned/downlink frequency, Doppler correction, signal metric, frame/decoder state placeholder, and a prominent Stop/Home path. Clearly distinguish `PREDICTED`, `SIGNAL`, and `DECODE` states.

- [ ] **Step 4: Save acquisition events into the Weather report**

  Include predicted pass, actual tune/retune times, receiver state, signal samples, failures, and completion reason even when no decode occurs.

- [ ] **Step 5: Run native build and a no-decode real-pass acquisition test before adding LRPT**

  Prove stable USB/IQ capture and non-preemptive lifecycle on real hardware. Record antenna geometry, target, orbital source, exact build, and observed RF evidence.

- [ ] **Step 6: Commit Task 3**

  ```bash
  git add apps/orcsdr-tab5/ui/weather_satellite_rx.* apps/orcsdr-tab5/ui/weather_rf_scheduler.* apps/orcsdr-tab5/ui/weather_runtime.* apps/orcsdr-tab5/ui/weather_dashboard.* tests/weather_satellite_rx_tests.cpp apps/orcsdr-tab5/main/CMakeLists.txt
  git commit -m "feat(weather): add scheduled Satellite Hunter"
  ```

### Task 4: Prototype and performance-gate LRPT decoding

**Files:**
- Create: `apps/orcsdr-tab5/ui/weather_lrpt_decoder.hpp`
- Create: `apps/orcsdr-tab5/ui/weather_lrpt_decoder.cpp`
- Create: `tests/weather_lrpt_decoder_tests.cpp`
- Create: `tests/fixtures/weather_lrpt/README.md`
- Add redistributable captured fixtures under: `tests/fixtures/weather_lrpt/`
- Create: `docs/testing/weather-lrpt-performance.md`
- Modify: `apps/orcsdr-tab5/main/CMakeLists.txt`

**Interfaces:**
- Consumes: bounded IQ/baseband blocks from Satellite Hunter and fixture files.
- Produces: decoder lock/state, frame/error counters, image scan data/segments, bounded resource metrics, and a clear “unsupported/performance gate failed” fallback.

- [ ] **Step 1: Establish fixture/provenance and exact algorithm requirements before implementation**

  Do not copy third-party decoder source. Record modulation/coding references, fixture capture method, expected externally decoded result/hash, and redistribution basis.

- [ ] **Step 2: Build host decode tests before Tab5 integration**

  Require deterministic lock/frame/image assertions from saved fixtures and corruption/error tests. A signal/carrier fixture alone is insufficient to claim image decoding.

- [ ] **Step 3: Implement decoder as staged bounded pipeline**

  Separate synchronization/demodulation/FEC/frame/image assembly so each stage exposes counters and has bounded memory. No display or SD access from the RF callback.

- [ ] **Step 4: Add instrumentation and a hard runtime resource gate**

  Measure per-stage DSP time, queue depth/drops, internal heap/PSRAM, USB overruns/drops, and watchdog margin. If real-time processing cannot maintain headroom, keep LRPT decode disabled on production Tab5 while retaining Satellite Hunter RF acquisition/reporting.

- [ ] **Step 5: Run saved-IQ A/B and real-pass tests**

  First prove saved-fixture decode. Then attempt a real pass using the exact candidate image and suitable 137 MHz antenna. Record both successful and failed runs; do not convert a PC-offline decode into Tab5 real-time acceptance.

- [ ] **Step 6: Commit Task 4 only at the measured capability level**

  ```bash
  git add apps/orcsdr-tab5/ui/weather_lrpt_decoder.* tests/weather_lrpt_decoder_tests.cpp tests/fixtures/weather_lrpt docs/testing/weather-lrpt-performance.md apps/orcsdr-tab5/main/CMakeLists.txt
  git commit -m "feat(weather): gate experimental LRPT decoding"
  ```

### Task 5: Add decoded imagery/report integration and optional orbital refresh

**Files:**
- Modify: `apps/orcsdr-tab5/ui/weather_dashboard.hpp` and `apps/orcsdr-tab5/ui/weather_dashboard.cpp`
- Modify: `apps/orcsdr-tab5/ui/weather_report_store.hpp` and `apps/orcsdr-tab5/ui/weather_report_store.cpp`
- Modify: `apps/orcsdr-tab5/ui/weather_satellite.hpp` and `apps/orcsdr-tab5/ui/weather_satellite.cpp`
- Modify: `apps/orcsdr-tab5/ui/weather_runtime.hpp` and `apps/orcsdr-tab5/ui/weather_runtime.cpp`
- Modify: `apps/orcsdr-tab5/ui/settings_app.hpp` and `apps/orcsdr-tab5/ui/settings_app.cpp`
- Create: `apps/orcsdr-tab5/ui/weather_orbit_provider.hpp`
- Create: `apps/orcsdr-tab5/ui/weather_orbit_provider.cpp`
- Modify: `docs/user-guide/dashboards/weather.md`
- Modify: `PROJECT_STATUS.md`
- Create: `docs/testing/weather-satellite-acceptance.md`

**Interfaces:**
- Consumes: accepted Task 3 acquisition and Task 4 decoder capability, Weather online policy.
- Produces: locally received image viewer where supported, satellite report assets, and optional manual/auto orbital-data refresh that never becomes a Weather boot dependency.

- [ ] **Step 1: Save decoded imagery only with direct-RF provenance**

  Add decoded image files/metadata to the report bundle, including target, pass, decoder, source hashes, and frame/error statistics. Network imagery remains a separate optional source class.

- [ ] **Step 2: Add an image/detail surface that fits existing Weather navigation**

  Do not add a sixth permanent tab. Open imagery/pass details from MAP, RF WEATHER, or REPORTS as a modal/full-screen child and return to the originating tab.

- [ ] **Step 3: Implement optional orbital refresh only behind Weather network policy**

  `disabled` performs no request. `manual` refreshes only on explicit action. `automatic` may refresh while existing Wi-Fi is available; failed refresh preserves last valid orbital data and surfaces stale age.

- [ ] **Step 4: Run full offline-first acceptance**

  Boot with Wi-Fi disabled, predict from cached orbital data, arm/catch a pass, save report, and view any directly decoded result without Internet. Separately test manual online refresh and then disconnect/reboot to prove cached continuity.

- [ ] **Step 5: Update product status to exact proven level and commit**

  ```bash
  git add apps/orcsdr-tab5/ui/weather_dashboard.* apps/orcsdr-tab5/ui/weather_report_store.* apps/orcsdr-tab5/ui/weather_satellite.* apps/orcsdr-tab5/ui/weather_runtime.* apps/orcsdr-tab5/ui/settings_app.* apps/orcsdr-tab5/ui/weather_orbit_provider.hpp apps/orcsdr-tab5/ui/weather_orbit_provider.cpp docs/user-guide/dashboards/weather.md PROJECT_STATUS.md docs/testing/weather-satellite-acceptance.md
  git commit -m "docs(weather): close out satellite weather acceptance"
  ```
