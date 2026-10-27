# A03 — Remote ID Inspector

> **Category:** AIRCRAFT · **Phase dependency:** P1 decoder + P3 fixture + P4 physical source · **Status:** proposed and unimplemented.  
> Shared [UI/UX rules](../../UI_UX_STANDARD.md), [routing](../../NAVIGATION.md), [data contracts](../../DATA_CONTRACTS.md), [rendering/tests](../../RENDERING_AND_ACCEPTANCE.md).

## Purpose and plain-language UX

Expose standards-level decoded messages and parser provenance for technically curious users while preserving the raw claim, parser errors and unknown fields. It is a protocol inspector, not a drone dossier.

## User how-to

1. Open INSPECT from Aircraft Details or select the latest valid message in Timeline.
2. Choose BasicID, Location, System, SelfID, Auth, or other types actually present; unsupported types stay visibly unavailable.
3. Compare raw-frame acquisition metadata and decoder interpretations; use BACK without losing selected packet.

## Tab5 1280 × 720 layout specification

Reference shell is reserved: common OrcSDR header y0–113, 52–60px inner-screen selector y118–178, content-safe rectangle x20–1260/y188–634, five global categories in y649–705. All controls go through the shared layout/hit system, not 25 independent coordinate owners.

- Left x20–480: bounded message-type list with RECEIVED/NOT SEEN and timestamp, 56px per row; paginate rather than tiny tabs.
- Middle x500–940: field/value inspector with a full value/units/validity indicator; allow controlled scrolling of long IDs.
- Right x960–1260: RECEIVER ORIGIN / PARSER RESULT, source, transport, payload length, decoder revision, capture timestamp quality, error reason.

Typography defaults to large 26–32px headings, 20–26px list rows and 36–56px headline metrics subject to M5GFX font measurement. Use current OrcSDR background/panel/cyan/green/muted palette. Green means actual supported reception, amber means caution/invalid/paused, not threat level. Each active tap region >=52px.

## Field requirements and provenance

- Raw frame capture reference only if actually retained; parse_status ok/incomplete/malformed/unsupported; decoder provenance, declared message version.
- All standard fields decoded by a pinned, verified OpenDroneID core and explicit absent/invalid/sentinel handling; no fabricated message type.
- Auth state differentiates message/payload present vs actual validated authentication (rare); provenance is immutable under replay.

Every reported location, ID, timestamp, altitude or speed remains a *broadcast claim* or a separately identified receiver measurement. Use true valid/absent/invalid states; never synthesize positions or interpret unknown ID as confirmed anonymous aircraft. TEST DATA remains clearly visible in every replay-derived view.

## Tap controls and screen navigation

- Tap a message type -> show latest matching message fields and exact age; select prior packet if bounded history retained.
- COPY/SAVE decoded fields only if logging/export path implemented and explicit privacy redaction applied; until then hide action.
- EXPLAIN shows short friendly glossary for BasicID, Location, System, SelfID and Auth, with a link back to A02.

Global Home/volume/Visualizer/Settings take precedence. Back preserves source/selected observation and scroll when record remains available. The whole suite has one ScreenController owner.

## No-data / unsupported / stale / failure presentation

- Unsupported protocol version -> UNSUPPORTED VERSION, not INVALID DRONE.
- Bad frame -> PARSE FAILED and bounded diagnostic code, but no recycled prior packet bytes.
- No raw capture saved -> RAW BYTES NOT RETAINED; still show normalized decoded fields.
- Unknown/invalid field -> NOT REPORTED or INVALID distinctly; TEST DATA banner on fixtures.

A source cannot show SCANNING merely because chipset and UI exist. Do not silently turn on an RTL tuner or alter C6 firmware. Time since reception uses monotonic clock; UTC only if verified.

## Builder how-to (work units)

1. Decoder runs outside renderer in a bounded adapter; view receives immutable ParsedMessageSnapshot and data validity flags.
2. Build reusable field-row table with fixed-height scrolling and source/age panel. Never hex-dump huge unbounded byte buffers.
3. Bind packet keys rather than global live pointer; sanitize all text before drawString.

View draws from immutable `DroneWatchSnapshot` on UI task only. Keep received packet parsing, network I/O, storage and map tiles in proper model/adapters/workers. Draw chrome on structural entry only, then invalidate bounded rows/cards and skip unchanged widgets. Avoid a per-screen full-frame framebuffer.

### 720 × 720 adaptive behavior

720×720 uses tabbed MESSAGE / FIELDS / ORIGIN subsections with large controls; a long field gets an in-place detail overlay. Derive from a common layout solver with touch-safe secondary navigation, not cloned screens.

## Acceptance cases unique to this screen

- Valid official reference packet fixture matches every decoded field; unsupported and malformed packet show distinct states.
- Long strings/invalid UTF-8/invalid coordinates are clipped/safe; absent auth is not shown verified.
- Switch message type 100 times and update age without a whole-frame clear; source labels remain consistent with A02.

Also run shared visual regression: 100 value/timer ticks update correct pixels without increasing the full-screen-clear count, off-page state publishes without screen writes, overlay clipping, 1280/720 geometry, and separately recorded physical Tab5 readability and RF continuity. Treat implementation, host build and hardware/RF evidence as distinct outcomes.
