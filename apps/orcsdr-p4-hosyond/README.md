# orcsdr-p4-hosyond

OrcSDR running on a **Waveshare ESP32-P4-Module-DEV-KIT** with a **Hosyond 7"
800x480 MIPI-DSI** panel.

This is the application that sits on top of the panel bring-up proved by
[`../orcsdr-p4-dsi-probe`](../orcsdr-p4-dsi-probe): it reuses that exact
bring-up sequence (ATTINY88 power, DSI PHY, TC358762 bridge, timing profile) and
then runs an OrcSDR screen and the OrcSDR node protocol on it.

It is deliberately standalone. `orcsdr-tab5`, OrcNode and the rest of OrcSDR are
untouched, because this board is still being characterised and its display path
is not yet a supported target.

## What it does

1. Brings the panel up with the proven profile `rpi-observed-stable`
   (`hfp 254 / hsw 2 / hbp 46`, bridge `hbp 300 / hfp 0`, both line totals
   1102 px) — see the probe README for how that value was derived from
   measurements.
2. Runs a boot screen test first: full-screen **white, red, green, blue**, 1.5 s
   each, so "nothing on screen" can never be confused with "the application
   drew something dark".
2. Composes an OrcSDR screen into a PSRAM framebuffer at 800x480 RGB888:
   the OrcSDR badge (from the tab5 asset), the wordmark, and a live status block
   (node id, uptime, free heap, frame count and refresh rate) over an activity
   bar that moves on every refresh, so a frozen screen is obvious.
3. Refreshes at **4 Hz** and re-submits the whole frame, which also recovers from
   any transient loss of sync.
4. Speaks the OrcSDR node protocol on the console UART: `hello` and
   `node_snapshot` at boot, then a `heartbeat` every 2 s, in the same shape the
   tab5 firmware uses so a host sees an ordinary OrcSDR node.

## Verified on hardware

From a captured boot (`COM8`, IDF 6.1):

- DSI video and DPI output start, backlight on
- `display content check: PASS` — the four perimeter corners and the panel
  centre hold the exact bytes the drawing code wrote
- `hello` and `node_snapshot` carry `node_id = p4hosyond_80f1b2d1e0d5`
- heartbeats arrive exactly every 2 s (`uptime_ms` 1000, 3000, 5000, 7000, 9000)
- no panic, abort or error in steady state

## Build and flash

Requires **ESP-IDF 6.1** (the DSI clock-lane and PHY-clock options this board
needs do not exist in older releases).

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\Espressif\v6.1\esp-idf\export.ps1'
cd apps/orcsdr-p4-hosyond
idf.py set-target esp32p4
idf.py build
idf.py -p COM8 flash
```

`COM8` is the CH343 UART bridge on the dev kit. Logs and the node protocol share
that port, so bring-up logs interleave with the JSON stream until the app starts;
after that the app emits only JSON.

## Screen layout

| Region | Contents |
| ------ | -------- |
| `y 0..152` | badge at (28,26), `ORCSDR` wordmark, accent rule, taglines |
| `y 164..392` | status panel: node, uptime, heap, frames, display description |
| `y 368..380` | activity bar, moves every refresh |
| `y 412..448` | application state lines |
| perimeter | 1 px accent border on x=0/799 and y=0/479 |

The perimeter is deliberate: it is the reference that makes any residual
scan-out offset visible without needing a separate calibration card.

## Known limitations

- **Touch is not implemented.** The FT5x06 at I2C `0x38` is detected and reset
  but no input path exists yet, so the screen is display-only.
- **Bridge readback does not work.** Generic reads from the TC358762 return NO
  RESPONSE, so bridge register application cannot be confirmed by readback.
- **Residual horizontal offset.** The scan-out displacement is fixed from
  measurement (see the probe README); if any offset remains it is visible against
  the accent perimeter and is corrected by changing the active profile's `hbp`,
  not by changing DSI timing.
- **Not the tab5 UI.** The 327-file M5GFX-based dashboard suite in `orcsdr-tab5`
  is not part of this build; this firmware proves the OrcSDR application stack
  and node identity run on this panel. Rendering that UI here is the follow-on
  work item.

## Stock firmware

The unit shipped with OrcNode 0.1.0-alpha (IDF v6.1) and a full 16 MB dump was
taken before the first write. See the probe README for the restore procedure.
