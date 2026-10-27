# M04 — Acoustic Monitor

> **Category:** MEDIA · **Dependency:** P8 opt-in Tab5 microphone feasibility and separately validated classifier · **Status:** proposal only; neither compatible video capture nor acoustic classification is proved here.  
> Common contracts: [OrcSDR UI/UX](../../UI_UX_STANDARD.md), [navigation](../../NAVIGATION.md), [sources](../../DATA_CONTRACTS.md), [rendering and acceptance](../../RENDERING_AND_ACCEPTANCE.md).

## Experience and role

Provide an explicitly opt-in audio experiment: measured sound level/spectrum at first, and **possible rotor sound** only after a real, disclosed classifier has passed positive/negative recording tests. A microphone cannot authenticate an aircraft, pilot or position.

## User how-to

1. Open MEDIA -> ACOUSTIC MONITOR; see MIC OFF and the privacy explanation until the user chooses START LISTENING.
2. After permission and hardware support, read measured microphone level or spectral features; all values have units/sample window.
3. Tap STOP to halt monitoring; optionally MARK EVENT, or SAVE CLIP only with separate explicit recording approval.

## Tab5 1280 × 720 layout

Use shared OrcSDR header y0–113 and untouched global hit-test areas, inner screen nav y118–178, content x20–1260/y188–634 and bottom five-category navigation y649–705. Coordinate rectangles below are draft constraints for the single layout solver, not instructions to clone separate screen-specific geometry. Maintain 52px touch, large readable text, and no 6-panel micro-dashboard.

- Left x20–915: large MIC OFF/LISTENING indicator, audio level plot from measured mic data and simple moving feature/threshold line if computed.
- Right x935–1260: input channel, sample rate, last sample age, speaker/replay interference indicator, privacy/clip state and help.
- Bottom area: 52px START/STOP; MARK EVENT and SAVE CLIP visible only when their behavior exists and recording enabled.

Use existing M5GFX/M5Unified RGB565 primitives for borders, text, bars and controls. Only a **real decoded video frame** may populate a video viewport; use plain text/status panels for unavailable content. For small bitmap buffering, measure frame stride, PSRAM/DMA and transfer ownership first.

## Required media/source fields

- Real microphone adapter status (ES7210 path must be physically verified), sample rate, channel, gain and monotonic sample count.
- Actual measured envelope/spectrum (dBFS relative to digital full scale) and clipping/noise floor; no calibrated acoustic SPL without microphone calibration.
- Classifier model identity, independent corpus, false-positive/miss evidence and threshold if a class label is eventually displayed; default no classifier.

No universal analog/digital video support, 5.8 GHz capability from RTL-SDR V4, unpermitted recordings, operator attribution or proven acoustic drone location is implied by this UI. `VIDEO SIGNAL`, `VIDEO SYNC` and `VIDEO FRAME` are distinct, progressively stronger evidence; acoustics remain a separate source with different validity.

## Actions and routing

- START LISTENING explicit opt-in with immediate on-screen feedback; STOP clears task permission and source-status transition.
- Change DISPLAY SCALE only; MARK creates typed acoustic_activity event—not aircraft ID.
- SAVE CLIP requires separate opt-in; playback/replay must visibly suspend mic or avoid feedback/echo confusion.

Global Home, volume, Settings and overlay handlers take precedence. A page transition must never silently start recording, alter C6 firmware or seize RTL-SDR acquisition. Device disconnect and pending storage actions must be cancelable/recoverable.

## Default, no-hardware, degraded and stale states

- Mic not verified or busy with speaker/I2S -> INPUT UNAVAILABLE/PAUSED with specific reason.
- Monitoring but no supported classifier -> AUDIO ACTIVITY / NO CLASSIFICATION (not DRONE QUIET).
- Classifier error or no validated corpus -> POSSIBLE ROTOR SOUND label disabled; never invent confidence percentages.
- Recording disabled -> nothing persisted, even if level chart is visible; TEST DATA for replayed fixtures.

Every fixture replay carries a persistent `TEST DATA` marker even if media looks convincing. Validity and source age remain visible. Disabled hardware actions explain required equipment rather than using dead buttons.

## Builder how-to

1. Probe actual Tab5 input and I2S coexistence separately from rendering, with board-specific audio ownership.
2. Compute bounded sample window features outside UI, publish envelope/spectrum and test provenance; no microphone audio retained absent opt-in.
3. Use small fixed-size audio bar chart and dirty plot updates; expose speaker/replay gate and guard against mic feedback.

All decoding, capture, storage and frame preparation run outside the UI drawing owner. The UI reads a bounded immutable snapshot, renders chrome on screen entry, and updates changed rectangles or a new **completed** image only. Follow `docs/UI_RENDERING_STANDARD.md`; no periodic full-screen clear and no new full-size sprite by default.

### Adaptive 720 × 720 layout

720×720: one large MIC state and level plot plus START/STOP; optional feature readout on an INFO page. Geometry/hit/typography are solved from actual display dimensions and font widths, never by shrinking the 1280p canvas blindly.

## Acceptance and rejection cases

- No mic, mic unplug/disable and speaker playback lead to correct unavailable/paused states.
- Synthetic rotors never produce an unmarked real classification; uncontrolled background noise never labeled confirmed drone.
- Verify opt-in and stop wipe buffers/stop writes, log privacy state and measure audio/RTL RF coexistence.

Also satisfy the shared redraw/overlay/no-source tests and record separate native ESP-IDF, exact-image physical video/audio, SD and RTL-SDR regression outcomes. A rendered UI is not proof of camera hardware support.
