# JS8 protocol facts and evidence

Status: receive-only reconstruction in progress.

This document is the evidence ledger for OrcSDR's independent JS8 receive implementation. A fact is not promoted to implementation input merely because it is commonly repeated elsewhere.

Status labels:
- **DOCUMENTED** — stated in public user/API documentation.
- **MEASURED** — recovered from a named recording or tone vector.
- **DERIVED** — mathematically reconstructed from measured frames.
- **REFERENCE-VERIFIED** — checked against a released decoder as a black box.
- **BLOCKED** — insufficient independent evidence to implement safely.
- **EXPERIMENTAL** — explicitly unstable or unpublished.

## Public evidence used in slice 1

1. JS8Call User Guide v3.0 technical implementation:
   https://js8call.com/downloads/JS8Call_User_Guide.pdf
2. JS8Call public API documentation v3.0.0:
   https://js8call.com/JS8Call-improved/d7/d15/md_docs_2API.html

The implementation in this slice does not use JS8Call decoder source, FEC tables, CRC code, or transmitter code.

## Physical layer

| Fact | Status | Evidence |
|---|---|---|
| 8 tones, 79 channel symbols | DOCUMENTED | User Guide modulation table |
| Normal: 12.64 s waveform, 6.25 baud, 6.25 Hz spacing, about 50 Hz occupied width | DOCUMENTED | User Guide modulation table |
| Fast: 7.9 s, 10 baud, 10 Hz / 80 Hz | DOCUMENTED | User Guide modulation table |
| JS8 40 (formerly Turbo): 3.95 s, 20 baud, 20 Hz / 160 Hz | DOCUMENTED | User Guide modulation table |
| Slow: 25.28 s, 3.125 baud, 3.125 Hz / 25 Hz | DOCUMENTED | User Guide modulation table |
| JS8 60 | EXPERIMENTAL | User Guide says its specification is unpublished and may change |
| Three seven-symbol synchronization blocks | DOCUMENTED | User Guide |
| Normal uses the same seven-tone sync block three times | MEASURED | Public API TX.FRAME example: positions 0..6, 36..42 and 72..78 are all 4,2,5,6,1,3,0 |
| Normal data-tone positions are 7..35 and 43..71 (58 total) | DERIVED | 79 symbols minus the three measured seven-symbol sync blocks |
| Fast / JS8 40 / Slow use three distinct sync blocks | DOCUMENTED | User Guide |
| Exact Fast / JS8 40 / Slow sync tone permutations | BLOCKED | Not yet independently measured from owner captures |
| Tone-to-bit labeling / Gray mapping | BLOCKED | Requires reconstruction from known frames |
| Exact continuous-phase / pulse-shaping details needed for a test encoder | BLOCKED | Not needed for slice 1 receive-side tone handling |

The OrcSDR module assumes its future backend receives 12 kHz mono analysis PCM because that is the established native weak-signal seam in OrcSDR. This is a project interface, not claimed here as a newly reconstructed JS8 protocol fact.

## Coding and acceptance

| Fact | Status | Evidence |
|---|---|---|
| Data frames have a 75-bit payload space | DOCUMENTED | User Guide Protocol/Data section |
| Exact FEC code definition | BLOCKED | Will be reconstructed from observed valid channel codewords and withheld verification frames |
| Exact CRC parameters and bit ordering | BLOCKED | Will be reconstructed after the FEC information word is recoverable |
| A displayed OrcSDR JS8 message requires FEC + CRC success | PROJECT RULE | OrcSDR truth model |
| Noise-only false decodes must remain zero in the regression corpus | PROJECT RULE | Decoder acceptance gate |

## Upper protocol

The User Guide publicly documents six frame families: heartbeat, compound callsign partial, compound directed command, directed command, Huffman data, and dictionary-compressed data. It also documents default/first/last/reserved transmission flags.

Those facts are **DOCUMENTED**, but their exact bit packing remains **BLOCKED** until independently reconstructed or verified from reference pairs.

## Slice-1 implementation status

Implemented:
- separate namespace `orcsdr::js8`;
- submode/profile records;
- Normal sync geometry from the public API tone vector;
- 79-tone validation;
- exact Normal sync verification for fixture/test work;
- extraction of 58 data tones from a verified raw Normal frame.

Not implemented:
- audio spectral detector;
- tone-to-bit demapping;
- FEC;
- CRC;
- frame unpacking;
- message assembly;
- SNR;
- firmware binding.

Therefore this module cannot currently emit a user-visible JS8 decode.
