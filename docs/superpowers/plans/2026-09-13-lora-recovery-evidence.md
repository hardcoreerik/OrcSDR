# LoRa Recovery Evidence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bound and measure adjacent-symbol recovery, then establish its host-comparable weak-signal and live-RF behavior on both antenna captures.

**Architecture:** Preserve the ANSI FFT, linear resampler, one-hypothesis clean path, and single CRC-gated adjacent-symbol candidate. Add measurement and failure guards before running a deterministic two-capture seed matrix, the complete archived corpus, and live LongFast reception.

**Tech Stack:** ESP-IDF 5.5.4, ESP32-P4/Tab5 on COM17, Python `unittest`, archived ORCIQ captures.

**Spec:** `docs/LORA_INDEPENDENT_VALIDATION.md`

## Global Constraints

- Do not add alternate combinations, CFO/timing hypotheses, resampler changes, or FFT passes without new trace evidence.
- Only host-valid vectors count toward host/native comparison.
- Recovery has exactly one candidate budget and runs only after primary payload CRC failure.
- FFT-table and recovery-workspace telemetry must report PSRAM provenance as well as bytes.
- A bounded injected allocation failure must prove no internal-memory fallback.
- Run both MLA-30+ and indoor 915 MHz whip captures with the same seeds.
- Do not open a PR until memory, matrix, full-corpus, and live-RF gates are satisfactory.

---

### Task 1: Memory and recovery guard telemetry

**Files:**
- Modify: `apps/orcsdr-tab5/ui/lora_native_decoder.hpp`
- Modify: `apps/orcsdr-tab5/ui/lora_native_decoder.cpp`
- Modify: `apps/orcsdr-tab5/ui/main.cpp`
- Modify: `tools/replay_lora_orciq.py`
- Test: `tools/test_replay_lora_orciq.py`

**Interfaces:**
- Produces: `orcsdr::lora_native::psram_bytes()` and `RTL_LORA_MEMORY` stage records.
- Produces: `recovery_attempted`, `recovery_symbols_considered`, `recovery_candidates_tested`, `recovery_success`, and `recovery_exhausted` profile fields.

- [x] **Step 1: Write failing parser tests**

```python
def test_memory_and_recovery_fields_are_parsed():
    assert _parse_fields("RTL_LORA_MEMORY stage=after_init psram_bytes=657160")["psram_bytes"] == 657160
```

- [x] **Step 2: Verify the focused test fails because the parser/fields are absent**

Run: `python -m unittest tools/test_replay_lora_orciq.py`

- [x] **Step 3: Add the minimal runtime guards and telemetry**

```cpp
constexpr uint32_t kRecoveryCandidateBudget = 1;
if (!esp_ptr_external_ram(g_scratch.recovery_alternates)) return false;
```

Record internal, DMA, PSRAM, decoder allocation, and decoder-task stack high-water values after initialization, before/after replay, and while live reception is running. Return initialization failure without an internal-memory fallback.

- [x] **Step 4: Build, flash, and verify boot plus stage measurements**

Run: `idf.py -B build-native-hosted3 build`, then flash COM17 and inspect no-reset serial evidence.

- [x] **Step 5: Commit and push the diagnostic checkpoint**

```text
test(lora): guard decoder memory budget
```

### Task 2: Complete deterministic corpus regression

**Files:**
- Update: `docs/LORA_INDEPENDENT_VALIDATION.md`

**Interfaces:**
- Consumes: existing corpus manifest and replay command.
- Produces: A/B/C/D/unknown classification and clean-path recovery counters.

- [x] **Step 1: Replay every archived corpus entry on COM17**

Require all prior positives, zero Class B, all known negatives, zero Class D, and zero unknown.

- [x] **Step 2: Verify clean positives use one hypothesis and zero recovery where unnecessary**

- [x] **Step 3: Record results, commit, and push**

- [x] **Step 4: Repeat the full corpus after the recovery ceiling change**

```text
docs(lora): verify recovery corpus regression
```

### Task 3: Two-capture weak-signal seed matrix

**Files:**
- Create: `tools/run_lora_native_matrix.py`
- Test: `tools/test_run_lora_native_matrix.py`
- Update: `docs/LORA_INDEPENDENT_VALIDATION.md`

**Interfaces:**
- Consumes: MLA-30+ and whip controlled ORCIQ captures, identical deterministic seeds, and replay JSON.
- Produces: one row per capture/seed/SNR with host validity, native result, symbol/alternate evidence, hypotheses, FFTs, runtime, and packet identity.

- [x] **Step 1: Write a failing aggregation test with literal expected rows**

Run: `python -m unittest tools/test_run_lora_native_matrix.py`

- [x] **Step 2: Implement the smallest sequential matrix runner**

Use -21, -22, and -23 dB for both captures. Run -24 dB only as a labeled non-comparative observation when the host oracle fails.

- [x] **Step 3: Run the matrix and classify every host-pass/native-fail row A-E**

- [x] **Step 4: Commit and push the evidence checkpoint without changing recovery**

```text
test(lora): map weak-signal recovery matrix
```

### Task 4: Live LongFast receive validation

**Files:**
- Update: `docs/LORA_INDEPENDENT_VALIDATION.md`

**Interfaces:**
- Consumes: satisfactory memory, corpus, and matrix checkpoints.
- Produces: trigger, preamble, CRC, runtime, recovery, zero-preamble, drop/blind-time, heap, and stack observations.

- [x] **Step 1: Restore automatic LongFast scanning at 906.875 MHz**

- [x] **Step 2: Observe an extended receive interval on the connected MLA-30+ antenna**

- [x] **Step 3: Repeat the same interval after the user swaps to the indoor 915 MHz whip**

- [x] **Step 4: Record evidence, restore the requested antenna state, commit, and push**

```text
docs(lora): record live recovery soak
```

- [ ] **Step 5: Review readiness without opening or merging a PR**

### Task 5: Close the decoder stack gate

**Files:**
- Modify: `apps/orcsdr-tab5/ui/lora_native_decoder.hpp`
- Modify: `apps/orcsdr-tab5/ui/lora_native_decoder.cpp`
- Modify: `apps/orcsdr-tab5/ui/main.cpp`
- Update: `docs/LORA_INDEPENDENT_VALIDATION.md`

- [x] **Step 1: Verify the ESP-IDF 5.5.4 / ESP32-P4 high-water unit**

- [x] **Step 2: Measure the compiled nested decoder frames**

- [x] **Step 3: Move task-owned packet/statistics storage to fixed PSRAM without changing the task stack size**

- [x] **Step 4: Build, flash, and run clean, weak-recovery, and automatic-live checks**

- [x] **Step 5: Commit and push the isolated stack checkpoint**
