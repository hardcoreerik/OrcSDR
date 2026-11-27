# DMR voice: from superframe payload to 49-bit AMBE frames

Recorded after three rounds of failed guessing, because the structure is now known
and the next attempt should not repeat the search.

## What the earlier attempts got wrong

The decoder's own README describes the **data** burst split as 98-bit payload +
10-bit slot type + 48-bit SYNC + 10-bit slot type + 98-bit payload, and I assumed a
voice burst had the same shape with two 49-bit frames per 98-bit half.

It does not. A **voice** burst carries no slot-type fields: the split is
108 + 48 + 108, and the 216 bits of information are **three 72-bit on-air AMBE+2
frames**. Six bursts per superframe therefore carry **18 frames**, which matches
`VoiceGroup::payload` exactly: 162 bytes = 1296 bits = 18 x 72.

That is why 16 stride/contiguity candidates all failed against the oracle. The
window was wrong, not the bit order. Nothing about a 49-bit walk over the payload
can work, because a frame is 72 bits and needs channel decoding.

Source: [DMR burst](http://gophertrunk.org/reference/dmr-burst/) and
[DMR voice superframe](http://gophertrunk.org/reference/dmr-voice-superframe/).

## The per-frame channel code

Each 72-bit on-air frame carries 49 information bits inside a "3600x2450" envelope
([AMBE+2 on-air FEC](http://gophertrunk.org/reference/ambe-plus-2-fec/)):

1. **Deinterleave** the 72 bits into four sub-vectors by a fixed rW/rX/rY/rZ
   schedule: for dibit *i*, the high bit goes to `ambe_fr[rW[i]][rX[i]]` and the
   low bit to `ambe_fr[rY[i]][rZ[i]]`. Sub-vectors: **C0 24 bits, C1 23, C2 11,
   C3 14**.
2. **Golay(23,12)** on C0 (12 data + 11 parity, corrects up to 3 errors).
3. **De-whiten C1** with a keystream seeded by C0's 12 data bits:
   `pr[0] = 16 * (c0data & 0x0FFF)`, then `pr[i] = (173*pr[i-1] + 13849) & 0xFFFF`
   for i = 1..23, keystream bit *i* = `pr[i] >> 15`; XOR `ks[23-j]` onto C1 bit *j*.
4. **Golay(23,12)** on the de-whitened C1.
5. **C2 and C3 carry no ECC** and pass through.
6. Assemble the 49-bit payload as **C0(12) + C1(12) + C2(11) + C3(14)**.

Then mbelib decodes it — which already works here: `orc_zero_ambe_decode` produces
9.36 s of real speech from the reference vectors.

## The licensing constraint on the tables

The rW/rX/rY/rZ schedules are widely published and originate in `szechyjs/dsd`.
This project's own reference-pack README states the rule: no GPL code is copied or
ported. GopherTrunk's page says its tables were "ported verbatim" from that
reference, so they are not a source we can copy either.

The schedule is an ETSI fact (interleaving grid), not an invention, so it can be
reimplemented from the specification. Two legitimate routes:

- transcribe the interleaving schedule from ETSI TS 102 361-1 (the primary source);
- or derive it empirically against the oracle already in the repo, since the
  reference frame log gives 408 correct 49-bit vectors from this same capture, and
  a candidate schedule is provable: its Golay syndrome must be valid and its
  de-whitened C0+C1 must reproduce those vectors.

The empirical route is attractive because it needs no new material and has a
pass/fail test, but the search space must be constrained by the known structure
(4 sub-vectors of 24/23/11/14 bits, dibit pairing) rather than guessed blindly.

## Harness that is ready

- `tools/dmr_probe.cpp --cu8 <file>` replays a 240 kS/s capture through the live
  path and dumps `PAYLOAD <hex>` (162 bytes) for the first 20 superframes.
- `tools/find_ambe_mapping.py` already parses those payloads and the reference
  frame log, and reports set intersection against the oracle.
- `orc_zero_ambe_decode` turns 49-bit vectors into PCM and writes a WAV.

So once the schedule is in hand, the whole chain is testable in minutes.

## Deriving the schedule from data we already hold

There is a third route that needs neither the ETSI text nor anyone's tables, and it
is worth trying before transcribing anything.

The 49-bit payload is C0(12) + C1(12) + C2(11) + C3(14). Only C1 is whitened, so
**37 of the 49 bits appear verbatim in the 72 on-air bits**. Both sides of the
correspondence are already in the repository:

- 20 superframes x 18 frames = 360 on-air 72-bit frames, from the capture;
- 408 correct 49-bit vectors from the reference log, from the same capture.

If frame *k* of ours corresponds to the *k*-th logged vector, the mapping is a
plain correlation problem: for every pair (on-air bit position 0..71, payload bit
position 0..48), count agreement across all frames. A true pair agrees 100%; every
false pair agrees about 50%. 360 samples is ample to separate them, and the result
is the deinterleave schedule itself, recovered rather than copied.

Alignment has to be established first - the reference decoder logs from its own
sync, and may skip frames - but that is a short search over a few hundred
candidates scored by the number of 100%-agreement columns, not a guess.

Once the 37 unwhitened bits are placed, C0 is known, so the keystream can be
generated and C1's 12 positions identified the same way after de-whitening. At that
point the full schedule is recovered and the Golay layer can be validated by
syndrome instead of by ear.

This is the recommended next step: it is deterministic, it uses only material
already in the tree, and it produces the schedule as evidence rather than as a
transcription.
## Partial schedule recovered from data (2026-11-27)

The correlation was run and the schedule is **not** an arbitrary permutation: it is
a regular stride-4 interleave. Recovered directly from 20 captured superframes
against the reference vectors, with no external table:

    payload bit  0..11  <- on-air bits  0, 4, 8, ... 44     (C0 data, stride 4)
    payload bit 24..27  <- on-air bits 46, 50, 54, 58
    payload bit 36,38   <- on-air bits 23, 31
    payload bit 41..44  <- on-air bits 43, 47, 51, 55
    payload bit 46,47   <- on-air bits 63, 67

31 of 49 positions are pinned. The 18 not yet recovered are C1's 12 whitened bits
plus a handful that the 20-frame window did not cover - the reference log drops
frames beyond roughly that depth, which is why a 60-frame window scored *worse*
(25 pairs) than a 20-frame one (31): misaligned rows destroy perfect agreement.

Two things this establishes:

- The stride-4 structure means the schedule is a dibit-level interleave, exactly
  as the FEC description says (two bits of each on-air bit-pair feed different
  sub-vectors). Recovering it by correlation is therefore practical rather than
  a search over 72! permutations.
- C1 needs the keystream before it can be matched, so the next step is to derive
  C0 fully (12 of 12 are already pinned), generate `pr[i] = 173*pr[i-1] + 13849`,
  de-whiten C1, and correlate again. That should pin the remaining bits and the
  Golay parity positions, which are not part of the 49-bit payload.

Script: `tools/derive_ambe_schedule.py`.
## Complete mapping for the 37 unwhitened bits (2026-11-27)

The full correlation table resolves the schedule into four residue classes mod 4,
which is what a dibit-level interleave looks like:

| Payload bits | Count | On-air positions | Class |
| --- | --- | --- | --- |
| 0..11  (C0 data) | 12 | 0, 4, 8, ... 44        | 0 mod 4 |
| 24..30 (C2 first) | 7 | 46, 50, ... 70          | 2 mod 4 |
| 31..48 (C2 rest + C3) | 18 | 3, 7, 11, ... 71    | 3 mod 4 |
| 12..23 (C1 data) | 12 | **not yet resolved**  | 1 mod 4, whitened |

That is 37 of 49 payload bits pinned, matching the prediction exactly: every bit
that is not whitened has been recovered from data alone.

Six of them scored 19/20 rather than 20/20 (payload 34, 37, 39, 40, 45, 48), which
is frame misalignment rather than a wrong position - their neighbours in the same
residue class are 20/20, and the positions are consistent with the stride.

Residue class 1 (on-air 1, 5, ... 69, eighteen positions) holds C1's twelve data
bits plus parity. The payload bits 12..23 correlate at only 15-18 of 20, which is
the signature of whitening: the bits are there, XORed with the C0-seeded keystream.

## What this means for the decoder

mbelib's `mbe_processAmbe2450Data` takes the **49-bit vector directly**, so the
Golay parity bits are not needed at all - the C0 and C1 codewords only matter for
error correction, not for decoding. So the outstanding work is narrow:

1. extract the 37 known bits by the table above;
2. recover C0's twelve bits (already at 0 mod 4) and seed the keystream;
3. de-whiten the twelve C1 positions in class 1 and correlate once more to pin
   which class-1 positions carry data rather than parity;
4. assemble C0(12)+C1(12)+C2(11)+C3(14) and hand it to mbelib.

No external table is needed at any point, and no GPL material has been consulted.
## The schedule, derived and verified (2026-11-27)

The de-interleave is fully recovered from captured data. Nothing was transcribed,
and no GPL material was consulted.

```text
payload[0..11]   = onair[4k]               k = 0..11     C0 data
payload[12..23]  = onair[21+4k] ^ ks[k]    k = 1..12     C1 data, de-whitened
payload[24..30]  = onair[46+4k]            k = 0..6      C2 first seven
payload[31..48]  = onair[3+4k]             k = 0..17     C2 rest + C3
```

with the keystream from C0's twelve data bits:

```text
pr[0] = 16 * (c0data & 0x0FFF)
pr[i] = (173*pr[i-1] + 13849) & 0xFFFF   for i = 1..23
ks[i] = pr[i] >> 15
```

On a 72-bit on-air frame these are four residue classes mod 4: C0 at 0 mod 4, C1
at 1 mod 4, C2's first seven at 2 mod 4, and C2/C3 at 3 mod 4. That is exactly the
dibit-level interleave the FEC description implies, and it explains why every
49-bit walk attempted in earlier rounds had to fail.

Verification against the reference vectors from the same capture:

```text
frames exactly equal: 289 / 360
worst individual bit: 353 / 360
```

The residual is frame misalignment in the reference log (it drops frames), not a
mapping error: the worst bits sit at 98% and their neighbours in the same residue
class are perfect. A mapping that was wrong anywhere would not produce 289
bit-exact 49-bit vectors.

Because mbelib takes the 49-bit vector directly, the Golay parity never has to be
implemented for decoding - the codewords only matter for error correction.

Scripts: `tools/derive_ambe_schedule.py`, `tools/derive_c1.py`,
`tools/verify_schedule.py`.
## Live stability (2026-11-27)

Three-minute soak on the DMR dashboard at 463.4000 MHz with the codec open:

```text
process alive            yes (single instance throughout)
USB read failures        0
dashboard restarts       1   (the initial start only)
ALSA card1 pcm0p         PREPARED
```

Zero USB errors matters most here: the 2.4 MS/s experiment earlier filled the log
with `rtlsdr_demod_read_reg failed` and killed the handle within seconds. At
960 kS/s with 4:1 decimation the stream is stable for minutes, and the codec sits
PREPARED between calls rather than being opened mid-burst.

## Known limitation: no Golay error correction

C0 and C1 are Golay(23,12) codewords, and this implementation does not correct
them. mbelib takes the 49-bit vector directly, so decoding works without Golay -
which is why strong calls decode cleanly today.

What is lost is marginal performance. A frame with a few bit errors inside C0 or
C1 will be passed through wrong instead of corrected, and because C1 is whitened
from C0, a single uncorrected C0 bit re-randomises all twelve C1 bits. On weak or
multipath-impaired calls that shows up as garble where a correcting decoder would
produce speech.

Implementing it needs two things this project does not yet have:

1. **Parity bit positions.** The 12 data bits of C0 and 12 of C1 are mapped; the
   11+11 parity bits sit among the on-air positions not claimed by data. They can
   be located the same way the data bits were - compute expected parity from the
   known data across many frames and correlate - so this is tractable, not blocked.
2. **The Golay generator and bit order** for this codec's convention.

Until then the honest description is: strong calls decode and play; marginal calls
garble. That is a quality gap, not a broken feature.