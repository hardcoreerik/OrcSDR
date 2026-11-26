# NXDN research recovery and implementation assessment — 2026-11-25

Scope: recover the NXDN research and real-air recordings that already exist, replay them through OrcSDR's own production
frontend/decoder, record what was actually measured, and advance the implementation only where evidence supports it.
Nothing here is copied from a GPL decoder, and no protocol field, CRC or identity is invented. Measured numbers below
were produced on 2026-11-25 on host (WSL Ubuntu, g++ 13.3, Release and ASan/UBSan) from branch
`feature/dmr-digital-voice-foundation` at `13d02a24` plus this change.

**Headline.** Positive real-air NXDN IQ exists (REA-Results), is byte-verified, and OrcSDR's own frontend acquires it:
FSW, LICH and CRC6-valid SACCH with a consistent RAN on both voice captures and 19 spaced control frames on the RCCH
capture. That closes most of Gate A on the short clips. The normative NXDN specification is not held locally, which blocks
Gates B (Golay/AMBE+2 extraction against a reference), C, D and E. The two longer "WaveShark" recordings are **not**
acquired by the decoder; cause unresolved.

## 1. Existing research inventory (reuse, do not redo)

REA-Results (private), branch `research/nxdn48-nxdn96-foundation-20261119`, commit `65713314c2effc357b43fbd91bb79766967435a6`:

| Document | What it already established |
|---|---|
| `inbox/2026-11-01_digital-voice-standards-whitepapers-quantified.md` | The 3 public NXDN Forum PDFs (whitepaper rev 04, 12 p; FDMA vs TDMA paper, 9 p; IWCE 2017 update, 39 p) are informative, 60 pages in total, **not** normative. Forum portal needs guest registration; not passed. NXDN48 = 4800 b/s = 2400 sym/s, NXDN96 = 4800 sym/s. |
| `inbox/2026-11-01_digital-voice-iq-capture-verification-matrix.md` | Fixture matrix; SigIDWiki `NXDN_IQ.zip`, GopherTrunk IQ-labelled WAVs (undecoded, unvalidated). |
| `inbox/2026-11-02_nxdn-realair-acquired_p25-phase2-acquisition-plan.md` | The three real-air CS16 clips, upstream test thresholds, source/destination blob match, "no identities published for these captures", DSD-neo CU8 transfer rejected (blob mismatch). |
| `inbox/2026-11-02_digital-voice-predevelopment-data-gap-audit.md` | Gap list: authoritative tables, a second independent source per rate, labelled identities. (Written before the clips were acquired; its "0 NXDN" count is superseded by the acquisition report.) |
| `inbox/fixtures/digital-voice/manifest.json` | Lists the three clips with `sha256: null` and `orcsdr_test_status: not_run` (both now measured, see §3/§4). |

OrcSDR: `docs/nxdn/*`, `components/orcsdr_nxdn/`, `tests/nxdn_replay_acceptance/`, `tests/fixtures/nxdn/`,
`tests/digital_voice_live/nxdn_synth.cpp`, and the experimental branch `agents/orcsdr-nxdn-voice-proof-20261124`.

An earlier assessment treated the absence of positive IQ **in OrcSDR's committed corpus** as absence of positive IQ.
That was wrong; the clips live in REA-Results by design (private, never committed to public OrcSDR).

## 2. Standards availability

Held and read (informative only): NXDN Forum whitepaper rev 04, FDMA vs TDMA information paper, NXDN Industry Update
IWCE 2017, and the Forum's public technical-specifications page (FDMA, 4FSK, AMBE+2, 6.25/12.5 kHz, 4800/9600 b/s,
Type-C/Type-D trunking).

**Not held, exact missing documents:** the normative NXDN Technical Specifications suite, in particular
**TS 1-A (Common Air Interface)** including the Layer 3 message definitions and CRC/FEC procedures; also the
physical-layer/frame-structure parts, the vocoder-frame/Golay definition for the AMBE+2 half/full rate, and the
Type-C/Type-D trunking procedures. Official route: https://www.nxdn-forum.com/download_file/ and
https://www.nxdn-forum.com/instructions/ (guest/member registration with identity and acceptance of terms). I did not
attempt to bypass it and it is not authorised.

**Owner action requested:** obtain (or authorise me to use) a legitimately acquired copy of TS 1-A and the related parts,
and record edition, access terms, and local SHA-256. Until then, the existing decoder's LICH/SACCH/CRC6 constants are
first-party implementations whose correctness is supported only by (a) synthetic round-trips and (b) the real-air
agreement below; they are not checked against the normative text.

## 3. Real-air fixture inventory and verified hashes

All three verified in this session: size, Git blob SHA-1 (expected = computed) and independently computed SHA-256.

| File | Bytes | Duration | Git blob SHA-1 (verified) | SHA-256 (computed) |
|---|---:|---:|---|---|
| nxdn48_rdch_voice_447100_16oct_48k.cs16 | 230400 | 1.2 s | `26aed395774380be0cf6a0ebdf391723a9335e00` | `8df8895c242c734a92f0f271f4e4faeb68289ff99cb1e542552449bc3e7e642f` |
| nxdn96_rdch_voice_447100_15oct_48k.cs16 | 153600 | 0.8 s | `01845523c82df3a279712b577a0f0180653f981f` | `0f49aba231d892d3ab6b1f470b3500e52542327f4ac43e6116c49eb861ed1b31` |
| nxdn96_rcch_cc_453775_19oct_48k.cs16 | 192000 | 1.0 s | `c4a1bdb2fdd36cc316877982781c99bc7043462d` | `45378edcaaa5ed8e72ffdf4e6ea2e885204839230f7810a7731d55412981ff0d` |

Origin `MattCheramie/GopherTrunk` @ `f06bfff493f47fdeb69807c8b08b2b32a253839f`. Capture rights not independently
verified: private development only; not committed to OrcSDR or CI artifacts.

Longer WaveShark/SigIDWiki captures (`tests/fixtures/nxdn/fetch_assets.py --fetch waveshark-nxdn48-12s waveshark-nxdn96-15s`):

| Asset | Archive SHA-256 (matches manifest = URL hash) | Extracted bytes | Extracted duration | Extracted SHA-256 |
|---|---|---:|---:|---|
| waveshark-nxdn48-12s | `2496c077...bb626` ✔ | 2265600 | 11.8 s | `6947e2181a92eb41cc268e0c5ef8e7eb4219b4eab7b036ea4614355bc752c9db` |
| waveshark-nxdn96-15s | `48bba222...22af3` ✔ | 2937600 | 15.3 s | `f719f7f78cc7cf0cd983ed89cca82901ec9242d053ea9958d56cba21ee0995d0` |

**Defect found and fixed in the fetcher:** the manifest `sha256` is the hash of the **.xz archive** (it is the URL
key), while `bytes` was a nominal 12 s / 15 s figure that does not equal the real extracted length. The original script
compared the extracted file to both, so a correct download failed with "wrong length". The original hashes in the
manifest are unchanged; the fetcher now verifies the archive hash and records `extracted_bytes`/`extracted_sha256`
observed after that verification (labelled as our observation, not the publisher's). The 453 MHz label on these files is
synthetic (tuning unknown). They share the SigIDWiki RF origin with the DSD-neo samples and are not independent of them.
Publisher-reported identities (NXDN48 source 901/group 0/RAN 1; NXDN96 source 2/group 0/RAN 0) remain unverified.

## 4. Actual OrcSDR replay results (production frontend → decoder)

`check_replay.py` verifies the blobs, replays at chunk sizes 1, 37 and 4096 and asserts **byte-identical** event streams
(passed for all three). Counts are OrcSDR observations; upstream thresholds are upstream assertions only.

| Recording | Dibits | FSW exact / near | FSW+LICH frames | SACCH CRC6-valid | RAN (CRC-valid) | Frame spacing (dibits) | Carrier | Raw 72-bit VCH candidates |
|---|---:|---|---:|---:|---|---|---:|---:|
| NXDN48 voice 447.100 | 2879 | 11 / 3 | 12 | 12, 0 rejected | 7 (all 12) | 192 ×9, 384 ×2 (one frame missed twice) | +407.9 Hz | 48 |
| NXDN96 voice 447.100 | 3840 | 18 / 1 | 19 | 19, 0 rejected | 7 (all 19) | 192 ×18 | +382.0 Hz | 40 |
| NXDN96 RCCH 453.775 | 4800 | 21 / 1 | 22 (RFCT 0, direction 1) | 0 (not applicable to RCCH) | none | 192 ×21 | −664.9 Hz | 0 |

Relation to the upstream reference thresholds (comparison, not equivalence):

- RCCH ≥18 FSW/LICH frames at exactly 192 dibits and carrier −750…−550 Hz: OrcSDR observed 22 frames, all 192 apart,
  −664.9 Hz (19 before the timing-gain change in §4a).
   **Meets the physical-layer part (Gate A).** It validates nothing about CAC CRC or Type-C grants.
- Voice ≥32 (NXDN48) / ≥24 (NXDN96) voice-channel frames, Golay ≤1.0/frame, pitch continuity ≥50%: OrcSDR produces only
  **raw, un-Golay-checked 72-bit candidates** (48 and 40). The count exceeds the upstream floor numerically, but the
  measurements are not the same thing. Golay corrections, 49-bit extraction and pitch continuity are **not measured**; the
  voice thresholds are therefore **not claimed as passed**.
- Observed on both voice clips: the SACCH `structure` field counts down 3,2,1,0,3,… on every consecutive traffic frame
  (31 CRC-valid frames; the NXDN48 skipped frame appears as a missing value exactly at the 384-dibit spacings). RAN 7 on
  all 31. RAN 7 is OrcSDR's CRC-valid reading; it has no independent confirmation (§8).
- Frame loss and the 4a fix: the first pass found only 19 RCCH frames, none before dibit 1017, although the signal is
  present from sample 0. FSW-level probing showed the dibit stream was degraded for the first ~800 dibits, not that the
  decoder rejected good frames. Sweeping loop constants on a scratch copy (carrier τ, symbol DC and level rates,
  Mueller-Muller gain) showed only the timing-loop gain mattered: 0.05→0.15 raised exact FSW hits from 19→22 on the RCCH
  clip with NXDN48 and NXDN96-voice unchanged or better, and carrier estimates identical. **Shipped change:** the
  Mueller-Muller gain in `frontend.cpp` is 0.15 (one constant). After it: RCCH 22 frames (first at dibit 441), NXDN48 12,
  NXDN96 voice 19; all synthetic suites and Release/ASan pass; chunk-independence unchanged. Caveats: three short clips
  from one origin, so the gain is tuned on very little data; it has not been tested on weak or noisy real signals, and a
  higher timing gain can cost noise margin. Treat as provisional until longer, independent recordings exist. NXDN48 still
  loses two frames (one exact FSW has a failed LICH) and has no explanation yet.

WaveShark long clips, same production path: **not acquired.** NXDN48 11.8 s: 29 one-dibit-error FSW matches but 0 exact,
1 LICH, 0 SACCH. NXDN96 15.3 s: 1 near match, 0 LICH. Findings from a separate numpy probe (diagnostic, not decoder
code): both are FM-like signals with plausible deviation; NXDN48 shows a 2400 Hz symbol-rate line; NXDN96 shows no clear
4800 Hz line; a brute-force 4-level mapping/timing search found no FSW above chance. Treat these two files as unproven
inputs (provenance/format/mapping unknown), not as decoder failures and not as positives.

## 5. Synthetic versus real-air evidence

| Evidence | Synthetic | Real air |
|---|---|---|
| FSW, PN9, LICH parity | `test_decoder.cpp`, `nxdn_synth.cpp` round-trips | 19/19 (RCCH), 19/19 and 12/12 voice frames agree |
| SACCH K=5 Viterbi + CRC6 + RAN | round-trip incl. corruption cases | 31/31 CRC-valid, RAN 7, structure countdown |
| Chunk-size independence | unit tests | byte-identical at 1/37/4096 on all 3 clips |
| Opaque SACCH reassembly | `test_reassembly.cpp` (10 tests, Release+ASan/UBSan) | 4 opaque 72-bit messages (NXDN96 ×4; NXDN48 ×0, its two lost frames break the anchor run) |
| Golay / 49-bit / AMBE+2 | none in this component | none |
| Layer 3 fields, CAC, trunking | none | none |

Synthetic round-trips prove self-consistency, not interoperability. The real-air agreement is the only interoperability
evidence for the physical layer, and it is limited to 3 short clips from one origin.

## 6. Current decoder capability (one frontend constant changed, see §4)

48 kS/s IQ → NXDN48/NXDN96 4FSK dibits; exact/1-error FSW; PN9 descrambling; LICH with parity; CRC6-valid SACCH with RAN
and raw 18-bit fragments; raw 72-bit voice candidates. Not implemented: Golay, 49-bit extraction, AMBE+2 output, FACCH,
CAC, any identity, trunking, P4/Tab5 wiring.

## 7. Layer 3 reassembly status

New, in its own module (`sacch_reassembly.hpp/.cpp`, no `main.cpp` or dashboard change): a **transport-only** reassembler
that orders CRC-valid SACCH fragments into **opaque 72-bit strings**. It carries RAN through, enforces exact 192-dibit
spacing, a consistent context (mode/RFCT/FCT/direction/RAN), successor ordering and anchoring, rejects malformed
input, is allocation-free and fixed-size, and resets on retune. The single protocol-shaped assumption (descending
structure 3→0, anchored at 3) is an **empirical observation from the three recordings, configurable and marked
spec-unconfirmed**. It assigns no message type, ID, call type or privacy class and makes no CRC claim: whatever
integrity check the assembled message carries is unknown until TS 1-A is available. `orcsdr_nxdn_replay --opaque`
prints the bit strings (keep that output out of public commits).

## 8. Identity-decoding acceptance requirements (Gates C and D)

1. Normative Layer 3 field definitions and CRC procedure from an authorised TS 1-A edition, cited by section/table/page
   in the code and in `docs/nxdn/`; a parser and CRC written independently from that text.
2. Independent test vectors (from the standard's examples or independently constructed from the text), run in
   Release and ASan/UBSan; the assembled messages from §7 become the input.
3. At least one real recording with **independently** verified source and destination identities (authorised capture
   metadata or a second, independent reference decoder run by the owner). The upstream tests publish none for the three
   REA clips, and the publisher-reported WaveShark IDs are unverified (and those files do not decode).
4. Clear separation of "CRC-valid", "parsed" and "independently confirmed" in every event and UI string.

## 9. Trunking research requirements (Gate E)

Type-C: CAC channel coding/CRC, grant, site and channel-map messages from the normative text; real RCCH recordings with
known system information (the RCCH clip here is 1 s and cannot validate this); traffic-following across retunes with
reset behaviour proven. Type-D: distributed SCCH, separately sourced recordings; no inference from Type-C. The Trunk
Recorder corpus submissions listed in `tests/fixtures/nxdn/README.md` are receipts, not files, and remain to be
resolved through its public metadata.

## 10. Experimental voice branch audit (`agents/orcsdr-nxdn-voice-proof-20261124`)

Reviewed `docs/nxdn/VOICE_BACKEND_HANDOFF.md` and `components/orcsdr_nxdn_voice/voice_gate.cpp`. Findings:

- Its real-IQ processing used a **Python research extractor** (discriminator, "affine FSW fitting", symbol-phase search),
  not the production frontend; it says so itself. OrcSDR's own frontend now reproduces its frame/RAN picture on the
  voice clips (NXDN96 19 frames both; RAN 7 both; its NXDN48 14 frames vs our 12; its RCCH 24 vs our 19), so the two
  paths agree on RAN and disagree on frame yield — a quantified Gate A gap, not an identity result.
- Its FACCH1/VCALL parser hard-codes a message layout (byte offsets for source/destination/type/option/cipher/key), a
  12-bit CRC (`0x80f`, preset all-ones), a 16×9 interleaver, a puncture pattern and a trellis, with a code comment citing
  "NXDN TS 1-A". **No copy of TS 1-A is available to this project, so these cannot be audited and must be treated as
  unverified.** The origin of the layout is not documented in the branch.
- Source ID 8814 comes from that parser on those clips. It is **not established by real air**: the unit test builds a
  synthetic message containing 8814, and no independent reference confirms it for the recording. Not promoted.
- The AMBE+2 synthesis reuse (existing V2 engine) may be reusable; its protocol adapter must pass Gates B–D itself.
  The code is written as dense single-line functions, which makes review hard; it should be reformatted before any merge.
- Not merged here, and nothing from it was imported.

## 11. Blockers and next steps

Blockers: (1) normative TS 1-A (owner action, §2); (2) no independent ground truth for identities; (3) the two long
recordings are not decoded and their provenance is unclear.

Doable now without the spec, in order (the RCCH start-up loss was addressed, see §4): (a) find why NXDN48 loses two frames (one exact FSW with a failed LICH) and validate the 0.15 timing gain on weak/noisy
real signals;
(b) investigate the WaveShark clips (check the true sample format/rate/deviation from the SigIDWiki page; a
discriminator-level eye diagram), without changing the decoder to fit them; (c) wire the reassembler into a bounded
read-only IQ tap only after the owner approves any Tab5 work; (d) when the spec arrives, implement FACCH/Layer 3 and the
CRC from it and test them on the §7 messages.
