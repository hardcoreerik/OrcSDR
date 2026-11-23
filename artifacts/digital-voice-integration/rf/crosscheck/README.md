# Third-party cross-check, 2026-11-23

DSD-FME (official release 20260715, `dsd-fme-x86-64-cygwin-portable`, sha256 006cfa42...e9f8, from github.com/lwvmobile/dsd-fme)
run on FM-discriminator WAVs made by `rf/fm_wav.py` from the same PC V4+LNA captures our decoder saw. Not committed software; the
binary stays in the session scratchpad.

| Capture | Our decoder (`dv_inspect`) | DSD-FME |
|---|---|---|
| 152.1575 Terra (30 s) | slot 1: 340 CSBK CRC-valid, ~115 MBC bursts unvalidated; slot 2: idle | Same slot pattern. CSBK/MBC decoded: C_ALOHA_SYS_PARMS (Net 13, Site 1.14), adjacent site 1.11, mass registration, local time, general site params. No grants, no voice, no calls. |
| 451.9125 TRBOWest (20 s) | both slots: sync + Golay valid, CSBK fails BPTC/CRC | CC 9, Net 1, Site 1.5 via SLC, but CSBKs also report `CSBK (CRC ERR)` |

Conclusions: our decoder is correct but shallow (CRC-checks CSBKs, never parses them); no call traffic was in the Terra capture;
the Capacity Max control channel fails CRC in both decoders, so it is not just our bug (cause unconfirmed).
