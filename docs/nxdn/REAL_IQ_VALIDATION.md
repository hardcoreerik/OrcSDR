# NXDN real-air input manifest and acceptance gates

Private repository: `hardcoreerik/REA-Results`, branch `research/nxdn48-nxdn96-foundation-20261119` at `65713314c2effc357b43fbd91bb79766967435a6`. Original public capture source: `MattCheramie/GopherTrunk` at `f06bfff493f47fdeb69807c8b08b2b32a253839f`, directory `internal/radio/nxdn/testdata/`. Format for **all three**: interleaved signed int16 little-endian complex IQ at 48,000 samples/sec.

| Original filename | Bytes | Expected Git blob SHA-1 |
|---|---:|---|
| nxdn48_rdch_voice_447100_16oct_48k.cs16 | 230400 | `26aed395774380be0cf6a0ebdf391723a9335e00` |
| nxdn96_rdch_voice_447100_15oct_48k.cs16 | 153600 | `01845523c82df3a279712b577a0f0180653f981f` |
| nxdn96_rcch_cc_453775_19oct_48k.cs16 | 192000 | `c4a1bdb2fdd36cc316877982781c99bc7043462d` |

The checker `tests/nxdn_replay_acceptance/check_replay.py` verifies exact blobs, records SHA-256 and actual OrcSDR frame/CRC/candidate counts, and rejects chunk-dependent event streams for chunk sizes 1,37,4096. CI retrieves the public source-pinned files instead of committing third-party binary recordings to OrcSDR.

Upstream targets — **not OrcSDR passes**: NXDN48 ≥32 VCH frames; NXDN96 ≥24 VCH frames; NXDN96 RCCH ≥18 FSW/LICH frames at 192-dibit spacing and −750 to −550 Hz offset. **OrcSDR only extracts raw voice codeword candidates today, not Golay-validated VCH**, and cannot honestly claim the first two targets yet. RCCH CAC CRC and Type-C grants are not validated by upstream FSW/LICH tests.

Local sandbox has run Release and ASan/UBSan synthetic IQ and protocol tests. Genuine-air decoder metrics and source SHA-256 remain pending execution of pinned CI replay; no observed numbers were fabricated. REA AUTO noise/AM controls must also be replayed before claiming classifier false-positive bounds. 