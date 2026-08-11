# OrcSDR serial CLI (Tab5 host protocol)

**Transport:** USB-serial (COM17 on the reference dev machine), 115200 baud, 8N1, line-terminated (`\n`).
**Firmware:** `apps/orcsdr-tab5/ui/main.cpp`, `process_command()` / `poll_serial()`.

This is the human/AI-facing control surface for the Tab5 radio — everything
needed to tune, scan, monitor telemetry, and pull files off the device
without touching the touchscreen. It's the same protocol the physical UI
itself drives internally (touch handlers call the same underlying functions
these commands do), so anything scriptable here is exactly what the device
is already doing live.

Send one command per line. Most commands reply with one or more lines
prefixed by the command's own name (e.g. `RTL_TUNE ...` replies
`RTL_TUNE_OK ...` or `RTL_TUNE_INVALID ...`). A command that doesn't match
anything produces no reply at all — there is no error line for "unknown
command," so typos fail silently. `RTL_HELP` is authoritative for the exact
command set; this document explains what each one does and how to use them
together.

## Quick start

```powershell
# One-shot: send a command, print whatever comes back for 2 seconds.
$port = New-Object System.IO.Ports.SerialPort COM17,115200,None,8,One
$port.ReadTimeout = 2000
$port.NewLine = "`n"
$port.Open()
$port.WriteLine("RTL_HELP")
Start-Sleep -Milliseconds 500
while ($true) { try { $port.ReadLine() } catch { break } }
$port.Close()
```

Any serial library in any language works the same way — this is a plain
line protocol, nothing OrcSDR-specific about the transport itself.

## Auth model

Two tiers, and the split is not fully consistent across the codebase (some
state-changing commands require auth, some don't — documented per-command
below rather than papered over):

- **Unauthenticated** — works immediately over the physical serial
  connection. Covers all status/query commands and several state-changing
  ones (`RTL_REC_START`, `RTL_TOOL`, `RTL_RDS_STATUS`, `RTL_FREQ` query).
- **`authenticated`** — gates the rest (`RTL_TUNE`, `RTL_VOLUME <n>`,
  `RTL_CAPTURE`/`RTL_LISTEN`, `RTL_STOP`, `RTL_PRESET_SCAN`,
  `RTL_PRESET_TUNE`). Requires the `PAIR`/`AUTH` HMAC handshake below.
  This exists for a remote/untrusted-host scenario (e.g. Bluetooth); if
  you're driving the device over a physically-attached USB cable, that
  trust boundary is arguably already crossed, but the gate is enforced as
  written today. Pair once per session:

```text
> PAIR <32-byte-hex-key>
< PAIR_OK                              (or PAIR_LOCKED if already paired to a different key)

> AUTH <16-byte-hex-nonce> <32-byte-hex-hmac-of("host"+nonce)>
< AUTH_OK <32-byte-hex-hmac-of("device"+nonce)>
```

The pairing key is stored in NVS after first pair and persists across
reboots. There is currently no documented out-of-band way to generate a
compliant nonce/proof pair from a plain script without replicating the
HMAC-SHA256 handshake — treat the authenticated commands as requiring a
proper pairing client, not something to hand-roll casually.

## Tuning and band control

| Command | Auth | Reply | Notes |
|---|---|---|---|
| `RTL_TUNE <BAND> <HZ>` | yes | `RTL_TUNE_OK band=... frequency_hz=...` | `BAND` = `FM\|AM\|WX\|CB\|LORA\|BROWSE`. Full retune (stops/restarts the capture path as needed). |
| `RTL_FREQ` | no | `RTL_FREQ_STATUS band=... frequency_hz=... mode=...` | Query only. |
| `RTL_FREQ <HZ>` | yes | `RTL_FREQ_OK band=... frequency_hz=...` | Hot retune *within* the current band — cheaper than `RTL_TUNE`, use for stepping/scanning. |
| `RTL_CAPTURE` / `RTL_LISTEN <BAND>` | yes | `RTL_CAPTURE_QUEUED ...` or `RTL_CAPTURE_BUSY_OR_UNAVAILABLE` | Older, band-limited entry point (`FM`/`KZEL`/`NOAA`/`WX`/`AM`/`LORA` only, no `CB`/`BROWSE`, no arbitrary frequency). `RTL_LISTEN` is continuous, bare `RTL_CAPTURE` is one-shot. Prefer `RTL_TUNE` for new work — this exists for compatibility with older tooling. |
| `RTL_STOP` | yes | `RTL_STOPPING` | Stops the active capture/stream. |
| `RTL_TOOL` | no | `RTL_TOOL_STATUS tool=RADIO\|SCOPE\|CAPTURE` | Query the active tool tab. |
| `RTL_TOOL <RADIO\|SCOPE\|CAPTURE>` | no | (none) or `RTL_TOOL_INVALID` | Switch tool tab. Case-insensitive value. |

Band default frequencies (used when a command doesn't specify one, e.g.
`RTL_LISTEN WX`): FM 96.113 MHz (last-tuned FM freq persists in NVS and
overrides this), AM/WX/CB/LoRa each have their own fixed default — see
`rtl_band_default_frequency()` in `main.cpp` for exact values, they're
band-plan specific and not usually worth hardcoding in a client.

## Volume

| Command | Auth | Reply |
|---|---|---|
| `RTL_VOLUME` | no | `RTL_VOLUME_STATUS volume=<0-32>` |
| `RTL_VOLUME <0-32>` | yes | `RTL_VOLUME_OK volume=...` or `RTL_VOLUME_INVALID` |

## Telemetry

| Command | Auth | Reply |
|---|---|---|
| `RTL_STATUS` | no | `RTL_SDR_STATUS connected=... vid=... pid=... speed=... serial="..."` — is the RTL-SDR dongle itself present/enumerated. |
| `RTL_SIGNAL` | no | `RTL_SIGNAL_STATUS band=... frequency_hz=... signal_dbfs=... stereo_locked=0\|1 left_dbfs=... right_dbfs=... rds_carrier=0\|1 rds_signal=...` — one-shot snapshot of everything the dashboard's meters show. |

`signal_dbfs` is the RF-level meter (matches the SIG bar). `left_dbfs`/
`right_dbfs` are FM stereo decoder outputs — meaningful only when
`stereo_locked=1`; when unlocked they mirror mono and both read the same
value. `rds_carrier`/`rds_signal` are RDS Stage 1 (carrier presence only,
see below) — always present on FM band regardless of whether the station
actually broadcasts RDS.

## FM presets

| Command | Auth | Reply |
|---|---|---|
| `RTL_PRESET_SCAN` | yes | `RTL_PRESET_SCAN_QUEUED` or `RTL_PRESET_SCAN_INVALID` (not on FM) | Sweeps 87.5–108 MHz, ~800 kHz steps, collects up to 10 stations by signal strength. Takes tens of seconds; poll `RTL_PRESET_LIST` afterward. |
| `RTL_PRESET_LIST` | no | `RTL_PRESET_LIST_BEGIN count=N` then N × `RTL_PRESET <n> frequency_hz=... level=...` then `RTL_PRESET_LIST_END` | Persists across reboots (NVS). |
| `RTL_PRESET_TUNE <n>` | yes | `RTL_PRESET_TUNE_OK index=... frequency_hz=...` or `RTL_PRESET_TUNE_INVALID` | 1-based index, matching the on-screen list numbering. |

## RDS (FM band only)

RDS decoding is staged — see `phasing.md` for the current status. Stage 1
(carrier detection) and Stage 2 (bit/block sync) are hardware-verified against
live 96.1 KZEL and a captured MPX replay. Stage 3 parsing/display of PS, PTY,
and RadioText remains open.

| Command | Auth | Reply |
|---|---|---|
| `RTL_RDS_STATUS` | no | see below |
| `RTL_RDS_CAPTURE_START` | no | starts an 8-second, 240 kS/s MPX capture in PSRAM |
| `RTL_RDS_CAPTURE_STOP` / `RTL_RDS_CAPTURE_SAVE` | no | stops and exports `.s16` plus `.json` metadata to SD |
| `RTL_RDS_CAPTURE_STATUS` | no | capture progress, frequency, SD state, and last path |
| `RTL_RDS_REPLAY <path.s16>` | no | resets and replays an MPX capture through the same RDS processor; live radio must be stopped |

```text
RDS_STATUS carrier=0|1 carrier_signal=<dB> block_locked=0|1 bler=<%>
           good=<n> total=<n> hyp0_streak=<n> hyp1_streak=<n>
           timing_chip_rate=<Hz> timing_correction_ppm=<ppm>
           nco_freq_off=<rad/sample> i_lpf=<n> q_lpf=<n> mu=<0..1>
           A=<hex16> B=<hex16> C=<hex16> D=<hex16>
           driver_overruns=<n> driver_drops=<n> effective_sps=<n>
           audio_chunks=<n> audio_drops=<n>
```

- `carrier` — Stage 1, whether 57 kHz subcarrier energy is present.
- `block_locked` / `bler` / `good` / `total` — Stage 2 block-sync status.
  `bler=100%` with `total=0` means block sync has never been achieved since
  tuning to this frequency, not that the signal is bad.
- `hyp0_streak` / `hyp1_streak` — best streak for each chip-pair polarity
  across four fractional timing phases. A streak of 4 correctly-spaced
  offset-word matches declares lock.
- `A`/`B`/`C`/`D` — last decoded block content (hex). **Not meaningful
  until `block_locked=1`** — treat as noise otherwise, per the current
  known-issue in `phasing.md`.

The legacy periodic diagnostic pair is compiled off by default. Use
`RTL_RDS_STATUS` for on-demand diagnostics without a continuous serial load:

```text
RDS_STAGE2 locked=... bler=... good=... total=... hyp0_locked=... hyp0_streak=...
           hyp1_locked=... hyp1_streak=... nco_freq_off=... i_lpf=... q_lpf=...
           bp_env=... A=... B=... C=... D=...
RDS_TIMING chip_rate=... mu=... symbols_sec=... correction_ppm=... freq_off=...
```

`i_lpf`/`q_lpf` are the complex 57 kHz baseband before carrier-independent
differential pairing. `timing_correction_ppm` reports the measured RTL sample
clock calibration (`-10` on the accepted fixture); `nco_freq_off` is retained
for protocol compatibility and currently reports zero. The driver/audio fields
are explicit, on-demand stream-continuity counters. `symbols_sec` should read
close to 2375 (the RDS biphase chip rate, not the final 1187.5 bit/s information
rate).

### MPX capture and replay

Capture stores signed 16-bit little-endian FM multiplex samples at 240 kS/s
under `/orcsdr/rds_debug/`, with a sibling JSON file containing the sample
rate, tuned frequency, sample count, radians-per-LSB scale, and start uptime.
The raw `.s16` file is directly consumable by Redsea:

```text
RTL_RDS_CAPTURE_START
... wait up to 8 seconds ...
RTL_RDS_CAPTURE_STOP
RTL_RDS_CAPTURE_STATUS
```

After copying the reported `.s16` file to a PC:

```bash
redsea --input mpx -r 240k < capture.s16
```

For deterministic on-device replay, stop the live radio first and use the SD
path reported by `RTL_RDS_CAPTURE_STOP`:

```text
RTL_RDS_REPLAY /orcsdr/rds_debug/001_96113000_mpx.s16
RTL_RDS_STATUS
```

## Recording (post-demod WAV capture)

| Command | Auth | Reply |
|---|---|---|
| `RTL_REC_START` | no | (switches to Capture tool, starts recording) |
| `RTL_REC_STOP` | no | (stops and exports WAV to SD) |
| `RTL_REC_STATUS` | no | multi-line status (buffered seconds, sample count, last file path — see `audio_rec_status_print()`) |
| `RTL_REC_SAVE` | no | re-exports the currently-held PCM buffer, useful after inserting an SD card mid-session |

Capped at `kAudioRecMaxSeconds` (12s) per recording, 48 kHz mono PCM,
written under `/orcsdr/rec_NNN_<BAND>_<HZ>.wav`.

## SD card file transfer

Chunked binary protocol (`SD_LIST`, `SD_GET_BEGIN`/`_CHUNK`/`_ABORT`,
`SD_PUT_BEGIN`/`_DATA`/`_ABORT`, `SD_REMOVE`) with SHA-256 verification and
staged-write rollback on failure. All paths must be under `/orcsdr/`.

**Use `tools/copy_to_tab5_sd.ps1`** rather than re-implementing this by
hand — it's the reference client and handles the chunking/hashing:

```powershell
.\tools\copy_to_tab5_sd.ps1 <local-file> /orcsdr/<name> -Port COM17
```

`SD_LIST` alone (no chunking needed) returns one `SD_LIST_ENTRY
bytes=... modified=... pathhex=<hex>` line per file, then
`SD_LIST_DONE count=N` — safe to call directly for a quick directory dump.

All SD writes are refused with `..._ERROR radio_busy` while a capture/
stream is active — stop the radio (`RTL_STOP`, needs auth) or wait for it
to be idle first.

## IQ / LoRa capture

`RTL_IQ_START`/`_STOP`/`_SAVE`/`_STATUS`, `RTL_IQ_RETRIEVE_BEGIN`/`_END`,
`RTL_IQ_GET_BEGIN`/`_CHUNK`/`_ABORT`, `RTL_LORA_AUTO ON|OFF`,
`LORA_SD_LOG ON|OFF|STATUS`, `LORA_MESSAGE_CLEAR` — raw IQ capture and the
LoRa/Meshtastic energy-triggered decode pipeline. See
[docs/lora/README.md](lora/README.md) for the intended workflow (these are
oriented around the LoRa energy-trigger + host-decode round trip, not
general-purpose IQ dumping).

## Example workflows

**Tune to a specific frequency and check signal:**
```text
> RTL_TUNE FM 101900000
< RTL_TUNE_OK band=FM frequency_hz=101900000
> RTL_SIGNAL
< RTL_SIGNAL_STATUS band=FM frequency_hz=101900000 signal_dbfs=-38.2 stereo_locked=1 ...
```

**Scan and tune to the strongest preset:**
```text
> RTL_PRESET_SCAN
< RTL_PRESET_SCAN_QUEUED
  (wait ~30-60s, sweeping the whole FM band)
> RTL_PRESET_LIST
< RTL_PRESET_LIST_BEGIN count=6
< RTL_PRESET 1 frequency_hz=94500000 level=-45.0
< ...
< RTL_PRESET_LIST_END
> RTL_PRESET_TUNE 1
< RTL_PRESET_TUNE_OK index=1 frequency_hz=94500000
```

**Poll RDS decode progress while developing the decoder:**
```text
> RTL_TUNE FM 96100000
> RTL_RDS_STATUS
< RDS_STATUS carrier=1 carrier_signal=-6.2 block_locked=0 bler=100.0% ...
  (poll RTL_RDS_STATUS; continuous stream diagnostics are disabled by default)
```

## What's not here yet

- No JSON output mode — everything is `key=value` space-separated text.
  Fine for line-oriented parsing, more work for a strict JSON client.
- `RTL_HELP`'s command list is maintained by hand alongside this doc — if
  you add a command, update both.
