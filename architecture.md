# OrcSDR Architecture

## Purpose and truth source

This document describes the implemented runtime boundaries for the Tab5
application. It is an architecture reference, not a release-acceptance record.
[`PROJECT_STATUS.md`](PROJECT_STATUS.md) remains the authoritative project-truth
document for build, flash, and hardware evidence.

## Runtime ownership

The native ESP-IDF application separates deterministic radio work from display
work:

- **Core 0:** USB Host and RTL-SDR bulk IQ ownership.
- **Core 1:** DSP/audio queueing, touch, UI service, and ESP-Hosted/Wi-Fi
  control.
- **DMA/I2S:** audio playback proceeds independently once audio blocks are
  queued. UI work must not run in the IQ or audio callback path.
- **ScreenController:** owns which one application surface may write to the
  framebuffer and receive screen-specific touch handling.

Radio, decoder, Wi-Fi, SD, and audio state may continue to update in the
background. They provide snapshots to a visible screen; they do not draw.

## ScreenController contract

`apps/orcsdr-tab5/ui/screen_controller.{hpp,cpp}` is the single runtime owner
of display routing. Its screen IDs are Home, FM, P25, ADS-B, LoRa, generic
Radio/Scope/Capture, Settings, and documentation mode.

Every transition follows one sequence:

1. Select the next screen and, when opening Settings, remember the exact
   return screen.
2. Deactivate the previous display owner and clear the framebuffer once.
3. Enter and draw the new screen's static chrome once.
4. Finish the transition; only the active screen may perform bounded dynamic
   repaint or screen-specific touch handling.

Settings is a full-screen route, not an overlay. Closing it restores the exact
previous surface: Home, FM, P25, ADS-B, LoRa, or generic radio.

`may_draw()` rejects dynamic display work during a transition or from an
inactive screen. `is_active()` permits the one intentional static draw while a
new screen is being entered. This distinction prevents stale dashboards,
waterfalls, meters, and Settings content from drawing over a newly selected
surface.

## Dashboard responsibilities

Each dashboard owns only its static renderer, bounded dynamic update regions,
and touch behavior while selected:

| Surface | Owner responsibilities |
|---|---|
| Home | Aggregate spectrum/waterfall and navigation snapshot |
| FM / P25 | Listening controls and active radio presentation |
| ADS-B / LoRa | Decoder snapshot presentation; no IQ parsing or capture work in UI |
| Generic Radio | Radio, Scope, and Capture presentation and controls |
| Settings | Full-screen configuration and exact-route return |
| Documentation | Temporary capture routing with state restoration |

No inactive dashboard may animate, repaint, poll touch, or modify display
state. A future dashboard must register a screen ID and route entry, update,
and touch through `ScreenController` before it writes display primitives.

The former generic **Browse** screen is retired as a user route. Until a band
has a dedicated dashboard, tuning AM, WX, CB, airband, marine, satellite, or
other general receiver ranges presents the shared Home workspace instead. The
underlying radio/scope/capture services remain internal implementation support,
not a competing navigation surface.

## Header constraint

The persistent header baseline is **Home**, **Device Settings**, **Battery /
Power**, and **Volume**. `dashboard_audio_control` is the single owner of the
Home, Settings, and FM/P25 volume-control geometry; it also owns their touch
hitboxes and overlap self-check.

- **Home:** routes to the Home screen. It may be omitted only when Home itself
  is the active surface.
- **Device Settings:** every user-facing dashboard reserves the 58 by 58 pixel
  rectangle at `(1211, 8)` for the settings gear. Status content must terminate
  before that rectangle; it may compress, but it may not cover, move, or omit
  the control. Closing Settings returns to the exact originating screen.
- **Battery / Power:** a readable battery state belongs in the top status area.
- **Volume:** FM and P25 use the shared global volume/mute control today.
  ADS-B and LoRa reserve the Home and Settings positions but still need their
  header-status reflow before the same global volume control can be added
  without covering live telemetry. This is an explicit migration item, not a
  reason to duplicate a header implementation.

New dashboard headers must preserve those rectangles and their touch routing.
New header controls must extend `dashboard_audio_control` rather than add a
second geometry or touch implementation.

## Diagnostics and validation

At boot, `ScreenController::self_check()` verifies transition blocking and
Settings return for Home, FM, P25, ADS-B, and LoRa. A failure stops boot rather
than shipping ambiguous ownership behavior.

`RTL_SCREEN_STATUS` reports the active and return screens plus transition,
rejected-draw, and visible-update counters. It is read-only and performs no
periodic allocation or catalog work.

Build evidence for this controller pass is native ESP-IDF compilation. The
remaining evidence gate is on-device transition acceptance across Home, FM,
P25, ADS-B, LoRa, generic Radio/Scope/Capture, and Settings. Until that gate
is recorded, this is implemented source architecture rather than a new
hardware-verified release claim.
