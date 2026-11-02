# Embedding a complete C6 recovery package

Build recovery images with `tools/release/build-hosted-c6.ps1` from the pinned
source. Its output includes the app-only OTA artifact plus four ROM recovery
segments and `c6-rom-recovery-manifest.json`. Device snapshots are not inputs.

For the LilyGO P4 app, set CMake variables `C6_ROM_PACKAGE_MANIFEST` to that
manifest and `C6_ROM_PACKAGE_SHA256` to its independently selected SHA-256.
The package generator checks the digest, the release source/version lock,
current local C6 source hashes, each binary hash/size, ESP32-C6 image headers,
the complete role set, and flash ranges including sector-rounded erase bounds.
It rejects any segment outside the fixed boot/table/OTA/app ranges. It ignores
the JSON's `trusted` boolean. The explicit builder pin is the trust boundary;
this is not a downloaded-package signature verifier.

Without these variables, `orcsdr_embedded_c6_rom_manifest()` returns null.
With them, the build embeds the complete images and immutable per-segment
metadata. `RTL_C6_ROM STATUS` reports whether the package is present and its
total size. It is read-only; the update worker is not enabled by this command.
The legacy `C6_FIRMWARE_BIN` application-only Hosted OTA option is separate.

Run `python tests/test_c6_rom_package.py` for package validation regressions.
The tests use synthetic files and never access hardware. Successful packaging
does not prove ROM recovery, Hosted shutdown/exclusive SDIO, BOOT release,
readback verification or successful C6 restart; those remain worker/device gates.

The P4 Wi-Fi owner accepts an asynchronous recovery handoff. Authenticated
`RTL_C6_ROM QUIESCE` requests Hosted teardown; `RTL_C6_ROM STATUS` reports access
0=idle, 1=requested, 2=ready, 3=failed, 4=releasing. `RTL_C6_ROM RESUME` releases
the handoff and resumes ordinary Wi-Fi policy without changing saved power or
credentials. Rotation is locked while the
handoff is active; normal Wi-Fi scan/connect and Hosted OTA are refused.

`tools/test_p4_recovery_access.py --port COM32 --output <report.json>` explicitly
tests teardown, receiver continuity and Wi-Fi resumption without ROM access.
The recovery worker retains ownership until its loader disconnects; the serial
resume helper refuses while that worker is busy.

## ROM-SDIO worker and on-device installation

The shared worker API now has separate authenticated serial actions:

- `RTL_C6_ROM CONNECT`: hold physical C6 BOOT first; quiesce Hosted, reset C6
  through the board adapter, connect the ROM and validate chip/security/images.
- Wait for status phase `release_boot`, then release physical BOOT.
- `RTL_C6_ROM RELEASED PROBE`: finish a read-only connection test and restart C6.
- `RTL_C6_ROM RELEASED INSTALL CONFIRM`: explicitly write the complete package
  and verify every segment by SHA-256 readback, then restart/reconnect and check
  Hosted 3.0.6. This is an actual flash action, not a status command.
- `RTL_C6_ROM CANCEL`: cancel before writing begins. Once writing starts,
  cancellation is refused so cleanup cannot interrupt a flash operation.

The worker never retries automatically. Rotation, diagnostic resume and Hosted
OTA are blocked while it is busy. BOOT-release waiting expires after 120 s.
If a failure or cancellation occurs before BOOT release is acknowledged, status
changes to `release_boot_failure`. Release physical BOOT and send
`RTL_C6_ROM RELEASED PROBE` to permit reset and Hosted resumption. A timeout
does not infer release, and INSTALL is refused in this failure phase. The C6
cannot start its normal application with BOOT held. Settings uses the same worker APIs;
physical BOOT remains necessary on this board. See the tested scope below.

The host worker guard tests compile the real API against narrow dependency
stubs: `python tests/test_c6_worker_guards.py`. They do not run the worker task.
`tools/test_p4_recovery_guards.py` checks authentication and rejected idle-state
installation on a freshly booted device without starting a worker or writing C6.

The ROM writer omits device NVS and PHY data. The C6 application can still erase
its NVS on existing initialization errors; preserving application settings
across factory ESP-AT migration is not guaranteed by omitting those segments.

## On-device installation evidence

The user completed Settings > Firmware & Updates on the connected LilyGO with the physical C6 BOOT procedure. Post-action serial status reported `phase=complete percent=100 result=0 worker_busy=0 access=0`. The Wi-Fi service reported `started=1 link_failed=0 c6=3.0.6 stage=none code=0`; no access point was configured (`saved=0`). RTL-SDR remained streaming at approximately960kSps after the operation. This verifies an on-device ROM-SDIO installation from ESP-Hosted2.12.9 to the embedded3.0.6 package on this board. The parent did not capture every intermediate phase because the user performed the controls directly.

Private native screenshots show the initial page and completed state; never include those device snapshots in distributed firmware packages. The finished screen originally repeated BOOT instructions and Update now, which was misleading. The corrected UI hides the update action after verified completion and shows installation success. Following a P4 restart, a reachable C6 reporting the matching embedded version shows Up to date with no update control; an unreachable C6 retains the recovery path. This display correction is installed in checkpoint r6. A post-restart native screenshot shows C6 UP TO DATE, version3.0.6, No update needed, and no update button. Four focused recording tests pass, including terminal completion, verified matching version, and recovery availability when transport is unavailable.

This installed-app test is not a fresh factory ESP-AT migration test. Factory conversion, interrupted-write recovery, all error prompts and physical rotation locking remain separate acceptance cases. The historical ROM-SDIO prototype evidence must not be substituted for these installed-app cases.
