# DMR real-air direct-mode replay — first verified native decode (2026-11-11 PST)

**Evidence class:** REAL-AIR, CAPTURED IQ → **native OrcSDR host C++ D1 bridge** → integrity-gated DMR metadata. **NOT** live ESP32-P4 hardware, negative-control acceptance or a whole-call decoder benchmark.

**GitHub Actions:** [DMR public real-air IQ research probe, run 43175761333](https://github.com/hardcoreerik/OrcSDR/actions/runs/43175761333) — **completed successfully**. The run was performed on 2026-11-12 UTC, still 2026-11-11 in America/Los_Angeles. The workflow explicitly distinguishes successful execution from successful identity decoding.

**Reproduce from:** `.github/workflows/dmr-public-real-iq.yml` and `tests/dmr_replay_acceptance/acceptance.py`. Workflow source baseline: `20212859acb6f7535c93963de6b751f06e77cb51`. IQ was downloaded at runtime and never committed or uploaded as an artifact. Only small JSON metrics were retained.

## Recorded-IQ provenance and byte integrity

Two 48 kS/s **channelized** direct/simplex CS16-LE complex-IQ slices from [MattCheramie/GopherTrunk commit f06bfff493f47fdeb69807c8b08b2b32a253839f](https://github.com/MattCheramie/GopherTrunk/tree/f06bfff493f47fdeb69807c8b08b2b32a253839f/cmd/gophertrunk/testdata). Their Git blob SHA-1 pins **exactly match** our preexisting `tests/digital_voice_replay/manifest.json` and the REA-Results research pins:

| Fixture | Bytes | Git blob SHA-1 verified on CI | SHA-256 measured on CI |
| --- | ---: | --- | --- |
| `dmr-directmode-446500-keyup-48k.cs16` | 345600 | `e305eac29985ff647b8aba0434fee8ab7db81f15` | `b09d35659bcbd242a0d23fad275fb38b0e84365a2bb7dcf067677b5856571bf6` |
| `dmr-directmode-446500-ptt2-48k.cs16` | 326404 | `b7d91747030e55f68637edb2198476047191d84a` | `dad93254a12416f640d61d65f9a5335e955c49ad92217f75dc322257ed421880` |

The upstream recording description attributes the captures to a DMR handheld transmitting on 446.500 MHz simplex, upstream source ID `3024109`, talkgroup `99` and colour code `1`. These upstream values were **not supplied to or injected into our C++ decoder**. The native decoder produced the values below from actual IQ, subsequently compared to upstream metadata.

## Actual OrcSDR results — full native IQ-to-identity path

| Metric | Direct-mode key-up | Direct-mode second PTT |
| --- | ---: | ---: |
| Verified complex IQ samples | 86400 | 81601 |
| Actual DMR sync events | **11** | **13** |
| Actual burst events | **11** | **13** |
| Both FEC and protocol integrity check valid | **9** | **10** |
| Validated source radio IDs | **3024109** | **3024109** |
| Validated talkgroups | **99** | **99** |
| Checked DMR colour codes | **1** | **1** |
| 257-sample vs 4096-sample replay metrics including event fingerprints | **identical** | **identical** |
| Identity result | **MATCH to independent upstream record** | **MATCH to independent upstream record** |

These are **OrcSDR observed outputs** from the compiled `dmr_d1_replay_bridge`, not source-project test assertions or mock JSONL. The C++ bridge consumes real 48-kS/s CF32 little-endian IQ produced deterministically by the acceptance harness from source-verified CS16, and emits only evidence-backed protocol events. FEC+integrity counts are checked receiver output, **not a guarantee that every burst was recovered**.

The replay workflow's job is a **research probe**. Its green status means source bytes, decoding invocation and chunk-consistency checks ran; the explicit `VALIDATED_IDS_OBSERVED` result confirms that checked radio identities were emitted in these two recordings. See the workflow summary and retained JSON reports for full counters.

## Parallel agent delivery and integration status

- **D1-R** agent delivered fail-closed `real_iq_gate.py` and regression suite, but its original four-fixture gate (two real DMR recordings plus two pinned synthetic noise/AM controls) was **BLOCKED** because those four bytes were not available in its native sandbox. **The two real-air positive recordings are now verified and measured independently in public CI. The complete four-fixture D1-R gate is still NOT RUN.**
- **D1-P** agent delivered measured, allocation-free, fixed-size first-burst pre-roll (78 physically sampled pre-sync/SYNC dibits) with host tests. The lead updated `d1_integration/**` to forward these and count recovered pre-roll acquisitions separately from missing history. **One-burst end-to-end DMR identity is NOT yet accepted.**
- **D1-S** agent changed the protocol to publish BS CACH-based slots only after four alternating 144-dibit burst intervals or verified embedded-LC evidence; direct-mode TS1/TS2 still come from direct sync. Formerly defective CACH assumptions in `tests/dmr_protocol_conformance/test_conformance.cpp` were replaced with explicit tentative/validated assertions. Independent Release/ASan conformance CI now passes.

**Open first-burst defect:** an independent integration test synthesizes a *single* complete direct-mode Full-LC IQ burst. D1-P acquires one lock and returns 78 measured pre-roll dibits; the bridge forwards 140 hard decisions across the single burst and tail. Exactly **one sync dibit** is sliced incorrectly; protocol D1-B still requires an *exact* 48-bit SYNC, so there is **no D1 event or identity for that one-burst vector**. This is a documented remaining robustness gap (not a fabricated CRC pass), distinct from the successful recorded-IQ multi-burst results. The regression prints diagnostic evidence and does not call this missing result accepted. Candidate next fix: standards-based, bounded sync bit-error tolerance with strict integrity gating, plus explicit false-sync and voice-only negative testing; do not overwrite measured dibits with known sync constants.

## Remaining acceptance gates

1. Execute the **entire** D1-R four-fixture gate against source-pinned direct-mode **plus exact noise and AM controls**, verify all SHA/byte pins, event digests and zero false validated identities. This public probe has not accessed or substituted the two pinned controls.
2. Investigate/fix the one-dibit **first-burst sync matching** limitation. A valid measured pre-roll and a correct full-LC message are distinct from a successfully framed and integrity-validated burst.
3. Run stronger real-air first-burst timing/noise, CVBS/BS slot-cadence, malformed/truncated input and multiple independent recordings. Do not infer broad sensitivity from two cuts of the same handheld session.
4. Measure ESP32-P4 CPU, internal RAM/PSRAM, stack and USB/SD interference before any production on-device deployment; F0c microSD power-loss and P25 profile compatibility remain unverified.
5. UI/M5GFX design is deliberately deferred by the operator; no firmware or P25 files were modified.

**How to reproduce:** push an authorized change in any D1 component/bridge path on the shared feature branch, or run the dedicated GitHub Actions workflow manually. It downloads the two pinned public CS16 slices into runner temp, checks exact hashes, builds the real C++ replay bridge, runs both captures at 257 and 4096 samples per chunk, prints observed metrics to Actions job summary and uploads **JSON reports only**. No raw IQ is committed or archived.
