# OrcDial phase 1

Standalone M5Dial V1.1 physical VFO prototype for OrcSDR. The Tab5 firmware is unchanged. The Dial can run offline; its display says `OFFLINE` and signal reads `-- dB` until an actual receiver supplies state. The included receiver sketch simulates a radio for link testing only.
The boot splash embeds the exact `orc_badge_104.png` used by the Tab5, copied from `apps/orcsdr-tab5/main/`.
The badge splash stays visible for five seconds before Home. Home shows the Orc badge and `OrcSDR`.

## Build

Board: M5Dial V1.1 (Stamp-S3A / ESP32-S3), using PlatformIO `m5stack-stamps3` and Arduino ESP32 framework. Dependencies and versions are pinned in `platformio.ini`: `espressif32@6.12.0`, M5Dial 1.0.3, M5Unified 0.2.24, M5GFX 0.2.31. M5Dial's current Arduino guide recommends its board package 3.2.2 or later; actual V1.1 compatibility with this PlatformIO package needs a physical check.

```powershell
cd F:\AI\OrcSDR-TEMP\m5dial-vfo\orcdial
$env:PYTHONIOENCODING='utf-8'
$orcdialPio = 'C:\Users\hardc\.platformio\penv\Scripts\pio.exe' # Python 3.11 PlatformIO environment on this PC
& $orcdialPio run -e dial
& $orcdialPio run -e receiver
```

To update the Dial on COM14:

```powershell
& $orcdialPio run -e dial -t upload --upload-port COM14
& $orcdialPio device monitor -p COM14 -b 115200
```

The optional receiver uses a second ESP32 DevKit (`esp32dev`); replace its board ID if the physical test board differs. Use its own port in the upload and monitor commands. Both devices must share a 2.4 GHz channel. The receiver stub uses channel 1 and prints `ORCDIAL_RECEIVER_READY`.

## Controls

- Home: press, tap, or turn to open the dashboard carousel. Turn to move through Home and the 15 current Tab5 dashboard destinations. Tap a side card to move one item, or press/tap the center card to open it. Tap the bottom to return Home.
- Each dashboard displays its own title and color; tunable dashboards choose a default tuning graphic. The five existing graphics can still be cycled on the active frequency view. ADS-B, LoRa, Wi-Fi Analysis, and Settings show `NO LIVE DATA` rather than invented measurements. Offline dashboard choices are local previews; a linked receiver must acknowledge `SET_DASHBOARD` and return `RADIO_STATE` before the selected state becomes authoritative.
- All 15 destinations have distinct code-drawn artwork inspired by the concept sheet: FM/AM scales, weather cloud and alert tower, airband runway, marine vessel, CB meter, ADS-B radar, satellite orbit, LoRa mesh, RF Lab graticule, P25 radio, shortwave globe, POCSAG pager, Wi-Fi arcs, and settings gear. Offline radio screens begin at representative preview frequencies and modes; these are labeled `OFFLINE` and are not RF readings.
- Rotate: tune by the selected step. Rapid detents get a modest 2x or 5x acceleration.
- While turning VFO, a short-lived frequency reel shows the neighboring steps above and below the authoritative center frequency, with a marker following the encoder. During a pending radio command the actual center stays unchanged until state returns.
- Five tuning graphics are available: **Reel** (original), **Dial** (needle and scale), **Odometer** (MHz/kHz/Hz windows), **Tape** (horizontal tuning ruler), and **Split** (large MHz and fractional readout). Tap the right side of the frequency view to cycle styles; the bottom label shows the active style. The chosen style affects only display graphics, never tuning commands or radio state.
- Short press on a dashboard: VFO → step → gain → squelch → volume → VFO. On Home it opens the carousel; on the carousel it opens the selected dashboard.
- Hold 0.9–4 s: return Home.
- Hold at least 4 s: start pairing/discovery. This is deliberately separate from tuning.
- Touch upper left on a dashboard: cycle mode when connected. Touch upper right: open the manual **Connect** screen. On that screen, touch center to start pairing and bottom to return Home. On a dashboard, bottom left returns Home and bottom right opens the carousel.
- Offline: these controls update local display state. Connected: the Dial waits for `RADIO_STATE` from the receiver; the receiver owns the displayed radio state.

To test pairing with the receiver stub, type `p` in its serial monitor. This opens a 30-second pairing window. Hold the Dial button for at least 4 seconds. Both devices save the peer MAC; subsequent boots scan for the known peer. The stub is a test target and its simulated frequency is not RF measurement. It always marks signal data invalid.

Compile-time demo display: add `-DORCDIAL_DEMO=1` to the Dial environment's `build_flags`. This disables ESP-NOW and labels the UI `DEMO / OFFLINE`.
The initial step is 5 kHz. Set `-DORCDIAL_DEFAULT_STEP_HZ=12500` (or another listed step) in both build environments to change the startup default.

## Verification levels

- **Code reviewed:** packet layout, CRC, control flow, pairing and state transitions inspected.
- **Host tested:** `tests/protocol_test.cpp` compiled and ran with Visual Studio `cl /std:c++17`; exit code 0. Covers round trip, CRC rejection, sequence ordering and frequency bounds.
- **Build verified:** `& $orcdialPio run -e dial` and `& $orcdialPio run -e receiver` both succeeded.
- **Hardware verified (partial):** the Dial on COM14 was flashed with esptool hash verification. Serial `ORCDIAL_BOOT`, `ESPNOW_INIT_OK`, and repeated `ORCDIAL_STATUS link=OFFLINE freq=146520000` were observed. The user confirmed the buffered display no longer flickers, the larger text reads well, and the new OrcSDR Home and side carousel look good. Dashboard views and five-second splash timing still need visual confirmation; pairing, ACKs, reconnect, and Wi-Fi coexistence still need physical checks. No Tab5 ESP-NOW support is claimed.

The protocol is described in [PROTOCOL.md](PROTOCOL.md). Tab5 integration and open feasibility gates are in [ORCSDR_ESPNOW_INTEGRATION_PLAN.md](ORCSDR_ESPNOW_INTEGRATION_PLAN.md).
