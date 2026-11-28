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
## Verified capability matrix (2026-11-27)

Both directions exercised from the Zero over `usb:1.53.4` with libiio 0.26.

| Capability | Reported range | Notes |
| --- | --- | --- |
| RX LO | 70 MHz - 6 GHz | what libiio accepts |
| TX LO | 46.875 MHz - 6 GHz | |
| RX sample rate | 2.083 - 61.44 MS/s | `2083333` refused with EINVAL; `2100000` accepted |
| RX gain | -1 to +73 dB | `hardwaregain_available: [-1 1 73]` |
| RX RF bandwidth | 200 kHz - 56 MHz | |
| TX attenuation | 0 to -89.75 dB | default was 24 dB |
| TX DMA | `cf-ad9361-dds-core-lpc`, 4x S16 output channels | voltage0/1 = TX1 I/Q |

### Both directions confirmed working

- **RX**: 9.49 MB/s delivered at 2.4 MS/s (100% of nominal), ~22.9 MB/s ceiling.
- **TX**: `iio_writedev` accepted a 960,000-byte buffer to
  `cf-ad9361-dds-core-lpc` and exited 0, with TX_LO at 100 MHz and attenuation at
  -89.75 dB. The DMA path works; nothing was radiated at that attenuation.

### Caveat on the frequency range

`frequency_available` reports **70 MHz - 6 GHz**, but that is the AD9361 range the
driver advertises. The Pluto carries an **AD9363**, whose practical range is about
**325 MHz - 3.8 GHz**. Tuning outside that will be accepted and read back happily
while giving poor or no sensitivity, so "supported within its range" should mean
the chip's range, not the advertised one, and the UI must not imply otherwise.

That distinction needs an empirical check before it is stated as fact in the app:
tune across the band and compare noise floor / a known signal at the edges.

### Safety default for TX

Attenuation defaults to 24 dB on this unit, which is not the safest starting
point. A transmitter should come up at **-89.75 dB** (effectively off) and be
raised explicitly, so that merely selecting the Pluto as a source cannot key a
signal into whatever is connected.
## Frequency sweep, 2026-11-27 (gcv 40 dB, 300 ms per step)

Measured with `tools/pluto_sweep.cpp`, which sweeps the LO at fixed manual gain and
records the CU8 noise/signal floor:

| Requested | Tuned | CU8 rms |
| --- | --- | --- |
| 70 MHz | 70 MHz | 7.31 |
| 90 MHz | 90 MHz | 17.46 |
| **100 MHz** | 100 MHz | **96.29** |
| 150 MHz | 150 MHz | 20.77 |
| 250 MHz | 250 MHz | 4.55 |
| 325 MHz | 325 MHz | 0.69 |
| 500 MHz | 500 MHz | 3.55 |
| 900 MHz - 4 GHz | within 2 Hz | 0.58 - 0.62 (flat) |

Three findings, one of them a correction:

1. **My ~325 MHz lower edge was wrong.** The AD9363 datasheet says 325 MHz, but this
   unit clearly receives at 100 MHz (rms 96.3, almost certainly FM broadcast) and
   at 150 MHz. The clone's front end evidently extends below the datasheet figure,
   so the honest lower bound is at or below 70 MHz until a calibrated source says
   otherwise.
2. **Below 70 MHz the driver refuses the write**, which is the advertised range
   enforcing itself at the bottom. That is a real floor, not a measurement gap.
3. **Above ~900 MHz the floor is flat at ~0.6** with no signals present. That does
   not prove sensitivity or deafness either way - it means the band is quiet here,
   and distinguishing working from deaf at UHF needs a known source.

## A design limit the sweep exposed

**`uint32_t` cannot represent the Pluto's advertised range.** 6 GHz overflows it, so
`PlutoReceiver` inherits RTL's 32-bit frequency interface and can only address up to
4.29 GHz. The sweep had to stop at 4.0 GHz for that reason, not because the radio
did.

This is fine for this unit (whatever its real top edge is, it is below 4.29 GHz), but
it is a latent trap: any future radio with a genuine 6 GHz range - an AD9361, an
ADRV9002, a real Pluto+ - cannot be expressed through this interface at all, and the
failure would be a silently truncated frequency rather than an error. Either the
interface moves to 64-bit Hz or the range must be clamped with an explicit check.
## Reaching the Pluto's own Linux (verified 2026-11-27)

The Pluto is a complete Debian 13 (trixie) host on a Zynq-7020: kernel 6.12,
`armv7l`, 1 GB RAM, 15 GB disk with 14.6 GB free, 2 cores, `iiod` as a systemd
unit. Treating it as "just a radio" caused two wrong conclusions this session, so
the access facts are recorded here.

```text
ssh root@192.168.2.1           password: analog
bind to the USB gadget:        ssh -b 192.168.2.3 root@192.168.2.1
```

**SSH listens only on the USB (RNDIS) interface.** Port 22 times out over the RJ45,
which earlier made it look as if there were no shell at all. Both paths reach the
same host, but the RJ45 does not serve SSH.

`eth1` loses its address whenever the gadget re-binds - which `fishball-usb-bind`
does on every `iiod` restart. So after any Pluto restart the host needs its address
restored before SSH works again:

```text
sudo ip addr add 192.168.2.3/24 dev eth1
```

### Internet for the Pluto, via the Zero

The Pluto has no network of its own; the Zero does (WiFi). Forwarding through the
Zero gives the Pluto apt access, which is what makes installing a reference decoder
on that side possible at all:

```text
# on the Zero
sudo sysctl -w net.ipv4.ip_forward=1
sudo iptables -t nat -A POSTROUTING -o wlan0 -j MASQUERADE
sudo iptables -A FORWARD -i eth1 -o wlan0 -j ACCEPT
sudo iptables -A FORWARD -i wlan0 -o eth1 -m state --state RELATED,ESTABLISHED -j ACCEPT

# on the Pluto
ip route add default via 192.168.2.3
echo 'nameserver 1.1.1.1' > /etc/resolv.conf
```

Verified: `ping 1.1.1.1` at 39 ms and `deb.debian.org` resolving.

These are runtime changes, not part of any image, so they vanish on reboot and must
be re-applied by whatever script drives a session.

### USB re-enumeration is the recovery for a wedged interface

Killing a process mid-stream leaves interface 4 claimed and every later attempt
fails with `Device or resource busy (16)`. Restarting `iiod` does **not** clear
this; forcing re-enumeration does:

```text
sudo sh -c 'echo 0 > /sys/bus/usb/devices/1-1.2/authorized; sleep 2; echo 1 > /sys/bus/usb/devices/1-1.2/authorized'
```

This matters for digital-television work in particular, where captures are started
and stopped constantly.