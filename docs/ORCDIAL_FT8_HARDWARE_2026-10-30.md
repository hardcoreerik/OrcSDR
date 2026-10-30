# Tab5/Dial flash and pairing check — 2026-10-30

The user authorized flashing the Tab5 on COM17 and re-pairing the Dial. Both
matching, previously built application images are now installed. This closes
the flash/pair/control acknowledgement gates only; it does not establish real
on-air FT8 delivery or physical display acceptance.

## Devices and installed images

| Device | Port / USB identity | Application source | SHA-256 |
|---|---|---|---|
| Tab5, ESP32-P4 revision 1.3 | COM17 / `30:ED:A0:E2:E6:95` | `8c1eb23a55bd5568e59bf0780ffbbb37bf703b68` | `0eb38cbbc11f122af96b9b92c2551a700d18bf84033bca1ac7784f0d5018c3b4` |
| M5 Dial, ESP32-S3 | COM14 / `E8:F6:0A:98:26:2C` | `ca2a25b3b67a49843bbe17f105e937f942247e50` | `0d3ab48fbad89533eeac0009562ca6c9046df9d72fd082a24b67b93c61c86cc2` |

Source hashes were checked against the saved artifacts before writing. Both
application-only writes at `0x10000` completed with `Hash of data verified`.
The Dial boot log explicitly reports the expected source commit and `dirty=0`.
The Tab5 subsequently answered authenticated status queries. No C6 firmware,
partition layout, bootloader, NVS credentials/settings, maps, or crash region was
written. Companion trust was deliberately changed through its normal commands.

The initial no-reset native Tab5 monitor produced no status reply; the subsequent
bootloader probe confirmed the chip and USB identity. After flashing, the serial
command loop and RTL-SDR device-status query responded successfully.

## Recovery preserved locally

Read only firmware/boot/partition regions; no NVS or credentials were extracted.
The existing Tab5 partition table exactly matched the candidate table.

| Recovery image | Bytes | SHA-256 |
|---|---:|---|
| Tab5 `0x2000..0x8fff` boot/partition region | 28672 | `a2024eb3487cb171847a3335e683731816564d2222a3708c6eb26459c90d9d97` |
| Tab5 factory app partition at `0x10000` | 6291456 | `97a1298666e66b109108a9dab5b845265c9e0788cbceb73824f4106d783049ab` |
| Dial app0 partition at `0x10000` | 3342336 | `76705a24de5c4e7601c1f1a021c83937d8ab0ff126db06c40ea63d993880699f` |

Tab5 recovery/log directory:
`F:\AI\OrcSDR-TEMP\orcdial-ft8-integration\.pio\ft8-integration\hardware-com17`.
Dial recovery/log directory:
`F:\Ai\OrcDial-local-work\slice1-validation\.pio\hardware-slice1\integration-com14`.
These local binaries and logs were not published as release assets.

Flash commands used esptool 4.12.0 for P4 and PlatformIO's esptool 4.9.0 for S3:

```text
esptool --chip esp32p4 --port COM17 --baud 921600 write_flash 0x10000 .pio/ft8-integration/firmware-8c1eb23a.bin
esptool --chip esp32s3 --port COM14 --baud 921600 write_flash 0x10000 .pio/hardware-slice1/firmware-ca2a25b.bin
```

## Pairing and command result

The existing serial authorization key was used through the repository's Tab5
helper; key material and authentication proofs were not logged. Tab5 initially
reported no companion trust. The Dial's old trust was removed through
`ORCDIAL_FORGET`, and `trust=0` was verified before starting pairing.

`RTL_ORCDIAL_PAIR START` and `ORCDIAL_PAIR START` reached the comparison state.
Both numeric codes were independently read and checked equal before sending
their respective `PAIR CONFIRM <code>` commands. Final results:

```text
RTL_ORCDIAL_STATUS bridge=1 trust=1 connection=Connected boot=1 failure=None protocol=4
ORCDIAL_SECURITY trust=1 connection=Connected boot=1 failure=None code=000000 protocol=4
ORCDIAL_STATUS link=LINKED pairing=0 channel=11 channel_valid=1 freq=906875000 dashboard=0 focus=0 step=125000 volume=128 ack=76 pending=0 transport=ESPNOW
```

The no-retune control check sent `ORCDIAL_DASHBOARD 0` while already on Home.
The Dial logged `TX seq=76 type=15 value=0`, then confirmed `ack=76 pending=0`
in authoritative receiver state. Frequency remained `906875000` throughout.
This verifies an encrypted control/ack round trip, not a tuning or DSP stress test.

Read-only Tab5 observations: RTL-SDR present (`0bda:2838`, high-speed USB);
FT8 `screen=0 runtime=stopped clock=1 store=0`; sampled DMA free 21611 bytes,
largest block 21504 bytes; secure worker minimum free stack 8568 bytes. These
are one-session observations, not worst-case memory or coexistence acceptance.

The local `pair_devices.py` and `control_check.py` scripts and their logs are in
the Tab5 recovery/log directory. Ports were closed after the checks.

## Remaining acceptance

Opening FT8 retunes to its selected FT8 band, so the live RF test awaits the
connected antenna and intended band. No synthetic decode was injected. No
actual FT8 preview reception, locator display, packet-loss/reconnection stress,
legacy firmware combination, heavy DSP/audio continuity, or physical
touch/encoder/readability/smoothness result is claimed. Nothing was merged or
released; the original development validation report remains the record of the
host checks and built source artifacts.


## Subsequent live 20 m FT8 check: initial milestone demonstrated

The user specified an outdoor telescopic antenna set for 20 m, connected through
50 feet of cable to the V3c dongle. Antenna length, matching and feedline loss were
not measured; the positive result is bounded to that reported setup.

`FT8 OPEN` selected FT8 at 14.074 MHz. Observation ran from 16:37:53 to about
16:40:27 UTC (09:37:53–09:40:27 Pacific) on October 30, for 154.1 seconds. There
were already 13 stored decodes at the start; the test did not manufacture those
or clear them. No synthetic signal or decode injection was used.

During observation the receiver store grew from 13 to 40 records, and completed
slots advanced from 6 to 15. The paired Dial accepted both summary (type 17) and
matching text (type 18) for new record IDs **19, 21, 25, 27, 31, 32, 34, 38, 40**.
The latest-only policy intentionally does not project every station in a slot.
There were 100 text acceptances including retries, not 100 distinct decodes.

Examples queried from the receiver's actual decode store included:

```text
ORC_FT8_DECODE utc=1793378325 mode=FT8 hz=1769 dt_ms=140 snr=-14 sync=81 kind=CQ call=W3YNI grid=FN00 msg="CQ W3YNI FN00"
ORC_FT8_DECODE utc=1793378385 mode=FT8 hz=1463 dt_ms=100 snr=-13 sync=84 kind=CQ call=KJ4KFJ grid=EN74 msg="CQ KJ4KFJ EN74"
```

Those are received-message fields, not verified transmitter identities or exact
positions. The complete wire contract still plots only validated CQ-associated
locator area centers. These source-store examples are not a separate per-field
readback or screenshot from the Dial.

Final sampled status: receiver `screen=1 runtime=ready mode=FT8 band=20m
 dial_hz=14074000 clock=1 rx_running=1 tap=1 store=40`; Dial `link=LINKED
 dashboard=17 pending=0` at 14.074 MHz. The last sampled decode took 2324 ms,
with `deadline_hit=0` and `slot_clipped=0`. Incomplete-slot skips changed from 1
to 2 around initial setup; this run is not proof of lossless reception or a
heavy-load/audio stress pass. No link failure, command rejection, acknowledgement
timeout, or crash marker was logged during this observation.

The user reported **"seems to be working"** while watching the devices. Together
with the serial evidence this demonstrates the first genuine local FT8 decode
path into the paired Dial on this setup. Physical acceptance remains preliminary:
no formal display capture, locator-map comparison, flicker/encoder/touch audit,
legacy firmware matrix, forced packet-loss/reconnect stress, or long-duration
DSP/audio coexistence test was performed. No release or merge readiness is claimed.

The devices were left on 20 m with FT8 running and their paired link connected;
serial ports were closed. Local reproducible observation script and evidence:
`live_20m.py`, `live-20m.log`, `live-20m-result.json`, `live-20m-summary.json` in the
Tab5 hardware working directory above. The later successful live check supersedes
the earlier "awaits antenna/band" gate only.
