# D1-C — DMR 4FSK to Tier II burst integration (host first)

**Scope:** `components/orcsdr_dmr/d1_integration/**` only. This is a fixed-memory host-oriented bridge between the independently authored D1-A 48 kS/s 4FSK receiver and D1-B hard-dibit Tier II protocol decoder. No firmware, M5GFX, tuner, P25, SD or DX discovery integration is claimed.

## Why the bridge exists

`d1_frontend::Frontend` detects a 24-dibit DMR synchronization pattern but **does not emit that acquisition sync or the earlier 54 payload dibits**. `d1_protocol::Decoder` expects complete 132-dibit bursts with sync in the middle. Feeding output directly and claiming the *first* acquired burst works would be false. This bridge deliberately accepts that the initial acquired burst is unavailable; after a continuous lock, subsequent complete transmitted bursts (including the next sync) can reach the protocol decoder intact. It never fabricates an acquisition packet, grants proof from sync alone, or invents source/talkgroup IDs.

For field DX and short key-up calls, first-burst loss is a serious limitation. Pre-roll acquisition, with enough buffered demodulated history to reproduce at least the missing 54 payload dibits plus sync (and any preceding CACH), is a **separate mandatory acceptance gate** before broad release claims.

## Contract

- `orcsdr::dmr::d1::integration::Receiver` accepts caller-owned `ComplexSample` arrays at exactly 48,000 sample/s. It holds a `Frontend`, a `Decoder` and a fixed 256-entry sample-index map; there is no heap allocation or queue in the integration core. The output is a synchronous callback with a copy of the protocol `Event`, an optional *relative* first-dibit sample index and an explicit discontinuity epoch. That index is not UTC or calibrated RF time.
- Input is processed in at-most-eight-sample steps with a single-symbol output buffer, preserving frontend backpressure and avoiding mixed old/new lock epochs in a batch. The integrator resets protocol state when frontend locks, loses lock or receives an explicit `discontinuity()`. It does not stitch bursts across gaps or retunes. Explicit reset also clears the epoch and statistics.
- `Receiver::stats()` separates observed acquisition counts, initial bursts unavailable, forwarded dibits, protocol events, and integrity-validated identities. Neither the acquisition count nor decoder correlation may be represented as a confirmed site or speech decode. An event is an identity only when `event.integrity_valid` and `event.evidence == identity_valid`.
- The integration is a *single 12.5 kHz channel*: observing both DMR slots in one carrier is distinct from following two unrelated RF frequencies.
- Callback must not recursively call `Receiver::process()`, block the SDR task, or retain transient pointers (the event itself is copied). Frame/event ownership should later pass to bounded queues outside the DSP callback.

## Reproduce host tests using the real D0, D1-A and D1-B sources

From the OrcSDR repository root:

```bash
cmake -S components/orcsdr_dmr/d1_integration -B build/dmr-d1-integration -DCMAKE_BUILD_TYPE=Release
cmake --build build/dmr-d1-integration --parallel
ctest --test-dir build/dmr-d1-integration --output-on-failure

cmake -S components/orcsdr_dmr/d1_integration -B build/dmr-d1-integration-asan \
  -DCMAKE_BUILD_TYPE=Debug -DDMR_D1_INTEGRATION_SANITIZE=ON
cmake --build build/dmr-d1-integration-asan --parallel
ctest --test-dir build/dmr-d1-integration-asan --output-on-failure
```

`test_d1_bridge.cpp` constructs valid, independently generated ETSI-style full-LC frames with CACH alternating TS1 and TS2, BPTC, Golay, RS-parity and repeated BS data SYNC. It synthesizes SRRC-filtered four-level instantaneous frequency into complex IQ at 48 kS/s. The test feeds the **real** frontend and decoder, expects integrity-gated group/source metadata after the acquisition burst, compares whole-buffer and single-sample chunking, verifies discontinuity and lock reacquisition, rejects null-input mistakes and checks silent IQ for false events. This is synthetic waveform evidence only; controlled real-air IQ acceptance is separate.

**Local development verification on 2026-11-11:** integration `.cpp` and test `.cpp` each compile with GCC C++17 and `-Wall -Wextra -Wpedantic -Werror` against minimal API-compatible declarations. This is a **syntax/API smoke check only**; it does *not* run or link the real D0/D1 implementations. Do not mark the full CTest suite `PASS` until CI or an independent checked-out build executes it. This limitation is intentional and recorded.

**Integrated CI verification (2026-11-11 PST / 2026-11-12 UTC):** [GitHub Actions run 43156539511](https://github.com/hardcoreerik/OrcSDR/actions/runs/43156539511) compiled the **actual** D0, D1-A, D1-B and D1-C sources using strict warnings-as-errors in separate Release and ASan/UBSan jobs. **Both jobs passed (1/1 CTest each)**. The synthetic full-LC stream acquired one 4FSK sync, decoded **eight complete bursts** and emitted **eight integrity-validated radio-identity events** after the intentionally missed acquisition burst. Both chunk-invariance and loss/discontinuity tests passed. This is **synthetic host evidence only**, not measured RF, commercial-vendor parity, calibrated decode sensitivity, device performance, or first-burst recovery.

## Real-IQ acceptance CLI (host only)

The `dmr_d1_replay_bridge` executable is now linked to the actual D0/D1 receiver. It accepts a **continuous** little-endian CF32 IQ stream at exactly 48 kS/s on stdin and emits `orcsdr-dmr-d1-bridge-event-v1` JSONL events to stdout. Its event sample offset is the **sample at which a full protocol event is emitted** (end-of-burst processing), not a fabricated acquisition-burst start timestamp. It never writes IQ or records private audio to disk.

```bash
cmake -S components/orcsdr_dmr/d1_integration -B build/dmr-d1-integration -DCMAKE_BUILD_TYPE=Release
cmake --build build/dmr-d1-integration --parallel
# The private IQ source is in a separate authorized local REA-Results checkout:
ROOT=/path/to/REA-Digital-Voice/inbox/fixtures/digital-voice
python3 tests/dmr_replay_acceptance/acceptance.py verify \
  --fixtures-root "$ROOT" --controls \
  --report /tmp/orcsdr-dmr-source-verification.json
python3 tests/dmr_replay_acceptance/acceptance.py run \
  --fixtures-root "$ROOT" --controls \
  --bridge-command "$(pwd)/build/dmr-d1-integration/dmr_d1_replay_bridge" \
  --chunk-samples 257 --timeout 30 \
  --report /tmp/orcsdr-dmr-real-iq-acceptance.json
```

**Measured host CI:** [D1 bridge run 43158609068](https://github.com/hardcoreerik/OrcSDR/actions/runs/43158609068) passed **2/2 CTest targets** in both Release and AddressSanitizer/UBSan builds: actual synthetic-IQ→metadata plus CLI exact-block, partial-block, truncated-sample, malformed-argument and CW negative tests. [Independent verification run 43158608897](https://github.com/hardcoreerik/OrcSDR/actions/runs/43158608897) also passed its Release/sanitizer protocol and Python migration/reference suites. **No actual REA IQ file was available to these GitHub CI runners.** These checks prove the host adapter compiles and correctly reports negative/malformed input; they do not prove recorded-IQ metadata recovery.

**Identity/provenance guard:** JSONL voice-LC group calls include source and talkgroup only after integrity and identity validation. Private-call destinations are not mislabeled talkgroups. A BS time slot inferred from a single CACH-like prefix is emitted as `null` because the current D1 protocol cannot prove CACH cadence; this intentionally sacrifices apparent slot precision to avoid false confidence until the D1-CACH-01 defect is fixed. The remaining D1 decoder output is unchanged. Invalid, partial and empty streams fail with a nonzero status. The `--fixture-id` argument is validated, not interpreted as proof of ground truth.

## Open gates

1. **Completed for synthetic host vectors:** Release and ASan/UBSan integration jobs on the real D0/D1 sources passed. Repeat independently after any decoder or bridge source change.
2. Verify first-burst behavior and measure how many successive valid DMR bursts are needed before complete metadata is available. Implement and test bounded pre-roll for short calls, if feasible within ESP32-P4 RAM and CPU budgets.
3. Replay pinned private DMR direct-mode IQ from REA-Results, then additional Tier II frames and negative controls; report actual source, sample format and ground truth. Do not check in private IQ.
4. **Resolved by independent rendered-ETSI review:** the printed V2.6.1 and V2.7.1 Golay parity rows match the implementation; the earlier difference was text extraction. Maintain live RF fixture parity as a separate quality gate. **Open defect D1-CACH-01:** D1 can infer a BS slot from an unverified CACH-like prefix; verify burst cadence before promoting slot confidence.
5. Measure CPU, heap, internal RAM/PSRAM, stack and drop counters on an actual ESP32-P4 before firmware integration. No production P25 source, current radio owner, or display route was modified here.

Current project truth: `docs/dmr/TRACKING.md`, `docs/dmr/D1_INTEGRATION_GATE_2026-11-11.md`.
