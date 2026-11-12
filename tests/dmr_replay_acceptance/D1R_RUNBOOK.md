# Phase D1-R — native decoder real-IQ acceptance

The D1-R gate is **an offline measurement procedure**, not a substitute DMR
receiver. The receiver is the existing C++ `dmr_d1_replay_bridge` from
`components/orcsdr_dmr/d1_integration`. This directory does not change the
frontend, protocol decoder, firmware, radio owner, UI, or private captures.

## Inputs and trust

The authoritative fixture metadata is read-only
`tests/digital_voice_replay/manifest.json`, which pins the private
`hardcoreerik/REA-Results` branch
`chatgpt/digital-voice-rsa-rea-2026-11-01`, directory
`inbox/fixtures/digital-voice/`, and GopherTrunk source commit
`f06bfff493f47fdeb69807c8b08b2b32a253839f`.

In this order, preflight verifies the following **actual local file bytes**:

| Test ID | Expected format | Byte count | Expected complex samples | Git blob SHA-1 pin |
|---|---|---:|---:|---|
| `dmr-directmode-keyup` | CS16 LE / 48 kS/s | 345600 | 86400 | `e305eac29985ff647b8aba0434fee8ab7db81f15` |
| `dmr-directmode-second-ptt` | CS16 LE / 48 kS/s | 326404 | 81601 | `b7d91747030e55f68637edb2198476047191d84a` |
| `auto-negative-noise` | CS16 LE / 48 kS/s | 384000 | 96000 | `3d872e2afb5aa8759520657c2c1c25fbd03acab7` |
| `auto-negative-am` | CS16 LE / 48 kS/s | 384000 | 96000 | `f8873579fe47229866fb2b1576726a22bfc4aa5d` |

All four files must exist and hash successfully **before any native decoder is
invoked**. Their Git blob SHA-1 includes Git's `blob <length>\0` prefix.
Local SHA-256 is independently calculated for all four. Only the two synthetic
controls have SHA-256 pins in the source manifest; for the two real-air
captures, local SHA-256 is *measured*, not a match to a source pin.

The private files must live outside the OrcSDR checkout and must **never** be
committed, uploaded, printed, or embedded in a report. The gate only reports
small metrics and hashes, not samples. The native bridge is not given a path to
an IQ file: `acceptance.run_bridge` feeds hash-verified 48 kS/s CF32 converted
samples to its standard input and validates its JSONL output. The original
IQ is also checked again while streaming.

## Repeatable commands

**1. Check out and mount the private data separately (authorized access only).**

```bash
# From a separate REA-Results checkout, *not* inside OrcSDR:
git -C /path/to/REA-Results fetch origin chatgpt/digital-voice-rsa-rea-2026-11-01
git -C /path/to/REA-Results worktree add /path/to/REA-DMR \
  origin/chatgpt/digital-voice-rsa-rea-2026-11-01
ROOT=/path/to/REA-DMR/inbox/fixtures/digital-voice
```

**2. Build the existing actual D0/D1 frontend/protocol/integration executable.**

```bash
# From OrcSDR repository root:
cmake -S components/orcsdr_dmr/d1_integration \
  -B build/dmr-d1-integration -DCMAKE_BUILD_TYPE=Release
cmake --build build/dmr-d1-integration --parallel
ctest --test-dir build/dmr-d1-integration --output-on-failure
```

**3. Run host harness tests.** No private IQ needed. An additional parser
fingerprint test requires the checked-out `acceptance.py`, which exists in this
repository; it is skipped only if the new tests are copied alone.

```bash
python3 -m unittest discover -s tests/dmr_replay_acceptance \
  -p 'test_*.py' -v
```

**4. Run real-IQ D1-R and capture JSON outside the repository.**

```bash
python3 tests/dmr_replay_acceptance/real_iq_gate.py \
  --fixtures-root "$ROOT" \
  --bridge "$(pwd)/build/dmr-d1-integration/dmr_d1_replay_bridge" \
  --chunk-sizes 257 4096 \
  --timeout 45 \
  --report /tmp/orcsdr-dmr-d1r-acceptance.json
```

Windows PowerShell equivalent after building a host executable:

```powershell
$root = 'D:\private\REA-DMR\inbox\fixtures\digital-voice'
py -3 tests/dmr_replay_acceptance/real_iq_gate.py `
  --fixtures-root $root `
  --bridge '.\build\dmr-d1-integration\Release\dmr_d1_replay_bridge.exe' `
  --chunk-sizes 257 4096 `
  --report "$env:TEMP\orcsdr-dmr-d1r-acceptance.json"
```

Exit codes: `0` = the defined harness checks passed, with upstream truth still
unverified; `2` = failure or review required; `3` = **BLOCKED** (fixtures,
binary, or preflight unavailable). The report is written with exclusive creation:
it will never overwrite an earlier run. Use a new output filename per test.

## What the gate compares

- **Integrity:** Full local file length, source Git blob SHA-1 and any pinned
  SHA-256, plus full streaming SHA-256 and consumed sample count.
- **Evidence:** Direct-mode synchronization patterns and counts, FEC, CRC-16,
  RS(12,9), embedded LC results, checked color codes and attributed time slots.
- **Identities:** Integrity-gated source IDs and group/talkgroup IDs only. D1
  private-call destinations must not be mislabeled as talkgroups. A sync or
  unverified Golay/CACH claim is insufficient identity evidence.
- **False identities:** Both *synthetic non-DMR* controls must have **zero**
  integrity-gated source/talkgroup reports and zero unverified identity claims.
  A false sync on noise is separately reported, not silently called a proven
  identity.
- **Chunk consistency:** Replay every capture at two independently selected
  chunk sizes. Exact parsed metrics **and the canonical SHA-256 of the entire
  ordered JSONL event sequence (including sample offsets)** must match. The
  native bridge must acknowledge the full input sample count. This fingerprint
  was added to the existing `acceptance.py` event aggregator; no raw IQ or
  event-level payload is persisted.
- **Expectation distinction:** For the first keyup capture, upstream metadata
  describes source ID `3024109`, group `99`, color code `1`, minimum eight data
  syncs and two voice syncs. These are **unverified upstream claims**, not
  OrcSDR results or independently established ground truth. The second PTT has
  no pinned verified identity. A disagreement is `REVIEW` (unless no valid
  identity/sync at all or another safety gate fails); no guessed ID is filled
  in. `min_superframes` remains **not assessed** without a validated superframe
  trace contract.

**Do not substitute host mock/test events for actual D1 output.** `PASS` only
means the defined operational checks passed on independently verified input;
it does not prove broader DMR performance, RF sensitivity, first-burst
acquisition, or the accuracy of upstream third-party expectations.

## Device and performance boundaries

The native D1-C bridge is currently a host executable, and the current
frontend omits acquisition pre-roll. Direct-mode short-keyup reliability,
scanning false-positive rates, SDR sensitivity, capture provenance, physical
RF or ESP32-P4 behavior all require separate evidence. The existing D1-C
component reports synthetic host CI coverage; that is not recorded-IQ
validation. Never label this D1-R gate complete before the external private
captures have actually been decoded by native OrcSDR and the report archived.

## Phase D1-N update: pinned negatives and five-size matrix

Phase D1-N extends this original D1-R runbook without modifying native D1 code. See
[`D1N_RUNBOOK.md`](D1N_RUNBOOK.md) for the exact read-only REA control recipe,
source-pin verification, Release and ASan/UBSan instructions, and required
chunk sizes **1, 7, 257, 1024, 4096**. The source-safe
[`D1N_MATRIX.json`](D1N_MATRIX.json) records 40 requested combinations;
[`D1N_REPORT.md`](D1N_REPORT.md) separately distinguishes measured hashes,
previous public real-air CI and unavailable native results. **BLOCKED** is not
an all-four-fixture pass. D1-P now implements bounded acquisition pre-roll;
the earlier paragraph in this historical runbook describing its absence refers
to the original D1-C baseline, not the subsequent integrated D1-P work.

The updated `acceptance.aggregate_events` reports `emitted_rejected_bursts`
(counting only emitted burst JSONL events with `fec=fail` or `check=fail`).
It does not measure candidates discarded internally before any event.
