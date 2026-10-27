# S02 — OrcNode Network

> **Category:** SYSTEM · **Dependency:** P5 after independently authenticated OrcNode adapter is qualified · **Status:** planned; capabilities and integrations below are not automatically present in OrcSDR.  
> Read [mandatory UI rules](../../UI_UX_STANDARD.md), [navigation contract](../../NAVIGATION.md), [data validity](../../DATA_CONTRACTS.md), [draw/test workflow](../../RENDERING_AND_ACCEPTANCE.md).

## Function and user benefit

Display connected and authorized remote OrcNode receivers as optional additional listening stations—not a magic area-wide drone detection network. Show each node's capture methods, observed last packet, authenticated connection and measured coverage intervals.

## How to use it

1. Open SYSTEM -> ORCNODE; view locally paired/authorized nodes and their last heartbeat.
2. Tap a node to inspect exact supported methods, connection age, source clock quality, last received event and synchronization limitations.
3. Reconnect or remove a node through confirmed actions. Tap VIEW EVENTS to filter Timeline by that node's source_id.

## 1280 × 720 display layout

Reference shell: shared OrcSDR header y0–113 owned by `dashboard_audio_control`; 52–60px in-category selector y118–178; content x20–1260/y188–634; bottom five-category tabs y649–705. The following rectangles are **suggested layout zones**, not duplicate hard-coded offsets per page.

- x20–845 list: node name/ID, CONNECTED/STALE/OFFLINE status, supported listening methods, last heartbeat and last actual observation.
- x865–1260 inspector: trust/pairing state, network interface, firmware, source clock skew qualification and observed vs missing coverage.
- Bottom: ADD/PAIR only if secure flow exists, RECONNECT, FORGET confirm and VIEW EVENTS; no free-form remote shell/control panel.

Keep the selected state and any saving/error banner legible on the real 5-inch Tab5; 52px minimum touch heights and descriptive button text. OrcSDR palette: black, dark panel, cyan focus, green measured-active, amber paused/uncertain, muted unavailable. All generic system pages must still clearly say what scanning is actually doing.

## Truthful fields and service dependencies

- Authenticated local node identity and connection state, node firmware/hardware version, advertised capability list with actual qualification.
- Clock provenance: local arrival monotonic, remote transmitted timestamp with uncertainty; cannot assume nodes are synchronized.
- Node source-specific event counters, dropped/gap intervals and origin, no mandatory upload of operator coordinates.

No mere presence of ESP32-C6 or an RTL-SDR dongle establishes Remote ID capability. No `SCANNING` without actual verified source callback health. `TEST DATA` must remain visible in replay on every related screen. Time and source identity are always explicit; avoid unsupported '0' figures and confidential device/network data in snapshots.

## Touch actions and navigation

- PAIR only through explicit user-approved authenticated link workflow; don't auto-trust LAN broadcast names.
- VIEW EVENTS -> W02 with node source_id; SOURCE DETAIL -> S01; REMOVE requires confirm and clearly forgets credentials/config only.
- RECONNECT is asynchronous and updates pending/failure; it must not stop Tab5 USB reception.

The screen is a client of lifecycle/worker interfaces; global header controls and overlays have priority. No in-page action may change C6 firmware, silently retune the RTL-SDR, reboot a device or start microphone/video recording. Every slow action paints pending feedback before work.

## No source / paused / stale / partial failure

- No paired nodes -> NO ORCNODES CONNECTED; basic local scanning may still work.
- Node reachable but data source inactive -> CONNECTED / NOT SCANNING; no green sensor.
- Clock drift or interrupted Wi-Fi -> TIME UNSYNCHRONIZED or OFFLINE with stale age, no interpolated observations.
- Unknown node -> NOT TRUSTED; never display its packets as authenticated without explicit pairing.

Every source or service that is unsupported remains informative; never create phantom data to fill empty lists.

## Builder how-to

1. Define versioned source envelope compatible with Data Contracts; pairing/authentication and transport lie in OrcNode adapter, not renderer.
2. Maintain bounded node roster and independent source_id namespaces; reconnect logic external to UI with explicit queue overflow reporting.
3. Incrementally update one node row and selected detail, no network calls in render.

One `screens::Id::drone_watch` framebuffer owner for all 25 screens, and common category/page router. Model/worker owns service interaction and source/progress snapshots; UI draws no network/SD/USB callbacks. Paint structural chrome once and dirty rectangles for changing values; never clear the whole page on simple progress updates or idle ticks.

### Adaptive 720 × 720

720-square: node roster with two rows and a dedicated NODE DETAIL screen; pairing opens a 52px-controls modal. Use the same ViewSnapshot and measure actual font/hit bounds.

## Screen-specific acceptance criteria

- Anonymous LAN packet cannot impersonate known paired node; remote-source IDs never silently merge with C6 local.
- Lost node and clock skew show correct event age and GAP without fabricated continuous listening.
- Pair/reconnect failures leave functional OrcSDR Home, Wi-Fi, and current RX unchanged; no full-screen update loops.

Add shared 1280×720/720×720 geometry tests, invalid/absent/replay tests, global Home/Settings/volume overlay priority, 100 changing-value ticks with unchanged full-screen-clear count, no inactive display writes, and actual physical Tab5/ESP-IDF/USB regression results before declaring hardware acceptance. The document alone does not implement the screen.
