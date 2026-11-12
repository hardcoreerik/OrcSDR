# DMR next phase — D1 real-IQ acceptance, acquisition and slot fidelity

**Plan date:** 2026-11-11 (America/Los_Angeles). **Branch:** `feature/dmr-digital-voice-foundation`.
**Authority:** [VISION](VISION.md) · [TRACKING](TRACKING.md) · [D1 integration findings](D1_INTEGRATION_GATE_2026-11-11.md) · [workstream ownership](WORKSTREAMS.md).
**Status:** This is an execution plan, not an acceptance claim or a request to merge `main`. M5GFX/dashboard design remains explicitly deferred.

## Why these are the next three jobs

The native D0 + D1-A 4FSK + D1-B burst/FEC/LC + D1-C IQ bridge now run together on **synthetic 48 kS/s IQ**: [D1 Release/sanitizer CI](https://github.com/hardcoreerik/OrcSDR/actions/runs/43158609068) passed **2/2 CTests in each configuration**. Independent [ETSI and migration CI](https://github.com/hardcoreerik/OrcSDR/actions/runs/43158608897) passed five D0/D1 CTests in Release and sanitizer, 13 ETSI-oracle, 16 acceptance-harness, and 38 P25-preview Python tests. These results are **host tests only**, without real DMR IQ or device hardware.

D1 is **NOT COMPLETE**:
1. **D1-R / P0 evidence:** no pinned real-air DMR IQ has been successfully run through OrcSDR's real `dmr_d1_replay_bridge` and accepted. The harness exists; raw IQ remains in a separate private research checkout and was not available to prior CI.
2. **D1-P / P0 sensitivity:** the frontend drops the first acquired 24-dibit SYNC and preceding 54 payload dibits. The bridge deliberately discards the first burst, risking missed short calls.
3. **D1-S / P0 slot fidelity:** `D1-CACH-01` is runtime-reproduced in production-linked conformance CI: any 7-bit TACT can decode as a codeword/one-bit correction, so an arbitrary 12-dibit preburst prefix may be reported as authentic CACH. The replay CLI currently hides uncorroborated BS slots (`null`), not a complete fix.

**Research resolution:** the rendered ETSI TS 102 361-1 V2.6.1/V2.7.1 Golay(20,8) matrices match; no Golay-generator defect is established. Do **not** alter the correct coefficients to match machine-extracted text.

## Concurrent agents: exclusive folders, one shared branch

| Workstream | Exclusive write scope | Concrete delivery | Acceptance / evidence |
|---|---|---|---|
| **A — D1-R: real-IQ acceptance** | `tests/dmr_replay_acceptance/**` | Execute/check pinned direct-mode IQ through the actual D1 replay binary; record a reproducible verification and playback report, fix harness bugs only | Exact fixture blob/format, source and expected IDs labeled **UPSTREAM** vs **ORCSDR_OBSERVED**; all event evidence integrity-gated; noise/AM negatives; chunk invariance. If private files unavailable, mark **BLOCKED/NOT RUN**, no invented success |
| **B — D1-P: pre-roll acquisition** | `components/orcsdr_dmr/d1_frontend/**` | Add a backward-compatible, bounded, allocation-free pre-lock dibit history/recovery interface with sample indices and explicit epoch/reset semantics | ETSI sync-bearing short/one-burst synthetic vectors, varied phase/CFO/noise, output-capacity backpressure, 1-sample vs large-chunk equivalence, reset and discontinuity; demonstrate genuine first-burst payload, not fabricated data; record SRAM footprint |
| **C — D1-S: CACH slot confidence** | `components/orcsdr_dmr/d1_protocol/**` | Distinguish *possible decoded TACT* from *cadence-validated CACH*, prevent uncorroborated BS slot claims and keep direct-mode slot logic intact | Reproduce and regress zero-prefix `D1-CACH-01`; valid continuous BS alternating slot cadence, corrupted prefixes, chunk splits, late entry, false-slot/no-identity assertions; keep CRC/RS identity checks separate |

All agents may **read** existing D0/D1, `tests/dmr_protocol_conformance/**`, the D1 bridge, ETSI PDFs and private fixture metadata. Only edit their assigned path. No firmware, UI, central CMake, central CI, other agents' directories, REA-Results, `main`, shared phase docs or production P25 profiles.

**Commit rule:** small owned-folder-only commits on `feature/dmr-digital-voice-foundation`. Immediately before pushing run `git pull --rebase origin feature/dmr-digital-voice-foundation`; push that branch only. Never merge or rebase `main`, force-push, or stage files from another folder. Report commit SHA, reproducible test commands, measured output, open protocol gaps.

## Lead / integrator work — starts after agent commits

**Lead-only writes:** `components/orcsdr_dmr/d1_integration/**`, `docs/dmr/**`, and project-specific GitHub Actions definitions. The lead will:

1. Validate source ownership, build and run each new agent's host suites and the existing D1 integration + independent ETSI suites, Release and sanitizers.
2. Wire the **new B pre-roll API** into the D1-C receiver without inventing absent bits, duplicated sync or timeline. Assert a valid **first-burst identity** on a *truly complete* independently constructed single-burst IQ vector; record first-burst latency and static memory.
3. Consume **new C CACH provenance** so BS slot labels are promoted only when supported by an explicitly recorded cadence criterion; avoid regressing the direct-mode TS1/TS2 logic or downstream JSONL schema.
4. Re-run **A real-IQ** acceptance on pinned `dmr-directmode-keyup` and `dmr-directmode-second-ptt`, followed by synthetic noise and AM negatives. Verify source bytes and hashes before use. Tier III captures are exploratory probes **not** proof of Tier III protocol support.
5. Update [TRACKING](TRACKING.md) with **actual** tests, SHA, fixture coverage, decoder counters, runtime/memory metrics and negative findings. Mark D1 host-real-IQ acceptance only if the evidence passes; no software-comparison superiority claim without shared replay conditions.

## Exit criteria: next milestone D1-host (not full device D1)

- At least one pinned, real-air **DMR direct-mode** IQ recording is independently verified and yields an RS/CRC/embedded-LC-integrity-backed source/group identity with expected values, or a clear documented failure/blocker remains and the gate stays open.
- D1-C decoder accepts a **complete single burst** after acquisition using actual measured IQ/symbol history, without claiming CRC from sync. Test it with no second burst and with realistic chunking/offsets.
- No arbitrary preburst 12-dibit prefix may be reported as **cadence-verified CACH**. Honest unknown slot is better than a false TS1/TS2. Valid BS cadence can assign a slot according to documented evidence.
- Release + ASan/UBSan integrated CI and independent ETSI conformance remain green; no unexpected negative-control validated identities. No regressions in source/talkgroup field typing.
- Host allocation and timing budgets measured, not inferred. **ESP32-P4 RF hardware, on-device task/PSRAM/stack budget and physical SD power-loss qualification remain separate F0c/D2 gates.** P25 configuration must remain intact.

## After this phase

- **F0c-hardware:** opt-in ESP-IDF microSD adapter build, bounded slot writes, power interruption and recovery with P25 profile isolation. Hardware-dependent; do not mark done from POSIX/host tests.
- **D2-discovery (later):** location-aware frequency search, receiver ownership, first/last heard and saved discoveries backed by the host-verified F0 journal. Respect the four offline-first/evidence-first vision principles.
- **CFG migration (later):** wire the read-only P25 sidecar and import preview to a non-destructive atomic apply only after all P25 scan/hold/aliases/gains persist without loss.
- **Dashboard:** deferred. Design against the actual 5-inch 1280×720 Tab5, existing P25 M5GFX patterns and correct **OrcSDR orc** branding only after radio data and memory budgets are proven.

## Restart commands (host, from OrcSDR root)

```sh
cmake -S components/orcsdr_dmr/d1_integration -B build/dmr-d1 -DCMAKE_BUILD_TYPE=Release
cmake --build build/dmr-d1 --parallel
ctest --test-dir build/dmr-d1 --output-on-failure

cmake -S tests/dmr_protocol_conformance -B build/dmr-conformance -DCMAKE_BUILD_TYPE=Release
cmake --build build/dmr-conformance --parallel
ctest --test-dir build/dmr-conformance --output-on-failure

python3 -m unittest discover -s tests/dmr_replay_acceptance -p 'test_*.py' -v
# The private REA fixture directory must exist outside OrcSDR:
ROOT=/path/to/REA-Digital-Voice/inbox/fixtures/digital-voice
python3 tests/dmr_replay_acceptance/acceptance.py verify --fixtures-root "$ROOT" --controls --report /tmp/dmr-fixture-verification.json
python3 tests/dmr_replay_acceptance/acceptance.py run --fixtures-root "$ROOT" --controls --bridge-command "$(pwd)/build/dmr-d1/dmr_d1_replay_bridge" --report /tmp/dmr-real-iq-results.json
```

Use external output directories for runtime reports. If an IQ fixture is absent, report the exact missing path and **NOT RUN** rather than generating a synthetic substitute and calling it real RF.
