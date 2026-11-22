# OrcSDR Cardputer Zero / Linux bring-up (experimental)

This independent target is the first Linux receiver experiment for OrcSDR, **not** a port of the ESP32-P4 firmware. It does not require ESP-IDF or M5GFX. It began as a headless, receive-only RTL-SDR diagnostic with synthetic IQ tests and unsigned 8-bit IQ-file analysis, and now also runs a native 320x170 graphical receiver on the device: FM/NFM, FT8/FT4, P25 Phase I, LoRa, ACARS, ADS-B, weather/marine, POCSAG, SSTV, RF Lab, WiFi survey and Dead Air, each with ALSA audio and evdev keyboard input.

Firmware-level claims in this file are qualified by what was actually observed. Where a screen says "verified", it means the named input was applied on a real Zero and the named output was captured or logged; it does not mean every mode was received off the air. Live decodes that never happened are called out as such in each section.

## Native Linux build / test

Requires CMake 3.16+, a C++17 compiler, and optionally pkg-config and the platform's librtlsdr-dev package. Without librtlsdr the offline program builds, but receiver list/capture commands fail clearly.

    cmake -S apps/orcsdr-cardputer-zero -B build/cardputer-zero-native -DCMAKE_BUILD_TYPE=Release
    cmake --build build/cardputer-zero-native --parallel
    ctest --test-dir build/cardputer-zero-native --output-on-failure
    ./build/cardputer-zero-native/orcsdr_cardputer_zero --demo
    ./build/cardputer-zero-native/orcsdr_cardputer_zero --help

Run from the repository root. Save diagnostics to a log in the working directory:

    ./build/cardputer-zero-native/orcsdr_cardputer_zero --demo 2>&1 | tee cardputer-zero-bringup.log

Analyze a saved unsigned 8-bit interleaved I/Q file (I0,Q0,I1,Q1,...), not a WAV, float32 or signed IQ file:

    ./build/cardputer-zero-native/orcsdr_cardputer_zero --iq-file /path/to/capture.u8 --samples 32768

## First physical device smoke test (NOT performed)

1. Install the official Cardputer Zero Linux OS using the manufacturer's imager. Back up existing microSD first. Record outputs of uname -m, cat /etc/os-release, free -h and lsusb.
2. Set the Cardputer Zero USB selector to **Host**. In Slave mode the hub, USB-A and Ethernet are disconnected. Connect the RTL-SDR to USB-A; use a powered hub if power is uncertain. Check physical USB enumeration and udev permissions.
3. If available on the target OS install cmake, g++, pkg-config and librtlsdr-dev. Build natively as above. Confirm CMake says "system librtlsdr enabled".
4. Run the receiver enumeration command and confirm the tuner:

       ./build/cardputer-zero-native/orcsdr_cardputer_zero --list

5. With a suitable antenna and an allowed test signal, take a bounded receiver sample, comparing against a known-good Linux host:

       ./build/cardputer-zero-native/orcsdr_cardputer_zero --capture --freq 100100000 --rate 1024000 --samples 32768

6. Record dongle type, USB power, sample rate, frequency, CPU and memory usage and receiver errors. Continuous real-time streaming, drop-accounting, audio, keyboard and display remain separate acceptance gates.

The 64-bin DFT and digital mean power are rough uncalibrated diagnostics, **not dBm or production spectrum**. Physical frequency coverage depends on the tuner and Linux driver, not CLI numeric limits. This target uses Linux **librtlsdr**, not the ESP-IDF esp-rtl-sdr host transport.

## Official SDK and display plan

Use [CardputerZero/Template](https://github.com/CardputerZero/Template) for LVGL, CMake, SDL desktop preview, Linux input/display and ARM64 build guidance, and [CardputerZero/AppBuilder](https://github.com/CardputerZero/AppBuilder) for packaging. Its cross-toolchain references an upstream CM0 BSP sysroot release (v0.0.4 in the checked template). Before importing any template code, pin the exact upstream commit, preserve licensing, build desktop simulation and test an ARM64 Debian package.

Proposed 320x170 screen: tuned frequency, receiver connection status, one S-meter row and a focused spectrum/waterfall, with keyboard actions for tune, gain, band, save and exit. No attempt to squeeze the Tab5 1280x720 touchscreen UI directly onto this device.

Details: [research and bring-up](../../../docs/hardware/cardputer-zero/RESEARCH_AND_BRINGUP.md) and [portability architecture](../../../docs/hardware/cardputer-zero/PORTABILITY_ARCHITECTURE.md).

## Native OrcSDR decoder integration (2026-11-06)

The Cardputer Linux target now compiles **the original OrcSDR source files** `acars_core.cpp`, `acars_selftest.cpp` and `adsb_decoder.cpp` from `apps/orcsdr-tab5/ui` directly. No copied decoder fork and no ESP-IDF firmware modifications. The host CTest suite covers ten ACARS simulated RF blocks, CRC/repairs, chunked replay, stream reset, ADS-B known frames/CRC and bounded CU8 queue/drop accounting. The application now runs on the device and tunes the shared frontends, but a live off-air ACARS message or ADS-B frame has not been decoded there; the ACARS and ADS-B dashboard sections state exactly what a device run did and did not produce.

Additional CLI modes:

```sh
./build/cardputer-zero-native/orcsdr_cardputer_zero --acars-demo
./build/cardputer-zero-native/orcsdr_cardputer_zero --decode-acars /path/to/acars-240k.cu8 --freq 131490000
./build/cardputer-zero-native/orcsdr_cardputer_zero --decode-adsb /path/to/adsb-2048k.cu8
```

The ACARS replay file must contain CU8 at 240 kS/s, wanted carrier at +60 kHz (example actual channel 131.550 MHz). ADS-B replay requires CU8 at 2.048 MS/s tuned near 1090 MHz. These are genuine decoders, not synthetic text injectors, but still offline file modes; live streaming into a decoder and Web server remain future gates.

See [decoder/Linux acceptance and reproducible log commands](../../../docs/hardware/cardputer-zero/ORCDEC_LINUX_ACCEPTANCE_20261106.md) and the [actual OrcSDR Web interoperability plan](../../../docs/hardware/cardputer-zero/LINUX_WEB_INTEROPERABILITY_PLAN.md).

## Capture-to-replay workflow

The CLI can now save a receiver capture as raw CU8 for later decoding or independent inspection. Saving requires real RTL-SDR hardware; an RF capture cannot be verified in GitHub CI.

```sh
./build/cardputer-zero-native/orcsdr_cardputer_zero --capture --freq 131490000 --rate 240000 --samples 262144 --save acars-240k.cu8
./build/cardputer-zero-native/orcsdr_cardputer_zero --decode-acars acars-240k.cu8 --freq 131490000
./build/cardputer-zero-native/orcsdr_cardputer_zero --make-acars-fixture demo-acars.cu8
./build/cardputer-zero-native/orcsdr_cardputer_zero --decode-acars demo-acars.cu8 --freq 131490000
```

Driver support for 240 kS/s **must be verified on actual hardware**; a capture taken at another rate must not be mislabeled as 240 kS/s. The automated CTest fixture/replay pair proves the generator writes a real CU8 fixture which the same original OrcSDR ACARS decoder independently reads back and decodes. No device or Web server is needed for these offline tests.


## Cross-platform integration checkpoint — November 2026

The existing Cardputer Linux project now compiles the actual OrcSDR ACARS and ADS-B implementations, includes the canonical-core source audit and Radio Service v1 specification, and has a bounded CU8 receiver-to-decoder pipeline. Original Web/WASM files can be served unchanged from a caller-supplied build directory on localhost only. See [the integration checkpoint](../../../docs/hardware/cardputer-zero/INTEGRATION_CHECKPOINT_20261106.md).

From the repo root, configure/build/test and keep logs next to the command invocation:

    cmake -S apps/orcsdr-cardputer-zero -B build/cardputer-zero -DCMAKE_BUILD_TYPE=Release 2>&1 | tee cardputer-zero-config.log
    cmake --build build/cardputer-zero --parallel 2>&1 | tee cardputer-zero-build.log
    ctest --test-dir build/cardputer-zero --output-on-failure 2>&1 | tee cardputer-zero-tests.log

Hardware-dependent examples (NOT physically verified on Cardputer):

    ./build/cardputer-zero/orcsdr_cardputer_zero --live-acars --blocks 64 2>&1 | tee cardputer-zero-acars-live.log
    ./build/cardputer-zero/orcsdr_cardputer_zero --live-adsb --blocks 128 2>&1 | tee cardputer-zero-adsb-live.log

To serve original gzipped Web assets for LOCAL preview (radio APIs currently return 501, all mutations are refused):

    ./build/cardputer-zero/orcsdr_cardputer_zero --serve-web ../OrcSDR-Headless/main/m5 --port 8780 2>&1 | tee cardputer-zero-web.log
    curl -i http://127.0.0.1:8780/api/v1/health

The asset preview is NOT the full web radio service. It is always bound to 127.0.0.1 and must not be reconfigured for remote use without authenticated, encrypted access. No private Headless artifacts are included in this public repo.

## Native graphical FM receiver (experimental, physical acceptance pending)

Build the new `orcsdr_cardputer_zero_gui` executable alongside the existing
`orcsdr_cardputer_zero` command line program. The native graphic path uses
one RTL-SDR owner, the original FM/FFT source-pinned from OrcSDR-Headless,
ALSA 48 kHz PCM, a native 320 x 170 RGB565 renderer, SDL2 desktop simulation,
Linux fbdev and evdev keyboard input. There is no browser engine on the LCD.

```bash
bash apps/orcsdr-cardputer-zero/tools/bootstrap_gui_audio_dev.sh
cmake -S apps/orcsdr-cardputer-zero -B build/cardputer-zero -DCMAKE_BUILD_TYPE=Release -DORC_ENABLE_GUI=ON
cmake --build build/cardputer-zero --parallel 1
ctest --test-dir build/cardputer-zero --output-on-failure
./build/cardputer-zero/orcsdr_cardputer_zero_gui --demo --screenshot demo.ppm
./build/cardputer-zero/orcsdr_cardputer_zero_gui --display fb --device 0 --audio default
```

The demo generates explicitly labeled synthetic visuals and emits **no**
unlabelled radio reception. The GUI has since been exercised on a Zero: real
framebuffer, the internal `tca8418c` keyboard, ALSA speaker routing, USB
reconnect and controlled exit have all been driven on hardware. Read
[the architecture and device handoff](../../../docs/hardware/cardputer-zero/UI_AUDIO_CONTROLS_IMPLEMENTATION.md).

## Capture the running LCD over SSH

With OrcSDR already running, capture the visible Linux framebuffer without
restarting the receiver or interrupting audio:

```sh
python3 ~/OrcSDR-zero-ui/apps/orcsdr-cardputer-zero/tools/capture_screen.py
# Or choose a new output filename:
python3 ~/OrcSDR-zero-ui/apps/orcsdr-cardputer-zero/tools/capture_screen.py ~/Pictures/orcsdr-live.png
```

The command prints the saved PNG path; the default is a timestamped file in
`~/Pictures`. Copy that path to your computer with `scp`. Existing output files
are never overwritten. It captures whatever is currently on the LCD, including
the launcher if OrcSDR is closed. Reading the framebuffer requires video-device
access. Python 3 and Pillow (`python3-pil`, included in the development bootstrap)
are required. RGB565 and 32-bit framebuffers, row stride, and panning offsets are
supported. This is a best-effort live read, not a synchronized renderer snapshot;
a rapidly changing frame can contain parts of consecutive redraws.

Run the focused pixel-decoding tests with:

```sh
python3 apps/orcsdr-cardputer-zero/tests/test_capture_screen.py
```

### Compact FT8 band controls

On the FT8 RX dashboard, Left/Right selects the previous/next band using the
shared OrcSDR FT8 presets (160 m through 2 m). The header displays the requested
dial and TUNING until the receive worker applies it, then RX. Changing bands
resets capture continuity and discards a partial UTC slot. Below 24 MHz the Zero
uses direct-Q sampling; higher bands use the normal tuner. A suitable receiver
and antenna are required. A completed slot with no messages does not establish
live decoding or RF sensitivity.

Device check: scripted evdev Left/Right input reached 50.313 MHz, returned to
14.074 MHz, and completed a UTC slot. No live messages were decoded in that run;
librtlsdr printed PLL warnings during tuner transitions. Physical key feel and
LCD readability remain separate checks. Home retains the supplied Orc badge
beside the frequency without reducing the spectrum/waterfall area.

### Compact P25 receive page

P25 RADIO is available in the Space selector or via `--dashboard p25`.
F opens MHz entry (24–1766 MHz); Left/Right tunes; T cycles 12.5 kHz,
100 kHz, 1 MHz, and 5 MHz steps. G cycles Auto/C4FM/CQPSK. S/D adjusts
volume; M mutes. The page shows decoded NAC, system/site identity, and the
current decoded grant. The compact receiver now supports Phase I grant following as described below.
Phase II voice is not integrated.

The Linux build compiles the existing P25 decoder core and Phase I IMBE
voice decoder with the repository's bundled mbelib sources and notices intact.
Only frames with valid, clear encryption metadata reach the vocoder. PCM is
bounded per input block; unknown/encrypted frames are dropped. Decoder and
vocoder history reset on tuning or modulation changes. Ordinary screen updates
preserve static branding and do not perform a full-screen clear.

Verification: `ctest --test-dir build/zero -R zero_p25_decoder_ui --output-on-failure`
passed on the Zero, including shared protocol/FEC and voice self-checks,
frequency entry limits, modulation cycling, and bounded redraw. Scripted evdev
input applied 460.125 MHz/C4FM and captured 13,733,888 samples. The run had no
P25 lock, decoded grant, or voice PCM; live RF/voice reception remains unverified.
The RTL driver emitted a PLL warning at initial tune. Frequency readback and
sample capture alone do not establish successful RF demodulation.

### FT8 UTC clock

On FT8, T opens the UTC clock panel. R requests a fresh network sync; F opens
manual UTC entry in `YYYYMMDDHHMMSS` format. Enter applies the value and disables
NTP for offline operation. Escape cancels entry or returns to FT8. Network sync
turns NTP back on. The main FT8 page displays UTC, NTP sync state, and seconds
until the next 15-second slot. UTC is independent of the device display timezone.
Clock changes discard incomplete slots; UTC from before and after a clock jump
is never combined into one decoded slot. Manual input has one-second precision;
use NTP for accurate network time. A synchronized system clock alone does not
prove live FT8 reception.

The installed `/usr/local/libexec/orcsdr-clock` helper also supports SSH:

```
/usr/local/libexec/orcsdr-clock --status
sudo -n /usr/local/libexec/orcsdr-clock --sync
sudo -n /usr/local/libexec/orcsdr-clock --set 20261119013000
```

The last command sets that exact example UTC time; use the actual current UTC.
The helper is root-owned, runs isolated Python, validates dates before mutation,
and accepts only status, sync, set, and validation operations. The device's
`/etc/sudoers.d/orcsdr-clock` permits hardputer to invoke only its sync/set paths.
OrcSDR itself runs without root. `--sync` uses the installed systemd-timesyncd
service. Other Linux time services require a matching adapter.

Verified on the Zero: valid manual set, NTP-off status without the daemon,
network sync with a fresh synchronization marker, keyboard panel navigation,
manual-entry cancellation, and return to FT8. Tests reject malformed dates and
shell input, and verify both directions of a UTC jump discard partial slots.

### Audio startup recovery

A transient ALSA busy-device error no longer leaves a receiver permanently
silent. FM and audio dashboards retry a closed output at most once per second.
FM logs failure/recovery and Home shows NO AUDIO when output is unavailable.
A device test held the speaker busy with aplay, observed FM's open failure,
released it, and verified automatic recovery to a RUNNING PCM stream owned by
FM's audio thread. Speaker/station sound was also confirmed by the user after
manual recovery of the earlier running build.

### FT8 waterfall and world map

The default FT8 receive view shows a live USB-audio waterfall from 0 to 3 kHz,
using the exact 12 kHz PCM supplied to the decoder. The shared 1920-point FFT
with a Hann display window is averaged in pairs: each waterfall row spans
320 ms, with 68 rows (21.76 seconds) visible. Decoder audio is unchanged. P selects
the existing waterfall palette. Two message rows remain below the waterfall;
Up/Down scrolls the current slot's visible messages.

M toggles an offline world map. It uses the same Natural Earth world outline,
whole-world projection, Maidenhead conversion, and bounded 64-decode store as
OrcSDR's main build. Yellow markers are decoded grid-square centres, not precise
station locations. Messages without a valid grid are retained in the decode
list but do not create a marker. The map shows an explicit empty state until
valid locators arrive; it never invents station locations. It is a coarse
outline map, with no online tiles or receiver-position/path overlay yet.
T opens the UTC controls from either view.

The visual model uses 106,832 bytes and the map cache/store 64,024 bytes on the
Zero. The basemap is cached; ordinary updates replace bounded waterfall,
message, map-marker, or UTC regions without clearing the whole screen.
Focused tests verify a 1 kHz input appears at the right horizontal position,
palette changes, valid/invalid grid placement, and no full-screen clears.
The user confirmed live FT8 decoding, also recorded in a device framebuffer
capture before installing this view. Live waterfall changes and map/clock
navigation were verified with scripted input on the device.

The compact screens use OrcSDR branding without the Oregon Radio Company
subtitle. FT8 map view uses a one-line header and a 240 x 120 map with a recent-contact sidebar and latest-message strip; the receive
view uses a 320 x 68 waterfall. UTC and navigation retain their bottom rows.
Natural Earth attribution remains in the map footer.

The FT8 map preserves a 2:1 geographic aspect ratio (240 x 120). Its sidebar
shows up to five unique recently heard callsigns with grids when known; the
latest decoded message occupies the full-width strip below the map.

Last heard retains five distinct callsigns for the current FT8 session with no
age timeout. A repeat refreshes that station and preserves its last valid grid;
only a new distinct station arriving when all five positions are occupied
replaces the least recently heard station. The header mascot is a tightly
framed 28 x 28 RGB565 face embedded in the executable. Map geometry remains 2:1
(216 x 108) beneath the taller icon header.

Integration checkpoint: The Satellite receive workspace is now in the selector. The LoRa/Mesh page is now
connected to the Linux native decoder as described below. The P25 page
supports Phase I reception and grant following; Phase II voice remains unavailable. Other compact dashboards have the limitations documented above; this
checkpoint does not claim full parity with the main Tab5 application.

### Automatic USB receiver recovery

The receiver retries failed USB open/read operations once per second while the
app remains open. Starting without the RTL-SDR and plugging it in later starts
the stream automatically. Removing/reconnecting it restarts capture with a new
stream generation; stale IQ and partial decoder slots are discarded. FT8's
session map/last-heard history is retained. The selected RTL device index still
applies; stable serial-number selection with multiple dongles is not provided.

Device verification disabled/re-enabled only the identified 0bda:2838 USB
interface. FM and FT8 both passed absent startup, initial connection, interrupted
stream, and automatic reconnection without pressing R. This is logical USB
removal testing; cable/power faults on other receivers remain a physical gate.
The dashboard selector uses Up/Down only; Left/Right no longer changes its row.
Scripted evdev input and framebuffer comparisons verified all four directions.

### Compact LoRa/Mesh receiver

Open LORA / MESH from Space or launch with `--dashboard lora`. F enters MHz,
T cycles the region plan, Left/Right selects the channel slot within it, G cycles
SF7 through SF12, Enter starts a manual diagnostic capture, and Up/Down scrolls
packet history. Bandwidth is currently fixed at 250 kHz. The region and slot are
restored from the saved settings on open; a first run with no saved plan starts on
US slot 20 at 906.875 MHz / SF11.

The existing shared LoRa preamble detector runs on 960 kS/s IQ. Automatic capture
retains 250 ms of pre-roll, ends after quiet or at a four-second cap, and passes
owned IQ to the existing native decoder. The receiver pauses while decoding,
then flushes buffered IQ and resumes preamble detection. A capture reserves up
to 7.68 MB plus a 480 kB pre-roll buffer and native decoder workspace. Manual
captures without a signal are bounded to approximately half a second. No
private channel key is read; packet summaries use the shared decoder's existing
public/plaintext handling. Changing region or slot rewrites the settings file, so
the selected plan survives a restart. Private-key setup, uninterrupted reception
during decode, and browsing history beyond the most recent packets remain unported.

Each decoded packet is appended as one tab-separated `header<TAB>text` record to
`$XDG_DATA_HOME/orcsdr/lora/packets.tsv` or `~/.local/share/orcsdr/lora/packets.tsv`,
created with mode 0600 and fsynced before the receiver resumes. The file is
append-only: existing records are never rewritten or pruned, so the user manages
disk growth. On open the last 16 packets (32 summary lines) are reloaded newest
first; older records stay on disk but are not shown. Control characters in a
packet payload are replaced with `?` so a record cannot break the line format.

The page keeps 32 packet-summary lines in RAM and reports capture count, CRC
results, preamble windows, and decoder duration. Static chrome is retained on
ordinary updates. Device testing tuned 915.125 MHz, changed SF11 to SF12, and
processed two 963,328-byte manual captures in 143/168 ms with zero CRC-valid
packets. That proves the capture/decode path ran, not live RF packet support.
The shared native self-check and compact-control/history/redraw tests passed.

Persistence verification (2026-11-21, Zero with an RTL2838/R820T receiver, Release
build with librtlsdr and ALSA enabled): with region 4 slot 3 saved and two packet
records on disk, LORA / MESH opened on JP 3/12 at 921.125 MHz and listed both
records newest first. A scripted T advanced the plan to ANZ and wrote
`lora_region=5 lora_slot=20`; a following Right wrote `lora_slot=21`, and the LCD
read ANZ 21/52 920.125 MHz. That covers plan save/restore and history reload. The
append path is covered by `zero_lora_packet_history` only: no live LoRa packet was
decoded, so reception during a real capture, and the cost of the per-packet fsync
inside the capture loop, remain unverified.

### Bias Tee antenna power

Open Settings in the Space selector and press B to request Bias Tee ON/OFF.
The default is OFF. The preference is saved with the other user settings on
clean exit, applies to all radio pages, and is reapplied after USB reconnect.
`--bias-tee on` or `--bias-tee off` explicitly overrides the saved preference
for a launch. Settings shows the requested value and WAIT RX, PENDING, APPLIED,
or DRIVER ERROR. APPLIED means librtlsdr accepted the command; it is not a
voltage measurement or proof the dongle has a wired Bias Tee. The transport
sends OFF before closing the device. Abrupt power loss/process termination
cannot guarantee an orderly OFF command. Use a compatible receiver and antenna
system: enabling Bias Tee places DC power on the antenna connector.

Fake-driver tests cover default OFF, ON/OFF requests, one call per change,
reapplication on reconnect, OFF on close, and command failures. Settings tests
cover persistence, legacy defaults, and invalid values. On-device tests use OFF;
antenna-port voltage and actual powered-LNA behavior are not verified.

### Satellite receive workspace

SATELLITE in Space, or `--dashboard satellite`, matches the main application's
general NFM browse workspace at 137.5 MHz. It is not an APT/image decoder or
orbital/pass tracker. F enters MHz; Left/Right tunes; T changes tuning step;
S/D changes volume; M mutes. Live spectrum and waterfall use the same portable
FFT as FM, with a 300 kS/s capture and a narrow 16 kHz channel indication.
The shared NFM path supplies 48 kHz speaker PCM. USB and audio recovery apply.

Device verification for the Satellite/Bias Tee addition: Settings ON/OFF UI
was exercised in demo mode without a receiver. The connected driver's OFF
command returned success and Settings showed OFF APPLIED. Satellite captured
live IQ at 137.5 MHz/300 kS/s, produced changing waterfall pixels, opened a
RUNNING speaker PCM stream owned by its audio worker, and accepted 145.800 MHz
entry. This is transport/render/PCM evidence, not reception of a satellite or
a measurement of antenna-port DC voltage. Actual ON output was not energized.

### ACARS channel tuning

F opens ACARS channel entry in MHz. Enter applies a valid 118–137 MHz channel
on the existing 25 kHz grid; Escape cancels. Left/Right or Z/C steps 25 kHz.
Up/Down and Enter retain message selection/detail behavior outside entry.
The requested channel maps to a receiver centre 60 kHz below it for the shared
240 kS/s frontend. Retuning flushes USB IQ, increments the capture generation,
and resets the channel frontend/decoder; the bounded message history remains.
The selected channel also survives the worker's automatic USB reconnect.

`zero_acars_channel_tuning` verifies entry, rejection of out-of-range/off-grid
values, clamping, stepping, and the RF offset. Device input applied 131.725 MHz,
stepped to 131.750 MHz and back, with logs confirming 131.665/131.690 MHz RF
centres. No live ACARS messages were decoded during that bounded run. Regional
channel menus, aircraft/activity views, and archive integration remain gaps.

### P25 Phase I follow and hold

On P25, F sets the control frequency; R toggles automatic Phase I follow and H
holds the first accepted talkgroup until hold is disabled or the control
frequency changes. Fresh clear FDMA grants can retune the receiver to the voice
carrier. The shared decoder and vocoder reset on each RF change. The receiver
returns to control after 3 seconds without voice acquisition or 1.8 seconds
without further voice frames, matching the main application's follow timeouts.
An encrypted voice indication returns to control and skips that talkgroup for
30 seconds. TDMA grants are not followed for voice; Phase II voice is still
unavailable. Manual frequency/modulation controls and volume/mute remain.

The portable state machine adapts the Phase I decisions in Tab5 main.cpp's
service_p25_follow, using the existing decoder's grant and encryption metadata.
Its test covers stale/encrypted/TDMA grants, hold selection, acquire/hang
returns, encryption cooldown, disable, changing control frequency and uint32
clock wrap. Existing P25 core/vocoder/UI tests passed too. Device testing
confirmed R/H controls, display changes and control retuning to 460.125 MHz.
No live P25 lock, grant, or followed call was received in that run. Profiles,
control-channel scanning, aliases, and the main multi-view UI remain gaps.

### Dead Air durable event journal

Opening DEAD AIR starts a dedicated storage worker and an exclusive session
JSONL file under `$XDG_DATA_HOME/orcsdr/dead-air` or
`~/.local/share/orcsdr/dead-air`. The page's RF events and Enter marks use the
main application's existing event queue, versioned serializer and durable
journal logic. Each file is created with mode 0600; existing files are never
overwritten. Saved counts include only flushed records. The page displays
storage errors and queue drops. Normal close drains the queue and writes the
session-end counts. Abrupt shutdown can leave an incomplete final record; no
claim of durability is made for records not confirmed saved.

Observation and manual-event times use the system's UTC when plausible; an
unknown clock remains null in the shared format. The receive/render paths
never perform disk writes. Existing logs are retained without automatic pruning;
users must manage disk space. This adds event evidence, not the main build's
audio clips, microphone, environmental sensors, sweep/review controls or
on-device archive browser.

The focused storage test verifies a durable UTC mark, graceful finish, failure
reporting, bounded redraw and preservation of a blocking existing file. Device
testing at 96.1 MHz recorded an Enter mark, parsed the JSONL, checked mode 0600,
and confirmed session_end saved=1/lost=0/producer_dropped=0 after clean exit.

### FT4 on the FT8 dashboard

G switches the receive mode between FT8 and FT4. Band selection uses the main
application's shared FT8/FT4 dial table, including 7.0475 MHz for FT4 on 40 m.
The exact dial is shown to four decimal places. UTC slot assembly uses 15-second
periods for FT8 and 7.5 seconds for FT4. Switching modes clears the partial slot,
resets the audio tap/backend and USB capture generation, and updates the header
and countdown. FT8 remains the startup default. Existing map/last-heard session
history is retained; decoded records carry the backend's actual mode. G remains
mode selection only on this dashboard; T still opens UTC controls and M toggles
the map. JS8 modes remain unavailable because the shared backend does not
advertise them as supported.

Local and Zero tests passed FT4 slot boundaries/gaps, original FT8 discontinuity
behavior, shared mode-specific band presets, and the native FT4 receiver. A
test-only synthetic IQ message decoded as CQ K1ABC FN42 through the full
CU8-to-USB-audio/slot/native-decoder path, with mode and grid asserted. It was
never transmitted. Device input selected FT4/7.0475 MHz, completed a UTC-aligned
7.5-second slot, opened the map, and returned to FT8/7.074 MHz. That live RF slot
contained zero decoded messages; live over-the-air FT4 decoding remains
unverified. These results do not replace RF antenna/band acceptance.

## Screen conventions

### Key hints

Every compact footer states the available keys as `KEY action` pairs, with the
key cap in amber and its action in grey:

```
Z/C BAND   G MODE   M MAP   T UTC   SPACE DASH
```

`CompactRenderer::keys()` draws the pair list and returns the next x, so a row
that must also carry an attribution (the FT8 map's Natural Earth credit) can
print it in muted grey first. Actions are spelled out rather than implied, and
`SPACE` is labelled with what it does — it opens the dashboard selector.

### Display ownership, and the launcher's status bar

OrcSDR renders into `/dev/fb0` directly. The vendor launcher
(`M5CardputerZero-APPLaunch`, `APPLaunch.service`) keeps `/dev/fb0` open too and
redraws its home screen and status bar over whatever is on the panel. Measured on
a Zero, the launcher rewrites the top rows about 1.8 times per second and OrcSDR's
next paint restores them roughly 80 ms later, so the visible effect is a flicker
of the launcher's clock and battery figure rather than a stuck overlay. Reading
`/dev/fb0` alone never shows the artifact: a capture almost always lands on an
OrcSDR frame, which is why this is worth measuring rather than screenshotting.

Stopping the launcher removes the flicker and leaves OrcSDR the only writer:

```sh
systemctl --user stop APPLaunch.service     # start it again to get the home screen back
```

That is only safe when OrcSDR was *not* started from the launcher, because a
launcher-spawned child sits in the same service cgroup and stopping the unit kills
it.

`tools/zero-run.sh` avoids that hazard. It freezes the launcher with `SIGSTOP`
for the lifetime of the GUI and resumes it with `SIGCONT` on exit: a stopped
process does not take its children with it, so this is safe however the app was
started, and the launcher redraws itself when OrcSDR closes.

```sh
apps/orcsdr-cardputer-zero/tools/zero-run.sh --display fb --dashboard ft8 \
  --input /dev/input/cardputer-zero-internal --audio plughw:ES8389Audio,0
```

Measured on a Zero: 266 top-row changes in 150s with the launcher running, 0 in
90s with it stopped, and 0 in 60s with it frozen by this script while FT8 kept
decoding. If the script itself is killed with `SIGKILL` the launcher stays frozen;
`kill -CONT $(pgrep -f M5CardputerZero-APPLaunch)` or
`systemctl --user restart APPLaunch.service` recovers it.

### Keyboard capabilities

The internal keyboard is the `tca8418c` controller at
`/dev/input/cardputer-zero-internal`. It reports the four arrow codes and
`KEY_HOME`. It does **not** report `KEY_FN`.

Fn is handled **inside the keyboard controller**, not in software: with Fn
latched, `F`/`X`/`Z`/`C` emit the arrow codes; with Fn off they emit the letter
codes. The application never sees a modifier and cannot tell whether Fn is on, so
navigation must not depend on it. The compact UI maps the four keys directly, the
way their legends read:

| Key | Action with Fn off |
| --- | --- |
| `F` | Up |
| `Z` | Left |
| `X` | Down |
| `C` | Right |

Frequency entry moved from `F` to `V` because `F` is now Up; every footer shows
`V FREQ`. The desktop SDL bindings follow the same mapping.

### Screen timeout

The launcher blanks and locks the panel after `dark_time` seconds of idle, which
also hides a running OrcSDR. Set `"dark_time": 0` in
`~/.config/cardputerzero/config.json` to keep the panel on during bring-up, then
restart the service. The setting is persistent, and the screen-off path is worth
disabling on a device under test regardless: the one unclean reset observed on
this unit was preceded by a burst of backlight writes from that path.
