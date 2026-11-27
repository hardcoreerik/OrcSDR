# D1 protocol handoff

**Ownership:** `components/orcsdr_dmr/d1_protocol/**` exclusively. D0 is linked
as an external dependency and was not edited or forked into this directory.

**Deliverables:** fixed-memory C++17 `Decoder` consuming dibits 0–3; 132-dibit
burst search; BS CACH + direct TS1/TS2 attribution; Golay(20,8) slot type;
BPTC(196,96) Hamming FEC; Full-LC RS(12,9) integrity, standard group/private
FLCO with 24-bit source/destination IDs; CSBK/Data Header/USBD masked CRC16;
strict four-fragment embedded-LC late entry; callback evidence levels.

**Test from repo root:**

```sh
cmake -S components/orcsdr_dmr/d1_protocol -B /tmp/orcsdr-dmr-d1 -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/orcsdr-dmr-d1 --parallel
ctest --test-dir /tmp/orcsdr-dmr-d1 --output-on-failure
```

See `README.md` for sanitizer commands, reproducible vector provenance,
D0 linking and exact protocol coverage. **No firmware build or flash needed.**

**Caveats:** no IQ replay on real-air DMR fixture yet, voice/speech output,
proprietary FLCO, CSBK scheduling, or full trunking. Strict embedded-LC
parity; BS CACH slot inference is not a CRC-backed identity. Investigate the
V2.7.1 PDF table extraction anomaly before production signoff; reference
Golay matrix is confirmed distance eight. Crucially `Evidence::sync_only`
never authorizes radio identities.

## D1-S CACH confidence correction

Addressed confirmed D1-CACH-01: Hamming can decode any 7-bit TACT,
so neither a plausible nor an uncorrected prefix confirms that CACH exists.
`CachCadence` requires four consecutive clean, alternating 144-dibit BS
sync/candidate windows before publishing TS1/TS2. Per-event
`CachConfidence` exposes diagnostic tentative versus cadence-validated
evidence, and the old `cach_present`/`slot_from_cach` API is retained but
now only set for corroborated CACH. A complete validated embedded-LC chain
can also confirm a same-slot CACH late-entry hypothesis; corrupted stage
TACT rejects it. Direct-mode sync remains authoritative for TS1/TS2.

Host-only tests added: standalone cadence model (sanitizer tested);
stream-level arbitrary-prefix, corruption, cadence, chunk, truncation and
direct-slot regression (requires D0+D1 build). No firmware/UI or other
folders changed. **Known external test conflict:** the separate
`tests/dmr_protocol_conformance` folder still asserts the former
known-defect result (and immediate 2-burst BS slot). Its owner must revise
those assertions to reflect D1-S without weakening the new negative tests.
