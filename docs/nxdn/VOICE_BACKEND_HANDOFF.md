# NXDN half-rate voice proof — 2026-11-24

Experimental host proof, not integrated firmware. Both rates use the existing V2 AMBE+2 engine, not a new codec implementation. Base: cb653715a93e48e4a582765c4c8f07b8d71ea41c. No claims of commercial superiority or human intelligibility.

## Evidence
Audited RSA-Sandbox and REA-Results. REA branch research/nxdn48-nxdn96-foundation-20261119 has three genuine-air CS16 LE complex 48 kS/s captures, sourced from MattCheramie/GopherTrunk commit f06bfff493f47fdeb69807c8b08b2b32a253839f, internal/radio/nxdn/testdata. All three Git blob hashes independently verified in sandbox:
- nxdn48_rdch_voice_447100_16oct_48k.cs16: 230400 bytes, 26aed395774380be0cf6a0ebdf391723a9335e00.
- nxdn96_rdch_voice_447100_15oct_48k.cs16: 153600 bytes, 01845523c82df3a279712b577a0f0180653f981f.
- nxdn96_rcch_cc_453775_19oct_48k.cs16: 192000 bytes, c4a1bdb2fdd36cc316877982781c99bc7043462d.

Actual IQ was processed in the research sandbox via discriminator, affine FSW fitting and symbol phase search; measured packed dibits replayed into the compiled C++ gate and V2 vocoder in the Linux sandbox. Not a production streaming frontend proof. The Python research extractor is an independently executable equivalent path; CI is intended to reproduce the complete path directly from pinned IQ.

| Rate | Measured RF frames | Qualified AMBE frames | Nonzero frames | Golay corrections | Rejected | PCM |
|---|---:|---:|---:|---:|---:|---:|
| NXDN48 | 14 | 16 | 16 | 0 | 0 | 0.32 s |
| NXDN96 | 19 | 32 | 32 | 3 | 0 | 0.64 s |
| NXDN96 RCCH | 24 | 0 | 0 | 0 | 0 | none |

Both voice captures RAN 7, VCALL source 8814, destination 0, cipher/key zero. Destination 0 must not be invented into a talkgroup label. NXDN96 duplicate FACCH1: 18 valid, zero invalid. PCM mono s16le 8 kHz, 160 samples per AMBE frame, no clipping. Local Release and ASan/UBSan gate tests passed including all 144 single-bit FACCH1 errors, unsupported full-rate, protected, unknown, invalid and discontinuity states. Four user's DMR IQ files tested in both modes: zero NXDN frames/output. Local NXDN hunter candidates are not verified clear voice recordings.

## Design and integration
Gate consumes complete, exact-sync, validated LICH/CRC6 SACCH traffic frames, with all 144 information dibits PN9-descrambled into 288 bits (including FACCH1). NXDN48 buffers four SACCH fragments in structure order 3,2,1,0, verifies clear half-rate VCALL before releasing 16 VCH. NXDN96 EHR requires matching clear duplicated CRC12 FACCH1 VCALL before/after each four-VCH block. 40 ms RF voice frame plus 40 ms signaling frame carries 80 ms speech; do not insert extra silence for the signaling frame. Unsupported EFR, mixed stealing, control, unqualified signaling, gaps and protected voice are silent. This conservative policy sacrifices recovery/latency and is not a complete NXDN receiver.

Extend existing native decoder observation to expose all information dibits and validated SACCH fields in one coherent event, not just candidate VCH. Preserve metadata path. Use separate gates/codec contexts for NXDN48/96, reset on RF epoch, mode/polarity/retune/gaps, reject unknown and protected calls. Queue bounded batches to shared audio worker; 20 ms at 8 kHz, resample continuously using existing audio path. NXDN48 initially buffers 320 ms; NXDN96 needs trailing FACCH confirmation. Never put decoder or codec on UI task. No second dashboard, tuner, audio owner, or codec. Add measured status/counters to existing Digital Voice pages and qualify live playback on Tab5 before claiming support.

Compare longer authorized captures against independent DSD-FME reference and commercial receivers with paired blind listening, frame recovery, latency, clipping and device CPU. Existing NXDN48 frontend needs real-time optimization. CAC/Type-C trunk following is separate future work. No third-party competitor source copied; preserve existing mbelib attribution and obtain codec patent/licensing review before distribution. Raw captures, dibits and decoded voice must not be committed/uploaded to public CI artifacts. 
