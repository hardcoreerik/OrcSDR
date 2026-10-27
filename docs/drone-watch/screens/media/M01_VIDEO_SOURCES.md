# M01 — Video Sources

> **Category:** MEDIA · **Dependency:** P7 external analog receiver and digitizer qualification · **Status:** proposal only; neither compatible video capture nor acoustic classification is proved here.  
> Common contracts: [OrcSDR UI/UX](../../UI_UX_STANDARD.md), [navigation](../../NAVIGATION.md), [sources](../../DATA_CONTRACTS.md), [rendering and acceptance](../../RENDERING_AND_ACCEPTANCE.md).

## Experience and role

This is the honest equipment browser for optional compatible **analog** FPV video. It explains whether a physical receiver, video digitizer, power, format and actual frame stream are present; '5.8 GHz' on a module label is not enough to claim a viewable drone video.

## User how-to

1. Open MEDIA -> VIDEO SOURCES; if no accessory is installed, read the required hardware explanation without any phantom channel thumbnails.
2. Connect a specifically qualified receiver/digitizer and observe independent RECEIVER, VIDEO SYNC and FRAME AVAILABLE status.
3. Select an actual supported input and tap VIEW VIDEO; switching receiver band requires an explicit start/scan/stop action.

## Tab5 1280 × 720 layout

Use shared OrcSDR header y0–113 and untouched global hit-test areas, inner screen nav y118–178, content x20–1260/y188–634 and bottom five-category navigation y649–705. Coordinate rectangles below are draft constraints for the single layout solver, not instructions to clone separate screen-specific geometry. Maintain 52px touch, large readable text, and no 6-panel micro-dashboard.

- Left x20–765: source list up to 3 visible cards with model, receiver frequency/band, true availability and physical attachment status.
- Right x785–1260: selected source connection map RX -> ANALOG OUTPUT -> DIGITIZER -> P4 FRAME with independent health for every stage.
- Bottom content: 52px VIEW VIDEO, SCAN CHANNELS (only if measured-supported), STOP, INFO; display whether band scanning will pause when viewing.

Use existing M5GFX/M5Unified RGB565 primitives for borders, text, bars and controls. Only a **real decoded video frame** may populate a video viewport; use plain text/status panels for unavailable content. For small bitmap buffering, measure frame stride, PSRAM/DMA and transfer ownership first.

## Required media/source fields

- Qualified receiver ID and firmware/control interface, supported bands/frequencies and actual tuned channel.
- Valid analog sync and PAL/NTSC identification when available from digitizer; UVC/other frame transport state, frame counter, drops and power health.
- USB Host and power owner, coexistence with RTL-SDR if a USB video interface is used; no parallel claim without hardware proof.

No universal analog/digital video support, 5.8 GHz capability from RTL-SDR V4, unpermitted recordings, operator attribution or proven acoustic drone location is implied by this UI. `VIDEO SIGNAL`, `VIDEO SYNC` and `VIDEO FRAME` are distinct, progressively stronger evidence; acoustics remain a separate source with different validity.

## Actions and routing

- SELECT SOURCE only updates inspector; VIEW VIDEO -> M02 if frames actually available or honest waiting/error mode.
- SCAN CHANNELS requests receiver-specific scanning only after explicit user action and shows coverage gaps; never affects RTL-SDR without arbitration.
- SOURCE HELP shows wiring/receiver compatibility and indicates which supported adapters are physically verified.

Global Home, volume, Settings and overlay handlers take precedence. A page transition must never silently start recording, alter C6 firmware or seize RTL-SDR acquisition. Device disconnect and pending storage actions must be cancelable/recoverable.

## Default, no-hardware, degraded and stale states

- No accessory -> VIDEO SOURCE NOT CONNECTED; no fake signal level/thumbnail.
- Receiver RSSI detected but no sync -> RF SIGNAL ONLY / NO VIDEO LOCK; not a frame.
- Sync present but digitizer absent -> VIDEO SYNC / NO FRAME DECODER; VIEW VIDEO cannot claim picture.
- Device disconnected mid-scan -> STOPPED, owner restored, status retained for diagnosis.

Every fixture replay carries a persistent `TEST DATA` marker even if media looks convincing. Validity and source age remain visible. Disabled hardware actions explain required equipment rather than using dead buttons.

## Builder how-to

1. Define VideoSourceCapabilities and VideoInputSnapshot as distinct from Remote ID and RTL-SDR source models.
2. Create a hardware adapter only after P7 power/format tests. Gate scan/view controls by exact measured capabilities and serial/device identity.
3. Use a simple boxed signal-chain diagram with 4 stages drawn from M5GFX rectangles/text; update only status pills/counters.

All decoding, capture, storage and frame preparation run outside the UI drawing owner. The UI reads a bounded immutable snapshot, renders chrome on screen entry, and updates changed rectangles or a new **completed** image only. Follow `docs/UI_RENDERING_STANDARD.md`; no periodic full-screen clear and no new full-size sprite by default.

### Adaptive 720 × 720 layout

720×720 shows selected physical input as one card and stage status in a vertical chain; source list expands with SELECT SOURCE button. Geometry/hit/typography are solved from actual display dimensions and font widths, never by shrinking the 1280p canvas blindly.

## Acceptance and rejection cases

- Unknown/unplugged accessories render connection diagram with every unavailable stage; no artificial thumbnail.
- RSSI-only trace does not enable VIDEO AVAILABLE; frame counter must increase from real digitizer.
- View/scan transition logs actual pause/restore and does not break RTL audio; repeated updates use dirty rectangles.

Also satisfy the shared redraw/overlay/no-source tests and record separate native ESP-IDF, exact-image physical video/audio, SD and RTL-SDR regression outcomes. A rendered UI is not proof of camera hardware support.
