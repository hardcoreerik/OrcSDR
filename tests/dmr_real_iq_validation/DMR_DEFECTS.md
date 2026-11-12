# DMR real-IQ defect ledger

This ledger **does not classify unrun real-IQ tests as decoder failures**. Source integrity was established independently, but the real-IQ/native-code interface was unavailable here. Severity below reflects potential product impact; reproduction status distinguishes existing source/CI evidence from this run's evidence.

## D1-CACH-01 — unverified CACH can imply a false time slot

- **Status:** `CONFIRMED — previously reproduced in independent production-linked conformance CI`; **this run: NOT REPRODUCED**. Earlier source proof: `tests/dmr_protocol_conformance/FINDINGS.md`; related CI from tracker: `43158073956`.
- **Severity:** moderate/high for slot identification and any persistent discovery/last-heard system that mistakenly treats CACH as authoritative.
- **Source mechanism:** a 7-bit Hamming(7,4,3) TACT word has covering radius 1, so a 12-dibit plausible prefix can decode as CACH without authenticating that the prefix is CACH. A zero prefix before a BS sync can yield an apparent TS1 assignment. A decoded candidate is not a cadence-validated CACH.
- **Reproduction:** `cmake -S tests/dmr_protocol_conformance -B build/dmr-conformance -DCMAKE_BUILD_TYPE=Release && cmake --build build/dmr-conformance --parallel && ctest --test-dir build/dmr-conformance --output-on-failure`; see `FINDINGS.md` for the exact 12-dibit prefix and 132-dibit sync-bearing stream.
- **Expected:** unknown/unverified slot without burst cadence.
- **Previously observed:** protocol layer promoted a plausible TACT into `slot_from_cach=true`; existing D1-C replay bridge hides uncorroborated CACH-derived BS slots as `null`. That bridge mitigation is **not** full CACH cadence validation.
- **Recommended correction (other agent's write scope):** use evidence-aware CACH presence and multi-burst cadence before promoting a BS slot, preserve direct-mode sync assignments; rerun full conformance and real-IQ tests.
- **In this run's four real captures:** `NOT ASSESSED`.

## D1-P — acquisition loses the first detected burst

- **Status:** `KNOWN DESIGN LIMITATION` in the documented D1 bridge at this inspected revision; **real-IQ impact NOT ASSESSED**. The separate D1-P agent may have newer unintegrated work.
- **Severity:** high for short direct-mode key-ups and burst-gap reception.
- **Source mechanism:** initial 24-dibit sync and earlier 54 payload dibits are not available to the integrator, so it intentionally omits the acquisition burst rather than inventing a payload.
- **Reproduction:** existing bridge Release `ctest`; for actual impact, use the A/B replay matrix and compare first lock against first integrity-validated identity in emitted events.
- **Expected:** a complete first burst can be recovered only if real buffered dibits and timestamps exist.
- **Previously observed:** synthetic CI dropped the first acquisition burst and decoded subsequent ones.
- **Recommended correction:** integrate the D1-P pre-roll API when independently validated; no synthetic start-of-burst insertion. Preserve reset, gaps and timestamps.
- **This run:** `NOT MEASURED`.

## DMR real-air gap/CFO/truncation sensitivity

- **Status:** `HYPOTHESIS — NEEDS NATIVE REPLAY`.
- **Severity:** unknown until measurement.
- **Fixture:** A `dmr-directmode-keyup`, B `dmr-directmode-second-ptt`; pin and byte size in `REAL_IQ_RESULTS.md`.
- **Reproduction:** `python3 tests/dmr_real_iq_validation/replay_matrix.py run --bridge /absolute/path/to/dmr_d1_replay_bridge`, with the four downloaded files under external fixture root.
- **Expected:** genuine 4FSK sync and integrity-proven metadata where present, across acquisition and true burst gaps, with no invented identities.
- **Observed:** **NOT MEASURED**, so **NO CONFIRMED REAL-IQ DECODER DEFECT** from this run.
- **Recommended investigation if run fails:** check energy/clipping, CFO, timing lock, actual dibits, sync, Golay, BPTC, CRC/RS and final LC integrity in that order; record exact logs/timestamps and then assign a reproduced defect ID.

## Acquisition environment blocker

- **Status:** `BLOCKED — native real-IQ execution unavailable` (environment limitation, **not** a proven decoder defect).
- **Evidence:** direct `git ls-remote`/`curl` from native execution sandbox failed because GitHub network/DNS was unavailable; connected GitHub source API provided authentic binary objects but not a native mounted fixture. No C++ source tree was mounted or cloned into the native sandbox.
- **Action:** execute `README.md` on a host with accessible downloads and compiler, then update metrics based on `real_iq_matrix_summary.json` and individual logs. Do not turn source-verification success into decode acceptance.
