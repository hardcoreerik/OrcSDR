# M02 — Live Analog FPV

> **Category:** MEDIA · **Dependency:** P7 after physical video frame capture gate · **Status:** proposal only; neither compatible video capture nor acoustic classification is proved here.  
> Common contracts: [OrcSDR UI/UX](../../UI_UX_STANDARD.md), [navigation](../../NAVIGATION.md), [sources](../../DATA_CONTRACTS.md), [rendering and acceptance](../../RENDERING_AND_ACCEPTANCE.md).

## Experience and role

A full-size viewing surface for a specifically compatible external **analog** FPV receiver, not a universal drone video decoder. Real frames occupy most of the screen; the overlay states tuned receiver, live frame age, scan coverage loss and recording permission.

## User how-to

1. From VIDEO SOURCES select a proven stream and tap VIEW VIDEO.
2. See actual incoming video frame (or a clear NO VIDEO FRAME explanation); show live FPS/drop count only after measuring frame arrival.
3. Tap SOURCE to change input, PAUSE VIEW to freeze playback (not secretly stop scanning), or RECORD if storage and consent gates pass.

## Tab5 1280 × 720 layout

Use shared OrcSDR header y0–113 and untouched global hit-test areas, inner screen nav y118–178, content x20–1260/y188–634 and bottom five-category navigation y649–705. Coordinate rectangles below are draft constraints for the single layout solver, not instructions to clone separate screen-specific geometry. Maintain 52px touch, large readable text, and no 6-panel micro-dashboard.

- Main x20–956: video content viewport inside y188–615, aspect-ratio-preserving black letterbox/PAL/NTSC; no invented image behind NO FRAME message.
- Right side x976–1260: source, frequency/channel, frame state, format, last-frame age, buffer/drop count and VIDEO VIEW: SCANNING PAUSED when true.
- Within content bottom/right: large SOURCE, VIEW PAUSE, RECORD controls with immediate feedback. Keep global header and five categories outside video clip.

Use existing M5GFX/M5Unified RGB565 primitives for borders, text, bars and controls. Only a **real decoded video frame** may populate a video viewport; use plain text/status panels for unavailable content. For small bitmap buffering, measure frame stride, PSRAM/DMA and transfer ownership first.

## Required media/source fields

- Raw decoded/digitized actual frame descriptor with bounded width, height, row stride, pixel format, timestamp and generation.
- Source input chain health, tuned channel, actual receiver scan state, measured frame count/FPS/drop count and latency if data permits.
- Recording worker status, free SD capacity and selected file identity only when started by user.

No universal analog/digital video support, 5.8 GHz capability from RTL-SDR V4, unpermitted recordings, operator attribution or proven acoustic drone location is implied by this UI. `VIDEO SIGNAL`, `VIDEO SYNC` and `VIDEO FRAME` are distinct, progressively stronger evidence; acoustics remain a separate source with different validity.

## Actions and routing

- PAUSE VIEW holds last **real** frame with FROZEN and last received timestamp; source acquisition behavior separately stated.
- RECORD prompts for opt-in and starts worker only when validated; STOP closes and verifies file, failures report NOT SAVED.
- SOURCE returns M01; tapping video area toggles controls only, not receiver tuning or automatic full-screen mode.

Global Home, volume, Settings and overlay handlers take precedence. A page transition must never silently start recording, alter C6 firmware or seize RTL-SDR acquisition. Device disconnect and pending storage actions must be cancelable/recoverable.

## Default, no-hardware, degraded and stale states

- No frame -> NO VIDEO AVAILABLE, source chain reason, keep last valid completed frame visibly stamped FROZEN/STALE if appropriate.
- Frame stalled -> STALE FRAME and age; never repeat as if live video.
- Unsupported digital/proprietary/encrypted feed -> UNSUPPORTED FORMAT; never imply workaround or decoder.
- Scan suspended while video view occupies receiver -> SCAN PAUSED WHILE VIEWING; restoring scan when leaving requires verified state.

Every fixture replay carries a persistent `TEST DATA` marker even if media looks convincing. Validity and source age remain visible. Disabled hardware actions explain required equipment rather than using dead buttons.

## Builder how-to

1. Accept frames through strict ownership/format API; worker/USB callback publishes completed buffers but never draws to M5.Display.
2. Use a measured small frame queue or double-buffer only when memory/USB host capacity supports it; convert to target RGB565 with clipping and bounded transfer sizes.
3. Reserve UI compositing regions and overlay updates; preserve prior finished frame until next fully ready, no black flash on unrelated status updates.
4. On exit, stop/restore video RX and recording deterministically without resetting USB radio.

All decoding, capture, storage and frame preparation run outside the UI drawing owner. The UI reads a bounded immutable snapshot, renders chrome on screen entry, and updates changed rectangles or a new **completed** image only. Follow `docs/UI_RENDERING_STANDARD.md`; no periodic full-screen clear and no new full-size sprite by default.

### Adaptive 720 × 720 layout

720×720 presents a full-width correctly letterboxed video area; hides optional numeric inspector behind SOURCE STATUS, never stretches the decoded frame. Geometry/hit/typography are solved from actual display dimensions and font widths, never by shrinking the 1280p canvas blindly.

## Acceptance and rejection cases

- Live signal present -> displayed frame hash/frame counter matches captured source on named hardware.
- Unplug, corrupt stride, unsupported dimensions and zero frames never read outside buffer or claim LIVE.
- 100 frame/status updates without full-screen clearing, map/global overlays not touched, measured UART/USB/SD/RTL coexistence separately.

Also satisfy the shared redraw/overlay/no-source tests and record separate native ESP-IDF, exact-image physical video/audio, SD and RTL-SDR regression outcomes. A rendered UI is not proof of camera hardware support.
