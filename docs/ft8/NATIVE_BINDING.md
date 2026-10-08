# Native FT8/FT4 decoder bound to the Tab5 (receive only)

Status: **first on-device decodes, 2026-10-08.** Branch `claude/ft8-native-bind`. Receive only: nothing here transmits.

The owner approved binding the native decoder to the firmware after the audio-tap proposal (`AUDIO_TAP_PROPOSAL.md`). This note records what was
built, what was measured on the Tab5, and the serial commands for scripting it.

## What runs on the device

```text
RTL block (CU8, 2.4 MS/s) --DSP task--> ft8_audio_tap (sidecar: own state, never edits the block)
        CIC3 /10 -> DC removal -> mix USB passband centre to 0 -> 41-tap FIR /5 -> 447-tap FIR /4 -> mix back, real part
        -> 12 kS/s int16 USB audio, no AGC, no limiter
   --> 32 s PSRAM ring (ft8_runtime)
   --> decoder task (core 0, priority 1): at each UTC slot end copies the slot, runs ft8_native_backend:
        zero-padded FFT grid (4 rows/symbol, half-bin) -> coarse search on a decimated copy -> per-candidate refinement by scoring
        the fine grid -> best candidates through soft demod, LDPC, CRC-14, unpack, plausibility
   --> decodes into the dashboard store (SNR shown as a dash: there is no calibrated estimator)
```

The tap runs only while the FT8 screen is touching the runtime (or in headless mode, see `FT8 RUN`) and only for the shortwave band; with it off no
existing path changes. A slot with a retune, a rate change or lost samples is skipped as incomplete, never reported as a quiet decode.

## Measurements on the Tab5 (ESP32-P4), one slot of FT8, 40 m

| stage | time |
|---|---:|
| fine FFT grid (384 rows x 898 bins) | about 2.8 s |
| coarse search (64 candidates) | about 0.5 s |
| refinement | about 0.08 s |
| FEC gates (32 candidates) | about 0.2 s |
| **total** | **about 3.6 s per 15 s slot** |
| audio tap | about 3.4 ms per 6.8 ms IQ block (50 percent of one core), worst block about 9 ms |

Findings that changed the design (all measured, not assumed):
- The first version used double precision in the sync scoring and the tap mixers. The P4 FPU is single precision, so those cost about 100 cycles per add:
  the sync search took 3.2 s and the tap ran slower than real time (every slot was lost). Single precision plus contiguous histories fixed both.
- Per-candidate exact correlation (Goertzel) cost about 10 s per slot on the P4 and was replaced by one FFT grid that every candidate reads. On the host
  that is 19 ms for the official FT8 recording, decoding the same messages (6 FT8 at K=32/gate 16, 7 at K=64/gate 32; FT4 3).
- The tuner does not sit on the dial: the driver quantizes tuning (7.074 MHz was tuned at 7.070 MHz), so the dial is 4 kHz inside the baseband. The tap now
  takes the dial offset from the driver's reported centre (`esp_rtl_sdr_get_center_freq`) every 100 ms.
- The Tab5 wall clock was 2.5 s behind the PC after boot (the hardware RTC has one-second resolution), which shifts every slot. After `settime` it is within
  10 ms. FT8 needs under about half a second; a product fix (align the system clock to the RTC tick, or NTP when Wi-Fi is up) is still to do.
- HF gain (corrected twice; this is the measured state). Manual gain does change what the tap sees: with the V4 plus MLA-30+ on 40 m the slot level rose
  3, 20, 104, 265 (0, 15, 25, 34 dB) and then flattened (253 to 303 at 40 to 50 dB) while IQ clipping went 0.04 percent (25 dB), 9 percent (34 dB), 22 to 32 percent
  (40 dB and up), and decodes kept rising to that knee. But an alternating A/B of the receiver's own AUTO setting against manual 34 dB (4 rounds each, 22 slots each) gave
  6.55 against 6.64 decodes per slot: the tuner AGC already sits at the knee. So no gain policy is added: leave the receiver in AUTO. (A hill-climbing gain seeker was built
  and removed after this result. Note that comparing slot level across an AUTO to manual change is invalid: the gain readout under AUTO is not the effective gain.)
  `FT8 STATUS` reports `iq_dbfs`, `iq_clip_pct` and `gain_tenth_db` so this can be rechecked on other dongles and antennas.

First on-device result (V4 plus MLA-30+, 7.074 MHz, 25 dB manual gain, about 3.5 minutes): 20 decodes, e.g. `CQ VE2LBI FN35`, `W5A W7ZR DM26`,
`KB3QPL K8OCN EN83`, `CQ KE5ETC EM22`; the same stations the PC capture of the same band produced.

## Serial commands (`FT8 <verb>`)

Queries need no authentication; commands that change state need the PAIR/AUTH session (see `API_SERIAL_CLI.md`).

| command | auth | what it does |
|---|---|---|
| `FT8 STATUS` | no | one line: screen, runtime state, mode, band, clock, tap rate, blocks, ring, per-block cost, slots decoded or skipped, stage times, slot RMS/peak/clipped, dial offset, store size, k, gate, fine_rows, deadline |
| `FT8 BANDS` | no | the band table (index, label, dial Hz) |
| `FT8 DECODES [n]` | no | the newest n decodes (default 20) |
| `FT8 TIME` | no | the wall clock the slot scheduler uses, in ms |
| `FT8 DUMP` | no | the last decoded slot as base64 PCM16 lines (about 480 KB) |
| `FT8 OPEN` | yes | open the FT8 dashboard |
| `FT8 BAND <index|label|dial_hz>` | yes | tune a band (e.g. `40m`) |
| `FT8 MODE <FT8|FT4>` | yes | decode mode (JS8 is disabled) |
| `FT8 CLEAR` | yes | clear the decode list |
| `FT8 RUN <0|1>` | yes | headless: keep the decoder running with no FT8 screen |
| `FT8 CONFIG <k> <gate> <fine_rows 4|8> <deadline_ms>` | yes | candidates refined (1-64), candidates sent through the FEC gates (1-64), grid resolution, per-slot time limit |
| `FT8 SAVE <name>` | yes | write the last slot to `/sd/ft8/<name>.wav` |

`tools/tab5_ft8.py` wraps these: `status`, `bands`, `band`, `mode`, `run`, `config`, `decodes`, `clear`, `save`, `dump <file.wav>` (a 12 kHz WAV on the PC),
`time` (Tab5 clock minus PC clock), `settime`, `watch <seconds>`, `raw "<any command>"`. It pairs from the key file (`--key`, `ORC_UI_DOC_KEY`, or
`.orclink/ui-doc.key`), never prints the key, and opens the port without resetting the board.

Typical session:

```bash
python tools/tab5_ft8.py --port COM17 settime
python tools/tab5_ft8.py --port COM17 raw "RTL_GAIN MANUAL 250"
python tools/tab5_ft8.py --port COM17 band 40m
python tools/tab5_ft8.py --port COM17 run 1
python tools/tab5_ft8.py --port COM17 watch 120
python tools/tab5_ft8.py --port COM17 decodes 40
```

## Known issues and next steps

- Clock: sub-second alignment at boot (see above).
- Gain: re-run the AUTO versus manual comparison on other dongles (V3, V4L) and antennas before assuming AUTO is best everywhere.
- The tap costs about half of the DSP core's real-time budget at 2.4 MS/s. Requesting 240 kS/s from the driver for the FT8 screen would remove the CIC stage.
- Bit-identity gate of the proposal (tap off versus baseline audio) is not yet measured; the dispatch is a single `if (active)` before the existing demodulation.
- Dashboard: band tables for FT4 and a decode-mode indicator; the heard-station Conditions view uses the same decodes.
