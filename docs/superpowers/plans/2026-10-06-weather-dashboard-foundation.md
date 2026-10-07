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

- **Receiver ownership and user intent:** merely opening Weather never retunes. An explicit user NOAA Listen/Scan action may intentionally take over the tuner through the normal foreground path, while automatic/idle Weather work must fail closed rather than preempt. Covered in Task 2 ownership tests and Task 5 runtime tests.
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

### Task 2: Make receiver ownership safe for explicit Weather jobs and idle borrowing

**Files:**
- Modify: `apps/orcsdr-tab5/ui/radio_session.hpp`
- Modify: `apps/orcsdr-tab5/ui/radio_session.cpp`
- Modify: `tests/radio_scan_tests.cpp`

**Interfaces:**
- Consumes: existing `radio::Session`, `Token`, `Band::wx`, owner/generation/state.
- Produces: `Owner::weather`, coherent foreground `acquire(...)`, non-preemptive `try_acquire(...)`, and `release(Token)`. The existing `owner_for_band(Band::wx) == Owner::radio` contract remains unchanged for legacy/generic WX callers.

- [ ] **Step 1: Add failing ownership tests without changing the legacy WX mapping**

  Preserve the existing assertion that `owner_for_band(Band::wx) == Owner::radio`. Add tests that a forced `acquire(Owner::weather, Band::wx, ...)` replaces an older foreground token; `try_acquire(Owner::weather, ...)` refuses a session whose state is `starting`, `running`, or `stopping`; an idle/ready session can be claimed without corrupting its previous token; a stale token cannot release a newer owner; and release returns the session to an unowned idle state.

- [ ] **Step 2: Run `tools/test-radio-scan.sh` and observe the missing API failure**

  Expected before implementation: compile failure for `Owner::weather`, `try_acquire`, and `release`.

- [ ] **Step 3: Implement an internally serialized claim/publication path**

  Keep `Session::acquire(...)` as the unconditional foreground takeover used by existing dashboards. Add:

  ```cpp
  Token try_acquire(Owner owner, Band band, uint32_t frequency_hz,
                    uint32_t sample_rate_sps);
  bool release(Token token);
  ```

  Session mutation/snapshot publication must be coherent: do not make `owner` visible before band/frequency/sample-rate/generation are valid. Use one short internal guard/critical section (portable C++ in this module) around acquire/try-acquire/release/snapshot rather than a check-then-store sequence across independent atomics. `try_acquire` succeeds only when the receiver state is idle (`disconnected`, `ready`, or `failed`) and must never replace a `starting`/`running`/`stopping` owner. `release` validates owner+generation.

- [ ] **Step 4: Run the radio-session and scan stress regressions**

  Run `tools/test-radio-scan.sh`. Expected: existing scan ownership tests remain green, the legacy WX owner mapping remains `Owner::radio`, and new Weather forced/idle claim/release cases pass.

- [ ] **Step 5: Commit Task 2**

  ```bash
  git add apps/orcsdr-tab5/ui/radio_session.* tests/radio_scan_tests.cpp
  git commit -m "feat(radio): add safe Weather receiver claims"
  ```

### Task 3: Build the dedicated five-tab Weather M5GFX surface

**Files:**
- Create: `apps/orcsdr-tab5/ui/weather_dashboard.hpp`
- Create: `apps/orcsdr-tab5/ui/weather_dashboard.cpp`
- Create: `tests/weather_dashboard_state_tests.cpp`
- Modify: `apps/orcsdr-tab5/ui/screen_controller.hpp`
- Modify: `apps/orcsdr-tab5/ui/screen_controller.cpp`
- Modify: `apps/orcsdr-tab5/main/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 Weather model snapshots, existing `dashboard_audio_control`, `focus_nav`, and `offline_map` presentation hooks.
- Produces: `weather::Tab { now, forecast, map, rf_weather, reports }`, presentation `Snapshot`, `ActionKind`, `Action`, `enter`, `leave`, `draw`, `update`, `handle_touch`, `select_tab`, `active`, and `self_check`.

- [ ] **Step 1: Write state/layout tests before drawing code**

  Cover five tabs, exact 256 px tab widths, footer at y=630, no card overlap with footer, action mapping for every primary RF/report control, no RF action on tab switching, and explicit unavailable states for missing map/SD/location/RTL-SDR.

- [ ] **Step 2: Add only the missing framebuffer owner**

  `dashboards::Id::weather` already exists and remains wire ID 5. Add `screens::Id::weather` and `name(Id::weather) == "weather"`; do not renumber or re-add the dashboard registry enum. Extend `screen_controller::self_check()` with Weather Settings-return ownership. Replace the fragile hard-coded transition total (`38` on the reviewed baseline) with a count derived from the exercised transitions, or update it in a way that cannot silently omit Weather.

- [ ] **Step 3: Implement the shared visual frame and five pages**

  Use the approved OrcSDR constants from the spec. NOW shows observation cards with source+age; FORECAST shows offline outlook/cached forecast state plus a Weather Radio shortcut; MAP uses injected/offline map state and never requires network tiles; RF WEATHER exposes Weather Radio, Local Sensors, Radiosondes, Weather Hunter placeholders/status, and Background Policy; REPORTS displays bounded recent summaries. Dynamic updates repaint only changed regions.

- [ ] **Step 4: Add interaction and display self-check coverage**

  `self_check()` must prove every tab/button rectangle fits in 1280x720, footer tabs are non-overlapping, missing data renders explicit labels, and no page transition emits a tune/listen action.

- [ ] **Step 5: Run focused host/self-checks and native compile**

  Expected: Weather state tests pass, `screen_controller::self_check()` passes, and the native Tab5 build compiles with the new framebuffer owner. Dashboard registry count remains unchanged because Weather was already registered.

- [ ] **Step 6: Commit Task 3**

  ```bash
  git add apps/orcsdr-tab5/ui/weather_dashboard.* apps/orcsdr-tab5/ui/screen_controller.* apps/orcsdr-tab5/main/CMakeLists.txt tests/weather_dashboard_state_tests.cpp
  git commit -m "feat(weather): add dedicated five-tab dashboard"
  ```

### Task 4: Add SD-backed Weather reports and on-device history

**Files:**
- Create: `apps/orcsdr-tab5/ui/weather_report_store.hpp`
- Create: `apps/orcsdr-tab5/ui/weather_report_store.cpp`
- Create: `apps/orcsdr-tab5/ui/weather_report_transaction.hpp`
- Create: `apps/orcsdr-tab5/ui/weather_report_transaction.cpp`
- Create: `tests/weather_report_store_tests.cpp`
- Modify: `apps/orcsdr-tab5/main/CMakeLists.txt`

**Interfaces:**
- Consumes: `weather_model` observations/report summaries, `storage::FileSystem`, optional 1280x720 RGB565 screen/map captures, valid UTC or uptime-relative timestamps.
- Produces: bounded `ReportStore`, `SaveRequest`, `SaveResult`, history enumeration, report open/delete/export metadata, pure serializers, and report bundle writer under `/orcsdr/weather/`. The transaction helper expresses atomic bundle/history commit steps independently of the concrete SD wrapper.

- [ ] **Step 1: Write host tests against pure codecs and a callback-driven transaction harness**

  `storage::FileSystem` is a concrete wrapper, so do not assume it can be replaced by a polymorphic fake. Host tests cover HTML escaping, CSV quoting, JSON source metadata, invalid-UTC fallback, bounded history parsing, malformed rows, manifest hashes, and a pure/callback transaction harness that injects failure at create/write/flush/rename/history-append steps. Assert an interrupted transaction never reports success or replaces the last committed history entry.

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

- [ ] **Step 3: Implement the concrete SD adapter using the proven repository semantics**

  Map the transaction steps to `storage::FileSystem` using the same `.part`/flush/close/rename/backup discipline already used by Shortwave/catalog storage. A failed bundle remains absent or explicitly incomplete and must not append a success row to history. Rebuild the bounded in-memory history from valid rows on load.

- [ ] **Step 4: Run host failure-injection tests plus on-device file-semantics checks**

  Expected: host transaction tests pass, existing storage file-semantics checks stay green, and device/manual SD acceptance in Task 7 proves the concrete adapter.

- [ ] **Step 5: Commit Task 4**

  ```bash
  git add apps/orcsdr-tab5/ui/weather_report_store.* apps/orcsdr-tab5/ui/weather_report_transaction.* tests/weather_report_store_tests.cpp apps/orcsdr-tab5/main/CMakeLists.txt
  git commit -m "feat(weather): add auditable SD report bundles"
  ```

### Task 5: Add the Weather runtime and make NOAA's seven WX channels real

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
- Consumes: Tasks 1-4, existing WX/NFM stream, `scan_engine`, `request_hot_retune_for`, canonical location/catalog/map/time/storage snapshots, and Task 2 ownership.
- Produces: dedicated Weather lifecycle; user-foreground NOAA Listen/Scan takeover; a claimed-token start seam for later automatic jobs; seven-channel WX normalization; Weather-owned stop/release; strongest/last NOAA sample state; report snapshot hooks; and the small main adapter needed to preserve the dedicated framebuffer.

- [ ] **Step 1: Write failing NOAA/runtime tests for the actual current blockers**

  Assert all seven NOAA channel centers round-trip through the WX normalization API; nearest/invalid input is handled deterministically; next/previous channel cycles all seven; strongest result uses the existing relative signal metric; `weather_runtime::enter()` performs zero tune/start calls; an explicit user Listen/Scan requests foreground takeover; an automatic claimed start fails if its token is stale; and Home/Stop cannot release a non-Weather token.

- [ ] **Step 2: Implement `weather_noaa` as the sole NOAA channel authority**

  Put the seven frequencies, index/nearest/next/previous helpers, transmitter-catalog matching, scan sample/result model, and labels here. Change the main WX clamp/step seams to delegate to these helpers so valid requests no longer collapse to the legacy fixed 162.400 MHz value.

- [ ] **Step 3: Extend the existing receiver start seam instead of creating a parallel receiver**

  Refactor the internal `queue_local_rtl_listen(...)` path so existing callers remain source-compatible while Weather can explicitly supply `Owner::weather` and, for future automatic work, an already claimed `radio::Token`. If a valid claimed token is supplied, the start path must use it and must **not** call `radio_session.acquire()` a second time. An explicit user Weather action supplies `Owner::weather` without a claim and is allowed to perform the normal controlled foreground takeover.

- [ ] **Step 4: Reuse the shared scan/hot-retune machinery for NOAA scan**

  Extend `ActiveScan` with a Weather NOAA scan rather than adding another scan loop. Permit `request_hot_retune_for()` on `RtlBand::wx` only when the live token belongs to `Owner::weather` and the target is one of the seven valid NOAA channels. Keep ADS-B's prohibition. Feed scan measurements into `weather_noaa`, cancel through the shared `scan_engine`, and preserve the Weather token across channel retunes.

- [ ] **Step 5: Route a running WX stream through the dedicated Weather framebuffer**

  Add the reviewed integration seams explicitly: `screen_for_band(RtlBand::wx) -> screens::Id::weather`; `refresh_active_screen()`; `draw_sdr_screen()`; Weather enter/update/touch service in the main loop; `active_dashboard_tab()`; visualizer return/redraw; and the UI-regression dashboard-band list. WX stream start/refresh must never fall through to Home or the generic Radio surface.

- [ ] **Step 6: Replace the generic `Id::weather` open path without tuning**

  `open_dashboard(Id::weather)` records the dashboard open, leaves Home, transitions to `screens::Id::weather`, and calls Weather runtime `enter()` without changing `rtl_ui_band`, frequency, stream state, or current receiver owner. Pressing NOAA Listen/Scan is the point at which the user intentionally takes over the tuner.

- [ ] **Step 7: Make Weather stop/release lifecycle token-correct**

  Runtime retains its Weather token. Stop/Home cancels a Weather scan and requests stream stop only when that token still owns the session. Release occurs after the matching Weather stream reaches its stopped/idle state; do not release early while USB/DSP shutdown is still in progress, and never stop/release a newer non-Weather owner.

- [ ] **Step 8: Add serial/test actions for reproducible regression**

  Extend authenticated `RTL_UI ACTION WEATHER` with at least `TAB <0-4>`, `NOAA_SCAN`, `NOAA_LISTEN <0-6>`, `STOP`, `REPORT_SNAPSHOT`, and `STATUS`. Unknown/unavailable actions return explicit invalid/busy states.

- [ ] **Step 9: Run host, native, navigation, and shared-scan regressions**

  Required checks: Weather open causes no tune; all seven WX centers survive clamp/retune; explicit Listen may take over; a preclaimed automatic start cannot overwrite a newer foreground owner; running WX stays on Weather screen; existing FM/AM/CB/Airband/P25/scan tests remain green; native Tab5 build passes.

- [ ] **Step 10: Commit Task 5**

  ```bash
  git add apps/orcsdr-tab5/ui/weather_noaa.* apps/orcsdr-tab5/ui/weather_runtime.* tests/weather_noaa_tests.cpp tests/weather_runtime_tests.cpp apps/orcsdr-tab5/ui/main.cpp apps/orcsdr-tab5/main/CMakeLists.txt
  git commit -m "feat(weather): wire dedicated NOAA Weather runtime"
  ```

### Task 6: Add offline network-policy plumbing, OrcDial semantics, and product documentation

**Files:**
- Modify: `apps/orcsdr-tab5/ui/settings_app.hpp`
- Modify: `apps/orcsdr-tab5/ui/settings_app.cpp`
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
- Consumes: Weather runtime/dashboard actions, the existing generic `NvsStore`/preferences instance, and existing OrcDial bridge.
- Produces: persisted `OnlineWeatherPolicy { disabled, manual, automatic }` with `disabled` default; authoritative Weather Dial actions/state; user/developer docs and repeatable regression entry points.

- [ ] **Step 1: Persist policy through the existing NVS abstraction**

  Do not modify `nvs_store.*`: it already provides `getUChar/putUChar`. Add a Weather policy key in the existing settings load/save adapter. Prove a fresh/erased key yields `disabled`; manual/automatic round-trip; disabling again does not power Wi-Fi or schedule a fetch. This task supplies policy only—no Internet provider is implemented.

- [ ] **Step 2: Wire Weather OrcDial actions without changing dashboard ID 5**

  Implement only actions backed by real Tab5 handlers: previous/next NOAA channel while the NOAA card owns focus, activate focused action where bounded, volume/mute/Home. Unsupported Weather Hunter/Satellite controls return error until their phases exist.

- [ ] **Step 3: Extend regression tooling**

  Add Weather open/tab navigation and “open does not retune” checks to the canonical UI regression. Include all-seven-channel normalization/selection, running-WX-stays-on-Weather, explicit foreground takeover, Wi-Fi-off Weather smoke, and a NOAA action path gated on an attached RTL-SDR.

- [ ] **Step 4: Update user/status/architecture documentation truthfully**

  Replace documentation saying Weather is only the shared WX receiver. Update the existing `wx.radio/scope/capture` help-media assumptions so documentation capture does not route through a retired generic Weather surface. Describe the dedicated dashboard as implemented only after the runtime exists. Mark Weather Hunter/SAME/sensors/radiosonde/satellite capabilities planned/not implemented until later phases land.

- [ ] **Step 5: Run documentation truth and final native build**

  Run `python tests/test_documentation_truth.py`, help-doc validation, focused Weather/radio-scan tests, canonical Tab5 native build, and UI regression parser/dry-run checks. Expected: all pass.

- [ ] **Step 6: Commit Task 6**

  ```bash
  git add apps/orcsdr-tab5/ui/settings_app.hpp apps/orcsdr-tab5/ui/settings_app.cpp orcdial/src/controller.hpp docs/ORCDIAL_CONTROL_MATRIX.md README.md PROJECT_STATUS.md architecture.md docs/user-guide/dashboards/other-bands.md docs/user-guide/dashboards/weather.md docs/help_media/manifest.json apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1
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
