# Pluto on the CardPuter Zero

How the existing `esp-pluto-sdr` work maps onto a Linux host, and what actually
has to be written.

## What already exists

[`hardcoreerik/esp-pluto-sdr`](https://github.com/hardcoreerik/esp-pluto-sdr) is an
independent ESP-IDF host component, verified on a Tab5 (ESP32-P4 rev v1.3) against
a FISH Ball PlutoSDR clone (fw v2.3). It is a **libiio `iiod` protocol 0.26 client**
with its own USB transport, not a bespoke radio protocol.

The parts of it that matter here, all of them already proven on hardware:

| Finding | Value | Still true on Linux |
| --- | --- | --- |
| Device identity | VID `0456`, PID `b673`, USB 2.0 high speed | yes |
| IIO devices | `ad9361-phy` and `cf-ad9361-lpc` | yes |
| USB pipe control | `RESET_PIPES=0`, `OPEN_PIPE=1`, bmRequestType `0x41`, wValue = pipe, wIndex = interface | libiio does this |
| Pipe teardown | **must** send `CLOSE_PIPE` before release, or the next session hangs until `iiod` restarts | libiio does this |
| Data conversion | int16 12-bit IQ, full scale ±2048 → u8 offset-128, with integer decimation | **reusable as-is** |
| Minimum rate | AD9361 will not run below ~2.083 MS/s | yes — a hard floor |
| Wire rate | ≥8.3 MB/s at the minimum rate, 4 bytes per complex sample | yes |
| Clean ceiling | ~5 MS/s of 16-bit IQ on the ESP32-P4; the limit is the Pluto's gadget, ~21-23 MB/s | computer-side is not the limit |

## What does not transfer

The ESP component's **transport and `iiod` client are not needed on Linux.** Its
`pluto_iiod.c` exists because an ESP32 has no libiio; Debian has libiio, and the
installed build here reports `backends: local xml ip usb serial`, so the USB path
is available out of the box.

Porting the C client instead of using libiio would reimplement a library, and would
also have to rediscover the `CLOSE_PIPE` bug that the ESP work already found and
fixed. That is the opposite of reusing work.

## What must be written on the Zero

One class, `PlutoReceiver`, matching the existing `RtlReceiver` surface
(`open`, `configure`, `retune`, `set_tuner_gain_auto`, `set_tuner_gain_tenth_db`,
`read_cu8`, `close`) so every demod and dashboard keeps working unchanged:

1. `iio_create_context_from_uri` against the Pluto, or `iio_create_default_context`
   after enumeration; find `ad9361-phy` and `cf-ad9361-lpc`.
2. Control: RX LO (`altvoltage0` `frequency`), `sampling_frequency`,
   `rf_bandwidth`, gain control mode and `hardwaregain`, and the offset trackers.
3. Streaming: an RX buffer on voltage0/voltage1, `READBUF`-equivalent reads, and
   the **int16 12-bit → CU8 conversion with decimation, ported from
   `pluto_iq.c`** — the one piece of the ESP driver that is directly reusable and
   already validated on the wire.
4. Report the delivered rate, not the requested one. The ESP notes are explicit
   that above the ceiling the loss is silent: reads simply return fewer samples.

## The link budget problem, stated plainly

The AD9361 floor is ~2.083 MS/s, so the Zero must absorb at least 8.3 MB/s of USB
while also running a demod and a 320x170 framebuffer. That is a real constraint on
this board, not a formality:

- the RTL path currently runs 960 kS/s and already cannot sustain 2.4 MS/s without
  USB read failures (measured earlier this session);
- the Pluto's minimum is 2.2x that rate, with twice the bytes per sample.

So the first experiment is throughput, not features: pull raw Pluto IQ on the Zero
and measure the delivered rate against nominal. If the CM0 cannot hold ~8.3 MB/s,
the honest options are decimation on the Pluto side, or a lower-rate profile, and
the driver design must reflect that rather than pretend otherwise.

## Status

- libiio 0.26 installed on the device from Debian trixie (arm64), `usb` backend
  present, `iio_info` runs.
- No Pluto is attached at the time of writing, so nothing beyond the library has
  been exercised.
- Next: attach the Pluto, confirm `iio_info -S` enumerates it, then measure
  sustained throughput at the minimum rate before writing `PlutoReceiver`.

## Hardware verified on the Zero (2026-11-27)

The Pluto (FISH Ball PlutoSDR Rev.A, Z7020/AD9361, fw exposing `ad9361-phy` and
`cf-ad9361-lpc`) enumerates and works. Three independent paths are available:

```text
usb:1.53.4                                     native IIO over USB
ip:fishball.local (eth1, RNDIS over USB)       USB Ethernet gadget
ip:fishball.local (eth0, RJ45)                 the Zero's own 100 Mbps port
```

`iio_info -u usb:1.53.4` reports the expected device tree, and the ESP work's
finding is confirmed on this host: `sampling_frequency_available: [2083333 1
61440000]`.

### The fix was power, not software

Before an extra power connection was added to the Pluto, it did not enumerate at
all and the RTL dongle on the same hub began failing with `error -71` (EPROTO) -
the classic signature of a bus-powered hub being overdrawn. One extra supply fixed
both: the Pluto appeared as `0456:b673` and the RTL failures stopped.

### Throughput: the concern was misplaced

Measured with `iio_readdev` on this board:

| Requested | Delivered | Of nominal |
| --- | --- | --- |
| 2.4 MS/s | 9.49 MB/s | **100%** |
| 30.72 MS/s (default) | ~22.9 MB/s | 18.7% - silent loss above the ceiling |

So the earlier worry that the Zero could not carry the Pluto's 8.3 MB/s minimum
was wrong: at 2.4 MS/s it delivers the full 9.49 MB/s with roughly 2.4x headroom
to the ~23 MB/s ceiling. The RTL's failures at 2.4 MS/s earlier in this project
were an RTL/dwcs2 path problem, not a general USB limit on this board.

### Rate note

`2083333` is rejected with EINVAL even though it is the reported minimum; `2100000`
is accepted, and `2400000` is applied as `2399999`. The driver evidently enforces
divisor-derived rates rather than the raw range, so a receiver must read back what
it got instead of assuming the request took.