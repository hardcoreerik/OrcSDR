# Digital Voice real-capture regression corpus

Real RTL-SDR captures, kept permanently as regression assets. No synthetic data. Collected 2026-11-23, Lane County, Oregon,
PC RTL-SDR Blog V4 + 0-6 GHz LNA (bias tee on), antenna not recorded.

## Capture format (all files)
Unsigned 8-bit interleaved I/Q (CU8), 240 000 samples/s, tuner gain 30 dB, bias tee on. The wanted channel sits **60 kHz above the
tuned centre** (tuned = channel - 60 kHz), the layout the Tab5 receiver uses. Replay it with `rtl_sdr`-style 480 000 bytes per second.

| File | Channel | Length | Source / when | What it contains |
|---|---|---|---|---|
| `iq/terra_152p1575_call_cc13.cu8` | 152.1575 MHz | 14 s | hunter, 14:56 PST | Fleetnet Terra control channel with a real group call on timeslot 2: SRC 4106011 and 4106019 to TG 6011885, colour code 13 |
| `iq/terra_152p685_call_cc8.cu8` | 152.685 MHz | 14 s | hunter, 14:57 PST | Second Terra site, colour code 8, SRC 4106019 to TG 6011885 |
| `iq/terra_153p068_call_cc8.cu8` | 153.068 MHz | 14 s | hunter, 15:58 PST | A Terra voice-only channel, colour code 8, TS1: SRC 4106001 and 4106008 to TG 6011885. The carrier is 2.4 kHz off tune; our decoder tracks it, DSD-FME needs the offset removed (`fm_wav.py --remove-offset`) |
| `iq/terra_152p1575_control_only.cu8` | 152.1575 MHz | 8 s | first 8 s of an earlier 30 s capture | Control signalling only (CSBK, multi-block control), no calls, timeslot 2 idle |
| `iq/capmax_451p9125_control_crcfail.cu8` | 451.9125 MHz | 8 s | first 8 s of a 20 s capture | TRBOWest Capacity Max control channel: frames sync, CSBK CRC fails in both decoders (documented limitation) |
| `iq/negative_nxdn_voice_chan_idle_454p125.cu8` | 454.125 MHz | 6 s | first 6 s of a 5 min capture | Idle NXDN voice channel, false-identification guard |

## Expected results
`manifest.txt` freezes them (events lower bound, identity-event upper bound, exact set of `src>dst@colourcode`). The test is
`tests/digital_voice_live/dv_corpus_tests.cpp`, registered as ctest `digital_voice_corpus`. It runs each file through the production
`dv_radio` chain (channelizer, DMR D1 receiver, NXDN48/96 frontends) exactly as the Tab5 does.

## Independent decoder output
`dsdfme/` holds DSD-FME 20260715 (official release `dsd-fme-x86-64-cygwin-portable`, sha256 006cfa42...e9f8) run on FM-discriminator WAVs made
from the same IQ (`artifacts/digital-voice-integration/rf/fm_wav.py`, command `dsd-fme -i x.wav -fa -Z -o null`):
- `terra_152p1575_call_cc13.txt`: `SLOT 2 TGT=6011885 SRC=4106011 ... Group Call`, plus `TV_GRANT` and Terra system parameters (network 13, site 1.14).
- `terra_152p1575_control_only_30s.txt`: system broadcasts only, no calls (full 30 s capture).
- `capmax_451p9125.txt`: `CSBK (CRC ERR)` on the Capacity Max control bursts, same as our decoder.
- `terra_153p068_call_cc8.txt`: `SLOT 1 TGT=6011885 SRC=4106001 ... Group Call` (22x) and `SRC=4106008` (3x), after removing the 2.4 kHz carrier offset. Without that correction DSD-FME decodes nothing from this file.
The 152.685 MHz capture was cross-checked with our decoder only (DSD-FME log not kept for it).

## Adding cases
Put the file in `iq/`, run `dv_corpus_tests <dir> --print`, review the numbers, check them against an independent decoder, then add a line.
