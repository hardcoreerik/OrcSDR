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
