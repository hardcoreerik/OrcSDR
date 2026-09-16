# V3c IQ Asymmetry Investigation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce raw-IQ and driver-state evidence that identifies the first V3c transition associated with the intermittent half-spectrum state, without changing receiver behavior.

**Architecture:** Reuse OrcSDR's pre-DSP PSRAM CU8 recorder and authenticated binary retrieval transport. Analyze captures offline and correlate their numerical asymmetry with one concise state line from an isolated diagnostic build of the pinned driver.

**Tech Stack:** ESP-IDF/C++20, PowerShell serial tooling, Python 3, NumPy, Matplotlib

**Spec:** `docs/superpowers/specs/2026-09-15-v3c-iq-asymmetry-investigation-design.md`

## Global Constraints

- Driver under test is exactly `e1ca40e04f8140245d56837cd149bf901f771441`, reporting `0.8.0-rc2`.
- Raw bytes are interleaved unsigned CU8 captured before FFT and demodulation.
- No DSP, FFT, waterfall, filter, UI, or driver behavior correction is allowed during evidence collection.
- No push, merge, tag, publication, or modification of existing dirty checkouts.
- Do not define a pass threshold until V3c GOOD, V3c BAD, and V4 baseline captures are measured.

---

### Task 1: Host IQ analyzer

**Files:**
- Create: `apps/orcsdr-tab5/tools/analyze_rtl_iq.py`
- Create: `apps/orcsdr-tab5/tools/test_analyze_rtl_iq.py`

**Interfaces:**
- Consumes: raw `I0 Q0 I1 Q1 ...` bytes and sample rate
- Produces: `analyze_capture(path, rate, fft_size, dc_guard, edge_guard)` metrics plus PNG/report/CSV CLI artifacts

- [ ] **Step 1: Write a failing synthetic test**

Create deterministic balanced complex noise and a one-sided analytic signal. Assert that DC removal makes both channel means approximately zero in analysis, balanced noise has a small half delta, and the one-sided fixture has a materially larger signed half delta.

- [ ] **Step 2: Verify the test fails for the missing analyzer**

Run: `python apps/orcsdr-tab5/tools/test_analyze_rtl_iq.py`

Expected: import failure because `analyze_rtl_iq.py` does not exist.

- [ ] **Step 3: Implement the minimum analyzer**

Use NumPy for CU8 conversion, independent I/Q DC removal, Hann-windowed complex FFT frames, FFT shift, and linear-power averaging. Use Matplotlib only for the two PNG outputs. Keep metric computation callable without plotting.

- [ ] **Step 4: Verify analyzer behavior and CLI artifacts**

Run the synthetic test, then run the CLI against its generated fixture with `--rate 2400000 --csv` and verify both PNGs, report, and CSV are non-empty.

- [ ] **Step 5: Commit the analyzer**

Commit message: `test: add raw IQ asymmetry analyzer`

### Task 2: One-second diagnostic CU8 capture

**Files:**
- Modify: `apps/orcsdr-tab5/ui/main.cpp`
- Modify: `apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1`

**Interfaces:**
- Consumes: authenticated `RTL_IQ_DIAG_START <transition>` while a radio stream is running
- Produces: existing `RTL_IQ_GET_*` byte stream plus JSON metadata beside the host `.cu8` file

- [ ] **Step 1: Add a failing parser/self-check for diagnostic metadata**

Add a host parser for the diagnostic start/status response and exercise it from `-SelfCheck` with a literal line containing a one-second byte count, bounded transition label, sequence, frequency, and rate. This catches malformed or incomplete metadata without asserting on source text.

- [ ] **Step 2: Verify the focused self-check fails**

Run: `apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1 -SelfCheck`

Expected: failure because the diagnostic response parser is absent.

- [ ] **Step 3: Generalize the existing recorder minimally**

Add `diagnostic` to `IqCaptureKind`, use a one-second byte limit for it, append its bytes in the shared DSP task immediately before `spectrum_offer_iq_snapshot`, and add authenticated start/status handling. Do not add another buffer or recorder.

- [ ] **Step 4: Add host capture orchestration**

Add an `-IqDiagnostic` mode to the existing regression script. Reuse its authentication, tune/status parsing, and serial session. Retrieve with `RTL_IQ_GET_*`, verify byte count and SHA-256, and write `.cu8` plus JSON metadata.

- [ ] **Step 5: Verify focused host checks**

Run the PowerShell self-check and `git diff --check`.

- [ ] **Step 6: Commit the capture path**

Commit message: `test: capture pre-DSP IQ diagnostics`

### Task 3: V3c driver state line

**Files:**
- Modify in isolated driver worktree: `src/esp_rtl_sdr.cpp`
- Modify in isolated driver worktree: `tests/host/test_profiles.cpp`

**Interfaces:**
- Consumes: successful cold start or hot-retune state
- Produces: one `V3C_RF_STATE` log line with owned configuration fields and transition name

- [ ] **Step 1: Add a failing host assertion for transition classification**

Cover cold normal, normal-to-normal, normal-to-direct, direct-to-direct, and direct-to-normal transition names using existing profile helpers.

- [ ] **Step 2: Verify driver host tests fail**

Run: `tests/scripts/run_host_tests.ps1`

Expected: failure because the transition classifier/state formatter is absent.

- [ ] **Step 3: Add the minimum state representation and log**

Report the V3 profile name, requested RF, active rate, derived route, PLL IF, restored demod IF, direct NCO if applicable, gain mode/value, RTL AGC state, and transition. Do not write or reinterpret registers.

- [ ] **Step 4: Verify driver host tests**

Run: `tests/scripts/run_host_tests.ps1`

- [ ] **Step 5: Commit diagnostic-only driver instrumentation locally**

Commit message: `test: report V3c RF transition state`

### Task 4: Diagnostic firmware integration

**Files:**
- Leave unchanged: `apps/orcsdr-tab5/main/idf_component.yml`
- Leave unchanged: `apps/orcsdr-tab5/dependencies.lock`
- Overlay for diagnostic build only: `apps/orcsdr-tab5/managed_components/esp_rtl_sdr/src/esp_rtl_sdr.cpp`

**Interfaces:**
- Consumes: local isolated driver instrumentation commit
- Produces: a diagnostic-only firmware artifact traceable to both source commits

- [ ] **Step 1: Overlay the diagnostic driver source in the generated build component**

Copy the committed diagnostic-only driver diff into the ignored generated component after verifying its base is exactly `e1ca40e`. Do not change the production pin or lockfile.

- [ ] **Step 2: Run targeted checks**

Run the analyzer test, OrcSDR UI self-check, driver host tests, and `git diff --check`.

- [ ] **Step 3: Build native Tab5 firmware**

Run: `idf.py -B build-native-hosted3 build`

- [ ] **Step 4: Record and flash artifact identity**

Record SHA-256 and byte size, then flash COM17. Verify flash hashes separately from runtime behavior.

### Task 5: V3c and V4 reproduction matrix

**Files:**
- Create under ignored evidence directory: `artifacts/v3c-iq-investigation/<capture>.cu8`
- Create beside each capture: `<capture>.json`, spectrum PNG, waterfall PNG, report TXT, optional CSV
- Create: `docs/testing/v3c-iq-asymmetry-investigation-2026-09-15.md`

**Interfaces:**
- Consumes: diagnostic firmware, V3c/V4 hardware, antenna provenance
- Produces: objective GOOD/BAD transition table and evidence-backed hypothesis

- [ ] **Step 1: Establish V3c cold baseline**

Power-cycle, go directly to FM 99.1 MHz, capture one second, analyze it, and record physical spectrum/audio/RDS observations separately.

- [ ] **Step 2: Run normal-tuner transitions**

Capture after 99.1 -> 96.1 -> 99.1 and after a second normal-VHF round trip.

- [ ] **Step 3: Run direct-sampling round trips**

Capture after 99.1 -> 10 MHz -> 99.1 and 99.1 -> 147.3 kHz -> 99.1.

- [ ] **Step 4: Run lifecycle transitions**

Capture after radio close/reopen and after V3c USB detach/reattach.

- [ ] **Step 5: Run V4 control**

Use the same firmware and antenna where practical; record antenna and band suitability explicitly.

- [ ] **Step 6: Compare and stop before fixing**

Identify the first GOOD-to-BAD and any BAD-to-GOOD transition. If intended state is identical, add only the smallest safe read-only state probe and repeat the discriminating pair.

- [ ] **Step 7: Deliver the investigation report**

Report dependency identities, cold/direct call sequences, capture implementation, example images, numerical report, reproduction table, strongest hypothesis, exact proposed driver correction, and regression tests. Request explicit approval before implementing the correction.
