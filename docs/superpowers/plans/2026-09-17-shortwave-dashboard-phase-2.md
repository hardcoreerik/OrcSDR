# Shortwave Dashboard Phase 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the Tab5 Shortwave Explorer with reliable direct tuning, local On Air and Hunt discovery, SD-backed Memory and Logbook tabs, and portable exports.

**Architecture:** `shortwave_dashboard` remains a presentation and action layer. New pure Shortwave library and hunt controllers own bounded records, parsing, ranking, and state; an SD adapter performs persistence using the existing storage wrapper. `main.cpp` remains a narrow bridge to the existing receiver and scan engine.

**Tech Stack:** C++17, ESP-IDF 5.5.4, M5GFX/M5Unified, existing `scan_engine`, `orcsdr_storage`, `time_service`, native self-checks, and the Tab5 PowerShell UI regression harness.

**Spec:** `docs/superpowers/specs/2026-09-17-shortwave-dashboard-phase-2-design.md`

## Global Constraints

- Work only in `F:\Ai\OrcSDR\.codex-worktrees\shortwave-dashboard-completion` on `codex/shortwave-dashboard-completion`.
- Do not add dashboard drawing, SD parsing, export formatting, or scan ranking to `main.cpp`.
- Do not add a receiver, DSP, audio, database, networking, or third-party dependency.
- Direct entry covers 24 kHz through 30 MHz exactly and must reject, not clamp,
  out-of-range input.
- AM is the only actionable Shortwave demodulator in Phase 2. Mode guidance may
  name likely SSB/CW/NFM/DRM usage but cannot expose controls that do not work.
- SD is the authoritative persistence layer for Shortwave memories and logs; NVS contains no sole copy of either.
- Failed SD operations must preserve the last valid on-card file and surface an explicit unavailable/write-failed state.
- Do not infer or claim station identity from signal energy or frequency alone.
- Preserve route/capability behavior: V3c Direct Q has no tuner-gain action below 24 MHz; V4 HF upconversion remains unchanged below 28.8 MHz.
- Build and hardware evidence remain separate claims. Flash only after focused checks and native build pass.

---

### Task 1: Make Direct Tune a real modal and lock its regression down

**Files:**
- Create: `apps/orcsdr-tab5/ui/shortwave_dashboard_state.hpp`
- Create: `apps/orcsdr-tab5/ui/shortwave_dashboard_state.cpp`
- Modify: `apps/orcsdr-tab5/ui/shortwave_dashboard.cpp:35-465`
- Modify: `apps/orcsdr-tab5/ui/shortwave_dashboard.hpp:15-48`
- Modify: `apps/orcsdr-tab5/main/CMakeLists.txt`
- Create: `tests/shortwave_dashboard_state_tests.cpp`

**Interfaces:**
- Consumes: existing `Snapshot`, `Action`, `ActionKind::tune_hz`, and `g_keypad` state.
- Produces: a keypad that owns the visible content region until `Cancel` or valid `Tune` returns an action.

- [ ] **Step 1: Add a failing host test for modal ownership**

  The production change this catches is a periodic snapshot update being
  allowed to redraw Live while a modal owns the content area:

  ```cpp
  DashboardState state;
  state.open(Modal::frequency);
  CHECK(!state.background_redraw_allowed());
  CHECK(!state.spectrum_allowed());
  state.close_modal();
  CHECK(state.background_redraw_allowed());
  ```

  Also verify Cancel returns to Live and `spectrum_active()` stays false while the keypad is visible.

- [ ] **Step 2: Compile the host test and observe the missing API failure**

  Run:

  ```powershell
  wsl.exe bash -lc "cd /mnt/f/Ai/OrcSDR/.codex-worktrees/shortwave-dashboard-completion && c++ -std=c++17 -Iapps/orcsdr-tab5/ui tests/shortwave_dashboard_state_tests.cpp apps/orcsdr-tab5/ui/shortwave_dashboard_state.cpp -o /tmp/shortwave_dashboard_state_tests && /tmp/shortwave_dashboard_state_tests"
  ```

  Expected before implementation: compile failure because `DashboardState` and
  `Modal` do not exist.

- [ ] **Step 3: Implement the smallest ownership guard**

  Add the minimal pure state object, use it as the dashboard's single modal
  authority, keep `g_snapshot = snapshot` at the start of `update()`, then
  return before
  `draw_frequency`, `draw_status`, `draw_quick_controls`, or `draw_controls`
  when `g_keypad` is true:

  ```cpp
  void update(const Snapshot& snapshot) {
    if (!g_active) return;
    const bool controls_changed = /* existing comparison */;
    g_snapshot = snapshot;
    g_saved_frequency = snapshot.frequency_hz;
    if (!g_state.background_redraw_allowed()) return;
    draw_frequency();
    draw_status();
    draw_quick_controls();
    if (controls_changed) draw_controls();
  }
  ```

  Keep keypad redraw exclusively in `draw_keypad()` after a keypad touch.

- [ ] **Step 4: Re-run the host test and native compile**

  Run:

  ```powershell
  wsl.exe bash -lc "cd /mnt/f/Ai/OrcSDR/.codex-worktrees/shortwave-dashboard-completion && c++ -std=c++17 -Iapps/orcsdr-tab5/ui tests/shortwave_dashboard_state_tests.cpp apps/orcsdr-tab5/ui/shortwave_dashboard_state.cpp -o /tmp/shortwave_dashboard_state_tests && /tmp/shortwave_dashboard_state_tests"
  cmd /c "call C:\Espressif\frameworks\esp-idf-v5.5.4\export.bat >nul && idf.py -B build-native-shortwave build"
  ```

  Expected: `shortwave_dashboard_state_tests: PASS` and the native build passes.

- [ ] **Step 5: Commit the modal repair**

  ```powershell
  git add apps/orcsdr-tab5/ui/shortwave_dashboard_state.* apps/orcsdr-tab5/ui/shortwave_dashboard.* apps/orcsdr-tab5/main/CMakeLists.txt tests/shortwave_dashboard_state_tests.cpp
  git commit -m "fix(shortwave): keep direct tune modal visible"
  ```

### Task 2: Extend the pure Shortwave model for tabs, schedules, and records

**Files:**
- Modify: `apps/orcsdr-tab5/ui/shortwave_model.hpp:8-87`
- Modify: `apps/orcsdr-tab5/ui/shortwave_model.cpp:9-164`
- Create: `tests/shortwave_model_tests.cpp`

**Interfaces:**
- Consumes: existing `BroadcastBand`, `StationCard`, `Memory`, and `LogEntry` contracts.
- Produces: `Tab`, `ScheduleMatch`, `schedule_matches`, `MemoryTable`, and `LogTable` fixed-capacity pure APIs used by the dashboard and library layers.
- Also produces: `SpectrumRegion`, `ModeHint`, `spectrum_region`, and
  `mode_hint_for` so the UI teaches band/mode context without inventing a
  decoded identity.

- [ ] **Step 1: Write failing host tests for the public pure API**

  Create tests using the repository's `CHECK` convention. Cover current-UTC matching, day masks, tolerance, band filtering, stable descending candidate ranking, duplicate-memory rejection, bounded text, and invalid log rejection:

  ```cpp
  CHECK(schedule_matches(card, 5935000, utc_minute, utc_weekday));
  CHECK(!schedule_matches(card, 5935000, outside_window, utc_weekday));
  CHECK(memories.upsert(memory));
  CHECK(!memories.upsert(memory));
  CHECK(logs.append(entry));
  CHECK(spectrum_region(24000) == SpectrumRegion::vlf_edge);
  CHECK(spectrum_region(3000000) == SpectrumRegion::hf);
  CHECK(mode_hint_for(9550000, Service::broadcast).mode == ModeHint::am);
  CHECK(mode_hint_for(7100000, Service::amateur_voice).mode == ModeHint::lsb);
  CHECK(mode_hint_for(14200000, Service::amateur_voice).mode == ModeHint::usb);
  ```

- [ ] **Step 2: Compile and run the new host test before implementation**

  Run:

  ```powershell
  wsl.exe bash -lc "cd /mnt/f/Ai/OrcSDR/.codex-worktrees/shortwave-dashboard-completion && c++ -std=c++17 -Iapps/orcsdr-tab5/ui tests/shortwave_model_tests.cpp apps/orcsdr-tab5/ui/shortwave_model.cpp -o /tmp/shortwave_model_tests && /tmp/shortwave_model_tests"
  ```

  Expected before implementation: compile failure for the new API.

- [ ] **Step 3: Implement bounded, allocation-free model types**

  Add fixed capacities and APIs with explicit results:

  ```cpp
  enum class Tab : uint8_t { live, on_air, hunt, memory, logbook };
  enum class RecordResult : uint8_t { ok, full, duplicate, invalid, missing };
  enum class SpectrumRegion : uint8_t { vlf_edge, lf, mf, hf };
  enum class ModeHint : uint8_t { unknown, am, lsb, usb, cw, nfm, drm };
  bool schedule_matches(const StationCard&, uint32_t frequency_hz,
                        uint16_t utc_minute, uint8_t utc_weekday);
  ```

  Keep `StationCard` only a local catalog record; callers display an honest
  schedule label rather than a decoded identity assertion.
  Change `Memory`/`LogEntry` validation to accept exact frequencies from
  24,000 through 30,000,000 Hz. Keep ITU broadcast-band lookup separate so an
  LF/MF/HF region label never masquerades as a broadcast allocation.

- [ ] **Step 4: Run the model test and native Shortwave self-check build**

  Run the host command from Step 2 and:

  ```powershell
  cmd /c "call C:\Espressif\frameworks\esp-idf-v5.5.4\export.bat >nul && idf.py -B build-native-shortwave build"
  ```

  Expected: `shortwave_model_tests: PASS` and the firmware containing
  `RTL_SHORTWAVE_MODEL_SELF_CHECK_OK` builds successfully. The marker is runtime
  evidence only after the later flash/serial task.

- [ ] **Step 5: Commit the pure model slice**

  ```powershell
  git add apps/orcsdr-tab5/ui/shortwave_model.* tests/shortwave_model_tests.cpp
  git commit -m "feat(shortwave): add local discovery model"
  ```

### Task 3: Add SD-backed library and export codecs outside the dashboard

**Files:**
- Create: `apps/orcsdr-tab5/ui/shortwave_library.hpp`
- Create: `apps/orcsdr-tab5/ui/shortwave_library.cpp`
- Create: `apps/orcsdr-tab5/ui/shortwave_library_io.cpp`
- Modify: `apps/orcsdr-tab5/main/CMakeLists.txt`
- Create: `tests/shortwave_library_tests.cpp`

**Interfaces:**
- Consumes: Task 2 record tables; `orcsdr::storage::FileSystem` from `orcsdr_storage.hpp`.
- Produces: `Library::load`, `save_memories`, `save_logs`, `export_all`, and `State` containing tables plus `StorageStatus`.

- [ ] **Step 1: Write failing codec tests with no SD dependency**

  Test quoted CSV, a comma/newline in notes, complete round-trip serialization,
  invalid-row counting, and ADIF's truthful field selection:

  ```cpp
  CHECK(encode_csv(entry, line, sizeof(line)));
  CHECK(decode_csv(line, &decoded));
  CHECK(std::strcmp(decoded.notes, "Signal, then\nvoice") == 0);
  CHECK(encode_adif(entry, adi, sizeof(adi)));
  CHECK(std::strstr(adi, "<QSO_DATE:") != nullptr);
  CHECK(std::strstr(adi, "<COMMENT:") != nullptr);
  ```

- [ ] **Step 2: Compile the codec test to prove the API is absent**

  Run:

  ```powershell
  wsl.exe bash -lc "cd /mnt/f/Ai/OrcSDR/.codex-worktrees/shortwave-dashboard-completion && c++ -std=c++17 -Iapps/orcsdr-tab5/ui tests/shortwave_library_tests.cpp apps/orcsdr-tab5/ui/shortwave_library.cpp -o /tmp/shortwave_library_tests && /tmp/shortwave_library_tests"
  ```

  Expected before implementation: compile failure for `encode_csv`, `decode_csv`, and `encode_adif`.

- [ ] **Step 3: Implement pure codecs and bounded library state**

  Define explicit storage outcomes and paths:

  ```cpp
  enum class StorageStatus : uint8_t { ready, unavailable, read_failed, write_failed };
  constexpr char kRoot[] = "/OrcSDR/shortwave";
  constexpr char kMemoriesPath[] = "/OrcSDR/shortwave/memories.csv";
  constexpr char kLogbookPath[] = "/OrcSDR/shortwave/logbook.csv";
  ```

  Use RFC-4180-compatible CSV quoting for all string fields. Build ADIF length
  tags from UTF-8 byte counts and only emit fields populated by the source
  entry; put non-QSO listener context into `COMMENT`.

- [ ] **Step 4: Implement SD I/O with verified atomic replacement**

  Follow `fm_config.cpp`'s existing `.part`/`.bak` approach, but do not copy it
  blindly: use `/OrcSDR/shortwave`, flush/close the temporary file, re-open and
  parse it before replacing the target, restore `.bak` if replacement fails,
  and restore a backup only when the final file is absent or invalid.

  ```cpp
  bool replace_verified(FileSystem& fs, const char* path,
                        const char* text, size_t bytes, char* error, size_t size);
  LoadResult load_with_backup(FileSystem& fs, const char* path, State* state,
                              char* error, size_t size);
  ```

- [ ] **Step 5: Re-run codec tests and build the native component**

  Run the host command from Step 2, then:

  ```powershell
  cmd /c "call C:\Espressif\frameworks\esp-idf-v5.5.4\export.bat >nul && idf.py -B build-native-shortwave build"
  ```

  Expected: all codec checks pass and the new CMake sources compile in the
  native Tab5 build.

- [ ] **Step 6: Commit durable library storage**

  ```powershell
  git add apps/orcsdr-tab5/ui/shortwave_library.* apps/orcsdr-tab5/main/CMakeLists.txt tests/shortwave_library_tests.cpp
  git commit -m "feat(shortwave): persist memories and logbook on sd"
  ```

### Task 4: Wrap the existing scan engine in a Shortwave Hunt controller

**Files:**
- Create: `apps/orcsdr-tab5/ui/shortwave_hunt.hpp`
- Create: `apps/orcsdr-tab5/ui/shortwave_hunt.cpp`
- Modify: `apps/orcsdr-tab5/main/CMakeLists.txt`
- Create: `tests/shortwave_hunt_tests.cpp`

**Interfaces:**
- Consumes: `scan::Engine`, Task 2 band lookup, and spectrum-derived dBFS samples supplied by the adapter.
- Produces: `Hunt::start`, `Hunt::observe`, `Hunt::cancel`, `Hunt::snapshot`, and sorted `Candidate` values.

- [ ] **Step 1: Write failing deterministic hunt tests**

  Verify a band produces in-range scan targets, strongest observations sort
  first with frequency as a stable tie-breaker, capacity eviction only removes
  the weakest candidate, and cancellation requests normal frequency restore:

  ```cpp
  CHECK(hunt.start(*band_for(5935000), 1000));
  hunt.observe(5935000, -48.0f);
  hunt.observe(5940000, -62.0f);
  CHECK(hunt.snapshot().candidates[0].frequency_hz == 5935000);
  ```

- [ ] **Step 2: Run the test before implementation**

  Run:

  ```powershell
  wsl.exe bash -lc "cd /mnt/f/Ai/OrcSDR/.codex-worktrees/shortwave-dashboard-completion && c++ -std=c++17 -Iapps/orcsdr-tab5/ui tests/shortwave_hunt_tests.cpp apps/orcsdr-tab5/ui/shortwave_hunt.cpp apps/orcsdr-tab5/ui/shortwave_model.cpp apps/orcsdr-tab5/ui/scan_engine.cpp -o /tmp/shortwave_hunt_tests && /tmp/shortwave_hunt_tests"
  ```

  Expected before implementation: compile failure for `orcsdr::shortwave::Hunt`.

- [ ] **Step 3: Implement the thin scan-engine adapter**

  Use `scan::Plan{Mode::frequency_range, ...}` with a fixed Shortwave settle
  delay owned by the hunt controller. Keep no FFT buffers or radio control in
  this module. The adapter receives one scalar measurement per engine index,
  converts it to `Candidate{frequency_hz, relative_dbfs}`, and maintains a
  bounded top-N list.

- [ ] **Step 4: Verify test and existing scan regression**

  Run the host command from Step 2 and:

  ```powershell
  wsl.exe bash -lc "cd /mnt/f/Ai/OrcSDR/.codex-worktrees/shortwave-dashboard-completion && c++ -std=c++17 -Iapps/orcsdr-tab5/ui tests/radio_scan_tests.cpp apps/orcsdr-tab5/ui/radio_session.cpp apps/orcsdr-tab5/ui/scan_engine.cpp -o /tmp/radio_scan_tests && /tmp/radio_scan_tests"
  ```

  Expected: `shortwave_hunt_tests: PASS` and `radio_scan_tests: PASS`.

- [ ] **Step 5: Commit the Hunt controller**

  ```powershell
  git add apps/orcsdr-tab5/ui/shortwave_hunt.* apps/orcsdr-tab5/main/CMakeLists.txt tests/shortwave_hunt_tests.cpp
  git commit -m "feat(shortwave): add local hunt controller"
  ```

### Task 5: Complete the five-tab Shortwave presentation and actions

**Files:**
- Modify: `apps/orcsdr-tab5/ui/shortwave_dashboard.hpp:8-48`
- Modify: `apps/orcsdr-tab5/ui/shortwave_dashboard.cpp:24-465`
- Test: `apps/orcsdr-tab5/ui/shortwave_dashboard.cpp:dashboard_self_check()`

**Interfaces:**
- Consumes: Task 2 `Tab` and schedule results, Task 3 library `State`, and Task 4 `Hunt::Snapshot` supplied in `Snapshot`.
- Produces: explicit tab/action values: `select_tab`, `hunt_start`, `hunt_stop`, `hunt_tune`, `memory_save`, `memory_tune`, `memory_delete`, `log_new`, `log_edit`, `log_delete`, and `log_export`.

- [ ] **Step 1: Add failing tab-navigation and action-map self-checks**

  Cover every bottom-tab hit, a no-results On Air state, Hunt start/stop/tune,
  Memory save/tune/delete, Logbook save/delete/export, and that Live's spectrum
  only receives touches while Live owns it:

  ```cpp
  CHECK(handle_touch(on_air_x, kTabsY + 40).kind == ActionKind::select_tab);
  CHECK(handle_touch(hunt_start_x, hunt_start_y).kind == ActionKind::hunt_start);
  CHECK(handle_touch(memory_save_x, memory_save_y).kind == ActionKind::memory_save);
  ```

- [ ] **Step 2: Run the self-check before tab implementation**

  Run:

  ```powershell
  & .\apps\orcsdr-tab5\tools\run-tab5-ui-regression.ps1 -SelfCheck
  ```

  Expected before implementation: new tab assertions fail because all non-Live
  buttons are disabled placeholders.

- [ ] **Step 3: Implement the shared dashboard state and five renderers**

  Add one `g_tab` and dispatch `draw_live`, `draw_on_air`, `draw_hunt`,
  `draw_memory`, or `draw_logbook` after drawing the shared header and bottom
  tab bar. Every tab must use the same existing 1280x720 header controls.
  Replace `LATER` with active labels only after the matching screen handles its
  action. Keep an empty-state card with a specific reason rather than a blank
  panel.

- [ ] **Step 4: Implement text entry reuse without a second keypad**

  Reuse the existing Shortwave modal infrastructure for label/notes fields by
  adding a `ModalKind` discriminator (`frequency`, `memory_label`,
  `memory_notes`, `log_notes`). Frequency retains numeric validation; text
  fields route through the existing `text_editor` service via one high-level
  action. Do not embed a duplicate keyboard in the dashboard.

- [ ] **Step 5: Re-run the focused UI check**

  Run:

  ```powershell
  & .\apps\orcsdr-tab5\tools\run-tab5-ui-regression.ps1 -SelfCheck
  ```

  Expected: all existing UI checks and the expanded Shortwave check pass.

- [ ] **Step 6: Commit complete Shortwave tab presentation**

  ```powershell
  git add apps/orcsdr-tab5/ui/shortwave_dashboard.*
  git commit -m "feat(shortwave): complete discovery and logbook tabs"
  ```

### Task 6: Wire only high-level Shortwave actions through the application adapter

**Files:**
- Modify: `apps/orcsdr-tab5/ui/main.cpp:9635-9778,12488-12756,16070-16079`
- Modify: `apps/orcsdr-tab5/main/CMakeLists.txt`
- Modify: `apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1` only to add stable Shortwave serial markers if absent

**Interfaces:**
- Consumes: Actions from Task 5, `shortwave_library::State`, and `shortwave::Hunt` from Tasks 3–4.
- Produces: current library/hunt results in `shortwave_dashboard_snapshot()` and radio/scan service calls from `handle_shortwave_dashboard_action()`.

- [ ] **Step 1: Add failing adapter self-check markers**

  Register the new pure library/hunt self-checks alongside the existing
  Shortwave model/dashboard checks, and assert the emitted serial markers:

  ```cpp
  if (!orcsdr::shortwave::library_self_check()) {
    Serial.println("RTL_SHORTWAVE_LIBRARY_SELF_CHECK_FAIL");
    return;
  }
  Serial.println("RTL_SHORTWAVE_LIBRARY_SELF_CHECK_OK");
  ```

- [ ] **Step 2: Run the UI harness before integration completes**

  Run:

  ```powershell
  & .\apps\orcsdr-tab5\tools\run-tab5-ui-regression.ps1 -SelfCheck
  ```

  Expected before adapter wiring: missing-library/hunt self-check markers or
  action routing failure.

- [ ] **Step 3: Add the smallest lifecycle bridge**

  Maintain one application-owned `shortwave_library::State` and one
  `shortwave::Hunt`. On Shortwave entry, mount/load only once per session and
  pass a snapshot view to the dashboard. On a library mutation, call its SD
  save API and refresh the view. On Hunt actions, start/service/cancel the
  existing `scan::Engine` callbacks. Do not copy drawing, serialization, or
  ranking code into `main.cpp`.

- [ ] **Step 4: Verify all focused host and UI checks**

  Run:

  ```powershell
  wsl.exe bash -lc "cd /mnt/f/Ai/OrcSDR/.codex-worktrees/shortwave-dashboard-completion && c++ -std=c++17 -Iapps/orcsdr-tab5/ui tests/shortwave_model_tests.cpp apps/orcsdr-tab5/ui/shortwave_model.cpp -o /tmp/shortwave_model_tests && /tmp/shortwave_model_tests"
  wsl.exe bash -lc "cd /mnt/f/Ai/OrcSDR/.codex-worktrees/shortwave-dashboard-completion && c++ -std=c++17 -Iapps/orcsdr-tab5/ui tests/shortwave_library_tests.cpp apps/orcsdr-tab5/ui/shortwave_library.cpp -o /tmp/shortwave_library_tests && /tmp/shortwave_library_tests"
  wsl.exe bash -lc "cd /mnt/f/Ai/OrcSDR/.codex-worktrees/shortwave-dashboard-completion && c++ -std=c++17 -Iapps/orcsdr-tab5/ui tests/shortwave_hunt_tests.cpp apps/orcsdr-tab5/ui/shortwave_hunt.cpp apps/orcsdr-tab5/ui/shortwave_model.cpp apps/orcsdr-tab5/ui/scan_engine.cpp -o /tmp/shortwave_hunt_tests && /tmp/shortwave_hunt_tests"
  & .\apps\orcsdr-tab5\tools\run-tab5-ui-regression.ps1 -SelfCheck
  ```

  Expected: all four commands pass.

- [ ] **Step 5: Commit the adapter wiring**

  ```powershell
  git add apps/orcsdr-tab5/ui/main.cpp apps/orcsdr-tab5/main/CMakeLists.txt apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1
  git commit -m "feat(shortwave): wire local dashboard services"
  ```

### Task 7: Build, inspect, flash, and record separate evidence claims

**Files:**
- Modify: `docs/validation/shortwave-phase2-YYYY-MM-DD.md`

**Interfaces:**
- Consumes: the Task 6 production artifact and its immutable SHA-256.
- Produces: a validation ledger that distinguishes source, build, flash, serial, RF, audio, and physical UI evidence.

- [ ] **Step 1: Run source and native build checks**

  Run:

  ```powershell
  git diff --check
  python tools/check_documentation_truth.py
  cmd /c "call C:\Espressif\frameworks\esp-idf-v5.5.4\export.bat >nul && idf.py -B build-native-shortwave build"
  Get-FileHash apps\orcsdr-tab5\build-native-shortwave\orcsdr_tab5.bin -Algorithm SHA256
  ```

  Expected: no whitespace/documentation errors, successful build, recorded
  binary size and SHA-256.

- [ ] **Step 2: Flash only the verified artifact to the authorized Tab5**

  Run:

  ```powershell
  cmd /c "call C:\Espressif\frameworks\esp-idf-v5.5.4\export.bat >nul && idf.py -B build-native-shortwave -p COM17 flash"
  ```

  Expected: bootloader, partition, and application hash verification. Do not
  claim user-setting preservation from this developer flash; M5Burner/NVS is a
  separate limitation and the Shortwave data acceptance is SD-based.

- [ ] **Step 3: Capture serial and physical UI acceptance separately**

  With `idf.py -p COM17 monitor --no-reset`, record boot and each Shortwave
  self-check. Then ask the user to confirm each item independently: direct
  keypad remains actionable; On Air empty/local result state; Hunt start,
  cancel, and candidate tune; memory save/reload after power-cycle; log save,
  reload after power-cycle, CSV/ADI export; header/tab layout; and audible
  reception where an antenna is suitable.

- [ ] **Step 4: Record antenna provenance for radio results**

  The ledger must name dongle, antenna, target frequency, band suitability,
  route, exact displayed frequency, and whether the result is source/build,
  serial, raw spectrum, audible reception, or user-observed touch behavior.
  A 27-inch dipole shortwave result is transition/UI evidence unless its band
  suitability is established; MLA-30+ is the expected HF reception antenna.

- [ ] **Step 5: Commit evidence-only documentation after acceptance**

  ```powershell
  git add docs/validation/shortwave-phase2-*.md
  git commit -m "docs(shortwave): record phase 2 acceptance"
  ```

## Plan Self-Review

- **Spec coverage:** Task 1 covers the direct-tune regression; Task 2 covers
  local schedules/data validation; Task 3 covers SD authority, atomic recovery,
  CSV, and ADIF; Task 4 covers Hunt; Task 5 covers all five tabs; Task 6 holds
  `main.cpp` to adapter responsibility; Task 7 separates build/flash/hardware
  acceptance and antenna provenance.
- **Scope:** No cloud, live schedule download, decoder, radio-driver, DSP, or
  NVS migration work appears in the plan.
- **Type consistency:** `shortwave_model` provides bounded records;
  `shortwave_library` owns their storage/export; `shortwave_hunt` owns only
  scan observations; `shortwave_dashboard` consumes snapshots/actions; and
  `main.cpp` supplies/executes those interfaces.
