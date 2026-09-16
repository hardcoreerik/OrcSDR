# OrcSDR serial CLI (Tab5 host protocol)

**Transport:** native ESP32-P4 USB Serial/JTAG (`COM17` on the reference bench;
the COM number is not a device identity), 8N1, line-terminated (`\n`). Examples
use 115200 for compatibility; see the native-USB note below.
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

Every operator-facing control must expose a matching serial command or record
why it cannot. `RTL_SCREEN_STATUS` is the read-only render-ownership diagnostic:
it reports the active screen, Settings return target, transitions, rejected
inactive draws, and visible update count.

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

## USB Serial/JTAG: baud, identity, and large transfers

The Tab5's PC-facing port is the ESP32-P4's native USB Serial/JTAG interface,
not an external USB-to-UART bridge. On the accepted bench unit Windows reports
`USB\VID_303A&PID_1001&MI_00`. Confirm the current COM assignment instead of
assuming `COM17`:

```powershell
Get-PnpDevice -Class Ports |
    Format-Table Status, FriendlyName, InstanceId -AutoSize

Get-CimInstance Win32_SerialPort |
    Select-Object DeviceID, Name, Description, PNPDeviceID
```

For this native USB connection, the `115200` or `921600` value passed to
`SerialPort` is configuration metadata, not a physical UART bit clock. A host
connection at 921600 was hardware-verified while firmware still used
`Serial.begin(115200)`; it did not provide an 8x transfer-speed increase. Keep
the existing scripts at 115200 unless the hardware path changes to a real UART
bridge. With a real UART bridge, both ends must use the same baud.

Large captures use binary chunks, not ASCII samples or hex text. The current
protocol uses 16 KiB host-to-device chunks and 2 KiB device-to-host chunks,
then verifies the complete file with SHA-256. Use the repository clients:

```powershell
# PC -> Tab5
.\tools\copy_to_tab5_sd.ps1 '.\capture.s16' `
    '/orcsdr/rds_debug/capture.s16' -Port COM17

# Tab5 -> PC
.\tools\copy_from_tab5_sd.ps1 '/orcsdr/rds_debug/capture.s16' `
    -Destination '.\capture.s16' -Port COM17
```

Transfer rules and failure meanings:

- Only one process may own the COM port. Close serial monitors before running
  a transfer or upload. `PermissionError(13)` / `Access is denied` usually
  means a monitor or an orphaned PlatformIO/esptool process still holds it;
  identify the exact holder and stop only that process.
- Do not run an automatic logger beside a binary transfer. Firmware now blocks
  radio auto-start while `SD_GET`/`SD_PUT` is active so radio logs cannot be
  inserted into file bytes.
- Do not accept byte count alone. Completion requires the device and host
  SHA-256 values to match. The RDS investigation verified two 3,840,000-byte
  downloads this way.

Classification: native-USB baud behavior is expected device operation (a
how-to), port contention is a host-side troubleshooting condition, and the
former radio-log interleaving was a firmware bug fixed by the transfer guard.

Do not leave this cable connected for radio or Android TV use. The PC USB
Serial/JTAG link supplies VBUS and keeps the JTAG device enumerated; under
Wi-Fi + RTL-SDR load that path has produced `ESP_RST_BROWNOUT` on a unit that
is stable with the same firmware when the flash cable is unplugged. Current
firmware turns the P4 brownout reset off (`CONFIG_ESP_BROWNOUT_DET=n`) so the
sag no longer reboots the chip; glitches are still possible. Flash, close the
COM port, unplug the cable, then use the LAN console.

## Auth model

Two tiers, and the split is not fully consistent across the codebase (some
state-changing commands require auth, some don't — documented per-command
below rather than papered over):

- **Unauthenticated** — works immediately over the physical serial
  connection. Covers all status/query commands and several state-changing
  ones (`RTL_REC_START`, `RTL_TOOL`, `RTL_RDS_STATUS`, `RTL_FREQ` query).
- **`authenticated`** — gates the rest (`RTL_TUNE`, `RTL_VOLUME <n>`,
  `RTL_CAPTURE`/`RTL_LISTEN`, `RTL_STOP`, `RTL_PRESET_SCAN`,
  `RTL_PRESET_TUNE`, `RTL_P25_IQ_START`, `RTL_P25_IQ_STOP`, and
  `RTL_P25_REPLAY`, and `ORC_RTC_SET`). Requires the `PAIR`/`AUTH` HMAC
  handshake below.
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

## Hardware clock

The Tab5 hardware RTC is trusted only after OrcSDR has established it and
successfully read the value back. Finding the RTC chip alone does not make its
calendar trustworthy. The offline helper authenticates and copies UTC from the
connected computer; it does not use an internet or AI service:

```powershell
python tools/sync_tab5_rtc.py COM17
```

| Command | Auth | Reply | Notes |
|---|---|---|---|
| `ORC_RTC_STATUS` | no | `ORC_RTC_STATUS valid=0\|1 utc=<epoch-or-0> source=hardware\|unavailable` | Reports trusted wall-clock state. |
| `ORC_RTC_SET <unix_utc>` | yes | `ORC_RTC_SET_OK utc=...` or `ORC_RTC_SET_ERROR ...` | Writes UTC to the hardware RTC, verifies readback, then persists the established marker. |

RTC time augments monotonic uptime. It never replaces `received_ms` or changes
packet age/order, and establishing the clock does not backfill old packet times.

## Tuning and band control

| Command | Auth | Reply | Notes |
|---|---|---|---|
| `RTL_TUNE <BAND> <HZ>` | yes | `RTL_TUNE_OK band=... frequency_hz=...` | `BAND` = `FM\|AM\|WX\|CB\|P25\|LORA\|BROWSE`. Full retune (stops/restarts the capture path as needed). |
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

## LAN web console

Off by default. Enable from Settings → Companion or the serial commands
below. The page is read-only (`GET /` and `GET /api/status`); it does not
tune, change volume, or return passwords or coordinates.

| Command | Auth | Reply |
|---|---|---|
| `RTL_WEB` / `RTL_WEB_STATUS` | no | `RTL_WEB_STATUS enabled=0\|1 listening=0\|1 url=http://…/\|offline` |
| `RTL_WEB ON\|OFF` | yes | `RTL_WEB_OK enabled=… listening=… url=…` |

## Telemetry

| Command | Auth | Reply |
|---|---|---|
| `RTL_STATUS` | no | `RTL_SDR_STATUS connected=... vid=... pid=... speed=... serial="..."` — is the RTL-SDR dongle itself present/enumerated. |
| `RTL_SIGNAL` | no | `RTL_SIGNAL_STATUS band=... frequency_hz=... signal_dbfs=... stereo_locked=0\|1 left_dbfs=... right_dbfs=... rds_carrier=0\|1 rds_signal=...` — one-shot snapshot of everything the dashboard's meters show. |
| `RTL_UI STATUS` | no | `RTL_UI_STATUS ... home_font=0\|1 graphics=0\|1` — active dashboard ownership plus the current display font and live spectrum/waterfall state. |

`signal_dbfs` is the RF-level meter (matches the SIG bar). `left_dbfs`/
`right_dbfs` are FM stereo decoder outputs — meaningful only when
`stereo_locked=1`; when unlocked they mirror mono and both read the same
value. `rds_carrier`/`rds_signal` are RDS Stage 1 (carrier presence only,
see below) — always present on FM band regardless of whether the station
actually broadcasts RDS.

## POCSAG (pager receive)

Never includes decoded message text — only lock/baud/FEC counters.

| Command | Auth | Reply |
|---|---|---|
| (periodic, debug verbosity, ~5 s) | no | Same line as `RTL_POCSAG STATUS` below, emitted only while POCSAG is the active band. |
| `RTL_POCSAG STATUS` | no | `RTL_POCSAG_STATUS lock=... baud=... inverted=0\|1 frequency_hz=... scanning=0\|1 batches=... sync_losses=... codewords=... valid=... corrected=... corrected_bits=... uncorrectable=... parity_failures=... messages=... truncated=...` on demand, at any verbosity, whether or not POCSAG is the active band (`RTL_POCSAG_STATUS_ERROR not_initialized` if the decoder hasn't been allocated yet). |
| `RTL_POCSAG_SCAN` | yes | `RTL_POCSAG_SCAN_QUEUED` or `RTL_POCSAG_SCAN_INVALID` (not on POCSAG). Dwells 4 s per channel in `/orcsdr/pocsag_scan.cfg` (or the built-in nationwide-US default if that file doesn't exist), looking for a real BCH-valid decode. Serial diagnostics: `RTL_POCSAG_DISCOVERY start candidates=...`, one `RTL_POCSAG_DISCOVERY_SAMPLE index=... frequency_hz=... relative_dbfs=... valid=... corrected=... uncorrectable=... messages=...` per channel, then `RTL_POCSAG_DISCOVERY_DONE found=0\|1 confidence=clean\|weak\|none best_index=... frequency_hz=... valid=... corrected=... messages=...`. `confidence=clean` means at least one genuinely BCH-valid codeword (syndrome 0, not merely corrected) was seen — a much stronger signal than `weak` (corrected-only, the same pattern a false sync lock on noise produces). Retunes to the winner only if `found=1`. |
| `RTL_POCSAG_SCAN_STOP` | yes | `RTL_POCSAG_SCAN_STOP_QUEUED`. Cancels an in-progress scan and restores the frequency the scan started from. |
| `RTL_POCSAG_TUNE <HZ>` | yes | `RTL_POCSAG_TUNE_OK frequency_hz=...` or `RTL_POCSAG_TUNE_INVALID usage: RTL_POCSAG_TUNE <HZ>`. Clamped to the RTL-SDR's general receive range (same clamp BROWSE uses — POCSAG has no fixed band). Switches into POCSAG if it wasn't already active, and updates the frequency the dashboard header and a future discovery scan will treat as current — unlike the generic `RTL_TUNE POCSAG <HZ>`, which retunes the radio but does not update POCSAG's own frequency-of-record. |
| `RTL_POCSAG_SET_BAUD <AUTO\|512\|1200\|2400>` | yes | `RTL_POCSAG_SET_BAUD_OK baud=...` or `RTL_POCSAG_SET_BAUD_INVALID use AUTO\|512\|1200\|2400`. Restricts the decoder's parallel search to the given baud only (`AUTO` re-enables all three). Applied on the next IQ block, and resets decoder state exactly as changing baud always does — expect a brief resync. Not persisted across reboot; power-cycling returns to AUTO. |
| `RTL_POCSAG_SET_POLARITY <AUTO\|NORMAL\|INVERTED>` | yes | `RTL_POCSAG_SET_POLARITY_OK polarity=...` or `RTL_POCSAG_SET_POLARITY_INVALID use AUTO\|NORMAL\|INVERTED`. Same mechanics as baud above. |

`lock` is `orcsdr::pocsag::LockState` (0=no_signal, 1=searching, 2=locked,
3=lost). Per-dashboard verbosity control and full command coverage for
every dashboard's functions beyond POCSAG is tracked as follow-up work,
not yet built.

## FM presets

| Command | Auth | Reply |
|---|---|---|
| `RTL_PRESET_SCAN` | yes | `RTL_PRESET_SCAN_QUEUED` or `RTL_PRESET_SCAN_INVALID` (not on FM). Sweeps 76–108 MHz in ~800 kHz steps and collects up to 10 stations by signal strength. Takes tens of seconds; poll `RTL_PRESET_LIST` afterward. |
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

Use `tools/copy_to_tab5_sd.ps1` and `tools/copy_from_tab5_sd.ps1` rather than
re-implementing the binary framing by hand; they handle chunking and hashing:

```powershell
.\tools\copy_to_tab5_sd.ps1 <local-file> /orcsdr/<name> -Port COM17
.\tools\copy_from_tab5_sd.ps1 /orcsdr/<name> -Destination <local-file> -Port COM17
```

`SD_LIST` alone (no chunking needed) returns one `SD_LIST_ENTRY
bytes=... modified=... pathhex=<hex>` line per file, then
`SD_LIST_DONE count=N` — safe to call directly for a quick directory dump.

All SD writes are refused with `..._ERROR radio_busy` while a capture/
stream is active — stop the radio (`RTL_STOP`, needs auth) or wait for it
to be idle first.

## Data Catalog

These commands invoke the exact same manual Data & Maps actions as the
touchscreen. They never run at boot and they do not create an alternate
download path. Connect Wi-Fi first, then check the signed catalog before
selecting a pack by its returned stable `id`.

| Command | Reply | Notes |
|---|---|---|
| `RTL_CATALOG_STATUS` | `RTL_CATALOG_STATUS`, one `RTL_CATALOG_PACK` per pack | Safe query of catalog state and the four stable pack IDs. |
| `RTL_CATALOG_CHECK` | `RTL_CATALOG_CHECK_QUEUED` or `_REJECTED` | Downloads and verifies the signed release manifest. Requires mounted SD and connected Wi-Fi. |
| `RTL_CATALOG_INSTALL <id>` | `RTL_CATALOG_INSTALL_QUEUED` or `_REJECTED` | Streams the selected published pack to SD, validates its hash/schema, then activates it atomically. Run a check first. |
| `RTL_CATALOG_REMOVE <id> CONFIRM` | `RTL_CATALOG_REMOVE_QUEUED` or `_REJECTED` | Removes only the selected installed pack. The literal `CONFIRM` is required. |

Example:

```text
RTL_CATALOG_CHECK
RTL_CATALOG_STATUS
RTL_CATALOG_INSTALL faa_aircraft
```

## UI regression

## Dashboard control

`RTL_UI` gives a serial agent the same semantic action handlers used by the
FM, P25, LoRa, and Settings touch views. It is authenticated because every
action can change device state. Credentials continue to use signed `SET_WIFI`;
ADS-B coordinates use `RTL_ADSB_LOCATION`.

```text
RTL_UI STATUS
RTL_UI OPEN ADSB
RTL_UI ACTION SETTINGS RANGE 50
RTL_UI ACTION P25 SURVEY
RTL_UI ACTION LORA VIEW 3
RTL_UI ACTION LORA DETAILS
RTL_UI ACTION FM TUNE 101900000
RTL_UI ACTION FM GAIN_AUTO
RTL_UI ACTION FM GAIN 254
RTL_FM_GAIN STATUS
```

The LoRa Traffic toolbar equivalents are `RTL_UI ACTION LORA DETAILS`,
`RTL_UI ACTION LORA EXPORT`, `RTL_UI ACTION LORA FILTER`, and
`RTL_UI ACTION LORA CLEAR`. All four require authentication.
`RTL_UI ACTION LORA SCAN` starts or stops the same bounded energy survey as
**Scan Band**; each `RTL_LORA_SURVEY` result is emitted after its 750 ms dwell.
`RTL_SIGNAL` reports a smoothed relative dBFS value from the same IQ stream;
LoRa Overview uses the unsmoothed value for faster visual response.

`RTL_UI OPEN` accepts `HOME`, `FM`, `P25`, `ADSB`, `LORA`, `RF_LAB`,
`WIFI_ANALYSIS`, or `SETTINGS`.
`RTL_UI ACTION` accepts a domain and one of its visible touch actions:

- `FM`: `TUNE`, `DOWN`, `UP`, `SEEK_DOWN`, `SEEK_UP`, `SAVE`, `STEP`,
  `FILTER_DOWN`, `FILTER_UP`, `SPAN_DOWN`, `SPAN_UP`, `SOUND`, `VOL_DOWN`,
  `VOL_UP`, `GRAPHICS`, `RECORD`, `GAIN_AUTO`, `GAIN <0-496>`, `SCAN`,
  `SETTINGS`, `HOME`. `GAIN_AUTO` selects the lowest tuner-gain step that
  reaches the target level; `RTL_FM_GAIN STATUS` reports its progress and
  selected gain.
- `P25`: `TUNE`, `PREV`, `NEXT`, `SURVEY`, `HOLD`, `HOLD_TG <id>`, `SKIP`,
  `FOLLOW`, `ENCRYPT_SKIP`, `RELOAD`, `SPAN_DOWN`, `SPAN_UP`, `SOUND`,
  `VOL_DOWN`, `VOL_UP`, `SETTINGS`, `HOME`.
- `LORA`: `VIEW <0-5>`, `NODE <index>`, `DETAILS`, `FAVORITE`, `FILTER`, `SCAN`, `IQ`,
  `LOG`, `CLEAR`, `EXPORT`, `FOLLOW`, `CHANNELS`, `SETTINGS`, `HOME`.
- `SETTINGS`: `WIFI_POWER <0|1>`, `WIFI_BOOT <0|1>`, `ANTENNA <0|1>`, `SCAN`,
  `CONNECT_SAVED <index>`, `FORGET <index>`, `MOVE_UP <index>`,
  `MOVE_DOWN <index>`, `RANGE <nm>`, `BRIGHTNESS <0-255>`, `ROTATION <1|3>`,
  `TIMEOUT <seconds>`, `VOLUME <0-255>`, `SOUND <0|1>`, `AUTO_START <0|1>`,
  `GRAPHICS <0|1>`, `WEB <0|1>`, `CATALOG_CHECK`, `CATALOG_INSTALL <index>`,
  `CATALOG_REMOVE <index>`, `CLOSE`.

Each succeeds with `RTL_UI_ACTION_OK`. Inputs are intentionally routed through
the existing dashboard handlers rather than duplicating touch-only state.

For P25, `SURVEY` starts or stops the bounded scan of the active profile's
control channels. Stopping restores the selected control channel. `HOLD`
holds the displayed grant, or arms a hold for the next grant when none is
displayed; `HOLD_TG <id>` selects a specific talkgroup. Repeat `HOLD` or
`HOLD_TG <id>` to release that hold.

## Wi-Fi automation

Wi-Fi automation uses the same bounded scan snapshot and Settings handlers as
the display. SSIDs are returned as hexadecimal bytes so arbitrary SSID text
cannot forge serial records. Passwords are never returned.

| Command | Auth | Reply / behavior |
|---|---|---|
| `RTL_WIFI_STATUS` | no | Station/Hosted state, scan/connect state, profile/AP counts, power, auto-connect, and antenna. |
| `RTL_WIFI_SCAN` | no | Queues one scan; wait for `RTL_WIFI_SCAN_RESULTS count=N` and `RTL_WIFI_COEX event=scan_complete`. |
| `RTL_WIFI_RESULTS` | no | Bounded `RTL_WIFI_AP` rows with `ssid_hex`, BSSID, RSSI, channel, and security flag. |
| `RTL_WIFI_PROFILES` | no | Priority-ordered SSID-only profile list; never returns passwords. |
| `RTL_WIFI_CONNECT_SAVED` | no | Compatibility shortcut for saved profile 0. Indexed connection uses `RTL_UI ACTION SETTINGS CONNECT_SAVED <index>`. |
| `RTL_WIFI_DISCONNECT` | yes | Disconnects Wi-Fi and restores the paused radio/audio path. |
| `SET_WIFI <ssid_hex> <pass_hex> <hmac>` | yes + signed payload | Provisions slot 0 and attempts connection without echoing credentials. |

Power, auto-connect, antenna selection, scan, indexed connection, forget, and
priority moves use authenticated `RTL_UI ACTION SETTINGS ...` commands listed
above. Invalid boolean values and profile indices return
`RTL_UI_ACTION_INVALID` instead of a false success.

Run the complete non-destructive hardware surface with:

```powershell
apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1 -WifiOnly -PairingKeyPath <key-file>
```

Add `-RequireWifiConnection` when a real saved profile must associate for the
test to pass.

The regression command checks the shared radio-control geometry and screen
ownership self-checks without changing receiver state or NVS. `RUN` also
exercises an actual screen handoff: Home to the current FM, P25, ADS-B, or
LoRa dashboard and back to Home. It refuses to run over Settings, NAV, keypad,
or documentation overlays.

| Command | Reply | Notes |
|---|---|---|
| `RTL_UI_REGRESSION CHECK` | `RTL_UI_REGRESSION_RESULT ... pass=1` | Passive checks only. |
| `RTL_UI_REGRESSION RUN` | `RTL_UI_REGRESSION_RESULT ... transitioned=1 restored=1` | Exercises the bounded handoff and confirms the original UI snapshot returned. |

Use the repeatable runner; it disables DTR/RTS before opening COM17 so it does
not reset the Tab5:

```powershell
.\tools\run-tab5-ui-regression.ps1 -Port COM17
.\tools\run-tab5-ui-regression.ps1 -Port COM17 -Run
.\tools\run-tab5-ui-regression.ps1 -Port COM17 -Soak -Cycles 10 -DwellSeconds 2
.\tools\run-tab5-ui-regression.ps1 -Port COM17 -Profile Smoke
.\tools\run-tab5-ui-regression.ps1 -Port COM17 -Profile Stress -Seed 12345
.\tools\run-tab5-ui-regression.ps1 -Port COM17 -Profile Stress -Cycles 1 -WifiEvery 1
.\tools\run-tab5-ui-regression.ps1 -Port COM17 -Profile Overnight -Cycles 500
.\tools\run-tab5-ui-regression.ps1 -Port COM17 -RadioScan -Cycles 10
```

`-RadioScan` repeatedly starts FM and P25 scans, transfers tuner ownership to
another dashboard during each scan, rejects stale restoration, and records heap,
DMA, stack, uptime, reset, watchdog, and panic evidence. The first cycle warms
the dashboards before the memory baseline is recorded. The run fails if free
heap falls by more than 4 KiB, DMA-capable heap falls by more than 2 KiB, or the
largest DMA-capable block falls below 20 KiB.

`-Soak` authenticates with `.orclink\ui-doc.key`, then drives every radio
dashboard through Home and Settings. `Smoke`, `Stress`, and `Overnight` provide
5, 50, and 500-cycle defaults. Stress and Overnight randomize the radio order
from the recorded seed and cycle Wi-Fi every ten passes. All profiles exercise
mute/unmute, query heap health, require advancing FM audio after every return,
and save a timestamped log under `artifacts\ui-soak`. The runner fails on panic,
watchdog, brownout, assertion, reboot, timeout, exclusive-screen violation, or
lost audio; after failure it stays attached briefly to capture reset evidence.
It restores the starting dashboard, tuning, sound, and verbosity when possible.
Use `-WifiEvery 1` for a focused Wi-Fi cycle on every pass.

## Serial verbosity and crash evidence

| Command | Reply | Notes |
|---|---|---|
| `RTL_SERIAL VERBOSITY` | `RTL_SERIAL_VERBOSITY mode=...` | Query without authentication. |
| `RTL_SERIAL VERBOSITY QUIET\|NORMAL\|DEBUG\|TRACE` | `RTL_SERIAL_VERBOSITY_OK mode=...` | Authenticated, persistent setting. `NORMAL` is the default. |
| `RTL_HEALTH` | `RTL_HEALTH_STATUS ...` | Heap, internal DMA, task count, uptime, and boot reset reason. |

`QUIET` retains errors, command replies, panic text, and reset evidence.
`NORMAL` adds normal lifecycle information. `DEBUG` enables periodic receiver,
RDS, power, and heartbeat diagnostics. `TRACE` additionally enables decoded
ADS-B frames, LoRa energy triggers, spectrum timing, and scan samples.

The partition table reserves a 256 KiB flash core-dump partition. With the
matching ELF from `build-native-hosted3`, inspect a retained panic using
`idf.py -B build-native-hosted3 -p COM17 coredump-info`.

Run the host-side crash/audio parser check without a device:

```powershell
.\tools\run-tab5-ui-regression.ps1 -SelfCheck
```

## Documentation capture

The authenticated documentation commands stage stable views and save an exact
1280x720 BMP through M5GFX. Use `tools/build-help-media.ps1` instead of driving
the commands by hand; it verifies the firmware/catalog, retrieves each BMP
through the hash-checked SD protocol, and always attempts state restoration.

| Command | Reply | Notes |
|---|---|---|
| `UI_DOC_LIST` | `UI_DOC_LIST_BEGIN`, one `UI_DOC_SCREEN` per view, `UI_DOC_LIST_DONE` | Enumerates the firmware-owned screen catalog. |
| `UI_DOC_SHOW <screen-id> <live\|demo>` | `UI_DOC_SHOW_DONE` | Enters documentation mode without persisting navigation or demo state. Demo views carry a visible `DEMO` badge. |
| `UI_CAPTURE <slug>` | `UI_CAPTURE_DONE ... bytes=... width=1280 height=720 firmware=... sha256=...` | Freezes the frame, stops reception if needed, and writes `/orcsdr/screenshots/<slug>.bmp`. |
| `UI_DOC_EXIT` | `UI_DOC_EXIT_DONE restored=true` | Restores the prior dashboard, view, sound, and reception state. |

All four commands require the normal `PAIR`/`AUTH` session. Arbitrary editors
cannot be selected; the only keyboard capture is a sanitized deterministic
example, so saved credentials and private location fields are never exposed.
ADS-B traffic screen IDs advertise `live` mode only and never substitute
synthetic aircraft when the sky is empty. The ADS-B Settings view is omitted
from documentation capture because it contains the receiver's saved location.

## IQ / LoRa capture

`RTL_IQ_START`/`_STOP`/`_SAVE`/`_STATUS`, `RTL_IQ_RETRIEVE_BEGIN`/`_END`,
`RTL_IQ_GET_BEGIN`/`_CHUNK`/`_ABORT`, `RTL_LORA_AUTO ON|OFF` (auth),
`RTL_LORA_TUNE <HZ>` (auth; `RTL_LORA_TUNE_OK frequency_hz=...` or
`RTL_LORA_TUNE_ERROR range=<min>-<max>`; hot-retunes if LoRa is already
streaming, otherwise switches into it), `RTL_LORA_PLAN_STATUS`,
`RTL_LORA_REGION_LIST`, `RTL_UI ACTION LORA REGION <1-based-index>`,
`RTL_UI ACTION LORA REGION_PREV|REGION_NEXT`,
`RTL_UI ACTION LORA SLOT <1-based-slot>`, and
`RTL_UI ACTION LORA SLOT_PREV|SLOT_NEXT` (selection commands require auth and
persist region/slot), `LORA_SD_LOG ON|OFF|STATUS`,
`LORA_MESSAGE_CLEAR` — raw IQ capture and the LoRa/Meshtastic
energy-triggered decode pipeline. See
[docs/lora/README.md](lora/README.md) for the intended workflow (these are
oriented around the LoRa energy-trigger + host-decode round trip, not
general-purpose IQ dumping). There is no live serial override for spreading
factor or bandwidth yet — those load once from `/orcsdr/lora.cfg` at boot;
tracked as follow-up work alongside POCSAG's `SET_BAUD`/`SET_POLARITY`
precedent.

## P25 validation and replay

These commands expose the P25 receiver state without depending on the visible
dashboard. Status is read-only. Capture is limited to 1 MiB and remains on the
configured control channel, so it cannot contain a followed voice call.

| Command | Auth | Reply | Notes |
|---|---|---|---|
| `RTL_P25_STATUS` | no | `RTL_P25_STATUS configured=... profile_id=... survey=... frame_sync=... p2_grants=... p2_sync_words=...` | Includes active-profile and operator-control state, recent grants, Phase I voice, Phase II mapping/probe, heap, stack-headroom, USB, IQ, and audio-drop counters. Identity fields come from decoded over-the-air data. |
| `RTL_P25_PHASE2_STATUS` | no | `RTL_P25_PHASE2_STATUS trace=... active=... tg=... carrier_hz=... slot=... sync_words=... complete_bursts=... duid_valid=... duid=... voice_bursts=... control_bursts=...` | Reports the last TDMA grant mapping and the fixed-memory 6,000-symbol/s traffic burst detector. A complete burst is 180 dibits. DUID fields report burst classification and one-bit correction; they do not claim Phase II descrambling, MAC, vocoder-frame, or audio decode. |
| `RTL_P25_PHASE2_TRACE` | no | `RTL_P25_PHASE2_TRACE enabled=0\|1` | Reports whether the diagnostic one-call transport probe is enabled. The default is off. |
| `RTL_P25_PHASE2_TRACE ON\|OFF` | yes | `RTL_P25_PHASE2_TRACE_OK enabled=0\|1` | When enabled, a clear TDMA grant is briefly tuned for burst-sync evidence and then returned to the control channel. Encrypted grants are reported but never probed for audio or decrypted. |
| `RTL_P25_PROFILE_LIST` | no | `RTL_P25_PROFILE_LIST_BEGIN`, zero or more `RTL_P25_PROFILE`, then `_DONE` | Lists the bounded SD profile store and marks the active system. |
| `RTL_P25_PROFILE_SELECT <id>` | yes | `RTL_P25_PROFILE_OK operation=select ...` | Validates and selects `/orcsdr/p25/<id>/profile.cfg`. |
| `RTL_P25_PROFILE_IMPORT <path> <id>` | yes | `RTL_P25_PROFILE_OK operation=import ...` | Imports a valid version-1 or version-2 file under a new safe profile ID and preserves the source. Duplicate IDs are rejected; the `p25_` prefix is reserved for signed catalog packs. |
| `RTL_P25_PROFILE_EXPORT <id> /orcsdr/exports/<file>` | yes | `RTL_P25_PROFILE_OK operation=export ...` | Writes a validated version-2 copy. Destinations outside the exports directory and nested paths are rejected. |
| `RTL_P25_PROFILE_RENAME <id> <name>` | yes | `RTL_P25_PROFILE_OK operation=rename ...` | Changes the local display name without changing the stable profile ID. |
| `RTL_P25_PROFILE_DELETE <id> CONFIRM` | yes | `RTL_P25_PROFILE_OK operation=delete ...` | Deletes the selected profile file. The literal confirmation is required. |
| `RTL_P25_MODULATION` | no | `RTL_P25_MODULATION configured=auto selected=c4fm timing_gain=... carrier_gain=...` | Reports the configured Phase I demodulator and the path selected by automatic acquisition. |
| `RTL_P25_MODULATION AUTO\|C4FM\|CQPSK` | yes | `RTL_P25_MODULATION_OK configured=...` | Selects automatic acquisition, the C4FM discriminator, or the linear CQPSK/LSM path. The setting is saved to the active profile and the configured receiver is reacquired. |
| `RTL_P25_ENCRYPTION_STATUS` | no | `RTL_P25_ENCRYPTION_STATUS detected=... algid=... kid=... muted_frames=... returns=...` | Reports the last valid LDU2 Encryption Sync result and cumulative mute/return counters for automated acceptance. It identifies and suppresses protected audio; it does not decrypt it. |
| `RTL_P25_SCAN` | yes | `RTL_P25_SURVEY ...` | Starts the configured control-channel survey. Use `RTL_UI ACTION P25 SURVEY` to toggle it from automation; cancellation restores the selected control channel. |
| `RTL_P25_IQ_START` | yes | `RTL_IQ_START source=p25 ...` | Requires a running P25 control channel. Voice following is suppressed while the bounded capture fills. |
| `RTL_P25_IQ_STATUS` | no | `RTL_P25_IQ_STATUS ...` | Reports capture state, size limit, source frequency, sample rate, and last saved path. |
| `RTL_P25_IQ_STOP` | yes | `RTL_IQ_DONE path=... source=p25 ...` | Stops and saves the capture in the existing ORCIQ CU8 format. |
| `RTL_P25_REPLAY /orcsdr/<file>.orciq` | yes | `RTL_P25_REPLAY_DONE ... modulation_configured=... modulation_selected=...` | Requires the live radio to be stopped; rejects paths outside `/orcsdr`, malformed headers, non-CU8 data, wrong sample rates, and truncated files. Use `RTL_P25_MODULATION` before replay to compare paths deterministically. |

The hardware acceptance runner supports either a hexadecimal pairing-key file
or an NVS-containing backup. It requires control lock, decoded identity,
multiple grants, voice follow and return, IMBE and PCM growth, control relock,
stable drop counters, a heap floor, and voice-task stack headroom:

```powershell
apps/orcsdr-tab5/tools/run-p25-validation.ps1 `
  -Port COM17 -ControlFrequencyHz 453925000 `
  -PairingKeyPath .orclink/ui-doc.key -CaptureFixture
```

Add `-RequireEncryptedVoice` during a controlled live test to require a valid
encrypted LDU2 detection, muted voice frames, and an immediate return to the
control channel. `-Modulation AUTO|C4FM|CQPSK` selects the demodulator for the
run and restores the previous setting afterward. For deterministic on-device
replay without waiting for live traffic:

Add `-WatchTalkgroup <TGID>[,<TGID>...]` to enable Phase II trace temporarily
and print the local watchlist. Any live Phase II talkgroup can satisfy the gate,
but its grant, traffic-channel retune, burst synchronization, control return,
and control relock must all match the same TGID. The result records whether the
observed TGID was on the supplied watchlist. The runner restores the previous
trace state afterward. This validates transport only; it does not require IMBE
or PCM. Add `-UseExistingSession` when the requested control channel is already
locked and restarting it would invoke the normal saved-profile fallback survey;
the runner still verifies that the live frequency and decoded control state
match `-ControlFrequencyHz` before collecting evidence.

```powershell
apps/orcsdr-tab5/tools/run-p25-validation.ps1 `
  -Port COM17 -PairingKeyPath .orclink/ui-doc.key `
  -ReplayOnly -ReplayPath /orcsdr/p25_control_lane_453925.orciq `
  -Modulation CQPSK
```

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
