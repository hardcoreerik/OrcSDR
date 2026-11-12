# Offline DMR real-IQ acceptance harness (host-only)

**Scope and truth:** This folder owns the acceptance **driver and evidence contract**, not a DMR receiver. It reads the existing, read-only `../digital_voice_replay/manifest.json`, authenticates private REA-Results recordings with their pinned Git blob SHA-1, computes local SHA-256 (and checks it **only if** the manifest pins it), and streams genuine 48 kS/s complex IQ into an explicitly supplied **external** D1 decoder bridge. No decoder/firmware/UI component or raw IQ is copied or modified. A passing host test does **not** establish any over-the-air DMR decode.

## Fixture priority and provenance

The default order **starts with direct mode**:

| Manifest ID | Format | Bytes | Grounded expectation status |
| --- | --- | ---: | --- |
| `dmr-directmode-keyup` | CS16 LE / 48 kS/s | 345,600 | Upstream research asserts source 3024109, group 99, color 1, minimum 8 data sync, 2 voice sync, 2 superframes — **not measured by OrcSDR** |
| `dmr-directmode-second-ptt` | CS16 LE / 48 kS/s | 326,404 | Upstream note calls this a second-PTT symbol-phase regression, **no known validated identity** |
| `auto-negative-noise` | CS16 LE / 48 kS/s | 384,000 | Synthetic non-DMR negative; any integrity-proven DMR identity is a false detection |
| `auto-negative-am` | CS16 LE / 48 kS/s | 384,000 | Synthetic AM non-DMR negative; same identity-false-positive gate |
| `dmr-tier3-control` | CF32 LE / 48 kS/s | 499,200 | Optional Tier III sample; Tier II D1 cannot be assumed to support its control frames |
| `dmr-tier3-voice-terminator` | CF32 LE / 48 kS/s | 537,600 | Optional Tier III sample; upstream assertions, not independently verified decoder results |

Source manifest: `tests/digital_voice_replay/manifest.json`. Private IQ: `hardcoreerik/REA-Results`, branch `chatgpt/digital-voice-rsa-rea-2026-11-01`, directory `inbox/fixtures/digital-voice` (**read-only**). Each entry declares the original upstream source commit and source rights information. **Captured radio content is not licensed for redistribution.** All nine real-air entries in the manifest have a pinned Git blob SHA-1 but no pinned SHA-256; an actual SHA-256 printed in a report is newly measured, **not** a match to a trusted external SHA-256 pin. The two synthetic negative entries *do* have SHA-256 pins.

The raw IQ files have no embedded format header: `.cfile` is treated as CF32 LE and `.cs16` as CS16 LE **only under this pinned collection's manifest**. Counts and suffixes alone cannot prove a format. CF32 remains native little-endian `(float I, float Q)`. CS16 signed little-endian `(int16 I, int16 Q)` is normalized to `value / 32768.0`. Both are converted/passed to the bridge as interleaved CF32 LE in 48 kS/s **without resampling**. Nonfinite CF32, RIFF/WAVE containers, incomplete samples, path escapes and mismatched pins fail closed. The input is hashed **before and while being streamed**. IQ is never saved or included in JSON reports.

## Run/test from OrcSDR repository root

Python 3.10+ stdlib only. No external Python dependencies, network or real SDR is required for the self-contained host unit tests.

```bash
python3 -m unittest discover -s tests/dmr_replay_acceptance -p 'test_*.py' -v
python3 tests/dmr_replay_acceptance/acceptance.py list
```

Keep the REA research checkout in a separate directory/worktree, **outside OrcSDR**. For example, using your own authenticated clone of the private REA-Results repository:

```bash
cd /path/to/REA-Results
git fetch origin chatgpt/digital-voice-rsa-rea-2026-11-01
git worktree add /path/to/REA-Digital-Voice origin/chatgpt/digital-voice-rsa-rea-2026-11-01
```

Set the fixture root to the directory containing `DMR/`, `P25/`, `NXDN/` and `AUTO/`:

```bash
ROOT=/path/to/REA-Digital-Voice/inbox/fixtures/digital-voice
# From OrcSDR repository root again:
python3 tests/dmr_replay_acceptance/acceptance.py verify --fixtures-root "$ROOT" --controls \
  --report /tmp/orcsdr-dmr-verified.json
# Include Tier III (not a required D1 Tier II acceptance claim):
python3 tests/dmr_replay_acceptance/acceptance.py verify --fixtures-root "$ROOT" \
  --controls --include-tier3
```

`verify` returns nonzero for missing or tampered fixtures. By default only the two **direct-mode** fixtures are selected; `--controls` adds the two synthetic negatives and `--include-tier3` adds Tier III only if no explicit `--id` was supplied. To target a single capture: `--id dmr-directmode-keyup`. `list` reads metadata without touching IQ. **Verification is not decoding.**

### Decoder replay with the D1 bridge (not yet integrated)

When an independently validated D1 host executable has been created (the front-end and protocol decoder components currently have different streaming contracts), use:

```bash
ROOT=/path/to/REA-Digital-Voice/inbox/fixtures/digital-voice
python3 tests/dmr_replay_acceptance/acceptance.py run \
  --fixtures-root "$ROOT" --bridge-command '/path/to/d1_replay_bridge' \
  --chunk-samples 257 --timeout 30 \
  --report /tmp/orcsdr-dmr-acceptance.json
```

**Default `run` order:** direct keyup → direct second PTT → synthetic noise → synthetic AM. An explicitly provided `--id` selects only those fixtures unless `--controls` is added. This is intentionally an *integration seam*, not a bundled fake decoder. The external bridge **must be reviewed and built against the actual D1 components** before the resulting events may support a real decoder acceptance claim.

The harness spawns the command **without a shell**, appending:

```text
--input-format cf32-le --sample-rate-hz 48000 --fixture-id <manifest-id>
```

The executable reads a continuous stream of complex little-endian CF32 from **stdin**, emits **one JSON object per stdout line**, exits `0` on success, and finishes with an end-of-stream summary. The harness checks the final count, schema, event order, field ranges, bridge exit code and a per-process timeout. Its outputs report `sync`, `fec`, `crc`, `rs`, `embedded_lc`, `check`, the `color_codes`, `slots_observed`, `slots_check_fec_bursts`, `source_ids`, `talkgroups`, `unverified_identity_claims`, and `false_identity_detections` for negative controls. **A sync alone never proves an identity.**

The end-of-stream declaration is the bridge's own assertion that it read all samples; it is **not** proof that its decoder produced the correct radio results. The bridge must be separately audited.

### JSONL event interface (v1)

Every event has `schema: "orcsdr-dmr-d1-bridge-event-v1"` and a `sample_offset` measured from the **start of the original complex-IQ capture** (0-based, monotonically nondecreasing, strictly less than the sample count). Sample offsets are **not** the D1 protocol's `burst_start_dibit`; an integration bridge must map these back to the IQ timeline. One `sync` event is emitted for each detected synchronization pattern, using the exact D0 name (`bs_voice`, `bs_data`, `ms_voice`, `ms_data`, `direct_ts1_voice`, `direct_ts1_data`, `direct_ts2_voice`, `direct_ts2_data`). A `sync` event is not a CRC result.

These illustrative records show the **wire schema only**, not observed RF results:

```jsonl
{"schema":"orcsdr-dmr-d1-bridge-event-v1","type":"sync","sample_offset":12000,"pattern":"direct_ts1_data"}
{"schema":"orcsdr-dmr-d1-bridge-event-v1","type":"burst","sample_offset":12000,"kind":"data","slot":1,"color_code":1,"fec":"pass","check":"pass","check_kind":"rs12_9","identity_valid":true,"identity":{"source_id":123456,"talkgroup":99}}
{"schema":"orcsdr-dmr-d1-bridge-event-v1","type":"summary","sample_offset":47999,"consumed_samples":48000}
```

- `kind`: `voice`, `data`, `control`, or `unknown`. Slot is `1`, `2`, or `null` (unknown), not `0` (the D1 internal unknown-slot sentinel). Color code is `0..15` or `null` if not decoded.
- `fec`: `pass` / `fail` / `unknown` (e.g. `bptc_valid`). `check`: independent integrity `pass` / `fail` / `unknown`. `check_kind`: `crc16`, `rs12_9`, `embedded_lc` or `none` for unavailable checks. `crc` counts **only CRC-16**, while `rs` counts RS(12,9), so RS-protected voice LC is **never mislabeled CRC-valid**.
- `identity_valid`: a strict boolean corresponding to D1's `Evidence::identity_valid` gate, **not** mere `sync_only`, `fec_valid`, `check_valid` or an inferred source ID. `identity` is `null`, or a map with nonzero 24-bit `source_id` and/or `talkgroup`. D1 group FLCO 0 maps to `talkgroup`; D1 private-call FLCO 3 must **not** be relabeled a talkgroup (omit it from `identity` or add a future explicit private-destination contract).
- Trusted identities are counted **only** when FEC and an appropriate RS / embedded-LC check pass **and** `identity_valid=true`. Otherwise any emitted identity is flagged `unverified_identity_claims` and omitted from trusted IDs. The harness does not infer an ID from signal energy, a sync, slot type or CACH alone.
- `slots_observed` are bridge claims and may be from direct-sync/CACH, **not inherently CRC protected**. `slots_check_fec_bursts` means such a burst also passed payload checks, not that slot attribution itself is CRC proven.
- `false_identity_detections` is counted where a bridge reports an integrity-proven radio identity on the manifest's **synthetic non-DMR negative controls**. For positive captures, disagreement with the upstream expected source/group/color is recorded as `not_observed`, **not** automatically called a verified false identity.
- The last event must be `type="summary"` with `consumed_samples` exactly equal to the number of complex samples, and `sample_offset=consumed_samples-1`. No event after it is permitted. No output is written to the repository by the harness.

### Integration notes for D1 owner

The existing D1 `Frontend::process()` accepts chunked CF32 complex IQ and returns hard dibits **after** acquiring sync. Its `process()` may consume less than the input length, and the caller must retry the unconsumed portion; `Symbol::sample_index` and frontend lock counters need an absolute capture-time mapping. The existing `d1::Decoder::feed()` needs contiguous dibits **including** the sync burst, and currently outputs a C++ `d1::Event` with `Evidence::sync_only`, `slot_type`, `fec_valid`, `check_valid`, `identity_valid`; `bptc_valid`, `integrity_valid`, `colour_code`, `slot`, `source_id`, and `destination_id` flags. **Those two existing contracts do not currently join trivially** because the frontend does not backfill pre-lock sync dibits. The bridge owner must resolve that honestly—e.g. with a bounded sync replay FIFO, or another independently tested symbol reconstruction. Do not modify either D1 owner directory from this acceptance workstream.

`Evidence::check_valid` alone does not justify emitting an identity. Use the C++ `Evidence::identity_valid` gate, distinguish RS(12,9) from CRC-16 by the actual validated packet type, and emit no identity for unsupported/private LC fields. The bridge must reset between each fixture and avoid carrying state across discontinuities. Treat all upstream `min_*` and ID references as **external assertions**; `min_superframes` and protocol-specific late-entry thresholds are not automatically assessed by this event v1 contract and appear as `not_assessed`, not passes.

## Testing record / limitations

- The host tests use **tiny synthetic temporary IQ** plus a deliberately fake bridge to verify framing, hash gates, error paths, time bounds and evidence classification. That fake output is never presented as real-air success.
- The exact private REA raw IQ corpus is **not present in this sandbox**. Its repository metadata and path/byte pins were checked read-only, but no real-capture hash pass, synchronization count, D1 CRC, recovered color/slot, source/talkgroup or false-positive rate has been measured here.
- The real D1 front-end and Tier II protocol decoder are not yet integrated as a host CLI. Nonzero report status is a gate failure, but zero means only the driver/bridge *reported* bounded evidence. Real acceptance still requires independent receiver audits, direct-mode playback, multiple chunks, negative-control playback and reconciled expected results.
- JSON report is explicitly forbidden inside OrcSDR; use `/tmp` (or another **external** folder). Raw IQ and generated recordings belong only in the private research checkout / local private folders. Never commit or distribute them from OrcSDR.

## Phase D1-N: exact synthetic controls and native four-fixture matrix

The native `dmr_d1_replay_bridge` is now implemented under the read-only
`components/orcsdr_dmr/d1_integration/` component. Phase D1-N adds
[`negative_control_repro.py`](negative_control_repro.py) to regenerate the
**exact** manifest-pinned REA noise/AM streams (with SHA-1 **and** SHA-256
checks), and [`d1n_matrix.py`](d1n_matrix.py) to run the original bridge
at five input chunk sizes in distinct Release and ASan/UBSan modes.

See [`D1N_RUNBOOK.md`](D1N_RUNBOOK.md), [`D1N_REPORT.md`](D1N_REPORT.md),
and [`D1N_MATRIX.json`](D1N_MATRIX.json). The exact negative hashes pass
independent byte-for-byte regeneration; the **current complete four-fixture
native decoder acceptance is BLOCKED**, not a proven zero false-identity count.
Only the original native bridge's integrity-gated fields may support future
RF identity results, and binary IQ/logs must never be checked in.
