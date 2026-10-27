# A01 — Aircraft List

> **Category:** AIRCRAFT · **Phase dependency:** P3 fixture viewer / P4 live decode · **Status:** proposed and unimplemented.  
> Shared [UI/UX rules](../../UI_UX_STANDARD.md), [routing](../../NAVIGATION.md), [data contracts](../../DATA_CONTRACTS.md), [rendering/tests](../../RENDERING_AND_ACCEPTANCE.md).

## Purpose and plain-language UX

Make decoded Remote ID observations explorable in one simple, bounded list. A row means a locally received broadcast/track record, not necessarily a separate physical aircraft. Aircraft identifiers are transmitter claims.

## User how-to

1. Tap AIRCRAFT from the five-category footer; land on a list sorted by last received time.
2. Select a row to see A02 Details, or tap the source/type filter to limit to an actual observed method.
3. Use BACK to return with selection and scroll retained; tap a stale row to inspect last-known evidence rather than current status.

## Tab5 1280 × 720 layout specification

Reference shell is reserved: common OrcSDR header y0–113, 52–60px inner-screen selector y118–178, content-safe rectangle x20–1260/y188–634, five global categories in y649–705. All controls go through the shared layout/hit system, not 25 independent coordinate owners.

- x20–885 list: 4–5 rows at comfortable touch height with claimed ID or UNKNOWN ID, source, last-seen age, report-validity icon and optional speed field.
- x905–1260 right: selected row summary (time, source, validity), DETAILS and MAP actions if location is valid; a separate no-selection explanation.
- A large two-control sort/filter row along top of content; avoid more than two tiny columns of secondary numbers.

Typography defaults to large 26–32px headings, 20–26px list rows and 36–56px headline metrics subject to M5GFX font measurement. Use current OrcSDR background/panel/cyan/green/muted palette. Green means actual supported reception, amber means caution/invalid/paused, not threat level. Each active tap region >=52px.

## Field requirements and provenance

- Bounded TrackSummary array with stable local track_key, latest observation_key, source IDs, decoded claim ID if present, last monotonic receive, and message count.
- Optional actual reported location and speed validity flags; state of source and expiry; no inferred country/model/type from ID strings.
- Grouping/merge confidence expressed as method note, never global unique-aircraft count unless association policy validated.

Every reported location, ID, timestamp, altitude or speed remains a *broadcast claim* or a separately identified receiver measurement. Use true valid/absent/invalid states; never synthesize positions or interpret unknown ID as confirmed anonymous aircraft. TEST DATA remains clearly visible in every replay-derived view.

## Tap controls and screen navigation

- ROW -> A02; select new row updates right detail only and no RTL control.
- SORT by newest/claimed ID only when fields available; FILTER by source and current/stale state; clear both with one visible control.
- MAP -> A05 only if valid reported position; when absent show LOCATION NOT REPORTED rather than disabled unexplained.

Global Home/volume/Visualizer/Settings take precedence. Back preserves source/selected observation and scroll when record remains available. The whole suite has one ScreenController owner.

## No-data / unsupported / stale / failure presentation

- No decoded RID but scanner active -> NO BROADCASTS OBSERVED; source unavailable -> SOURCES NEEDED.
- Stale row -> AGE / LAST RECEIVED label and no green current marker; expired selected row retains immutable snapshot or clears with reason.
- Malformed/partial ID -> UNKNOWN ID; never transform Wi-Fi AP or MAC into aircraft ID.
- Mixed fixture/live prohibited unless obvious TEST DATA boundary and separate list.

A source cannot show SCANNING merely because chipset and UI exist. Do not silently turn on an RTL tuner or alter C6 firmware. Time since reception uses monotonic clock; UTC only if verified.

## Builder how-to (work units)

1. Generate a snapshot of at most fixed visible rows from the shared store; avoid dynamic strings and per-frame sorting in draw().
2. Track the selected key across sorting/updates and clamp scroll; virtualize long history rather than allocate full list surfaces.
3. Use dirty rectangles for changed row age/source status and selection highlights; newly inserted rows can repaint bounded list region, not header/footer.

View draws from immutable `DroneWatchSnapshot` on UI task only. Keep received packet parsing, network I/O, storage and map tiles in proper model/adapters/workers. Draw chrome on structural entry only, then invalidate bounded rows/cards and skip unchanged widgets. Avoid a per-screen full-frame framebuffer.

### 720 × 720 adaptive behavior

720×720: list rows full width and DETAILS as full page instead of right inspector. Show two or three highly readable fields per row; extra fields in detail. Derive from a common layout solver with touch-safe secondary navigation, not cloned screens.

## Acceptance cases unique to this screen

- One repeated aircraft ID across many packets remains one row according to documented association rules, with message count.
- Two unrelated sources at same RSSI never auto-merge; new invalid position does not leave stale MAP action as live.
- Sort/scroll preserve correct selected key, 720-square view readable, ages ticking do not call fillScreen.

Also run shared visual regression: 100 value/timer ticks update correct pixels without increasing the full-screen-clear count, off-page state publishes without screen writes, overlay clipping, 1280/720 geometry, and separately recorded physical Tab5 readability and RF continuity. Treat implementation, host build and hardware/RF evidence as distinct outcomes.
