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
