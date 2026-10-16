# ACARS dashboard (UI foundation)

Status: **Host-tested; running on a spare Tab5 with a live receiver adapter; two real off-air ACARS blocks decoded (2026-10-15, see LOCAL_BUILD_2026-10-13.md).**
The dashboard itself is presentation only. Live data comes from `acars_receiver.*` (see "Live reception" below), which feeds the
native decoder (`acars_core.*`) from the RTL-SDR and posts into the model. When no receiver is attached (host tests, or a build
without the hooks) LIVE truthfully reads `RECEIVER FEED NOT CONNECTED`. DEMO mode data is synthetic and labelled as such.

The dashboard has run on two Tab5 units with a live receiver (see `LOCAL_BUILD_2026-10-13.md` for what was and was not checked).
Host tests use a recording stand-in for M5GFX; they prove geometry, ownership, state and honesty rules, not how the screen
looks or feels to touch.

## Screen layout (1280 x 720)

| Region | y | Owner |
|---|---|---|
| Shared OrcSDR header: brand left, battery and Home / Mute / Visualizer / Settings right | 0 - 104 | `audio_header` (shared) |
| Shared status-bar reserve, x 420 - 866, y 12 - 66 | inside the header | **left empty** (Home alone draws there) |
| Context strip: LIVE or DEMO chip, receiver state, requested frequency and tune status | 110 - 150 | `acars_dashboard` |
| Content (one of six views, or the message detail page) | 158 - 638 | `acars_dashboard` |
| Tab bar: LIVE, AIRCRAFT, CHANNELS, ACTIVITY, ARCHIVE, SETUP | 646 - 720 | `acars_dashboard` |

The dashboard draws the shared brand (`audio_header::draw_brand("ACARS")`) and then calls the application's
header hook, exactly like the SSTV dashboard, so battery, Home, Mute/volume tray, Visualizer and Settings are
the shared controls with their shared hit tests. It never draws a title, value or decoration in the
status-bar reserve, resets the font to the built-in one before the shared controls draw, and clips every text
and shape to its card. All geometry lives in `acars_dashboard_layout.hpp` and is shared by drawing,
hit-testing and the tests.

## Views and touch controls

| View | What it shows | Controls |
|---|---|---|
| LIVE (default) | Newest CRC-verified blocks: time, tail, flight, label, MHz, text preview, integrity; status card; receiver notice | Tap a row: message detail. NEWER / OLDER scroll the 48-message history, six rows at a time |
| Message detail | Parsed ACARS fields, raw text (escaped or plain, scrollable), application interpretation | BACK, NEWER, OLDER message, raw-text UP / DOWN |
| AIRCRAFT | Messages grouped by tail: flight, count, last label, last seen, repaired count; recent messages of the selected tail | Tap an aircraft, tap one of its messages, SORT (recent / most messages), UP / DOWN |
| CHANNELS | Regional presets with their documented source, the requested channel, tune status | Tap a preset, REGION, CUSTOM `- 25k` / `+ 25k` / CLEAR |
| ACTIVITY | Decoder counters and derived ratios, reserved signal-diagnostics card | none (information only) |
| ARCHIVE | Honest "not available" state; in DEMO a synthetic archive | In DEMO: TAIL and LABEL filters, CLEAR FILTERS, UP / DOWN, tap a row |
| SETUP | Preferences and demo controls | See below |

Taps above y = 104 (the shared header) are never handled by the dashboard; Home, Settings, Mute and
Visualizer are handled by the existing global touch path.

### SETUP

| Item | Effect |
|---|---|
| Preset region | North America / Europe / Asia-Pacific preset list (persisted) |
| Text format | Control bytes shown as `<XX>` (escaped) or `.` (plain) in the detail page (persisted) |
| Raw text in detail | Show or hide the raw text card (persisted) |
| Corrected-frame marks | FIX badge and amber bar for repaired blocks, on or off (persisted) |
| Message filter | All verified, or repaired only (persisted) |
| Aircraft sort | Most recent, or most messages (persisted) |
| Demo mode | Enters or leaves synthetic data. **Never persisted**; leaving the screen ends it |
| Demo scenario | Cycles MIXED, FULL HISTORY, EMPTY, DISCONNECTED, PAUSED, NO CLOCK (DEMO only) |
| Archive (SD), Multi-channel | Shown as PLANNED and intentionally inert |

Persisted keys: `acars_region`, `acars_preset`, `acars_fmt`, `acars_raw`, `acars_corr`, `acars_filter`,
`acars_sort`, `acars_custom` (the firmware's NVS preferences, via an injected `Store`). Invalid stored
values fall back to defaults. The requested frequency is stored as a UI preference only: no gain or tuning
is persisted as though hardware had applied it.

## What works today, and what does not

Implemented and host-tested: all six views, message detail, aircraft grouping, scrolling, preset and custom
frequency *requests*, preferences and their persistence, DEMO mode with six scenarios, bounded producer
queue, honest unavailable states.

Available with the receiver adapter (`acars_receiver`): live reception, a `TUNED` report once blocks arrive from the requested
centre, and UTC receive time when the Tab5 clock is valid (otherwise `+m:ss` since boot).

Not available (shown as such, never simulated): SNR / RSSI / carrier-lock / symbol-timing, channel activity, multi-channel
reception, SD archive, multi-block reassembly, application-message interpretation, aircraft position,
ADS-B correlation.

Counters are used for what they are: `sync_candidates` is a count of frame starts, not signal quality; a
CRC pass is a verified block, not an interpreted message. The LIVE status card counts blocks the UI has
received; the ACTIVITY tiles show the decoder's own `Stats`.

## Channel presets and their sources

Assignments vary by operator and change over time; these are listener starting points, not a promise of
traffic. Each preset carries its source in `acars_dashboard_model.cpp`.

| Region | Presets (MHz) | Sources |
|---|---|---|
| North America | 131.550 (primary), 130.025 (secondary), 129.125, 130.450, 131.125, 130.425 | [acarsdec README](https://git.brmlab.cz/brmlab/acarsdec) (USA primary / secondary); [SigIDWiki](https://www.sigidwiki.com/wiki/ACARS), [RadioReference](https://wiki.radioreference.com/index.php/ACARS) (US / Canada channels); 130.450 is described by some sources as a busy-hub overflow channel; [Airframes](https://docs.airframes.io/docs/technology/acars/intro) lists 129.125 as global |
| Europe | 131.725 (primary), 131.525 (secondary), 131.825, 131.550 | acarsdec README (primary, secondary); Wavecom ACARS decoder help and the UDXF table (131.825, least corroborated); SigIDWiki (131.550 worldwide) |
| Asia-Pacific | 131.550, 131.450 (Japan) | SigIDWiki, RadioReference. Sources disagree on 131.475 versus 131.450 for Japan; 131.450 is used |

The CHANNELS list is a set of choices, not simultaneous reception. The decoder is single-channel today.
Choosing a channel changes the **request**. With the receiver adapter running, the adapter retunes the radio and reports `TUNED` once
IQ blocks arrive at the expected rate from the requested centre; without a receiver the screen reads `NOT TUNED - NO RECEIVER RUNTIME`.
Custom frequencies are limited to 118.000 - 137.000 MHz on the 25 kHz grid.

## Data model (`acars_dashboard_model.*`)

No M5GFX or ESP-IDF dependency. Display capacities: 12 message rows, 8 aircraft rows, 6 channel rows.
Internal bounds: 48 messages per store, 16 tracked aircraft per store (least recently seen evicted), 6 message
references per aircraft, 16 decoder events in the producer queue.

- `MessageRecord` carries the decoder `Message` fields plus source (live or demo), integrity (CRC verified
  or CRC verified after n-bit repair), `time_valid`, UTC seconds when valid, device uptime, assigned preset
  index, and a sanitised preview built once at ingest. `text_length` is authoritative; text may contain NUL.
  Fields are made printable at ingest, tails are normalised (leading `.` and space padding removed).
- `AircraftRecord`: tail, latest flight, last label, message and repaired counts, last seen, history refs.
- `ChannelRecord`: frequency, label, selected (the request), `receiving` and `activity_measured` (always
  false until a runtime reports them), decoded count.
- `Snapshot`: all of the above plus receiver state, `Capabilities`, preferences, decoder `Stats`, queue
  drops, requested and tuned frequency, tune status, battery, sound, and three revisions.
- `Capabilities` (live reception, multi-channel, archive, signal measurements, application decoding,
  reassembly, ADS-B correlation) are all false by default. They are advertisements by a runtime, not claims.

Sanitising: preview keeps printable ASCII, turns CR / LF / TAB into spaces and any other byte into `.`;
the escaped form shows `<CR>`, `<LF>`, `<TAB>` and `<XX>` for other bytes (worst case 5 characters per byte, `<TAB>`,
sized for a full 250-byte block). Output is always bounded and NUL-terminated.

Revisions: `snapshot().revision` changes only when something displayed changes. A stable revision means no
repaint, and `update()` does nothing, so there is no idle polling load and no animation.

## Flicker rules

Everything below the shared header (context strip, content, tab bar) is composed in one PSRAM sprite
(1280 x 612 x 16 bit, about 1.5 MB, allocated when the screen opens and released when it closes) and
pushed to the display once per change, so the screen never shows a cleared or half-drawn frame. If the
sprite cannot be allocated the screen falls back to clearing and drawing in place (works, can flicker).
Battery and mute state do not bump the snapshot revision, so they never repaint. Data-only changes repaint at
most every 250 ms; tab and detail changes repaint at once. While the shared volume tray is open over the
content, repaints are held back (`overlay_open` hook) so the tray is not erased, and run when it closes.
Host tests assert one push per change and no direct display drawing in sprite mode; whether it is actually
flicker-free on the panel (including push time) needs a Tab5.

## Memory ownership

`acars_app` allocates one `State { Model, Controller }` (about 55.5 KB + 1.5 KB; `Snapshot` 6.2 KB,
`MessageRecord` 440 B) on first open with `heap_caps_malloc(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`,
falling back to a normal 8-bit heap allocation. There is deliberately **no static fallback object and no large
global**: like the POCSAG dashboard's lazy snapshot, a static would reserve its full size in internal BSS at
link time even if never used. Rings are cleared in place; no ~20 KB temporary is ever built on a task stack.
Drawing allocates nothing: strings are formatted into small stack buffers.

## Demonstration mode

DEMO is entered only from SETUP > DEMO MODE (or by a dedicated documentation-capture build). It is
**never persisted**, ends when the screen closes, and always shows a `DEMO DATA` chip in the context strip
(content area, outside the reserved status bar), `DEMO (SYNTHETIC)` in the detail title and
`SYNTHETIC DEMO ARCHIVE - NOT STORED` in ARCHIVE.

Isolation is structural: live and demo records live in two separate stores. `Model::inject_demo` refuses to
run outside demo mode and forces `SourceKind::demo`; only `acars_demo_data.cpp` calls it. Real traffic posted
during DEMO still goes to the live store, and the display shows the demo store. Leaving DEMO clears every
demo record. No archive or network feed exists, and nothing reads demo records for output. Tail numbers,
flight ids and text are invented (suffix `DM`, text contains `DEMO`). Scenarios exercise several
registrations, short and long bodies, ETX / ETB / unknown end markers, repaired blocks, missing flight or
address, embedded NUL and control bytes, empty and over-full history, paused and disconnected receivers, and
no valid clock.

## Build and verify

```
bash tools/test-acars-smoke.sh      # existing decoder smoke gate, unchanged
bash tools/test-acars-ui.sh         # UI host tests (model, controller, layout, renderer, app)
SANITIZE=0 bash tools/test-acars-ui.sh   # skip the ASan/UBSan pass
python3 tools/check_documentation_truth.py
```

Firmware: the supported ESP-IDF procedure is `apps/orcsdr-tab5/tools/build-tab5-idf.ps1` (see
`docs/TAB5_BUILD_POLICY.md` and `apps/orcsdr-tab5/README.md`). The ACARS sources are listed in `apps/orcsdr-tab5/main/CMakeLists.txt`. The
initial cloud authoring session did not run a firmware build. The local ESP-IDF 5.5.4 build now passes,
including `main.cpp` integration; see [the local build record](LOCAL_BUILD_2026-10-13.md) for fixes, sizes and
hardware gates. The firmware has since been flashed to two Tab5 units and decoded live traffic; the checklist in that record
says which checks were done over serial, which from photos and video, and which were not run.

What the host tests cover: capacity and overflow, queue drops, aircraft grouping and eviction, time validity,
sanitising (including embedded NUL, all byte values, worst-case sizes, cut-off buffers), repaired-frame state,
demo / live separation, capability and tune-status honesty, preference round trips, stable revisions, a
two-thread test of the queue and stats mailbox, every control's touch result, header-tap rejection, layout
geometry against the status-bar reserve, the real renderer on every view and demo scenario (no primitive
touches the reserve except a background clear, all text clipped, built-in font before the shared header
hook, no leaked clip, no idle redraw, 250 ms data throttle), and app lifecycle with `ScreenController`
ownership (Settings and Visualizer round trips, closing, reopening).

Still needs a physical Tab5: how every view looks and reads at arm's length; touch accuracy and target
sizes; font metrics (the host stand-in estimates glyph widths); header controls drawing correctly after the
dashboard (battery, mute tray, visualizer, settings); Home tile and recents; Settings and Visualizer return;
redraw latency; behaviour with a real RTL-SDR streaming in the background; long-run memory.

## Live reception

`acars_receiver.*` is the receiver adapter. It asks the radio for a 240 kS/s stream on the general VHF (BROWSE) band, centred
60 kHz below the selected ACARS channel (the carrier then sits at a quarter of the sample rate, so the mixer to DC needs no
multiplications and the tuner's DC spike stays out of the channel). `Frontend240k` low-passes and decimates to 24 kS/s;
`Decoder` runs ten bit-phase hypotheses in parallel (there is no clock recovery) and delivers each CRC-verified block once.
Blocks, decoder counters, the receiver state and the tune result flow into the same model the DEMO data uses, with
`SourceKind::live`. The wall-clock time is attached only when the Tab5 clock is valid.

Behaviour that matters when reading the screen: opening the dashboard starts the receiver and closing it hands the sample rate
back; Settings keeps it running; an absent dongle reads `RECEIVER UNAVAILABLE`; a stream at another rate is counted and ignored.

Serial diagnostics (the project convention is that serial mimics the screen): `RTL_ACARS STATUS` prints the dashboard state and the
receiver counters; `RTL_UI TAP <x> <y>` (authenticated) injects a touch through the normal routing; `RTL_UI OPEN ACARS` opens the screen.

## Archive (saved blocks)

Every CRC-verified live block is appended to `/orcsdr/acars/YYYYMMDD.jsonl` on the SD card (UTC day; `undated.jsonl` when the clock
is not valid). One flat JSON object per line, fixed key order: `v`, `utc`, `up`, `f`, `tail`, `flt`, `lbl`, `mode`, `ack`, `blk`,
`end`, `cor`, `fix`, `raw`, `n`, `x` (the text as hex, exact), `p` (a printable preview for grep). Lines are buffered and written every
20 s, when the buffer fills, and when the screen is closed. At most 30 dated files are kept (the oldest are deleted). Nothing is
uploaded anywhere. Only CRC-verified blocks are saved. DEMO data never reaches the log.

The ARCHIVE tab browses the newest 300 saved blocks: at screen open the newest part of the log (up to 320 KB) is read back once per
boot, and new blocks are added as they arrive. It shows `SAVING TO SD`, `NO SD CARD` or `SD WRITE ERROR`. Without a card the tab still
holds this session's blocks. Tail and label filters work; flight, frequency and text search are not built yet. Retention counts
files, not bytes.

Checks: `acars_archive_tests` (format round trip, hostile bytes, truncated lines, store, logger, retention, failure recovery, history
load) on the host, and `RTL_ACARS ARCHIVETEST` on the device (writes and reads back 12 records in a scratch folder on the real card,
then deletes them; it passed on a Tab5 with an SD card). The real log gets its first real entries when an aircraft is decoded.

## Known limitations

- The live path exists (`acars_receiver.*`) and has decoded only two real blocks so far; otherwise the decoder has only seen a synthetic AM/MSK
  fixture. No sensitivity, false-block rate or gain behaviour is measured.
- One channel at a time; the selected channel is the only one decoded.
- UTC time is shown only if a producer supplies it; otherwise `+m:ss` since boot, labelled as such.
- History is RAM-only (48 messages) and lost at power-off.
- The aircraft view groups by decoded address only; it does not know ICAO ids, positions or flights beyond
  what the block carries.
- "Message number and flight id" inside block text are decoder-extracted; the UI does not interpret them.
- Font widths on the real display are not measured; long values are fitted with an ellipsis using the real
  `textWidth`, which has not been exercised on hardware.
