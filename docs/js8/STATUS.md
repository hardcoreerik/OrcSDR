# JS8 decoder status

## 2026-10-08 — slice 1

Branch target: `claude/ft8-native-bind`.

Implemented in the standalone `orcsdr::js8` module:
- documented submode profiles;
- Normal 79-tone frame geometry;
- independently evidenced Normal sync pattern from the public JS8Call API tone vector;
- raw-tone validation and extraction of the 58 data tones;
- optimized and ASan/UBSan host unit tests.

Current decode capability: **no user-visible JS8 messages yet**.

Blocked pending owner/reference captures and reconstruction:
- tone-to-bit mapping;
- exact FEC;
- exact CRC;
- upper-layer bit packing;
- SNR calibration.

Fast, JS8 40 and Slow timing/bandwidth are recorded from public documentation, but their exact sync patterns remain disabled until independently measured. JS8 60 remains experimental.

No firmware binding, UI, transmit path, PTT, CAT, scheduler or flashing work was added.


## Reconstruction tool ready

The host-only `js8-fec-reconstruct` tool is now ready for the PC capture corpus. It does not contain an external FEC matrix. It derives the GF(2) code-space rank and parity-check basis from supplied 174-bit observations.

Current blocker remains real/controlled JS8 frames plus an independently established tone-to-bit mapping.


## Tone-label search ready

The host reconstruction lab can now:
1. accept verified Normal 79-tone frames;
2. extract the 58 data tones;
3. enumerate all 40,320 tone-label permutations;
4. rank the resulting 174-bit codeword corpus;
5. pass promising mappings into the GF(2) parity-basis derivation tool.

No tone mapping has been claimed yet. The capture corpus is the next evidence dependency.


## Candidate-local PCM demodulator

The standalone module now includes a heap-free Normal-mode candidate demodulator. Given 12 kHz PCM plus a caller-supplied frame start and tone-0 audio frequency, it measures all eight tone energies per symbol, returns the 79 raw tone decisions, and reports sync hits, sync contrast and winner margin.

A deterministic host-only waveform fixture based on the public API tone vector recovers all 79 tones with added noise and rejects a deliberately wrong base-frequency candidate. This validates the receive primitive only; it is not a claim that the synthetic waveform models every JS8 modulation detail.

There is still no whole-passband candidate search, FEC, CRC, message parser, SNR calibration or firmware binding.


## Bounded Normal sync search

The standalone JS8 module now has a heap-free spectral-grid candidate search for Normal mode. It scores the three independently established sync blocks, applies bounded non-maximum suppression, and returns caller-owned candidates ranked by sync contrast.

Synthetic energy-grid tests recover the injected candidate exactly, reject a uniform-noise grid at the test threshold, and refuse Fast because its exact sync permutations have not been independently established.

This is still pre-FEC: a sync candidate is evidence of a JS8-like frame, not a decoded message.
