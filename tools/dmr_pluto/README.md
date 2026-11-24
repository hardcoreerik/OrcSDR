# DMR conducted test harness (Pluto -> cable -> Tab5)

Receive-chain test with a **known** DMR call, so a test does not depend on live traffic. The Pluto transmits through a cable and attenuators
into the receiver. **Nothing here radiates.** Never connect an antenna to the Pluto TX port. Receive-only on the Tab5 side.

## What exists (all built and checked 2026-11-24)

| File | Job |
|---|---|
| `dmr_gen.py` | Builds a BS-mode DMR call (voice LC header x3, N voice superframes with embedded LC, terminator, other slot idle) as 4FSK at 4800 baud from 72-bit AMBE+2 frames in a text file. No voice encoder: content is whatever frames you give it (`test_frames_18.txt` = 18 frames, 3 speech + 15 silence, from our own recorded capture). Writes a label (ids, colour code, slot, every frame sent, SHA-256). |
| `cu8_loopback.py`, `loopback_test.sh` | **No-RF proof.** Converts the call to a 240 kS/s CU8 (corpus layout) with optional noise and decodes it with the production chain. Result: 144 of 144 frames identical at clean, +30 ... +3 dB; 126 at 0 dB; 36 at -3 dB (wideband SNR over 240 kHz). A software reference curve for weak-signal work. |
| `pluto_tx.py` | Transmit through OrcAnomaly's safety layer (ISM window 902.2-927.8 MHz only, attenuation floor 20 dB with readback, silence on every exit, 60 s bound). Refuses unless the wiring is stated and the expected level at the receiver is below -20 dBm. `--dry-run` needs no Pluto. |
| `tab5_loop_test.py` | Tab5 side: tune 915.0 MHz, hold, development captures on, sample the voice counters, score against what was sent. |
| `sweep.py` | Attenuation ladder: for each step the Pluto sends calls while the Tab5 scores them. |

Default test channel is **915.000 MHz** so any stray leakage stays inside an unlicensed band at very low level. LO sits 150 kHz below the carrier
(feedthrough), image 300 kHz below.

## Expected result per call (default call)

8 qualified groups, 144 frames (24 speech + 120 silence), 0 errors, identity 4106008 > 6011885, colour code 8, TS1. The Tab5 speech file
(`/orcsdr/dmr_*_ts1_4106008.wav`) and, at weak levels, an IQ capture (`/orcsdr/dviq_*_915000.cu8`) are saved when the dev toggles are on.

## Before ANY transmission (checklist, every time)

1. Pluto TX1 -> attenuator(s) -> SMA cable -> RTL-SDR (V4L) antenna input. **No antennas on either end.** Unused Pluto TX2 terminated.
2. Say what is in the chain (pad value, cable, which dongle). The scripts take it on the command line and refuse without it.
3. Expected level at the receiver (printed by `pluto_tx.py`): PA about +19 dBm at 0 dB, waveform 6 dB under full scale, minus programmed
   attenuation, minus the pad. Start at 60 dB programmed + 30 dB pad = about -77 dBm. The RTL-SDR must see far below +2.5 dBm.
4. The Pluto is not reachable at 192.168.2.1 or 192.168.1.228 right now: connect it and pass its real address with `--uri ip:<address>`.
5. Disk or SD-card writes on the Pluto are done by you, not by Claude.

## Tonight (suggested order)

```
# 1. regenerate and prove it in software (no RF)
python tools/dmr_pluto/dmr_gen.py --frames tools/dmr_pluto/test_frames_18.txt --src 4106008 --dst 6011885 --cc 8 --slot 1 --superframes 8 --out call.npz
sh tools/dmr_pluto/loopback_test.sh build/dv-voice/dv_voice_replay          # expect EXACT
# 2. dry run the transmit plan (prints level, SHA, refusals; sends nothing)
python tools/dmr_pluto/pluto_tx.py call.npz --dry-run --atten-db 60 --physical-pad-db 30
# 3. one real conducted call, strong, to prove the chain end to end
python tools/dmr_pluto/pluto_tx.py call.npz --atten-db 60 --physical-pad-db 30 --wiring "TX1 -> 30 dB pad -> SMA cable -> V4L" --seconds 20 --uri ip:<pluto> --yes-conducted-no-antenna
#    (second terminal, started first)   python tools/dmr_pluto/tab5_loop_test.py --channel-mhz 915.0 --seconds 40 --expect-calls 4
# 4. only then the ladder
python tools/dmr_pluto/sweep.py call.npz --wiring "TX1 -> 30 dB pad -> SMA cable -> V4L" --physical-pad-db 30 --ladder 60,66,72,78,84,89 --seconds 30 --uri ip:<pluto> --yes-conducted-no-antenna
```

## Not done / unverified

- No transmission has ever been made with this harness. The Pluto path (`pluto_tx.py` real mode, OrcAnomaly import, buffer size on firmware v2.3,
  sample-rate behaviour) is untested; only its refusals and the dry-run plan were exercised.
- The expected levels are estimates; no power measurement has been taken. The DMR slot timing of a real BS (guard times, CACH short LC) is simplified.
- Voice content is recorded frames, not new speech (no AMBE+2 encoder).
