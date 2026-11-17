# Digital voice receive-side module boundary and main.cpp refactor handoff

**Canonical work branch:** `feature/dmr-digital-voice-foundation`.  
**Concurrent refactor (read-only):** `claude/orcsdr-main-cpp-refactor-dcac06`.  
**Scope:** DMR, D-STAR, System Fusion and future receive-only protocol detection. **Not** Pluto or TX.

## Invariants

1. **Do not add DMR, D-STAR, YSF or NXDN demodulation, protocol parsing, IQ queueing, FEC, CRC or RF acquisition algorithms to `apps/orcsdr-tab5/ui/main.cpp`.** This is a hard architectural boundary while the separate main.cpp refactor proceeds.
2. Existing portable receiver implementations remain isolated in `components/orcsdr_dmr/**` and `components/orcsdr_digital_voice_rx/**`. Their synthetic/real-IQ CLI test tools may build without M5GFX, ESP-IDF, live USB or dashboard code.
3. Reuse the **single existing radio/USB owner** and a shared, channelized 48 kS/s complex-IQ stream. A protocol attachment must be optional, read-only, bounded and nonblocking. It never retunes the radio or takes ownership of P25 private state.
4. Host-validated metadata is **not** a claim that D-STAR/YSF consume raw RF or that the Tab5 has digital voice. FEC/CRC-qualified identities, unsupported messages, unknown slots, real-air captures, P4 CPU/heap and vocoder licensing each have distinct evidence gates.
5. Avoid duplicate service registration and new dashboard IDs until the concurrently refactored firmware service/routing boundaries are verified in the refactor branch. Preserve append-only persisted registry IDs when a new dashboard is eventually approved.
6. Keep PlutoSDR and every TX path in separate repositories/branches. This work changes only receive-side protocol logic, host validation, and portable contracts.

## Refactor coordination

The in-progress refactor branch `claude/orcsdr-main-cpp-refactor-dcac06` already changes the Tab5 application's `main.cpp`, `apps/orcsdr-tab5/main/CMakeLists.txt`, and nearby modules. Do **not** modify that branch from Digital Voice work. Before an on-device integration PR:

- Inspect the current refactor branch's final service/radio adapter APIs, not older line offsets into `main.cpp`.
- Put any new firmware orchestration in a small, independently owned `digital_voice_*` adapter (named to avoid collisions with existing refactor modules), with only the minimum application registration hook approved in the refactor review.
- Connect to a single producer-owned **48 kS/s channelized IQ tap**; define sample ownership, retune epoch, discontinuity, truncation and drop counters explicitly.
- Keep DSP and decoded metadata structs entirely outside `main.cpp`.
- Run host protocol/real-IQ gates and Tab5 P25, radio-owner, USB and UI regressions on a *combined integration candidate* before merging.
- Do not merge `main` or the refactor branch merely to advance host DMR/D-STAR/YSF tests.

## Current host-only acceptance correction

The two pinned DMR MS-sync recordings do not independently identify TS1 or TS2. The acceptance harness must not manufacture time slots. Exclusive direct-slot sync plus checked bursts retains stronger slot evidence requirements. `real_iq_gate.py` retains upstream capture disagreements as reviews; `d1n_matrix.py` independently judges verified native identity/integrity evidence, preserving those upstream reviews in separate fields. This is a **host test change**, not receiver DSP change.

See `docs/dmr/D1N_CLOUD_NATIVE_40_RUN_FINDINGS_2026-11-13.md` for the measured 40-run failure that motivated the correction. Any final PASS claim requires a fresh native CI run on the revised branch commit. No RF hardware, D-STAR/YSF IQ frontends or firmware paths were modified as part of this fix.
