# T-Display-P4 battery gauge read-only driver

`bq27220_gauge` is a small read-only service for a BQ27220 connected to an
already-owned ESP-IDF I2C master bus. The caller supplies both that bus and the
7-bit address, so the component does not assume every board profile has a
gauge and does not create or own a second bus. The T-Display-P4 V1.0 LilyGO
configuration documents BQ27220 at address `0x55` on I2C1, whose documented
pins are SDA 7 and SCL 8.

Each sample reads four TI standard commands using a command-pointer write and
two-byte read, with a 100 ms transaction timeout:

| Command | Register | Value |
| --- | ---: | --- |
| `BatteryStatus()` | `0x0A` | Raw 16-bit status and selected flags |
| `Voltage()` | `0x08` | Unsigned millivolts |
| `StateOfCharge()` | `0x2C` | Unsigned percent, accepted only in `0..100` |
| `Current()` | `0x0C` | Signed milliamps |

Words are assembled little-endian (`low byte | high byte << 8`). A sample is
valid only if all four reads succeed and SOC is in range. On failure the
sample is cleared and `valid` remains false; the caller receives the
`esp_err_t` and owns polling, staleness, and presentation policy. `BATTPRES`
is exposed only as `battery_present_reported`: TI documents that it may be
host-managed when battery-insertion detection (`BIEnable`) is disabled. This
driver does not infer cell absence from that bit, alter configuration, or
write control/data-flash commands. A missing/unresponsive gauge therefore
produces an error and invalid sample, never a synthetic 0% reading.

The driver creates only an I2C device handle at the caller-provided address;
destroying it removes that handle and leaves the shared bus intact. The caller
must serialize access according to its bus ownership policy and should poll
from a worker, not a UI thread.

## Background service contract

`orcsdr_battery_start(existing_bus, address_7bit)` creates the gauge device
handle on the supplied bus and starts one low-priority worker. The service
polls once per second and performs no UI work. It neither creates nor configures
an I2C bus. The supplied bus must remain valid for the process lifetime. There
is intentionally no stop/deinit API: deleting the device handle while the
worker may be polling it would be unsafe. Duplicate starts are rejected. If
device-handle allocation or worker creation fails, startup reports the error
and releases any created handle so a later startup attempt can retry.

Snapshot reads are nonblocking with respect to I2C. The worker reads outside the
state lock; the lock is held only to update or copy snapshot state. The last
complete successful sample expires at an age of 3 seconds. A failed read clears
the raw sample and makes the UI percentage unavailable (`-1`), while retaining
the last-success age and the fact that the gauge was previously seen. Until a
successful read, last-success age is `UINT32_MAX` and the UI percentage is
unavailable.

The service considers percent usable only when a complete sample is still
fresh, the raw sample is valid, the gauge reports BATTPRES, and voltage is
nonzero. BATTPRES is only the gauge's reported status bit; it does not prove a
cell is physically installed. Its behavior depends on gauge configuration
(including BIEnable), so the board/profile integration must not present it as
independent cell detection. Zero voltage or an unreported-presence flag yields
an unavailable percentage rather than a fabricated 0%. A valid, fresh SOC of
0% is still reported as 0% when those eligibility checks pass.

The host service tests exercise the same polling and snapshot policy with fake
I2C and FreeRTOS backends. They do not verify physical I2C timing, actual RTOS
scheduling, or hardware gauge behavior. Component build/startup integration
remains the board-level caller's responsibility.

## P4 profile and UI integration — 2026-11-03

The LilyGO profile selects BQ27220 on its already-owned I2C1 bus at `0x55`.
Board startup starts the optional worker only after successful display bring-up;
an unsupported gauge/bus or startup failure does not fail the display. Other
profiles default to no gauge. The app installs a nonblocking snapshot provider
in the existing M5 power interface before starting UI/serial tasks. Dashboard
snapshots and Settings use that interface rather than a second board-specific UI.
`RTL_BATTERY STATUS` reports validity, age, reported presence and raw readings.

Home updates only its battery widget when the percentage changes. A compact
header omits the percent suffix only when it cannot fit at the existing readable
text size; legacy wide headers retain it. Other dashboards consume the shared
reading, but continuous battery-only refresh is not qualified on every page.

Validation performed in the integration worktree:

- WSL `ctest --test-dir build/bq27220-host --output-on-failure`: 8/8 passed.
  Windows CTest cannot execute these Linux-built binaries; that invocation did
  not run tests and was corrected to WSL.
- WSL `python3 apps/orcsdr-tab5/tests/test_home_battery_recording.py`:
  `HOME_BATTERY_RECORDING_PASS`.
- `python tests/test_freq_keypad_layout.py`: 7/7 passed, including 100%/clamped
  values fitting the battery cell at size 2 without a full-screen clear. The new
  bounds assertion failed before the production fix on both LilyGO orientations
  and Hosyond.
- ESP-IDF 5.5.4 build, app-only COM32 flash and flash hash verification passed;
  local logs: `.local/battery-ui-build.log`, `.local/battery-ui-flash.log`.
- Two fresh hardware samples reported 22%, 3964 mV, +6 mA, `ESP_OK`; a later
  sample reported 3963 mV and 0 mA. These are gauge reports, not an independent
  measurement or state-of-charge calibration.
- Native r18 portrait/landscape PNG captures show the battery indicator inside
  the header. Auto-rotation was restored after capture. Initial post-reset RTL
  and Wi-Fi status was not ready; later RTL streaming was about 2.4 MS/s and
  saved Wi-Fi joined successfully. This is a short coexistence check, not
  sustained receiver/audio acceptance.

Physical unplug/no-cell behavior, calibrated accuracy, sustained workload and
human readability acceptance for this new indicator remain open. Captures and
device-specific files stay local and are not installation artifacts.

## Provenance

- LilyGO board pin/address source: [`t_display_p4_config.h` at
  `a47cf73e2c9d442c0f00b618749150f4482dcd0a`](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h).
- Board component source: [`T-Display-P4` README at
  `7b021f795f6842fe6d62d3b2f2931cd0f03d3f80`](https://github.com/Xinyuan-LilyGO/T-Display-P4/blob/7b021f795f6842fe6d62d3b2f2931cd0f03d3f80/README.md).
- Register semantics: TI BQ27220 Technical Reference Manual
  [SLUUBD4A](https://www.ti.com/lit/ug/sluubd4a/sluubd4a.pdf), revised
  November 2022.
- Implementation is an independent, minimal C implementation from those
  documented register facts; no LilyGO driver code is copied. The LilyGO
  device-driver repository is GPL-3.0; this file remains under OrcSDR's
  AGPL-3.0-only plus commercial-license model and is not derived from that
  driver implementation.
