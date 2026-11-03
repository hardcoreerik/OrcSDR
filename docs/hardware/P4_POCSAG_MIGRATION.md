# Shared POCSAG responsive migration

The existing `pocsag_dashboard.cpp` renderer, decoder snapshots and actions are
reused. `pocsag_dashboard_layout.hpp` supplies safe-viewport rectangles for
painting, touch, keyboard focus, plots and paging. Legacy 1280 x 720 rendering
remains the default. Compact views cover Hosyond 800 x 480 and both LilyGO
safe rectangles.

## Views and retained data

- LIVE retains the newest message, scan progress, decode counters, measured
  soft-symbol plot and inbox. Discovery scanning is disabled when the adapter
  supplies zero candidates; the LilyGO adapter currently has no discovery scan.
- IDS and SESSION paginate all twelve retained records. Full message text and
  metadata have an independent detail pager. Selection survives viewport
  changes, and Session selection tracks the retained message across inserts.
- SIGNAL retains sync, baud, polarity, FSK deviation, batches, sync losses,
  total/valid/corrected/bad words, corrected bits and parity failures, plus
  measured soft symbols and recent FEC outcomes. FEC W/B means corrected
  codewords / corrected bits. Baud and polarity use shared painted hit regions.
- ACTIVITY retains message totals, identity count, valid percentage, bad words,
  message-type distribution and ranked CAPCODE hits. Its pager separates the
  metrics from rankings; every retained top-eight ranking remains reachable.

The P4 rotation callback retains the active view and selection without leaving
the dashboard or restarting the receiver. Compact focus regions use the same
rectangles, omit disabled controls, and retain focus during value updates.

## Redraw and memory

Per-widget signatures skip unchanged drawing. Data updates repaint only owned
cards/readouts, while view/paging/rotation transitions may establish a new
layout. No new framebuffer or plot canvas is allocated. A host compile-time
check bounds the additional layout/cache state to 2 KiB; on-device heap and
appearance acceptance remain separate.

The adapter also checks all allocations before placement construction, freeing
partial allocations on failure. This prevents a null placement construction
in its former allocation sequence; allocation-failure device injection has
not been performed.

## Named serial controls

Use normal serial authentication and open POCSAG first (`RTL_UI OPEN POCSAG`).
These requests are queued to the UI owner and execute the same handlers as
touch. An accepted queue emits `RTL_POCSAG_QUEUED`, followed by
`RTL_POCSAG_ACTION_OK` or `RTL_POCSAG_ACTION_ERROR` after context validation.

| Command | Meaning |
| --- | --- |
| `RTL_POCSAG VIEW 0..4` | LIVE, IDS, SIGNAL, ACTIVITY, SESSION |
| `RTL_POCSAG NEXT` / `PREVIOUS` | List or Activity page |
| `RTL_POCSAG DETAIL_NEXT` / `DETAIL_PREVIOUS` | Full ID/message detail page |
| `RTL_POCSAG SELECT 0..11` | Select an absolute retained ID/message index |
| `RTL_POCSAG BAUD 0` / `512` / `1200` / `2400` | Set baud in SIGNAL; zero is automatic |
| `RTL_POCSAG POLARITY 0..2` | Automatic, normal, inverted, in SIGNAL |
| `RTL_POCSAG SCAN` | Existing scan action only when candidates are supported |

Invalid arguments and inactive/busy requests are rejected. These P4 commands
do not establish app-wide command parity on other targets.

## Software verification

`python -m pytest tests/test_pocsag_dashboard_layout.py tests/test_p4_layout_contracts.py -q`
passed ten tests before the final command/focus additions. The focused POCSAG
runner passed again after those additions. It compiles the production renderer,
real focus engine and input against a recording display; checks final-record
selection, full detail text, rotation state, named controls, focus retention,
large corrected-bit readout and bounded updates. Geometry checks retain the
legacy sentinel and exercise Hosyond and both LilyGO orientations.

Physical captures, receiver continuity, memory observations and RF decode
acceptance must be recorded separately. Host-seeded messages are test data,
not evidence of received pager traffic. Private device snapshots must never
be packaged or published.

## First device pass

r15d built and flashed with the app hash verified. Twelve native PNGs cover
all five views and Activity rankings in both LilyGO orientations. The compact
Signal readouts and independent list/detail pagers fit in those captured
states. An apparent missing header in the image preview was rejected as a
firmware defect: decoded portrait header bytes were identical between LIVE
and IDS captures.

Named view, baud and polarity requests acknowledged on-device; baud/polarity
were restored to automatic. Manual rotation retained POCSAG. Runtime status
showed the receiver streaming about 0.94 MSps at 152.0075 MHz while Wi-Fi stayed
connected. Returning Home restored 96.1 MHz and about 2.4 MSps. POCSAG had
28,773,652 bytes free PSRAM and 97,911 bytes internal heap; Home afterward had
29,080,908 and 106,311 respectively. These are one active/exit pair, not a leak
or long-duration stress test.

The device had no decoded pager messages. RF decoding, populated hardware
views, physical keyboard behavior and human readability/touch acceptance
remain unverified. The limited Grok review attempt exited without a parseable
verdict and is incomplete. A screenshot-driven follow-up removes the empty
plot center line through NO SAMPLES YET and labels the Activity pager; its
latest build/device checks must be recorded after completion.

## Latest installed checkpoint: r15f

The final app build passed and the COM32 app-only flash hash verified. r15e
captures cover all five views plus Activity rankings in both orientations;
r15f recaptures LIVE after correcting its receiving-state cache key. A
recording regression covers a receiver-start transition with otherwise
unchanged empty-message data. The focused POCSAG runner passed both tests
after that correction, including a production legacy-renderer/control pass.

The latest native Live PNGs show RECEIVER ACTIVE and no center line crossing
the empty-plot message. Invalid VIEW 99 is rejected by the serial parser;
selecting an absent record returns an action error. Runtime status reports
streaming at 960,127 samples/s at 152.0075 MHz, connected Wi-Fi, 28,772,620 bytes
free PSRAM, 97,911 bytes internal heap and 58,323 bytes free DMA-capable heap.
Auto-rotate is ON and POCSAG is left open for human acceptance.

The user accepted the five tabs in portrait and landscape: "Fits well and
touch works." This closes sampled physical layout/readability/touch acceptance
for this installed checkpoint. RF decode, populated device views,
physical keyboard behavior, overlay interactions and sustained stress remain
open; this checkpoint does not complete the full LilyGO platform goal.
