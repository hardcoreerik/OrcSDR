# Native Digital Voice audio integration (DMR)

Status as of 2026-11-24. Branch `local/digital-voice-integration` (published to `feature/dmr-digital-voice-foundation`). Receive only.

This is the record of integrating the OrcSDR AMBE+2 vocoder (Codex branch `agents/orcsdr-vocoder-voice-proof-20261124`, commits `87e0eec9` V1 and `cb653715` V2) into the native DMR receive chain, so the Tab5 turns live DMR voice into speech on its own speaker with no PC, no external executable and no extra codec hardware. Read the **Honest status** section first.

## Honest status

| Stage | What | Result |
|---|---|---|
| A | Reproduce the imported V2 host results | **Done.** Same counts as the Codex README (468 / 198 / 18 frames, 0 on control-only). `artifacts/digital-voice-integration/stageA_voice_proof.log`. |
| B | Production chain (channelizer, 4FSK frontend, D1, call manager, vocoder, resampler) on the three real calls | **Done.** 468 / 90 / 18 frames; call 1 and call 3 are bit-identical to V2 (466/468 and 18/18 frame matches, the other two are bit-error differences); control-only, idle NXDN and noise-only give no speech. Call 2 is the weakest capture: native qualifies 90 of V2's 198 frames. |
| C | Speaker path isolation with known-good PCM | **Done on the Tab5** (host and device): the speaker accepted 800 ms of a 1 kHz tone through the same resampler and ring, 0 dropped chunks. Nobody has listened, so "audible" is unconfirmed. |
| D | On-device SD replay of recorded IQ | **Done on the Tab5**: the recorded call played from the SD card through the production chain on the P4 gives exactly the host result (26 groups, 301 speech and 167 silence frames, 0 errors, 216 speaker chunks, 0 dropped). Nobody has listened. |
| E | Live RF, RTL-SDR V4L, authorised unencrypted channel | **Not achieved.** 40 minutes on 152.1575 MHz (plus about 10 minutes earlier) produced control-channel traffic only: 0 voice groups. The receive chain, scanner and service ran stable (see "Device results"). |
| F | Scanner-integrated live reception | **Not achieved**, for the same reason; the hold-on-voice path is exercised only by host tests and by the SD replay. |

**The highest stage genuinely completed is D (on-device replay of real recorded IQ to the speaker path).** Live speech and a human listening test remain open.

Nothing in the table above is claimed beyond what its row says. A host build is not a firmware build, and a firmware build is not live reception.

## Why the user heard static (root cause)

Digital Voice holds the receiver on the general browse band (NFM). The DSP task demodulates every block it receives and hands the audio to `queue_audio_samples()`, which feeds the speaker. A DMR channel demodulated as analogue FM is noise, so with the radio running the speaker played the FM discriminator output of a digital signal: static. Nothing in the old chain ever produced speech from DMR.

Fix: while the Digital Voice audio service runs, `queue_audio_samples()` refuses every producer except the vocoder (`g_dv_audio_exclusive`; the vocoder uses `queue_audio_samples_unfiltered()`). A diagnostic counter, `nfm_blocked`, in `RTL_DV AUDIO STATUS` counts the analogue blocks refused, so the static source is observable. The discriminator is never offered as "speech"; a monitor of it would be an explicit diagnostic mode and does not exist yet.

## Provenance

- `components/mbelib` is the same pinned upstream mbelib (`9a04ed5c`, ISC) the P25 IMBE path already used, with the AMBE+2 3600x2450 backend added by Codex commit `87e0eec9`.
- `components/orcsdr_vocoder` is the Codex adapter (DMR 72-bit deinterleave, extended Golay C0 rejection, silence types muted, gain limiting). The DMR interleave tables come from szechyjs/dsd (ISC notice in `THIRD_PARTY.md`).
- No DSD-FME code is used. The DSD-FME reference pack (local only, `F:\Ai\OrcSDR-data\digital-voiceocoder_reference_pack.zip`; its text parts are in `artifacts/digital-voice-integration/vocoder_reference_pack/`; hashes in `AUDIO_LOCAL_ONLY.md`) is validation evidence only: it is read by `tools/dv_voice_compare.py` and `tools/dv_voice_audio_compare.py` and never by the receiver or the vocoder. Every WAV listed below was produced from original IQ by native code.
- One change to the vendored mbelib: `ambe3600x2450.c` read `prev_mp->log2Ml[57]` (one past a 57-entry array) when the interpolation index reached 56. UBSan found it. The index is now clamped to 56. The decoded PCM of all three real calls is byte-identical before and after the clamp (md5 of every WAV compared).
- **Licensing and patents are not assessed.** The ISC notice on mbelib does not establish freedom to distribute an AMBE+2 implementation. Commercial codec and patent status remains an owner decision. Development testing is authorised; nothing here is a distribution decision.

## Architecture

```
RTL-SDR IQ 240 kS/s -> dv::Channelizer -> 48 kS/s -> dv_radio worker (core 0)
   -> frontend (4FSK) -> d1::Decoder  -- data bursts --> d1::CallManager ----+
                              |  (voice superframes A..F)                   |
                              +--- VoiceGroup ------------------------------+--> QualifiedGroup
                                                                               |
dv_radio voice queue (12 x 360 ms, single consumer) <--------------------------+
   -> dv_voice_service task (core 1) -> dv_voice::Engine
        per-slot orc_vocoder context -> 160 samples @ 8 kHz per 20 ms frame
        -> 6x polyphase resampler (192 taps, 3.9 kHz cutoff) -> playout ring (2 s, PSRAM)
   -> existing speaker sink queue_audio_samples_unfiltered() (volume, mute, recording, web audio)
```

Files:

| File | Role |
|---|---|
| `components/orcsdr_dmr/d1_protocol/src/d1.cpp`, `d1.hpp` | Voice superframe tracking (`VoiceGroup`), embedded-LC decode (layout fixed, see below) |
| `components/orcsdr_dmr/d1_protocol/src/d1_call.cpp`, `d1_call.hpp` | `CallManager`: privacy, identity, slot-local call state |
| `apps/orcsdr-tab5/ui/dv_radio.{hpp,cpp}` | Worker side: call manager wiring, bounded voice queue, `pop_voice`, `voice_status` |
| `apps/orcsdr-tab5/ui/dv_voice_audio.{hpp,cpp}` | `Engine`: codec, resampler, playout ring, listening selection, phases. Host-portable. |
| `apps/orcsdr-tab5/ui/dv_voice_service.{hpp,cpp}`, `dv_voice_service_task.cpp` | Service and pump task, `RTL_DV AUDIO` commands, self test, support line |
| `apps/orcsdr-tab5/ui/digital_voice_*` | LIVE panel and controls (widget-level repaint), snapshot plumbing |
| `apps/orcsdr-tab5/ui/main.cpp` | Thin hooks only: exclusive gate, `audio_out`, `audio_allowed`, `audio_path_ready`, support line |

`main.cpp` grew by about 25 lines (gate, wrapper, hooks).

## Voice frame contract

`d1::VoiceGroup` is one complete BS voice superframe (bursts A to F, 18 AMBE+2 frames, 360 ms). It is delivered only when the voice SYNC of burst A matched exactly and every one of bursts B to F decoded its EMB (QR(16,7,6), unique, at most two corrected bits), kept one colour code, and none looked like a SYNC. The payload is the 216 over-the-air voice bits of each burst (108 before and 108 after the 48-bit centre), MSB first, three 72-bit frames per burst, before any deinterleave or FEC (`payload[burst*27 + frame*9 .. +9]`). Voice bits are never taken from data or control bursts. A superframe with a missing burst is dropped whole; there is no partial delivery. Embedded signalling other than the LC (for example Talker Alias) does not discard a superframe; the embedded LC is decoded only when the four EMB LCSS values are 1, 3, 3, 2.

## Embedded LC layout (fixed)

The old D1 decoder filled a plain 7x11 matrix row by row and read the 5-bit checksum from the last five positions. The test encoder made the same mistake, so the two agreed with each other. Measured on the real corpus the old layout validated **0 of 22** embedded LCs; the corrected layout validates **20 of 22** (19 of 22 without the single-bit row correction that was tried and dropped).

ETSI TS 102 361-1 B.2.2.1 layout now used: 8 rows by 16 columns, sent column by column over four bursts; rows 0 to 6 are Hamming(16,11,4) words, row 7 is column even parity; the 72 LC bits fill rows 0 and 1 (11 columns) and rows 2 to 6 (columns 0 to 9); the checksum (sum of the nine LC octets mod 31) is column 10 of rows 2 to 6, MSB first. Integrity checks were not weakened: any row or column parity failure rejects the LC.

Independent evidence: (1) real captures, where the new layout validates and the old one does not; the LC identities decoded from the embedded LC equal those from the RS-checked voice LC header of the same call; (2) `d1_protocol/tests/test_d1.cpp` (`voice_superframe`) shows the superseded layout is rejected. The unit encoder is still written by us from the standard's description, so the real captures are the independent part. A conformance vector from a second implementation would be stronger and is not available.

## Call, slot and privacy state

`CallManager` is per slot (TS1 and TS2 are independent) and fixed-size. Three privacy states:

- **UNKNOWN**: voice is on the air but identity or privacy cannot be established. Never played.
- **CLEAR**: identity validated by an RS-checked voice LC header immediately before the group, or by a checksummed embedded LC in the group, or by an unbroken same-slot continuation of such a call, and no protection indicator was seen.
- **PROTECTED**: EMB PI bit, PI header, LC protect flag or the service-options privacy bit. Audio withheld, nothing decoded, nothing recovered. There is no key handling anywhere in this code.

Rules (ported from the offline proof's gate and extended): a data burst on the slot between two groups ends continuity; a header or embedded identity that conflicts with the other ends the call; colour code must match; identity must be re-confirmed within about 2 s; up to two lost superframes are tolerated inside one call (the epoch continues, the audio worker inserts silence and resets the codec); a terminator or more than four superframes of silence ends the call; a retune, IQ loss or ring drop resets the decoders, flushes the voice queue and drops buffered speech. Late entry (embedded LC only) is supported; a group with an unknown slot is never played.

## Audio engine

One codec context per slot, one call voiced at a time. Selection: AUTO (first active slot, sticky until its call ends), TS1, TS2, MUTE. The other slot is metadata only. Silence frames (AMBE types 124 and 125) are muted but still run the codec so predictors stay continuous. Uncorrectable frames are silence, not static; gaps are silence, never repeated speech. Synthesis is serialized (mbelib uses `rand()`). No allocation per frame. Playout starts 250 ms after the first qualified group lands (`target_buffer_ms`, `RTL_DV AUDIO BUFFER <ms>`). Measured on the Tab5 with an SD replay whose group spacing varied between 362 and 443 ms: a 100 ms hold underran on 18 of 20 groups, 250 ms on 2 of 26, 400 ms on 1 of 26 (latency 219 ms). The jitter to absorb is the spread of the group spacing plus the synthesis time of the first slice; the live value has not been measured because no live voice arrived. The engine reports underruns, overruns, latency, buffered milliseconds, clipping, synthesis time and the corrected-versus-uncorrectable bit counts separately. The speaker volume and mute stay the existing ones: the user's sound switch is the master mute.

Latency is dominated by the superframe: audio of a group exists only after the sixth burst (about 360 ms) plus the hold. On the Tab5 the group-arrival-to-first-sample time was 82 to 136 ms at a 100 ms hold and 105 ms at 250 ms, so a heard word lags the air by roughly 0.7 s.

## Serial interface

`RTL_DV AUDIO STATUS | ENABLE | DISABLE | SLOT AUTO|1|2 | MUTE | UNMUTE | RESET | TONE [ms] | SELFTEST` (authentication required for everything except STATUS). `TONE` plays known-good 1 kHz PCM through the same resampler, ring and speaker: it isolates the speaker path and is not counted as decoding. `SELFTEST` decodes one real recorded superframe (3 speech and 15 silence frames expected), checks resampler level and frequency, slot selection, mute and the protected state on the device itself. `RTL_SUPPORT REPORT` gains one `ORC_SUPPORT_DV_AUDIO` line only while the service runs. STATUS prints: phase, listen mode, slot, privacy per slot, groups, frames, speech/silence/error/concealed, corrected bits, uncorrectable frames, lost superframes, underruns, overruns, flushes, synthesis time (last/max/avg), buffered and peak milliseconds, latency, RMS and peak, clipped samples, test samples, NFM blocks refused, queue pending and drops, qualified/protected/unqualified groups, calls, pump count and time, stack high-water mark.

## Screens

Existing LIVE page, both densities: a voice panel with the state (LISTENING, ACQUIRING VOICE, BUFFERING, DECODING SPEECH, SILENCE FRAME, DEGRADED AUDIO, CALL ENDED, PROTECTED VOICE, PRIVACY UNKNOWN, AUDIO MUTED, AUDIO UNAVAILABLE), slot and identity, counters, a level meter and AUTO / SLOT 1 / SLOT 2 / MUTE buttons. It is a retained widget: an audio-only change repaints that one rectangle and nothing else (`dashboard_voice_tests.cpp` proves every drawn primitive lies inside it and that an idle snapshot draws nothing). SCAN holds on a qualified voice group because each group refreshes the scanner's verified-activity time, so a call is held and played, and the scan resumes after the hold time once the call ends. SKIP retunes, which flushes the voice queue. No new dashboards. SIGNAL and SETUP show the existing diagnostics; the audio counters live in `RTL_DV AUDIO STATUS` and the support line.

## Test results (host)

All numbers from `build/dv-voice` (Release) and `build/dv-live-san` (ASan + UBSan) on 2026-11-24.

Stage B: native versus the offline V2 sandbox, per capture (frames, exact sequence matches, and parameter matches against DSD-FME):

| Capture | Native frames | V2 frames | DSD-FME frames | Native = V2 (exact sequence) | Native params = DSD-FME | V2 params = DSD-FME |
|---|---:|---:|---:|---:|---:|---:|
| terra_152p1575_call_cc13 | 468 | 468 | 468 | 466 | 374 | 374 |
| terra_152p685_call_cc8 | 90 | 198 | 153 | 76 | 56 | 71 |
| terra_153p068_call_cc8 | 18 | 18 | 18 | 18 | 17 | 17 |

"Params = DSD-FME" is a sequence match of the post-FEC 49-bit vectors; repeated silence vectors make it a lower-quality measure, not a bit-accuracy proof. Call 2 is the weakest capture: its first call is noisy, native decodes fewer of its superframes (all-or-nothing superframes) than the offline soft-slicer extractor.

Audio fidelity, call 1 (7.56 s, TS2, unit 4106011), native 8 kHz WAV versus the sandbox V2 WAV and versus DSD-FME: sample-identical to V2 on 59,115 of 60,480 samples (maximum difference 52, from the two frames that differ); envelope correlation with DSD-FME 0.963; voiced frames 155 versus 157; median F0 ratio where both voiced 1.00; F0 jump rate 11 percent versus 16 percent; no clipping; RMS 1.37 times DSD-FME (our fixed output gain); 104 muted silence frames versus 2 for DSD-FME (DSD-FME fills silence frames with comfort audio, we mute them). `output/native/audio_compare_call1.json`. This is a measurement of similarity, not of intelligibility: **no human has listened yet**.

Negative and regression: control-only, idle NXDN, Capacity Max control (CRC fail) and pure noise at 0, -10 and -30 dB below signal power all produced 0 voice groups and empty WAVs (identical hash). Corpus identities and discovery database are unchanged (the manifest identity-event counts rose 11 to 22, 13 to 15, 24 to 25 because late-entry identities now validate; the exact identity sets did not change). Host suites passing: `digital_voice_native_voice` (91 checks), `digital_voice_corpus`, `digital_voice_live`, `digital_voice_discovery_db`, dashboard suites (5), D1 protocol, integration and frontend, serial support; Release and ASan/UBSan.

Impairments on call 1 (frames delivered of 468, lost superframes, engine speech/silence/error, concealed frames):

| Case | Groups | Lost superframes | Speech | Silence | Error | Concealed |
|---|---:|---:|---:|---:|---:|---:|
| clean | 26 | 0 | 301 | 167 | 0 | 0 |
| carrier offset +1500 / -1500 / +3000 Hz | 26 | 0 | 301 | 167 | 0 | 0 |
| noise 20 dB / 12 dB below signal | 26 | 0 | 302 | 166 | 0 | 0 |
| noise 8 dB below signal | 24 | 2 | 276 | 155 | 1 | 37 |
| clock drift +50 ppm | 24 | 2 | 282 | 150 | 0 | 36 |
| clock drift +200 ppm | 22 | 4 | 246 | 150 | 0 | 72 |
| clock drift -100 ppm | 26 | 0 | 301 | 167 | 0 | 0 |
| burst loss 100 ms / 500 ms | 25 / 23 | 0 | 286 / 264 | 164 / 150 | 0 | 0 |
| splice (no time gap) 37 ms / 300 ms | 25 / 23 | 0 | 286 / 264 | 164 / 150 | 0 | 0 |

Positive clock drift (samples dropped) loses voice SYNCs; negative drift does not. A +-1 dibit search for the EMB cadence was implemented, measured, and removed because it recovered nothing.

## Static buffer sizes

`d1_bridge::Receiver` grew from about 7.8 KB to 8.9 KB (three voice-superframe tracks); the budget assertion in `test_d1_bridge.cpp` moved from 8 KiB to 10 KiB. The decoders are allocated in internal RAM when it fits and in PSRAM otherwise (`dec_internal` in `RTL_DV_STATUS` shows which). The Tab5 internal boot pool is nearly full, so this is to be watched. Engine: codec contexts and the 2 s playout ring (192 KB) are PSRAM.

## How to enable and test live DMR audio

Receive only; bias tee stays off on the Tab5's V4L. Audio is on by default whenever the Digital Voice dashboard is open; nothing else starts it.

1. Flash the firmware (app image only keeps your settings). Make sure the Tab5 sound switch in the header is on and the volume is up.
2. Open the Digital Voice dashboard (Home, Digital Voice). The LIVE page shows LISTENING and the voice panel.
3. Check the path before relying on RF, from a serial terminal (pairing key required for everything except STATUS):

```
RTL_DV AUDIO SELFTEST        # expects checks=13 failed=0 known_group_speech=3 silence=15
RTL_DV AUDIO TONE 600        # you should hear a 1 kHz tone: speaker path only, not decoding
RTL_DV AUDIO STATUS          # nfm_blocked rising means the analogue NFM audio (the old static) is being refused
```

4. Play recorded speech with no radio: copy a 240 kS/s CU8 capture whose channel sits 60 kHz above its centre to the SD card, then `RTL_DV REPLAY /orcsdr/<file>.cu8 <channel Hz>`. The event lines are labelled REPLAY and the LIVE panel goes BUFFERING, DECODING SPEECH.
5. Live: `RTL_DV PLAN 152.1575:dmr,152.685:dmr,152.345:dmr,153.068:dmr`, `RTL_DV SCAN START` (or the SCAN button). The scanner holds on a verified call, the voice is played, and the scan resumes after the hold once the call ends. Choose AUTO / SLOT 1 / SLOT 2 / MUTE on the LIVE panel or `RTL_DV AUDIO SLOT 1|2|AUTO`, `RTL_DV AUDIO MUTE`.
6. If audio stutters, raise the hold: `RTL_DV AUDIO BUFFER 400` (default 250 ms). `RTL_DV AUDIO STATUS` reports `gap_ms` (group spacing seen: ideal 360), `underruns`, `latency_ms`.
7. Protected or unidentifiable voice is never played; PROTECTED VOICE or PRIVACY UNKNOWN is shown instead. `RTL_SUPPORT REPORT` includes an `ORC_SUPPORT_DV_AUDIO` line while the service runs.

## Device results (M5Stack Tab5, COM17, RTL-SDR V4L with the 140/440 MHz antenna, bias tee off)

Firmware `v0.6.0` build `Nov 24 2026`, ESP-IDF 5.5.4, flashed app-only. Raw logs: `artifacts/digital-voice-integration/audio/` (`device_session.jsonl`, `tone_replay.jsonl`, `live_session.jsonl`).

- **Boot loop and fix.** The first image aborted at boot (`Could not reserve internal/DMA pool`) because the self test used a 38 KB static buffer; large buffers and the resampler taps now live in PSRAM. After the fix the boot is clean and internal free RAM after Digital Voice opens is about 36 to 54 KB (largest block 15 to 31 KB).
- **Static source confirmed on the device.** With the radio running on the browse band, `nfm_blocked` rose by about 50 blocks per second (949 in the first 18 s, 17,447 by the end of the replay run): that analogue audio previously reached the speaker.
- **Self test on the P4:** 13 checks, 0 failed; the recorded superframe decodes to 3 speech and 15 silence frames, identical to the host.
- **Stage C:** `RTL_DV AUDIO TONE 800`: speaker chunks 0 to 18, dropped 0, 6,400 test samples; the voice counters stayed at zero (not counted as decoding).
- **Stage D:** `RTL_DV REPLAY /orcsdr/dv_live_call_152p1575.cu8 152157500`: 26 qualified groups, 468 frames (301 speech, 167 silence, 0 errors), 2 calls, 216 speaker chunks, 0 chunk drops, 0 voice queue drops, 0 ring drops, no IQ loss. Same numbers on every run.
- **Cost on the P4:** synthesis about 6.5 ms per frame, about 110 ms of CPU per 360 ms superframe (about 31 percent of one core; the host needs 0.7 ms per frame); the decode worker (core 0) about 25 percent of a core; the DSP channelizer about 20 percent of core 1. Longest pump step 41 ms after slicing the synthesis into 3-frame steps (it was 161 ms when a whole group was synthesized in one go). Voice task stack: 5.4 KB of 12 KB still free at the lowest point (a single 18-frame group needs about 7 KB of stack). The decoders are in PSRAM (`dec_internal=0`) because the largest internal block is smaller than they are; this was already the case before this work.
- **Live, 40 minutes (00:23 to 01:03 by the PC clock, after midnight) plus about 10 minutes earlier:** scanner plan 152.1575, 152.685, 152.345 and 153.068 MHz, DMR only. The scanner stayed on 152.1575 MHz in "HOLD ON VERIFIED ACTIVITY" for the whole run because control-channel integrity events keep renewing the hold (existing scanner behaviour, not changed here). Result: 55,904 decoder events, 132 acquisitions, 0 identities, **0 voice groups, 0 speaker chunks**; the antenna sees the channel only about 1 dB above the noise and it was after midnight. Worker 25 to 26 percent, 0 ring drops, 0 IQ loss, no reboot (uptime 2,497 s). An earlier live run right after a boot showed the worker at 93 percent with ring drops; the same plan measured again minutes later was 26 to 30 percent with none, so that was contention in the first minutes after boot (Wi-Fi and storage on core 0) and is not proven further.
- **Not done on the device:** a human listening to the speaker, speech from live RF, a protected or unknown-privacy voice call (only host tests), two simultaneous slots, the physical flicker check of the new panel, the voice panel in DECODING state (the screenshot `artifacts/digital-voice-integration/audio/live_panel_decoding_replay.png` shows BUFFERING during an SD replay: slot, identity, counters and the four listening buttons render; the older ENC / PRIVACY card still reads UNKNOWN, the voice panel is the source for voice privacy).

## Known limitations

- A superframe is all-or-nothing: one bad EMB or a missed voice SYNC drops 360 ms of speech. A weak signal therefore has gaps rather than degraded speech. Single-slot capture (the other slot's audio is metadata only).
- No Talker Alias display, no direct-mode (MS) voice, no DMR Tier II simplex voice sync handling beyond what BS voice does; BS (repeater) voice only.
- Positive sample-clock drift of 50 ppm or more loses voice SYNCs (see table).
- Silence frames are muted, not comfort noise.
- No transcript or listening evaluation exists. Intelligibility is unmeasured by a human.
- The device results above are what was actually done; everything else is not verified.
- Licence and patent status of AMBE+2 is unassessed (owner decision).

## Next priorities

1. Owner listening test of the three native WAVs and of a live call; record what is understandable.
2. Live reception on an authorised unencrypted channel with the RTL-SDR V4L and the 140/440 MHz antenna (the antenna decodes Terra poorly: about 1 dB above noise).
3. A second implementation's embedded-LC conformance vector.
4. Comfort noise for silence frames and a second-slot mix, if wanted.
5. Decide the AMBE+2 licence and patent position before any distribution.

## Reproduce

```sh
cmake -S tests/digital_voice_live -B build/dv-voice -DCMAKE_BUILD_TYPE=Release && cmake --build build/dv-voice -j
cd build/dv-voice && ctest                                   # includes digital_voice_native_voice
./dv_voice_replay ../../tests/digital_voice_corpus/iq/terra_152p1575_call_cc13.cu8 152157500 ../../output/native terra_152p1575_call_cc13
python3 tools/dv_voice_compare.py output/native output F:/Ai/OrcSDR-data/digital-voice/vocoder_reference_pack.zip output/liborcsdr_vocoder.so   # the reference pack is local only, see artifacts/digital-voice-integration/AUDIO_LOCAL_ONLY.md
tools/dv_voice_impairments.sh build/dv-voice/dv_voice_replay tests/digital_voice_corpus/iq/terra_152p1575_call_cc13.cu8 152157500 output/impair
```

The WAV files are radio audio and are kept out of the repository; the originals and their SHA-256 sums are in `F:\Ai\OrcSDR-data\digital-voice\native_voice_2026_11_24\native\SHA256SUMS.txt`.
