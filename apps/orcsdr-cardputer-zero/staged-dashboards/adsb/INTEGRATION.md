# Cardputer Zero ADS-B coordinator integration contract

## Scope and wiring

**Selector ID:** `adsb`  **Title:** `ADS-B`  **Target:** native Linux 320 × 170 LCD.

Staged library API: [`adsb_module.hpp`](adsb_module.hpp), [`adsb_renderer.hpp`](adsb_renderer.hpp). Existing code used, not forked:

- `apps/orcsdr-tab5/ui/adsb_decoder.hpp` and `.cpp` (shared native CRC/DF17+DF11/CPR decoder)
- `apps/orcsdr-cardputer-zero/src/iq_queue.hpp` (`orc::linux_sdr::IqMetadata`)

The standalone CMake compiles the existing shared decoder as a source. **When integrated into a target that already links that decoder, compile/link only `adsb_module.cpp` and `adsb_renderer.cpp` in the app to avoid duplicate definitions.** Do not fork decoder code, change top-level CMake or overwrite the coordinator's FM/Home/palette work.

### Required host-side calls

```cpp
using namespace orc::zero::adsb;
Controller adsb_controller;          // decoder/consumer thread owner
Renderer adsb_pixels;               // UI thread owner
const RadioRequest r = adsb_controller.radio_request();
// Host: acquire sole physical receiver, tune to r.center_hz (1090000000),
// set r.rate_sps (2048000), verify real readbacks and bump generation.
adsb_controller.activate(generation, monotonic_now_ns);
adsb_controller.receiver_ready(monotonic_now_ns);
// Decoder/consumer thread for each queued, valid CU8 block:
const Result rc = adsb_controller.consume_cu8(block.cu8.data(),
                                                block.cu8.size(), block.meta);
// Push a thread-safe bounded snapshot via the existing app-owned UI dispatch seam.
Snapshot s = adsb_controller.snapshot(monotonic_now_ns);
// UI thread (NEVER called inside receiver/decoder callback):
adsb_pixels.render(s, host_palette_index);
// Host blits only updated regions or diffs of adsb_pixels.pixels() to LCD.
// On leaving selector: stop consumer, flush queued old-generation work,
// release SINGLE shared receiver ownership; call adsb_controller.stop().
```

**No other code acquires or opens the SDR in this module.** In particular, this stage does not create a competing `RtlReceiver` object. If consumer thread or lifetime differs from the coordinator, enforce the contract by dispatching actions there instead of sharing a mutable `Controller` across threads.

### Input/state/ownership

The app holds `Space` (selector), `P` (palette), `Tab` (menu), `Q` (exit), `G` (RF gain) and global controls. The dashboard only receives **up/down/enter/Escape** mapped to `Controller::navigate(Action::up/down/open_detail/back)`. If Escape in list does not close a detail, the host handles navigation; `Action::request_gain` is a symbolic host request, not an internal gain setter. No volume/ALSA call, fake speaker indicator, or AF playback in ADS-B. RF gain changes must be implemented and validated in the central receiver seam and should create a new generation where required.

`Controller` owns decoder state, active `generation`, last IQ index and monotonic receive time. `IqMetadata.frequency_hz` is **actual RF center**, `sample_rate_sps` the **actual complex rate**, `receiver_generation` a lifecycle gate, `first_sample_index` a **source-relative sample index** (not an RF hardware timestamp). `dropped_samples_before` only reflects **known app queue** loss; true upstream USB losses remain unknown.

`Controller::consume_cu8` returns typed results: `accepted`, `invalid_data`, `invalid_radio`, `inactive`, `old_generation`, `invalid_time`. Invalid radio includes both wrong center and wrong rate; no speculative 2.56→2.048 conversion is performed. Any `discontinuity`, known queue loss or noncontiguous sample index resets the native decoder and invalidates CPR/history. On a new `generation`, the coordinator calls `activate` and drops old queued samples. Disconnection/error/receiver loss suspends capture and invalidates CPR; explicit `stop` clears the table and exits. There is **no reconnect loop**. The host handles reconnect/errors, and must reflect connection status independently of presence/absence of aircraft.

`Model::apply_verified()` accepts **only the existing decoder's CRC-valid callback output**. It is public for headless model testing and deterministic previews, not a network/API injection surface. Do not feed it decoded text logs or unverified Mode S candidates.

### UI and timing

- UI calls `snapshot(monotonic_now_ns)` periodically (e.g. 1 Hz for age labels), not in the RF callback. Aircraft expire after 90 s; status is stale after 10 s without a valid frame following earlier reception. No receiver / connecting / waiting / stale / disconnected / error / DEMO are distinct.
- `Renderer` owns RGB565 framebuffer `320*170*2 = 108800` bytes and caches previous `Snapshot`. Initial render clears once; further data revisions clear only widget/row/detail rectangles. Host must implement its **existing LCD dirty-rect or framebuffer-diff blitter**; `full_clears` tests alone do not establish hardware flicker behavior.
- Colors are OrcSDR green/cyan on dark background with amber error/pending. The selected host palette is accepted, but **there is deliberately no RF plot** to palette-map in this first ADS-B dashboard. Do not create a second settings or palette system.
- Detail shows only valid altitude ft / ground speed kt / heading degrees / vertical rate fpm / relative signal / validated global CPR latitude-longitude. No calculated distance/bearing/GPS reference. Unknown fields are `--`.
- Active table is 32 entries, insertion-order stable, deterministic eviction of oldest receive-time (ICAO tie-break), 6 visible rows. Per-session unique-ICAO ledger is capped at 256 and marks `unique_capped` if it reaches capacity; the UI must show `>=256` if that cap is reached rather than falsely implying an exact count. The host should not add total frames, unique ICAOs and current active ICAOs together; they are different counters.

### Global changes required before physical operation

1. **Selector/registry and host `gui_main.cpp`:** add `adsb`/`ADS-B`, shared radio request (1090 MHz, 2.048 MS/s), activation/generation/ready, CU8 handoff, typed errors, and UI actions. Include staged `.cpp` sources in the existing native target.
2. **Single radio dispatch:** switch central receiver on selector change rather than creating a second tuner; verify applied rate and center in metadata, generation and continuity. If overview/FM runs at 2.56 MS/s, **retune/reconfigure to 2.048 MS/s** for this dashboard or add a separately tested anti-aliased, metadata-correct host resampler. No silent rate relabeling.
3. **Physical display:** use host framebuffer differential blit; verify 320×170 legibility, no flicker, and the host's palette/selector overlays and global keyboard ownership. Retain FM settings unchanged.
4. **Lifecycle:** terminate streaming/flush queue on error or exit; prevent stale generation packets from entering ADS-B after navigation away.

There is **no required change to the shared ADS-B decoder itself** for this 2.048 MS/s input. Optional later work: a shared channel resampler that produces correct 2.048 MS/s IQ with validated timing and anti-aliasing; a bounded preview of the real relative-signal history if the host supplies dirty-rect blitting and budget. Neither is needed to land this first staged module.
