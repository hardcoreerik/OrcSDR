# Weather Dashboard Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the generic WX route with a dedicated five-tab, offline-first Weather dashboard that can listen/scan NOAA Weather Radio on demand, render local/offline context, and save auditable SD-backed Weather reports without taking the receiver merely because the dashboard was opened.

**Architecture:** Add a pure Weather model, dedicated M5GFX dashboard, thin Weather runtime, and SD report store. Extend shared receiver-session ownership with a non-preemptive Weather acquisition path so explicit and future background Weather jobs can borrow the single tuner truthfully; keep `main.cpp` as a narrow hook layer. Reuse existing `RtlBand::wx` NFM DSP/audio, canonical receiver location, signed `noaa_weather` catalog, `offline_map`, `time_service`, and `orcsdr_storage`.

**Tech Stack:** C++17, ESP-IDF 5.5.4, M5GFX/M5Unified, existing `radio_session`, WX/NFM receiver path, `offline_map`, `orcsdr_storage`, `time_service`, catalog services, native host checks, documentation-truth checks, and Tab5 UI regression tooling.

**Spec:** `docs/superpowers/specs/2026-10-06-weather-dashboard-design.md`

## Global Constraints

- Weather is offline-first. Network weather enrichment is disabled by default and no network fetch occurs merely because Weather opens.
- Opening Weather must not tune, start, stop, or preempt the RTL-SDR.
- One RTL-SDR represents one RF job at a time; cached observations always show source and age.
- Background Weather work never steals the receiver; this foundation only supplies the safe ownership primitive and foreground NOAA operations.
- Reuse the current WX/NFM demodulator and audio path. Do not add a second receiver/DSP/audio stack.
- Keep Weather dashboard drawing, report serialization, NOAA channel logic, and map presentation out of `main.cpp`.
- Use the existing canonical receiver location and existing `noaa_weather` catalog slot; do not create Weather-owned location state or a second catalog updater.
- Maintain Weather dashboard wire ID `5` for OrcDial and the existing dashboard registry ordering.
- Weather UI uses the established 1280x720 OrcSDR chrome: black background, panel `0x0841`, cyan `0x2e7f`, green `0x6fe8`, muted `0x8c71`, grid `0x2945`, 132 px header, footer at y=630, and five 256 px tabs.
- SD is the durable Weather report source of truth. NVS may hold small preferences but never the only copy of a report.
- Failed report writes preserve prior complete reports/history and surface an explicit error.
- No raw IQ is saved as part of a normal Weather report.
- Build and hardware/RF evidence remain separate claims. Do not mark NOAA RF behavior verified from host/UI tests alone.

## Review Focus

- **Receiver already owned by another dashboard:** Weather must open and browse cached/local data without retuning; an explicit NOAA action reports busy/unavailable instead of preempting. Covered in Task 2 ownership tests and Task 5 runtime tests.
- **No RTC / invalid wall clock:** reports and observations retain uptime-relative evidence and never fabricate UTC. Covered in Task 1 model tests and Task 4 report tests.
- **Missing SD, map pack, or receiver location:** each tab remains usable and shows an explicit state rather than blank content. Covered in Tasks 3 and 4 self-checks.
- **Partial/interrupted report save:** a `.part` failure must not replace a prior valid history/report entry. Covered in Task 4 storage tests.
- **Weather opened while another audio mode is playing:** no implicit retune or stop occurs; leaving Weather returns Home without hidden Weather ownership. Covered in Task 5 integration/serial regression.

---

### Task 1: Add the pure Weather observation and source model

**Files:**
- Create: `apps/orcsdr-tab5/ui/weather_model.hpp`
- Create: `apps/orcsdr-tab5/ui/weather_model.cpp`
- Create: `tests/weather_model_tests.cpp`
- Modify: `apps/orcsdr-tab5/main/CMakeLists.txt`

**Interfaces:**
- Consumes: no display, RTOS, network, or receiver APIs.
- Produces: `weather::SourceKind`, `Freshness`, `ObservationMeta`, bounded `Observation`, `AlertSummary`, `ReportSummary`, `RegionalProfile`, NOAA channel constants, freshness helpers, and optional consensus calculation used by all later Weather components.

- [ ] **Step 1: Write failing host tests for source honesty, missing values, freshness, and NOAA channels**

  Tests must assert: exact seven U.S. NOAA frequencies in ascending order; missing values remain unavailable rather than numeric zero; `direct_rf`, `local_sensor`, `local_derived`, `cache`, and `online` remain distinguishable; a relative-time observation remains relative when UTC is invalid; and consensus reports input count, spread, oldest age, and refuses fewer than the configured minimum inputs.

- [ ] **Step 2: Compile the focused test and verify the new API is missing**

  Run:

  ```bash
  c++ -std=c++17 -Iapps/orcsdr-tab5/ui tests/weather_model_tests.cpp apps/orcsdr-tab5/ui/weather_model.cpp -o /tmp/weather_model_tests
  ```

  Expected before implementation: compile failure for undefined Weather model types/functions.

- [ ] **Step 3: Implement the bounded pure API**

  Define the shared contract with fixed-size embedded-friendly storage. Required public signatures include:

  ```cpp
  enum class SourceKind : uint8_t { direct_rf, local_sensor, local_derived, cache, online };
  enum class Freshness : uint8_t { live, recent, stale, expired, unavailable };
  enum class ValueKind : uint8_t { temperature_c, humidity_percent, pressure_hpa,
                                   wind_speed_mps, wind_gust_mps, wind_direction_deg,
                                   rain_rate_mm_h, rain_total_mm };

  struct ObservationMeta {
    SourceKind source = SourceKind::cache;
    uint32_t observed_utc = 0;
    uint32_t observed_uptime_ms = 0;
    uint32_t age_seconds = 0;
    Freshness freshness = Freshness::unavailable;
    uint8_t input_count = 0;
  };

  struct Observation {
    ValueKind kind;
    float value = 0.0f;
    bool valid = false;
    ObservationMeta meta{};
    char source_label[32]{};
  };

  constexpr uint32_t kNoaaWeatherChannelsHz[7] = {
      162400000u, 162425000u, 162450000u, 162475000u,
      162500000u, 162525000u, 162550000u};
  ```

  Keep freshness thresholds field-specific through a helper/config table rather than one global magic age. Add North America sensor discovery bands as profile data (`315 MHz`, `433.92 MHz`, `902-928 MHz/915 MHz`) without implementing decoders in this phase.

- [ ] **Step 4: Run focused tests and static warning build**

  Expected: `weather_model_tests: PASS`, no dynamic allocation required by the pure model, and no ESP/display headers in the model.

- [ ] **Step 5: Commit Task 1**

  ```bash
  git add apps/orcsdr-tab5/ui/weather_model.* tests/weather_model_tests.cpp apps/orcsdr-tab5/main/CMakeLists.txt
  git commit -m "feat(weather): add offline observation model"
  ```

### Task 2: Make receiver ownership safe for Weather borrowing

**Files:**
- Modify: `apps/orcsdr-tab5/ui/radio_session.hpp`
- Modify: `apps/orcsdr-tab5/ui/radio_session.cpp`
- Modify: `tests/radio_scan_tests.cpp`

**Interfaces:**
- Consumes: existing `radio::Session`, `Token`, `Band::wx`, current owner/generation state.
- Produces: `Owner::weather`, non-preemptive `try_acquire(...)`, `release(Token)`, and truthful ownership semantics used by `weather_runtime` and later `weather_rf_scheduler`.

- [ ] **Step 1: Add failing ownership tests**

  Add tests that acquire FM, then prove Weather `try_acquire` fails and leaves the FM token/state unchanged. Release FM, prove Weather can acquire `Band::wx`; prove a stale token cannot release a newer owner; prove releasing Weather returns owner to `none`; and assert `owner_for_band(Band::wx) == Owner::weather`.

- [ ] **Step 2: Run `radio_scan_tests` and confirm failure before implementation**

  Expected before implementation: compile failure for `Owner::weather`, `try_acquire`, and `release`.

- [ ] **Step 3: Extend `Session` without breaking existing foreground call sites**

  Keep current `acquire(...)` behavior for existing callers. Add:

  ```cpp
  Token try_acquire(Owner owner, Band band, uint32_t frequency_hz,
                    uint32_t sample_rate_sps);
  bool release(Token token);
  ```

  `try_acquire` succeeds only when `owner_ == Owner::none`; it must never overwrite, replace, or reenter a current owner. A Weather workflow that already holds a token retains and retunes that token instead of reacquiring it. `release` uses owner+generation validation and clears owner/state only for the live token. Do not implement a Weather-specific global mutex outside `radio_session`.

- [ ] **Step 4: Run radio/session regressions**

  Run the repository's existing `radio_scan_tests` command plus `Session::self_check()`. Expected: existing owner behavior remains green and the new Weather non-preemption cases pass.

- [ ] **Step 5: Commit Task 2**

  ```bash
  git add apps/orcsdr-tab5/ui/radio_session.* tests/radio_scan_tests.cpp
  git commit -m "feat(radio): add non-preemptive receiver ownership"
  ```

### Task 3: Build the dedicated five-tab Weather M5GFX surface

**Files:**
- Create: `apps/orcsdr-tab5/ui/weather_dashboard.hpp`
- Create: `apps/orcsdr-tab5/ui/weather_dashboard.cpp`
- Create: `tests/weather_dashboard_state_tests.cpp`
- Modify: `apps/orcsdr-tab5/ui/screen_controller.hpp`
- Modify: `apps/orcsdr-tab5/ui/screen_controller.cpp`
- Modify: `apps/orcsdr-tab5/ui/dashboard_registry.cpp`
- Modify: `apps/orcsdr-tab5/main/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 Weather model snapshots, existing `dashboard_audio_control`, `focus_nav`, and `offline_map` presentation hooks.
- Produces: `weather::Tab { now, forecast, map, rf_weather, reports }`, presentation `Snapshot`, `ActionKind`, `Action`, `enter`, `leave`, `draw`, `update`, `handle_touch`, `select_tab`, `active`, and `self_check`.

- [ ] **Step 1: Write state/layout tests before drawing code**

  Cover five tabs, exact 256 px tab widths, footer at y=630, no card overlap with footer, action mapping for every primary RF/report control, no RF action on tab switching, and an unavailable-state snapshot for missing map/SD/location/RTL-SDR.

- [ ] **Step 2: Add `screens::Id::weather` and prove screen ownership/self-check behavior**

  Insert a dedicated Weather framebuffer owner without renumbering dashboard wire IDs. Extend `screen_controller::self_check()` so Weather can own, enter Settings, return, and release just like other first-class dashboards.

- [ ] **Step 3: Implement the shared visual frame and five pages**

  Use the approved OrcSDR constants from the spec. NOW shows observation cards with source+age; FORECAST shows offline outlook/cached forecast state plus a Weather Radio shortcut; MAP uses an injected/offline map state and never requires network tiles; RF WEATHER exposes Weather Radio, Local Sensors, Radiosondes, Weather Hunter placeholders/status, and Background Policy; REPORTS displays bounded recent summaries. Dynamic updates repaint only changed regions.

- [ ] **Step 4: Add interaction and display self-check coverage**

  `self_check()` must prove every tab/button rectangle fits in 1280x720, footer tabs are non-overlapping, missing data renders explicit labels, and no page transition emits a tune/listen action.

- [ ] **Step 5: Run focused host/self-checks and native compile**

  Expected: Weather state tests pass, `screen_controller::self_check()` passes, and the Tab5 native build compiles with the dedicated dashboard registered.

- [ ] **Step 6: Commit Task 3**

  ```bash
  git add apps/orcsdr-tab5/ui/weather_dashboard.* apps/orcsdr-tab5/ui/screen_controller.* apps/orcsdr-tab5/ui/dashboard_registry.cpp apps/orcsdr-tab5/main/CMakeLists.txt tests/weather_dashboard_state_tests.cpp
  git commit -m "feat(weather): add dedicated five-tab dashboard"
  ```

### Task 4: Add SD-backed Weather reports and on-device history

**Files:**
- Create: `apps/orcsdr-tab5/ui/weather_report_store.hpp`
- Create: `apps/orcsdr-tab5/ui/weather_report_store.cpp`
- Create: `tests/weather_report_store_tests.cpp`
- Modify: `apps/orcsdr-tab5/main/CMakeLists.txt`

**Interfaces:**
- Consumes: `weather_model` observations/report summaries, `storage::FileSystem`, optional 1280x720 RGB565 screen/map captures, valid UTC or uptime-relative timestamps.
- Produces: bounded `ReportStore`, `SaveRequest`, `SaveResult`, history enumeration, report open/delete/export metadata, and report bundle writer under `/orcsdr/weather/`.

- [ ] **Step 1: Write failing storage/serialization tests with a host fake filesystem**

  Cover first run, missing SD, valid round-trip, HTML escaping, CSV quoting, JSON source metadata, invalid UTC fallback, bounded history, malformed history row, missing report directory, `.part` interruption, and SHA-256 manifest entries.

- [ ] **Step 2: Define the report bundle contract**

  Required normal files:

  ```text
  /orcsdr/weather/history.jsonl
  /orcsdr/weather/reports/<session-id>/report.html
  /orcsdr/weather/reports/<session-id>/session.json
  /orcsdr/weather/reports/<session-id>/observations.csv
  /orcsdr/weather/reports/<session-id>/rf_events.csv
  /orcsdr/weather/reports/<session-id>/alerts.json
  /orcsdr/weather/reports/<session-id>/manifest.sha256
  ```

  `screen.bmp` and `map.bmp` are optional. Do not emit raw IQ by default.

- [ ] **Step 3: Implement atomic report/history writes**

  Reuse the RF Lab pattern of temporary files, flush/close, final rename, and hashes. A failed bundle remains either absent or explicitly incomplete; it must not append a success row to history. Rebuild the bounded in-memory history from valid rows on load.

- [ ] **Step 4: Run storage tests including failure injection**

  Expected: all recovery/error cases preserve the last valid history/report set and return explicit status.

- [ ] **Step 5: Commit Task 4**

  ```bash
  git add apps/orcsdr-tab5/ui/weather_report_store.* tests/weather_report_store_tests.cpp apps/orcsdr-tab5/main/CMakeLists.txt
  git commit -m "feat(weather): add auditable SD report bundles"
  ```

### Task 5: Add the Weather runtime and on-demand NOAA operations

**Files:**
- Create: `apps/orcsdr-tab5/ui/weather_noaa.hpp`
- Create: `apps/orcsdr-tab5/ui/weather_noaa.cpp`
- Create: `apps/orcsdr-tab5/ui/weather_runtime.hpp`
- Create: `apps/orcsdr-tab5/ui/weather_runtime.cpp`
- Create: `tests/weather_noaa_tests.cpp`
- Create: `tests/weather_runtime_tests.cpp`
- Modify: `apps/orcsdr-tab5/ui/main.cpp`
- Modify: `apps/orcsdr-tab5/main/CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 1-4, current WX/NFM start/retune/stop hooks from `main.cpp`, canonical location/catalog/map/time/storage snapshots, and Task 2 `radio_session` safe ownership.
- Produces: dedicated Weather lifecycle; foreground `listen_noaa(channel)`, bounded `scan_noaa_channels()`, stop/release; strongest/last NOAA sample state; report snapshot hooks; and a small action bridge in `main.cpp`.

- [ ] **Step 1: Write NOAA plan/runtime tests**

  Test exact seven-channel stepping; strongest result selection by the receiver's existing relative signal metric; scan result age; busy receiver refusal; cancel/release; and the critical invariant that `weather_runtime::enter()` produces zero tune/start calls.

- [ ] **Step 2: Implement `weather_noaa` as model/controller logic, not a second DSP path**

  Keep channel plan, scan sample records, catalog transmitter matching, and display labels here. Use the existing `RtlBand::wx` and filter/audio path for actual listening. A scan is a foreground Weather action and samples channels sequentially; this foundation does not claim SAME monitoring.

- [ ] **Step 3: Implement `weather_runtime` with injected hooks**

  Required hooks include receiver availability/try-acquire, start/retune/stop, relative signal snapshot, Home/Settings navigation, canonical location, storage/report access, and optional map/catalog state. `enter()` only builds a dashboard snapshot. `leave()` stops/releases only a Weather-owned foreground job and does not stop a receiver owned by someone else.

- [ ] **Step 4: Replace the generic `Id::weather` open path with the dedicated runtime**

  In `main.cpp`, change `open_dashboard(Id::weather)` so it records the dashboard open, leaves Home, transitions to `screens::Id::weather`, and calls the Weather runtime without setting `RtlBand::wx` or retuning. Keep the bridge limited to hooks/snapshots/action dispatch.

- [ ] **Step 5: Add serial/test actions for reproducible UI regression**

  Extend authenticated `RTL_UI ACTION WEATHER` with at least `TAB <0-4>`, `NOAA_SCAN`, `NOAA_LISTEN <0-6>`, `STOP`, `REPORT_SNAPSHOT`, and `STATUS`. Unknown/unavailable actions return explicit invalid/busy states; they never silently tune generic Browse.

- [ ] **Step 6: Run host, native, and navigation regressions**

  Required checks: Weather open causes no tune; existing FM/AM/CB/Airband ownership tests stay green; NOAA Listen reaches existing WX NFM path; leaving releases Weather ownership; screen-controller/registry self-checks and native Tab5 build pass.

- [ ] **Step 7: Commit Task 5**

  ```bash
  git add apps/orcsdr-tab5/ui/weather_noaa.* apps/orcsdr-tab5/ui/weather_runtime.* tests/weather_noaa_tests.cpp tests/weather_runtime_tests.cpp apps/orcsdr-tab5/ui/main.cpp apps/orcsdr-tab5/main/CMakeLists.txt
  git commit -m "feat(weather): wire offline dashboard and NOAA actions"
  ```

### Task 6: Add offline network-policy plumbing, OrcDial semantics, and product documentation

**Files:**
- Modify: `apps/orcsdr-tab5/ui/settings_app.hpp`
- Modify: `apps/orcsdr-tab5/ui/settings_app.cpp`
- Modify: `apps/orcsdr-tab5/ui/nvs_store.hpp`
- Modify: `apps/orcsdr-tab5/ui/nvs_store.cpp`
- Modify: `orcdial/src/controller.hpp`
- Modify: `docs/ORCDIAL_CONTROL_MATRIX.md`
- Modify: `README.md`
- Modify: `PROJECT_STATUS.md`
- Modify: `architecture.md`
- Modify: `docs/user-guide/dashboards/other-bands.md`
- Create: `docs/user-guide/dashboards/weather.md`
- Modify: `docs/help_media/manifest.json`
- Modify: `apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1`

**Interfaces:**
- Consumes: Weather runtime/dashboard actions and existing settings/OrcDial bridge.
- Produces: persisted `OnlineWeatherPolicy { disabled, manual, automatic }` with `disabled` default; authoritative Weather Dial actions/state; user/developer docs and repeatable regression entry points.

- [ ] **Step 1: Add a policy persistence regression with disabled as the erased/default value**

  Prove a fresh settings state yields `disabled`; changing to manual/automatic round-trips; disabling again does not power Wi-Fi or schedule a fetch. This task supplies policy only—no Internet provider is implemented.

- [ ] **Step 2: Wire Weather OrcDial actions without changing dashboard ID 5**

  Implement only actions backed by real Tab5 handlers: previous/next NOAA channel while the NOAA card owns focus, activate focused action where bounded, volume/mute/Home. Unsupported Weather Hunter/Satellite controls return error until their phases exist.

- [ ] **Step 3: Extend regression tooling**

  Add Weather open/tab navigation and “open does not retune” checks to the canonical UI regression. Include an explicit Wi-Fi-off Weather smoke path and a NOAA foreground action path gated on an attached RTL-SDR.

- [ ] **Step 4: Update user/status/architecture documentation truthfully**

  Replace documentation saying Weather is only the shared WX receiver. Describe the dedicated dashboard as implemented only after the actual runtime exists. Mark Weather Hunter/SAME/sensors/radiosonde/satellite capabilities as planned/not implemented until later phases land.

- [ ] **Step 5: Run documentation truth and final native build**

  Run `python tests/test_documentation_truth.py`, help-doc validation if changed, focused Weather tests, canonical Tab5 native build, and UI regression dry-run/parser checks. Expected: all pass.

- [ ] **Step 6: Commit Task 6**

  ```bash
  git add apps/orcsdr-tab5/ui/settings_app.hpp apps/orcsdr-tab5/ui/settings_app.cpp apps/orcsdr-tab5/ui/nvs_store.hpp apps/orcsdr-tab5/ui/nvs_store.cpp orcdial/src/controller.hpp docs/ORCDIAL_CONTROL_MATRIX.md README.md PROJECT_STATUS.md architecture.md docs/user-guide/dashboards/other-bands.md docs/user-guide/dashboards/weather.md docs/help_media/manifest.json apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1
  git commit -m "docs(weather): integrate dashboard policy and controls"
  ```

### Task 7: Hardware acceptance and closeout evidence

**Files:**
- Create: `docs/testing/weather-dashboard-foundation-acceptance.md`
- Update only after evidence: `PROJECT_STATUS.md`, `docs/user-guide/feature-status.md`

**Interfaces:**
- Consumes: exact candidate firmware commit and physical Tab5/RTL-SDR/antenna/SD card.
- Produces: reproducible evidence record; no code behavior for later tasks depends on optimistic hardware claims.

- [ ] **Step 1: Build the exact candidate and record identity/hash before flashing**

- [ ] **Step 2: With Wi-Fi disabled from boot, open all five Weather tabs and prove no automatic tuner retune**

  Record starting owner/frequency, Weather entry, tab navigation, Home return, and unchanged receiver state.

- [ ] **Step 3: Exercise NOAA Listen and seven-channel scan with a suitable VHF antenna**

  Record antenna, receiver model, channel, relative signal/SNR metric, audio observation, scan result, and manual scope comparison. Do not convert relative dBFS into calibrated dBm.

- [ ] **Step 4: Exercise reports with SD present, removed, and restored**

  Save a snapshot/report; verify files/hash manifest independently on a PC; attempt save with SD unavailable; restore SD and prove prior report still opens.

- [ ] **Step 5: Run the canonical UI/driver regression and a repeated enter/leave soak**

  Confirm no USB/audio/DSP regression, no display tearing introduced by Weather updates, and no hidden Weather ownership after Home.

- [ ] **Step 6: Record results and update feature status only to the level actually proven**

  Use `Implemented`, `Regression-Tested`, `Hardware-Verified`, and `RF-Verified` separately. Do not claim Hunter/SAME/sensor/sonde/satellite support from this phase.

- [ ] **Step 7: Commit acceptance documentation**

  ```bash
  git add docs/testing/weather-dashboard-foundation-acceptance.md PROJECT_STATUS.md docs/user-guide/feature-status.md
  git commit -m "docs(weather): record foundation acceptance"
  ```
