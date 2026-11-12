# Phase D1-F — independent DMR DATA sync / false-identity audit

**Scope:** `tests/dmr_protocol_conformance/**` only. D0, D1-S, the production D1
receiver, D1 integration, firmware, M5GFX, P25, and UI are **read-only**.

This audit tests the acceptance policy, *not* the decoder's own implementation
as an oracle. The hard-dibit burst constructor is independently derived from
ETSI TS 102 361-1 V2.7.1 (2026-05): §6.2/Fig. 6.5 burst field placement;
§9.1/Table 9.2 eight 48-bit sync words; Annex B.1 BPTC; B.3.5 Golay;
B.3.6/Table B.18 RS; B.3.8 CRC and Table B.21 masks. The original ETSI PDF:
https://www.etsi.org/deliver/etsi_ts/102300_102399/10236101/02.07.01_60/ts_10236101v020701p.pdf

We checked the **rendered** PDF page 91 (zero-based page index 90) for all
voice/data syncs and page 60 (index 59) for 98+10+48+10+98 field order.
These are transformed, minimal numerical fixtures rather than third-party
source or a copied production sync algorithm.

## Tests

- `test_false_sync.cpp` links actual checked-out D0/D1 C++ sources through the
  shared conformance subject. The fixture independently builds all five
  supported complete DATA packet types (1,2,3,6,11), using the ETSI
  systematic matrices / RS parity and independent bitwise CRC recurrence.
  Exact control and single-bit corruptions are compared with the decoder's
  events, **requiring** the original kind/slot/IDs/integrity. No test silently
  normalizes a corrupted measured sync.
- Exhaustive **48** sync-bit positions for each of **four** DATA modes and
  **five** supported payload types = **960** one-bit test packets.
- Exhaustive **4×C(48,2)=4,512** and **4×C(48,3)=69,184** distinct two-/three-bit
  corruptions of a valid Full LC burst, plus longer consecutive corruption
  masks, which **must not emit even a tentative event**.
- Fully BPTC-valid but RS/CRC-invalid, BPTC-reserved-bit-invalid and
  Golay-invalid fuzzy-sync candidates **must** produce zero events.
- Exactly matched voice sync may produce a *tentative* `sync_only` event;
  one-bit VOICE corruption cannot upgrade to DATA or yield identity.
- One-bit candidate sync geometry is verified independently: the minimum
  pairwise distance among four DATA words is at least 8, and the radius-one
  neighborhoods do not overlap. This does not remove the runtime uniqueness
  check or replace packet integrity verification.
- The D1-S model must keep arbitrary CACH prefixes *tentative* until a clean,
  alternating, 144-dibit four-burst cadence. Single-bit TACT corruption,
  deliberate timing gap, reset, incomplete stream, and many input chunk sizes
  are checked. The test requires source/talkgroup not to promote from CACH.
- Deterministic `0xD1F20261111` seed: random dibits, an AM-envelope-like
  quantized dibit **proxy**, and random dibits with exact/one-bit sync planted
  at known offsets. These are **not real AM IQ** and **not** the exact pinned
  REA negative-control files; only the separate D1-N gate can close that.
- `test_false_sync_oracle.py` is pure Python, does not import production
  modules and independently checks printed sync geometry, 1/2/3+ bit errors,
  and a seeded 250,000-word random 48-bit candidate sweep.

## Run and collect evidence (repository root)

Use Python 3.10+, CMake >=3.16 and a C++17 compiler. Builds live outside the
owned source directory; repository source dependencies are **never modified**.

```sh
python3 -m unittest discover -s tests/dmr_protocol_conformance \
    -p 'test_false_sync_oracle.py' -v
cmake -S tests/dmr_protocol_conformance -B build/dmr-d1f-release \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build/dmr-d1f-release --parallel
ctest --test-dir build/dmr-d1f-release --output-on-failure -V \
    | tee ./dmr-d1f-release.log
```

In addition to the five existing conformance suites, CMake registers
`dmr_false_sync_adversarial` and (if Python3 is found)
`dmr_false_sync_geometry`. To capture **stderr and exit status** in the log
with Bash pipefail, use:

```sh
set -o pipefail
ctest --test-dir build/dmr-d1f-release --output-on-failure -V \
    2>&1 | tee ./dmr-d1f-release.log
```

The log appears in the **directory from which you execute the command**.
Sanitizers (Linux GCC/Clang):

```sh
cmake -S tests/dmr_protocol_conformance -B build/dmr-d1f-asan \
    -DCMAKE_BUILD_TYPE=Debug -DDMR_CONFORMANCE_SANITIZERS=ON
cmake --build build/dmr-d1f-asan --parallel
ctest --test-dir build/dmr-d1f-asan --output-on-failure -V \
    > ./dmr-d1f-asan.log 2>&1
```

For the exact adversarial test alone:

```sh
ctest --test-dir build/dmr-d1f-release -R dmr_false_sync_adversarial \
    --output-on-failure -V > ./dmr-d1f-adversarial.log 2>&1
```

Audit stdout records the C++ compiler version, configured Git HEAD SHA,
fixture seed, corruption candidate counts, total hard-dibit *bit exposures*,
all observed events, tentative versus integrity-backed identities, false
identity count, and wall-clock runtime. **Bit exposures count retransmissions
across separate test inputs; they are not independent RF samples.** Test
assertions are explicit and active with `NDEBUG` in Release.

The GitHub workflow `.github/workflows/dmr-conformance-migration.yml` already
executes this directory's CMake/CTest in **Release and ASan/UBSan** on pushes
to this feature branch. That workflow is not edited here. Verify the final
GitHub Actions run **against the pushed SHA** before declaring source-linked
acceptance; a standalone Python PASS or C++ syntax check is insufficient.

## Interpretation

A valid CRC or RS syndrome establishes packet coding integrity **not
cryptographic authenticity**. An adversary capable of constructing valid DMR
bursts can construct valid source IDs. Likewise, a D1-S
`cadence_validated` CACH is stronger than arbitrary Hamming TACT, but cannot
prove RF transmitter identity or actual timeslot location. The audit measures
whether *unsupported* evidence is promoted; it does not claim security
against a transmitter deliberately sending valid but deceptive protocol data.

See `FALSE_POSITIVE_ENGINEERING_REPORT.md` and `DEFECT_LEDGER_D1F.md`.

## Verified CI result (2026-11-12 UTC)

GitHub [run 43180888644](https://github.com/hardcoreerik/OrcSDR/actions/runs/43180888644)
executed on checked-out source SHA
`9f3dec34614ac8e3e0334121167f892a4249cea3` with GCC 13.3.0:
**7/7 conformance CTests PASS** in Release and **7/7 PASS** under
ASan/UBSan; both jobs also passed **3/3** native D1 CTests. Adversarial
executable measured 21,613,846 repeated-input bit exposures, 74,972
constructed candidates, 0 false identities, and elapsed 0.075295 s
Release / 0.786366 s sanitizer Debug. The source-independent Python
geometry oracle passed 5/5 in each CTest matrix and locally. The existing
13-test ETSI Python oracle in the workflow also passed.

For full metrics and the distinction between intentionally tentative
SYNC-only diagnostics and CRC/RS-backed identities, see
`FALSE_POSITIVE_ENGINEERING_REPORT.md`.
