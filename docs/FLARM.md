# FLARM receiver

FLARM is a dedicated Home dashboard using the same renderer as ADS-B: Radar,
List, Target, Stats and Settings. It passively receives legacy AIR V6 and newer
AIR V7 position messages and detects the generation automatically. It uses the
Tab5's existing RTL-SDR; selecting another RF mode takes over that tuner.

## Use

1. Open **Home → All dashboards → FLARM**.
2. Set the receiver's nearby latitude and longitude in FLARM Settings or global
   Location settings. Location and radar range are shared with ADS-B.
3. Connect Wi-Fi so NTP can establish UTC. The receiver continues after Wi-Fi
   disconnects, using the system clock. A reboot requires a fresh time fix.
4. Radar and List populate from decoded broadcasts. Target shows address type,
   address, AIR generation, aircraft type and the receiving channel. Altitude is
   WGS84 ellipsoid height in feet; it is not pressure altitude or a flight level.

This initial frequency plan is **Europe: 868.2 and 868.4 MHz simultaneously**,
using a fixed 868.3 MHz center and 960 kS/s CU8 stream. Other regional hopping
plans are not implemented. FLARM and 1090 MHz ADS-B cannot receive concurrently
with this one tuner. The FLARM decoder is independent of the LoRa decoder.

No network traffic feed, FLARM transmission, collision warning, callsign lookup,
FAA enrichment, or persistent aircraft archive is added. Received stealth and
no-track flags remain visible in the target summary. AIR V7 message payload
versions 1–3 are recognized; other message types are rejected. This is not a
claim to support every future FAMP version.

## Receiver boundaries

- `flarm_decoder_core` is portable C++17. Two digital mixers, 33-tap low-pass
  filters and decimation feed 2FSK discriminators. Eight timing phases per
  channel search Manchester sync, recover inverted payload bytes, and check
  CCITT CRC including the NRF905 address prefix before payload decoding.
- V6 and V7 have separate decryption and coordinate reconstruction paths.
  Explicit byte/bit operations avoid packed-bitfield ABI dependencies, unaligned
  loads and signed-shift overflow. UTC attempts are bounded to the receive
  second and its immediate neighbors. V7 validates timestamp and version fields;
  V6 validates parity. These checks are not cryptographic authentication.
- `flarm_receiver` owns 64 bounded tracks in a lazily allocated PSRAM state,
  outside the boot-time internal DRAM pool. Legacy V6 targets need two
  consistent broadcasts before display because their plaintext checksum is
  only parity. Tracks expire after 30 seconds; the display selects the six
  freshest, matching the existing ADS-B display capacity.
- The DSP task alone mutates the decoder. Captured IQ carries session generation,
  sequence and UTC; old-session blocks are rejected and a gap resets acquisition.
  Track and stats snapshots use short critical sections. Rendering stays on
  the UI task. No file or network access occurs inside IQ decoding.
- UTC readiness is asserted only by a successful NTP callback or an explicit
  serial time-setting command; an arbitrary nonzero system clock is insufficient.
  Receiver position must be nearby because both protocols compress coordinates.

Serial diagnostics:

```text
RTL_UI OPEN FLARM
RTL_FLARM_START
RTL_FLARM_STOP
RTL_FLARM_STATUS
RTL_FLARM_UTC <unix_seconds>
```

Mutating commands require the existing authenticated serial session. Status is
read-only. The UTC override is useful for offline operation; provide accurate
current Unix seconds. Existing `RTL_ADSB_LOCATION <latitude> <longitude>` stores
shared receiver coordinates. The Stats page exposes UTC/location readiness,
V6/V7 counts, CRC rejects, sample rate and receiver drops.

## Validation

Run `bash tools/test-flarm-core.sh`. The optimized and ASan/UBSan runs cover:

- Independent SoftRF encrypted V6/V7 vectors, including southern coordinates,
  high latitude, negative climb, privacy flags and a key-boundary timestamp.
- Every single-bit corruption in the 26-byte packet, missing context, invalid
  length and adjacent-second decoding.
- Complete synthetic CU8 IQ on each EU channel, both polarities, ±5 kHz offset,
  sample-clock offset, odd block boundaries, simultaneous channels, noise and gaps.
- Receiver snapshots, generation reset, expiry and V6 repeat confirmation.
- The actual shared dashboard code compiled with a host drawing recorder:
  protocol-specific labels, mode-switch snapshot clearing, Settings return,
  navigation registry and stale tuner-owner rejection.

`ORCSDR_UI_OUTPUT=/existing/directory bash tools/test-flarm-core.sh` additionally
writes SVG render recordings for visual inspection. These use substitute host
font metrics and board services; they do not replace physical display acceptance.

**Review evidence as of 2026-09-16:** merged upstream `main` at `c231f46`,
preserving the current sample-rate metadata, AM/shortwave navigation, shared
header and ADS-B gain controls. Optimized and ASan/UBSan FLARM/receiver/dashboard
host tests pass, including inert FLARM channel controls and working ADS-B gain
controls. The portable core also passed a warning-clean ESP32-P4 GCC compile
on 2026-09-08; that earlier compile is not a full firmware build.

Full ESP-IDF 5.5.4 firmware build and over-the-air Tab5 acceptance remain pending.
The current Mac has no configured ESP-IDF installation. Earlier setup attempts
could not fetch all ESP-IDF submodules and Python/toolchain dependencies.

Before calling this hardware-verified, build with the pinned native toolchain,
then measure both-channel reception and IQ drops on the Tab5 against a known
receiver and timestamped live captures of each generation. Exercise Home,
Settings return, ADS-B/FLARM switching, stop/restart, USB reconnect, NTP acquisition
and operation after Wi-Fi loss. The embedded DSP budget and sensitivity remain
unmeasured; synthetic IQ does not establish real RF performance.

## Source provenance

The packet implementation was adapted from
[SoftRF Legacy.cpp](https://github.com/lyusupov/SoftRF/blob/a4c21fc45b251ab2e3b3a543ab69771e63328ef6/software/firmware/source/SoftRF/src/protocol/radio/Legacy.cpp)
and its accompanying `Legacy.h`, with RF framing checked against
[`almic.cpp`](https://github.com/lyusupov/SoftRF/blob/a4c21fc45b251ab2e3b3a543ab69771e63328ef6/software/firmware/source/SoftRF/src/driver/radio/almic.cpp).
The reference revision is pinned; copyright and GPL-3.0-or-later notices remain
in the adapted source. `tools/generate-flarm-fixtures.py` rebuilds the fixtures
using that upstream encoder in a temporary host program. Transmit code is never
compiled into the application.

FLARM also publishes [FTD-116 FAMP](https://www.flarm.com/en/integration/flarm-famp-public-protocol/).
That document has separate terms. This change uses the GPL SoftRF reference,
not copied code or test data from FTD-116. OrcSDR's commercial licensing offer
does not relicense these third-party GPL contributions; see `LICENSING.md`.
