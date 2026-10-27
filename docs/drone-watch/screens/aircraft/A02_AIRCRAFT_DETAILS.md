# A02 — Aircraft Details

> **Category:** AIRCRAFT · **Phase dependency:** P3 fixture / P4 live source validation · **Status:** proposed and unimplemented.  
> Shared [UI/UX rules](../../UI_UX_STANDARD.md), [routing](../../NAVIGATION.md), [data contracts](../../DATA_CONTRACTS.md), [rendering/tests](../../RENDERING_AND_ACCEPTANCE.md).

## Purpose and plain-language UX

Give a friendly explanation of the selected Remote ID broadcast and its latest decoded fields. A details card never upgrades a broadcaster's self-reported information to verified truth.

## User how-to

1. From LIVE WATCH latest event or AIRCRAFT LIST, tap DETAILS.
2. Read claimed identity, LAST HEARD, source and received message types at a glance.
3. Tap INSPECT to see field-by-field messages, MAP for reported valid position, or TRACK to see *observed fragments*.

## Tab5 1280 × 720 layout specification

Reference shell is reserved: common OrcSDR header y0–113, 52–60px inner-screen selector y118–178, content-safe rectangle x20–1260/y188–634, five global categories in y649–705. All controls go through the shared layout/hit system, not 25 independent coordinate owners.

- Left x20–815: ID/UNKNOWN header, source tag, last-seen age and large primary decoded value(s); labeled RECEIVED REMOTE ID.
- Right x835–1260: grouped cards STATUS, LOCATION CLAIM, ALTITUDE and reported SPEED only if each decode is valid. Show actual unit/reference.
- Bottom within content: large INSPECT / MAP / TRACK buttons, plus BACK as context navigation; don't squeeze long IDs beside map.

Typography defaults to large 26–32px headings, 20–26px list rows and 36–56px headline metrics subject to M5GFX font measurement. Use current OrcSDR background/panel/cyan/green/muted palette. Green means actual supported reception, amber means caution/invalid/paused, not threat level. Each active tap region >=52px.

## Field requirements and provenance

- Selected track_key and observation_key, source_id, transport, claimed reported ID including type and validity; group policy.
- Decoded message types, last valid update per field and per-field stale/unsupported/invalid state, not one global validity bit.
- Claimed lat/lon only when both valid, altitude + datum/reference, speed + units and heading; auth page may be present but unverified.

Every reported location, ID, timestamp, altitude or speed remains a *broadcast claim* or a separately identified receiver measurement. Use true valid/absent/invalid states; never synthesize positions or interpret unknown ID as confirmed anonymous aircraft. TEST DATA remains clearly visible in every replay-derived view.

## Tap controls and screen navigation

- INSPECT -> A03 with specific message selected; MAP -> A05 centered on claimed location and timestamp; TRACK -> A04.
- Tap source badge -> S01; tap LAST HEARD -> W02 filtered to related record.
- BACK restores prior list scroll or WATCH event, including if record expired while detail open.

Global Home/volume/Visualizer/Settings take precedence. Back preserves source/selected observation and scroll when record remains available. The whole suite has one ScreenController owner.

## No-data / unsupported / stale / failure presentation

- Selected key missing -> RECORD UNAVAILABLE with back action, not a random next aircraft.
- No coordinates -> LOCATION NOT PROVIDED; don't show 0.0/0.0 placeholder.
- Decoded but no auth verification -> BROADCAST CLAIM / UNVERIFIED even with auth messages present.
- Stale selected item -> HISTORY / LAST SEEN, no green dot; malformed fields shown INVALID not left over.

A source cannot show SCANNING merely because chipset and UI exist. Do not silently turn on an RTL tuner or alter C6 firmware. Time since reception uses monotonic clock; UTC only if verified.

## Builder how-to (work units)

1. Separate latest-field snapshot timestamps; invalidate each field individually instead of carrying stale positions into current view.
2. Use one reusable metric/metadata card component with clipped text and optional values; simplify ID with tap-to-view-full but never ambiguous silent truncation.
3. Selection navigation binds stable ID; screen doesn't initiate periodic radio changes or downloads.

View draws from immutable `DroneWatchSnapshot` on UI task only. Keep received packet parsing, network I/O, storage and map tiles in proper model/adapters/workers. Draw chrome on structural entry only, then invalidate bounded rows/cards and skip unchanged widgets. Avoid a per-screen full-frame framebuffer.

### 720 × 720 adaptive behavior

720×720: summary card + paginated metrics, with three large bottom actions accessed by a single ACTIONS tray; no two-column squeeze. Derive from a common layout solver with touch-safe secondary navigation, not cloned screens.

## Acceptance cases unique to this screen

- Partial Location update clears plotted eligibility; altitude reference unknown displays unreported, not assumed AGL/MSL.
- Auth message page present without signature check still marked unverified.
- Selected record expiration and navigation under rapid packets leave correct source/age; dirty redraw tested.

Also run shared visual regression: 100 value/timer ticks update correct pixels without increasing the full-screen-clear count, off-page state publishes without screen writes, overlay clipping, 1280/720 geometry, and separately recorded physical Tab5 readability and RF continuity. Treat implementation, host build and hardware/RF evidence as distinct outcomes.
