# Coordinator handoff: ACARS dashboard

**Selector**: `acars` / `ACARS`. Owned folder only; do not assume the
coordinator's unpushed Home/FM, Space selector, P palette, 2.56 MS/s FM overview,
chrome or framebuffer screenshot updates are in this staged tree.

## Required host wiring (not performed here)

1. In `apps/orcsdr-cardputer-zero/CMakeLists.txt`, integrate this module library
   and link the production GUI target as appropriate, without building duplicate
   `acars_core.cpp` symbols if the app already links `orc_native_decoders`.
   The standalone CMake compiles `acars_core.cpp` directly *only for its own
   independent test binary*. `acars_selftest.cpp` is test-only, never part
   of a second runtime receiver.
2. In `apps/orcsdr-cardputer-zero/src/gui_main.cpp` (or the coordinator's newer
   selector/registry), register `acars` / `ACARS` and display the 320×170
   `Renderer` surface. Space remains dashboard selection and P remains shared
   palette cycling. Preserve all coordinator's unpushed UI work.
3. In the existing physical receiver owner (`src/fm_session.*` and/or its
   successor), **pause/stop the FM worker and drain its queued data before**
   activating the ACARS worker. Reuse the one `RtlReceiver` instance and
   `IqMetadata` semantics from `src/iq_queue.hpp`; do not start a competing
   librtlsdr session. Under host control, tune the requested center and rate,
   **read back** both values, then activate the decoder only if they match.
4. Native evdev / SDL keyboard translation remains owned by the host; hand
   Up/Down/Enter/Esc through `handle_compact_key`, and route `F` to the shared
   frequency editor; its validated result feeds `make_radio_request`. Use a new
   monotonic receiver generation on EVERY retune, reconnect or activation.
5. UI owner calls `service`, gets `Snapshot`, then calls `render(snapshot,
   steady_clock_monotonic_ns)`. Copy **only** `dirty_rects()[0..dirty_count())`
   to LCD, using coordinator's display DMA/ownership contract. A new revision
   is not permission to clear the whole LCD. Any background input/decoder
   service must never draw over a modal. Test partial LCD flush on real unit.

The host must apply real `gain_auto` / `gain_tenth_db` using existing radio
controls, then report confirmed state via `set_gain`. This module does not
set hardware gain. No audio stream is requested or produced; shared S/D/M
controls keep their semantics for other modes but are not used as fake ACARS
volume. Because ACARS is text-only, there is **no waterfall palette in use**;
P cycles the host's single global palette as before, with no second settings
file or competing palette owner.

## Exact calls

```cpp
using namespace orc::zero::acars_dashboard;
Dashboard acars;                  // persistent per app; do not copy
Renderer pixels;                  // on UI task; ~110 KiB framebuffer
RadioRequest req;
if (!make_radio_request(131550000, req)) { /* reject operator entry */ }
// On UI thread before asynchronous tune:
acars.receiver_connecting();
// On sole decoder/receiver worker after host verified tune and rate readback:
// new_generation must be > every generation previously used by this Dashboard
if (!acars.start(req, new_generation)) { /* expose error */ }
// For each host-owned, bounded CU8 block (SINGLE producer):
// meta.frequency_hz == req.center_hz; meta.sample_rate_sps == 240000;
// meta.receiver_generation == new_generation; meta.first_sample_index is
// the source-relative COMPLEX sample index; meta.monotonic_ns is observed time.
if (!acars.consume(bytes, byte_count, meta)) { /* stop current receiver session */ }
// On unplug / stop, decoder worker:
acars.receiver_failed();  // or acars.stop() for expected exit
// On UI owner on a regular paint tick:
acars.service();
const Snapshot snap=acars.snapshot();
pixels.render(snap, steady_clock_ns);
for (size_t i=0; i<pixels.dirty_count(); ++i) {
  const Rect r=pixels.dirty_rects()[i];
  // flush the matching RGB565 region; host device adapter supplies this
}
// On UI input:
acars.handle_compact_key(Key::down);
acars.handle_compact_key(Key::enter);
acars.handle_compact_key(Key::escape);
```

`RadioRequest` reports station/channel **131,550,000 Hz**, tuner **131,490,000
Hz**, sample rate **240,000 complex samples/s**. The existing `Frontend240k`
uses fixed +60,000 Hz mixing, 129 FIR taps and /10 decimation; the existing
`Decoder` receives **24,000 complex samples/s**. Never send the 2.56 MS/s FM
overview into this path or relabel CU8 metadata. Host must switch RX rate when
switching between FM and ACARS, or implement an independently proven bounded
channelization upstream with correct metadata in a separate reviewed change.

### Lifecycle, thread, and safety

- Decoder worker: `start(req,generation)`, `consume(cu8,n,meta)`, `stop()` or
  `receiver_failed()`. These calls must be serialized with each other. They
  never acquire sound or USB handles. `consume` rejects null, odd, empty,
  oversize, unsupported rate, center mismatch, old generation, and sample
  overflow; false is fatal for current worker activation.
- UI task: `receiver_connecting()`, `service()`, `snapshot()`, `input()` /
  `handle_compact_key()`, `set_gain()`, `Renderer::render()`. The decoder callback
  writes a 16-slot single-producer/single-consumer queue; only UI mutates
  history/selection. The queue drops **newest** when full and counts loss.
  Coordinator must avoid concurrently destroying the object or switching
  live/demo modes while a decoder call is active.
- On retune/gap, `Frontend240k::reset` and `Decoder::reset_stream` together
  discard partial RF state, not received history. Unknown upstream USB loss is
  not invented. Observed queue drops are counted separately; discontinuities
  are a reset event. A stale decoded event cannot be reattached to a newer
  generation or wrong channel. Reconnect requires explicit host action; there
  is no spinning reconnect loop.
- ESC closes ACARS detail (input remains ACARS-owned until then). Shared key
  actions are excluded from the ACARS key adapter and remain coordinator-owned.
  On dashboard exit, stop consumption and radio/audio work, drain or discard
  pending events, and restore the single receiver to its normal owner.
- `RadioRequest` is the **complete** ACARS-specific persisted configuration.
  The host can serialize the selected channel as a uint32_t Hz and validate it
  using `make_radio_request` when restoring. No second NVS/JSON/settings layer.

## Acceptance left to the coordinator

Required common files are the selector/GUI, existing radio owner, top-level
linking and LCD dirty-flush adapter. No canonical decoder modification is
requested by this module. Verify on real Cardputer with a VHF antenna and
actual traffic, accurate signal/rate readback, antenna gain, continuity under
switching, physical 320×170 readability, UI ownership, partial flush/flicker,
perf/CPU/heap/stack, and message provenance. Host compile success alone is not
physical acceptance. Preserve the coordinator's palette and FM state.
