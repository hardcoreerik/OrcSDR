# OrcSDR native AMBE+2 — experimental voice proof V2

This is an isolated offline engineering workstream, not live Tab5 audio integration. It adds a pinned mbelib AMBE+2 backend (same upstream revision as the existing P25 IMBE implementation), a caller-owned context, DMR frame deinterleave, extended Golay C0 rejection, bounded PCM gain and nominal silence muting. No change to the existing P25 path.

## Reproduce on a Linux host

From the repository root, with GCC, Python, NumPy and SciPy:

```sh
mkdir -p output
gcc -O2 -Icomponents/mbelib -Icomponents/orcsdr_vocoder components/mbelib/{mbelib,ambe3600x2450,ecc}.c components/orcsdr_vocoder/orcsdr_vocoder.c tools/orcsdr_vocoder/voice_cli.c -lm -o output/voice_cli
gcc -O2 -fPIC -shared -Icomponents/mbelib -Icomponents/orcsdr_vocoder components/mbelib/{mbelib,ambe3600x2450,ecc}.c components/orcsdr_vocoder/orcsdr_vocoder.c -lm -o output/liborcsdr_vocoder.so
set -o pipefail
python tools/orcsdr_vocoder/replay_v2.py tests/digital_voice_corpus/iq output output/voice_cli output/liborcsdr_vocoder.so 2>&1 | tee output/voice-proof.log
```

Optional comparison only: append `--reference /path/to/vocoder_reference_pack.zip`. The reference pack is external test evidence, not a dependency; no DSD-FME code, audio or frame vectors are copied into this commit. V1 replay is retained for historical comparison.

## Local sandbox results (2026-11-24 UTC)

| Input | Accepted V2 frames | External DSD-FME frames | Exact sequence-aligned parameter matches |
|---|---:|---:|---:|
| terra_152p1575_call_cc13.cu8 | 468 | 468 | 374 |
| terra_152p685_call_cc8.cu8 | 198 | 153 | 71 |
| terra_153p068_call_cc8.cu8 | 18 | 18 | 17 |
| terra_152p1575_control_only.cu8 | 0 | not compared | not compared |

Primary call: 378 frames, 7.56 s, radio 4106011, TS2. Other qualified audio epochs: 90, 162, 36 and 18 frames. Exact matching uses SequenceMatcher and is not proof of bit accuracy, especially for repeated silence vectors. The 45 additional frames in the second capture are not independently validated; no claim of superiority or intelligibility. Results are on four uploaded files; CI covers the repository corpus. Reference labels are not supplied to extraction.

## Integration requirements

Port the voice extraction and bounded call state to the production native DMR frontend/D1 protocol decoder. Do not embed the offline Python path on ESP32-P4. Preserve single receiver ownership, existing audio ownership and P25 behavior. Validate independent slot continuity, EMB/LC integrity, clear privacy state, missing bursts, retune epochs and queue discontinuities; no protected/unknown voice playback. Correct the existing D1 embedded LC checksum-bit layout against the standard and add independent test vectors. Avoid global codec RNG concurrency (serialize synthesis). Qualify timing, CPU, heap, stack, queue bounds, IQ loss, 8-to-48 kHz PCM resampling, mute, slot selection and the actual speaker on Tab5. First target single selected slot audio with dual-slot metadata. Do not enable live audio until regression and device qualification are complete.

Limitations: experimental BS-mode offline synchronizer only, strict CACH and embedded gating, no exhaustive negative/impairment acceptance, no device benchmark, no confirmed speech transcript. Legacy error diagnostics in parameter traces differ from extended Golay corrections in the adapter. Commercial codec rights/patent status remain unassessed; ISC source notice does not establish freedom to distribute codecs.
