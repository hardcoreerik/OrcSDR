# ESP-Hosted P4/C6 version compatibility — 2026-09-29

## Scope and identity

Dated engineering evidence for the Tab5 Wi-Fi coprocessor pairing. It backs the change
that stops OrcSDR from requiring a 3.0.6 C6 (pull request #130). It is not a claim about
every possible ESP-Hosted version.

Test platform: the owner's M5Stack Tab5 (ESP32-P4 host, ESP32-C6 SDIO slave), RTL-SDR Blog V4,
ESP-IDF 5.5.4, OrcSDR built with `espressif/esp_hosted` 3.0.6 as the P4 host library. The
C6 was moved between 2.12.6 (the M5 factory version, installed with M5Burner's C6 tool) and
3.0.6 (OrcSDR's embedded image, installed through Firmware & Updates) during the runs.

## Why this mattered

OrcSDR used to refuse to start Wi-Fi unless the C6 reported exactly 3.0.6 and pushed users to
update it. M5 Launcher is built on an ESP-Hosted 2.x host, so a C6 on 3.0.6 breaks Launcher's own
Wi-Fi and OTA. Anyone who installed OrcSDR through Launcher and then updated the C6 for OrcSDR
lost Launcher's Wi-Fi.

## Pairing results

| P4 host library | C6 firmware | Result |
|---|---|---|
| 3.0.6 | 3.0.6 | Wi-Fi works (the previous shipping configuration). |
| 3.0.6 | 2.12.6 | **Wi-Fi works.** Full battery below passed. |
| 2.12.6 (M5 harness, Launcher) | 2.12.6 | Wi-Fi works; Launcher OTA works. |
| 2.12.6 (M5 harness, Launcher) | 3.0.6 | **Wi-Fi fails.** The host reads the C6 version, but the scan returns `ESP_FAIL` (no reply to `Req_SetWifiMode`). |

So a newer host can drive an older C6, but an older host cannot drive a newer C6. That is why a
3.0.6 C6 breaks Launcher, and why a 2.12.6 C6 is the version that works with both.

## What passed on the 3.0.6 host with a 2.12.6 C6

Run with a test build that bypassed the version gate (commit `15d5e67`, never merged):

- Wi-Fi connected on the first attempt after boot; saved profile reconnected after restarts.
- Data & Maps: signed catalog check worked; the 72 MB FAA aircraft pack downloaded, verified and
  installed, and reinstalled cleanly after removal.
- Firmware & Updates showed the C6 difference and offered the update.
- Settings (Wi-Fi, location, rotation) survived restarts.
- Station changes and direct tuning worked while Wi-Fi was connected.
- M5 Launcher: with the C6 on 2.12.6 its Wi-Fi worked, it listed OrcSDR 0.3.0-beta.1 from OTA,
  and OrcSDR installed and ran from it.
- Download speed was the same on 2.12.6 and 3.0.6 (about 525 KiB/s for the 72 MB pack over the
  10 MHz SDIO link), so 3.0.6 gives no measured speed benefit on the Tab5.

## What the change does (pull request #130)

- 2.12.6 and 3.0.6 are the tested C6 versions and start Wi-Fi normally.
- Any other readable version is attempted with a logged warning instead of a hard block; only an
  unreadable version stops Wi-Fi.
- Firmware & Updates reports a supported C6 as "no update needed" and offers 3.0.6 as an optional
  update with a warning that it stops M5 Launcher's Wi-Fi and OTA.

Verified on the device with the final build:

| C6 | `RTL_WIFI_C6_STATUS` | Wi-Fi |
|---|---|---|
| 3.0.6 | `state=current match=1` | connected |
| 2.12.6 | `state=optional match=1` | scan found 7 access points; later connected to the saved network |

The Firmware & Updates screen was confirmed visually ("no update needed", the Launcher warning
and the optional 3.0.6 button).

## Related finding: restart hang (issue 127)

While running the mismatch battery about 1 in 5 software restarts hung at boot with the SD card
failing to mount. That is independent of the C6 version (it reproduced on both pairings) and is
fixed separately in pull request #128.

## Not tested

- C6 firmware other than 2.12.6 and 3.0.6.
- Launcher with a 3.0.6 C6 beyond the failure shown above.
- Returning from 3.0.6 to 2.12.6 from inside OrcSDR: only the 3.0.6 image is embedded, so that
  still needs M5Burner's C6 tool.
- A long soak on the 2.12.6 pairing beyond the runs above.
