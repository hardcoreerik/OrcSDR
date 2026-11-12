# D1-F false-sync / false-identity engineering report

**Review branch:** `feature/dmr-digital-voice-foundation`.
**Independently inspected initial remote SHA:** `e6905ba4cdc283d79cf905c37f60c7e79e58b4f6`.
**Standards basis:** ETSI TS 102 361-1 V2.7.1 (May 2026), printed PDF
page 91 Table 9.2 (48-bit SYNC) and printed page 60 Fig. 6.5 (data burst),
plus B.1, B.3.5, B.3.6, B.3.8 and B.21 for the independent FEC and checksums.

## Acceptance policy under attack

`Decoder::scan` permits a *unique* one-bit **DATA** sync candidate only when
Golay(20,8) yields slot type 1,2,3,6,11, BPTC(196,96) decodes a complete
payload and RS(12,9) or the correct masked CRC-16 validates that packet.
The corrected SYNC bit is **not** copied into the measurement. A fuzzy voice
candidate must not create an event. Exact SYNCs can emit lower-confidence
`sync_only`, `slot_type` or `fec_valid` events but cannot advertise identities
without independent packet integrity.

## Synthetic fixture definitions (byte-exact regeneration)

`test_false_sync.cpp` builds a 132-dibit burst, transmitted bit MSB first:
98 payload bits; 10 slot-type bits; 48 SYNC bits; 10 slot-type bits; 98
payload bits. The 20 slot-type bits are a systematic Golay(20,8) word.
The payload is BPTC(196,96) with encoded bit 0 reserved and encoded index
`i` sent at `(181*i)%196`. CC=7. LC group destination=99, private
destination=4209000, source=3024109, FID=0. Packet types 1 and 2 use
ETSI RS parity masks 0x96 / 0x99; CSBK type 3 uses 0xA5A5,
data header type 6 uses 0xCCCC, and USBD type 11 uses 0x3333 CRC masks.
SYNC patterns are Table 9.2 BS DATA `DFF57D75DF5D`, MS DATA
`D5D7F77FD757`, direct TS1 DATA `F7FDD5DDFD55`, and direct TS2 DATA
`D7557F5FF7F5` (all hex, 48-bit, MSB-first).

The four matching VOICE patterns are tested separately. Mutations flip
transmitted bit `p` (0..47) in the SYNC portion, where `p=0` is the first
transmitted SYNC bit. The 4×5×48 single-bit cases are exhaustive.
All `4*(C(48,2)+C(48,3)) = 73,696` distinct 2/3-bit cases are exhausted
for type-1 LC. The high-bit masks flip leftmost 4/8/16/24/36/48 bits.
Deliberately broken integrity mutates payload byte 7 bit 5 *before BPTC
re-encoding*, preserving a valid FEC and slot type but invalidating
RS/CRC. Reserved BPTC bit 0 and four Golay bit errors are tested separately.

The seeded stress fixtures use `std::mt19937_64(0xD1F20261111)` in C++:
300,000 uniform random dibits, 300,000 sinusoidal AM-envelope-like hard-dibit
proxies with dither, and 300,000 random dibits with exact / one-bit DATA
SYNCs inserted at burst-start offsets 0,997,1994,... (sync begins at
start+54). The last stream intentionally generates *tentative* exact
SYNC events without valid packet integrity. The pure Python source-independent
oracle seeds `random.Random(0xD1F20261111)` and checks 250,000 random
48-bit words. **The Python and C++ PRNG sequences are different by design.**
None of the proxies substitutes for pinned real IQ or pinned synthetic AM/noise.

## Evidence status

| Gate | State | Notes |
|---|---|---|
| Rendered ETSI Table 9.2 and Fig. 6.5 | Verified | Source is authoritative ETSI PDF |
| Python sync geometry | PASS (5/5) | GCC-independent Python 3 reference; 0/250,000 random 48-bit candidates |
| C++ strict syntax on temporary ABI-compatible headers | PASS | GCC 14.2 `-Wall -Wextra -Wpedantic -Werror`, **not** linked execution |
| Source-linked Release against actual D0/D1 | **PASS: 7/7 CTest** | GCC 13.3.0; 0.075295 s for adversarial executable |
| Source-linked Debug ASan/UBSan | **PASS: 7/7 CTest** | GCC 13.3.0; 0.786366 s for adversarial executable |
| Native D1 protocol tests in same CI jobs | **PASS: 3/3 CTest each** | Separate production D1 unit/CACH gates, no decoder edits |
| Recorded IQ, device and pinned D1-N negatives | NOT RUN here | Separate acceptance workstream; no claim of on-air negative-control pass |

### Recorded source-linked run (GitHub Actions)

- [CI run 43180888644](https://github.com/hardcoreerik/OrcSDR/actions/runs/43180888644), **all three jobs passed**, UTC 2026-11-12, workflow `DMR Protocol and Migration Host Gates`.
- Exact checked-out source and compiled test SHA: **`9f3dec34614ac8e3e0334121167f892a4249cea3`**; both protocol matrix jobs printed that SHA, compiler **GCC 13.3.0** and `seed=0xd1f20261111`.
- Total independently constructed / mutated candidate cases: **74,972**; input bit exposures across repeated test executions: **21,613,846** bits (**not** independent RF bits). One-bit valid DATA syncs **960/960** recovered with unchanged packet identity/fields; two-bit **4,512/4,512** and three-bit **69,184/69,184** rejected without any event; **24** four-or-more-bit cases rejected.
- **100** candidates with valid-looking fuzzy sync but incorrect RS/CRC, reserved BPTC or invalid Golay produced **zero events**. **192** one-bit VOICE candidates produced **zero events**. Pairwise DATA sync minimum distance **10 bits**, radius-one overlaps **0**, so an ambiguous radius-one DATA candidate is geometrically unavailable with the four current ETSI DATA words.
- Uniform random hard dibits: **300,000 dibits, 0 events / 0 identities**; AM-envelope-like hard-dibit proxy: **300,000 dibits, 0 events / 0 identities**. In **300,000 dibits** with **301 deliberately planted** DATA sync patterns, **151 exact-sync tentative events**, **zero fuzzy events** without CRC/RS and **zero verified identities**. These 151 events are *expected unverified diagnostics*, not validated packet/identity false positives.
- **Aggregate for complete suite:** `events=1188`, `tentative=759`, `verified_identities=429`, **`false_identities=0`**. The 429 are from deliberately valid fixture payloads; **none** from random, AM-like, corrupted or planted-sync-only negative controls.
- Measured adversarial executable wall time: **0.075295 s Release**, **0.786366 s sanitizer Debug**. Python CTest five geometry oracle cases passed in each matrix; the workflow Python job independently passed existing **13/13** ETSI oracle tests. The Python geometry oracle run separately in the sandbox also passed **5/5**.

These are deterministic *synthetic hard-dibit* observations, not a statistical estimate of real-RF false alarm probability, and no native RF hardware or real captured AM-IQ was measured. All CI results are directly reproducible using `README_D1F.md`.

## Preliminary engineering judgment

The acceptance policy is defensible as a **conservative packet-validation
heuristic**: it does not promote a one-bit fuzzy correlation without a
separate protected payload, and it does not claim to repair multiple sync
errors. Its correctness is stronger than a raw fuzzy-sync classifier.
However, CRC and RS parity are error-detection codes, **not authentication**.
False-identity rates under real interference and attackers deliberately
constructing complete protocol-valid bursts are outside this gate. A finite
0-failure test provides only an upper bound for the tested distribution and
should never be reported as proof of zero field false alarms.

See `DEFECT_LEDGER_D1F.md` for reproduced versus tentative issues.
