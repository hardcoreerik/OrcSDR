# D1 public real-IQ execution verdict

**Overall:** `BLOCKED — NATIVE REAL-IQ REPLAY NOT RUN`.

**Scope:** `tests/dmr_real_iq_validation/**`, `hardcoreerik/OrcSDR`, branch `feature/dmr-digital-voice-foundation`. Inspected pre-delivery HEAD: `3665e17ec1f29c715310f1a244fcb65473432e11`. The real native D1 replay bridge exists in `components/orcsdr_dmr/d1_integration/tools/d1_replay_bridge.cpp` and depends on actual D0/D1 front-end and protocol sources. It was **not recompiled or exercised on real IQ** in this session.

## What passed

- **4/4 source objects verified:** direct key-up, direct second PTT, Tier III control and voice terminator, source pinned to GopherTrunk commit `f06bfff493f47fdeb69807c8b08b2b32a253839f`. Recalculated Git blob SHA-1 and raw SHA-256 directly from retrieved binary objects, matching all four manifest blob pins and byte sizes. Values: `REAL_IQ_RESULTS.md`.
- **10/10 new local unit tests passed:** deterministic `replay_matrix.py` behavior (atomic downloader, source hash rejection, external-output-only protection, chunk fingerprints, conservative identity/noise outcomes). These are tests of the *orchestration logic*, not a substitute DMR decoder.
- The production D1 CMake bridge, independent conformance tests, and existing replay harness were inspected read-only through the connected repository; previously published CI results are historical, not this execution's test outputs.

## What is unproven

- **0/4 real-air recordings replayed through native D1 in this run**. No recovered source IDs, color codes, talkgroups or slots can be reported as OrcSDR observations.
- **0 measured native integrity results, 0 measured gap reacquisitions, 0 measured CPU/RAM/CFO/timing results.** These are unavailable measurements, not zero successes or failures.
- Pinned synthetic noise and AM controls are held in a separate private fixture corpus and were not accessed as native byte streams.
- No Release/sanitizer CMake/CTest executed for current branch in native sandbox, so host build and sanitizer gate cannot be marked passed *for this run*.
- D1-P first-burst integration, D1-S CACH slot verification, direct-mode gap handling and eventual ESP32-P4 in-device acceptance remain open.

## Next exact action

Run the **Linux, WSL or Windows commands in `README.md`** on a machine with public-source access. The new `replay_matrix.py fetch` downloads the four public files directly to an external directory and validates the original manifest pins again. `replay_matrix.py run` invokes the existing host executable across input chunks 1, 7, 257, 1024, 4096 and writes 20 per-run reports + logs and one final summary outside the checkout. Failure and data-unavailability states are explicit. Optional authenticated private controls can be added later without modifying source files.

Do not call this a D1 real-air acceptance PASS until at least the direct-mode A fixture produces integrity-validated source/talkgroup/CC results and chunk-invariant ordered event evidence. If metadata does not match upstream assertions, preserve the mismatch as a genuine engineering finding and diagnose the pipeline; do not rewrite the IQ pin or upstream expectations to manufacture a pass.
