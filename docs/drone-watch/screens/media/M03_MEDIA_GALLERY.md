# M03 — Media Gallery

> **Category:** MEDIA · **Dependency:** P5 optional audio journal; P7 verified video recordings · **Status:** proposal only; neither compatible video capture nor acoustic classification is proved here.  
> Common contracts: [OrcSDR UI/UX](../../UI_UX_STANDARD.md), [navigation](../../NAVIGATION.md), [sources](../../DATA_CONTRACTS.md), [rendering and acceptance](../../RENDERING_AND_ACCEPTANCE.md).

## Experience and role

Browse **actual files saved by the owner** during Drone Watch sessions, with source and timestamp provenance and a safe playback flow. It is not an automatic archive of intercepted video or ambient audio.

## User how-to

1. Open MEDIA -> GALLERY; with no opt-in recordings, see NO SAVED MEDIA and a simple explanation.
2. Select an actual indexed item to review type, source, capture time/clock quality, size, format and verification result.
3. Tap PLAY/VIEW for locally supported format; tap VERIFY to check recorded hash, or DELETE only after explicit confirmation.

## Tab5 1280 × 720 layout

Use shared OrcSDR header y0–113 and untouched global hit-test areas, inner screen nav y118–178, content x20–1260/y188–634 and bottom five-category navigation y649–705. Coordinate rectangles below are draft constraints for the single layout solver, not instructions to clone separate screen-specific geometry. Maintain 52px touch, large readable text, and no 6-panel micro-dashboard.

- Left x20–820: paginated recording cards with type, user-provided title or session ID, capture source and length/bytes if known.
- Right x840–1260: selected recording preview (only from actual decoded frame or audio graph), provenance, integrity status, and OPEN/VERIFY/DELETE actions.
- Top filter by AUDIO/VIDEO/ALL; storage free/status and recording permission indicated separately from current receive source.

Use existing M5GFX/M5Unified RGB565 primitives for borders, text, bars and controls. Only a **real decoded video frame** may populate a video viewport; use plain text/status panels for unavailable content. For small bitmap buffering, measure frame stride, PSRAM/DMA and transfer ownership first.

## Required media/source fields

- Versioned media index or safe bounded directory scan from approved SD root, with session_id, source_id, file path, format and capture metadata.
- Optional SHA-256 or manifest hash, file existence, actual size, integrity check result, media decode support and clock quality.
- Explicit mic consent and recording origin; no raw operator/home coordinates in thumbnails/filenames by default.

No universal analog/digital video support, 5.8 GHz capability from RTL-SDR V4, unpermitted recordings, operator attribution or proven acoustic drone location is implied by this UI. `VIDEO SIGNAL`, `VIDEO SYNC` and `VIDEO FRAME` are distinct, progressively stronger evidence; acoustics remain a separate source with different validity.

## Actions and routing

- Select changes preview panel and metadata; OPEN invokes bounded local playback, never executes arbitrary file/URI.
- VERIFY launches storage worker, shows pending immediately, then match/mismatch with concrete hash metadata.
- DELETE uses confirmation and distinct file removal success/failure, preserving list on failed deletion; EXPORT only for supported format.

Global Home, volume, Settings and overlay handlers take precedence. A page transition must never silently start recording, alter C6 firmware or seize RTL-SDR acquisition. Device disconnect and pending storage actions must be cancelable/recoverable.

## Default, no-hardware, degraded and stale states

- No SD inserted -> SD CARD NOT AVAILABLE, no invented previously saved items.
- Index missing -> NO SAVED MEDIA, not decoder failure; corrupt entry -> INVALID RECORDING, skip unsafe bytes.
- Hash absent -> NOT VERIFIED, not verified-false; format unsupported -> FILE EXISTS / PREVIEW UNAVAILABLE.
- USB/SD access conflict -> BUSY and wait/cancel option, no blocking UI redraw.

Every fixture replay carries a persistent `TEST DATA` marker even if media looks convincing. Validity and source age remain visible. Disabled hardware actions explain required equipment rather than using dead buttons.

## Builder how-to

1. Share Dead Air's bounded journal/worker design patterns while auditing current SD locking/unmount issues; namespace recordings under Drone Watch only.
2. Implement finite manifest parsing with checked lengths, path traversal rejection, file format allowlist and explicit validity flags.
3. Draw list and inspector separately; asynchronous VERIFY/SAVE only updates small pending/result widgets.

All decoding, capture, storage and frame preparation run outside the UI drawing owner. The UI reads a bounded immutable snapshot, renders chrome on screen entry, and updates changed rectangles or a new **completed** image only. Follow `docs/UI_RENDERING_STANDARD.md`; no periodic full-screen clear and no new full-size sprite by default.

### Adaptive 720 × 720 layout

720×720: three media rows plus OPEN DETAILS; full-page preview with BACK, no cramped file table. Geometry/hit/typography are solved from actual display dimensions and font widths, never by shrinking the 1280p canvas blindly.

## Acceptance and rejection cases

- No card/corrupt manifests/path traversal attempts never crash or leak data outside archive.
- Hash-match versus mismatch vs unknown show three distinct statuses; DELETE failure keeps real record.
- Rapid list page changes during SD I/O do not freeze RF/UI or redraw full screen; physical playback tests needed.

Also satisfy the shared redraw/overlay/no-source tests and record separate native ESP-IDF, exact-image physical video/audio, SD and RTL-SDR regression outcomes. A rendered UI is not proof of camera hardware support.
