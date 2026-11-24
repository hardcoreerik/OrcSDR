# Vocoder reference pack (third-party decoder output)

Purpose: reference audio and frame logs from a **third-party** DMR decoder, so the OrcSDR vocoder being written separately has something to compare
against. **Nothing in this pack is source code, and none of it may be copied into OrcSDR.** It is evidence of what a correct decode of our recordings
sounds like and what the voice-frame stream looks like.

## What produced the audio

| Item | Value |
|---|---|
| Decoder | **DSD-FME** ("Digital Speech Decoder: Florida Man Edition", lwvmobile/dsd-fme), official Windows release `20260715`, file `dsd-fme-x86-64-cygwin-portable-20260715.zip` (sha256 `006cfa420e79033b51ded97ed4d2d0a9715e350ffcfe35c41e89b9e6ef00e9f8`), build string `AW 2026-34-g69d3115 (CYGWIN)` |
| Vocoder inside it | **mbelib 1.3.4** (the open reverse-engineered AMBE/AMBE+2 decoder). DSD-FME also lists Codec2 support; it is not used for DMR. |
| Licences | DSD-FME and mbelib are GPL-family projects. They were run as black boxes. OrcSDR does not link, copy or port them, and its repository rule is no GPL code. |
| Not assessed | Patent/licence status of the AMBE+2 vocoder in any jurisdiction. That is a decision for the project owner. |

## How the recordings were turned into audio

1. Source: three 14 s raw IQ recordings (`tests/digital_voice_corpus/iq/terra_152p1575_call_cc13.cu8`, `terra_152p685_call_cc8.cu8`, `terra_153p068_call_cc8.cu8`).
   Format: unsigned 8-bit interleaved I/Q, 240 kS/s, tuned 60 kHz below the channel (the channel sits +60 kHz from the file centre). RTL-SDR Blog V4, 30 dB gain.
2. `artifacts/digital-voice-integration/rf/fm_wav.py`: shift the channel to baseband, 8 kHz low-pass, decimate to 48 kS/s, FM-discriminate, write a 48 kHz mono WAV.
   For the 152.685 and 153.068 MHz recordings the carrier is off tune (about 2.4 kHz) and DSD-FME has no frequency tracking, so the script was run with
   `--remove-offset`. Without it DSD-FME decodes nothing from those two files.
3. `dsd-fme.exe -i <wav> -fa -o null -P -7 <dir> -Z`: auto-detect, no audio device, **per-call WAV files** into `<dir>`, payload logging to the console.
   (The single-file `-w` option produced silent files; per-call output works.)

## Contents

- `audio/` four per-call WAV files, **8 kHz, mono, 16-bit**, named by DSD-FME: date, system, colour code, talkgroup, source.
  - `..._CC_13_GROUP_TGT_6011885_SRC_4106011.wav` 7.56 s, 152.1575 MHz (speech, the best one)
  - `..._CC_13_GROUP_TGT_6011885_SRC_4106019.wav` 1.44 s, 152.1575 MHz
  - `..._CC_8__TGT_6011885_SRC_4106019.wav` 3.06 s, 152.685 MHz
  - `..._CC_8_GROUP_TGT_6011885_SRC_4106008.wav` 0.36 s, 153.068 MHz (almost all silence frames; not useful as speech)
- `frame_logs/` DSD-FME console lines for the voice bursts of each recording, in decode order: burst markers (`VC1`/`VC2`), call
  identity lines (`TGT=... SRC=...`) and one line per vocoder frame: `AMBE <14 hex digits> err = [a] [b]`.
  The hex is the 49-bit AMBE+2 parameter vector (7 bytes, last bit padding) **after** DSD-FME's own FEC step; `[a] [b]` are its FEC error counts for the frame.
  Frame `F801A99F8CE080` is the common silence frame.

## What was measured about the audio (objective, no listening)

| File | Duration | Voiced fraction of active frames | Pitch (median, 10-90 %) |
|---|---|---|---|
| unit 4106011, 152.1575 MHz | 7.56 s | 81 % | 136 Hz (127-151) |
| unit 4106019, 152.1575 MHz | 1.44 s | 62 % | 163 Hz (129-205) |
| unit 4106019, 152.685 MHz | 3.06 s | 80 % | 170 Hz |

Different unit IDs give different, steady pitches and the energy sits at 300-3400 Hz with talk-spurt pauses, which is what speech looks like. On the first call
DSD-FME decoded 468 vocoder frames (408 distinct, 70 with a non-zero FEC error count, 53 silence frames).

## Limits

- Whether the audio is **intelligible** has not been judged by a person yet; the owner is listening.
- DSD-FME's frame vectors are post-FEC parameter vectors, not the raw 72 channel bits per frame. For a decoder that starts from channel bits
  (`VoiceFrame.bits[9]`, see `docs/digital-voice/VOCODER_TASK_PROMPT.md`) they can only be used to compare **parameters and audio**, not the
  deinterleave/FEC stage. The raw channel-bit extractor is not built yet.
- DSD-FME's audio is itself a reference by a different implementation, not ground truth. Where the two disagree, neither is automatically right.
- No call here is encrypted (no privacy indicator seen), and nothing in the pack touches encryption.
