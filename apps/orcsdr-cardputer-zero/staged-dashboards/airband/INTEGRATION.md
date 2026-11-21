# Cardputer Zero Airband AM — exact coordinator integration seam

## Selector and ownership

- Register selector ID **`airband`**, title **`Airband AM`**, in the app-owned registry. This staged directory MUST NOT edit the selector, `src/gui_main.cpp`, `src/compact/control_model.hpp`, or `src/fm_session.*`.
- Add `airband.cpp` and `airband_render.cpp` to the existing Linux GUI target only when the coordinator has reconciled its unpublished Home/FM UI work. `CMakeLists.txt` here is a standalone test project, not a replacement for the parent CMake.
- Only the host owns `orc::linux_sdr::RtlReceiver` and the actual `orc::zero::AudioSink`. **Stop the FM session and drain its queue before starting airband**; re-own only one USB receiver. Do not run two capture loops on one dongle.
- The `Controller` is **not thread-safe**: controller action, readback, consume, and snapshot must be serialized by the coordinator (e.g. a single worker with an action-command queue, whose result is copied into a GUI snapshot). `Renderer` must be used exclusively by the GUI/render thread, from **copied snapshots**. No radio calls, ALSA writes, or IQ decoding happen on the GUI thread.

## Activation → readback → consume → output → stop

```cpp
using namespace orc::zero::airband;
Controller am;
am.activate(kGuardHz);
Request rf = am.requested_radio(); // selected +125k, preferred 2,560,000 sps
// Host asks its ONE capture worker to tune/configure at rf.rf_center_hz/rf.sample_rate_sps.
// Only on successful receiver.frequency_hz()/sample_rate_sps() readback:
am.radio_applied(actual_center, actual_rate, ++receiver_generation);
// Host configures ONE AudioSink for 48 kHz S16 mono, then:
am.audio_available(audio_sink_opened_and_audible, error_text_or_null);
// DSP worker handles BoundedIqQueue::try_pop() and calls:
am.consume(block.cu8.data(), block.cu8.size(), block.meta, pcm_callback, ctx);
// pcm_callback forwards mono 48 kHz S16 to AudioSink::write_mono(samples,n,100,false)
// on the audio/DSP worker only. All user gating and gain are ALREADY in core PCM.
// UI acquires serialized copy:
Snapshot status = am.snapshot();
renderer.render(status, palette_256_rgb565_from_shared_owner);
// Blit renderer.dirty_rects()[0..dirty_count()) ONLY; palette change:
renderer.invalidate_palette();
```

The callback MAY receive short frames (≤256). Never let ALSA block USB reads: use `BoundedIqQueue` between capture and DSP; choose capacity/overflow accounting on the host. `Controller::consume` itself is allocation-free after construction and rejects zero/odd/oversized blocks (`kMaxIqBytes=32768`). It does not own or block on the queue. The callback must not enqueue unlimited data; drop old generation PCM when receiver changes.

**On retune:** `am.action(tune_up/down/guard)` or `am.tune_direct(hz)` makes requested center and `tune_pending` visible immediately. Do **not** mark applied until actual RF center and sample rate read back. Flush host IQ queue and call `AudioSink::discard()` before processing the new generation. `radio_applied()` must receive the new generation and exact center. On rejection call `radio_failed("...")`; old selected channel is restored but reception remains in ERROR until a fresh successful readback. Do not silently report rejected tune as successful.

**On receiver loss/disconnect:** stop producer, flush host IQ queue, discard pending ALSA frames, call `disconnected(reason)`, and release the sole receiver. On dashboard exit, flush/discard and call `stop()`; do not keep the decoder playing in the background. On audio open/write error, call `audio_available(false, sink.last_error().c_str())` and close/stop that sink. Don't retry ALSA on every audio callback. The tested coordinator-specific ALSA name was `plughw:ES8389Audio,0` rather than `default`; keep this as a host configuration setting, never a hardcoded card index or a claim of speaker acceptance.

### Source IQ metadata contract

Use the existing `orc::linux_sdr::IqMetadata` from `src/iq_queue.hpp`. `frequency_hz` is **applied RF tuner center**, not the desired voice frequency; `sample_rate_sps` is **actual** 2.56 or 2.4 MHz; `receiver_generation` must match `radio_applied`; `first_sample_index` is a monotonically increasing **source-relative sample index**, not an RF hardware timestamp. Set `discontinuity` for reset/retune/USB gaps. Known application-level queue drops can be published with `set_known_queue_drops()`, **not interpreted as total USB hardware loss**. The core rejects mismatched rate/center/generation. A sample-index gap resets FIR, CIC, oscillator, level, plot timing and unflushed PCM.

### Gain and settings

`am.action(gain_*)` changes the **requested** gain in the copied snapshot; the host must translate that into its validated `RtlReceiver::set_tuner_gain_auto()/set_tuner_gain_tenth_db()` capability steps. `set_gain_readback(actual_auto, actual_db10)` confirms the applied value and clears squelch learning. The independent model exposes requested gain values but **does not enumerate or validate the actual tuner gain table**; ensure hardware-specific checks in the adapter. On failure, report error and restore confirmed host gain state. No extra USB tuner is opened.

Persist module-specific `Settings {volume, squelch_db, gain_tenth_db, user_muted, gain_auto}` and the last selected Hz through the coordinator settings facility, with `settings()`/`restore_settings()` validation. **Do not modify FM state or global settings schema in this folder.** Keep `Space`, `P`, `Tab` and `Q` global; send other recognized keys through `keyboard_action()` or typed `Action` calls. Arrow events, generic menu/help overlays and selected palette stay owned by the host.

## Draw and palette

`Renderer::framebuffer()` is full 320×170 RGB565 scratch storage (~109 kB). The first render initializes chrome once; subsequent renders clear/draw ONLY changed header, frequency, status, or plot regions. `dirty_rects()` returns at most 6 rectangles; the host **must send only dirty rectangles to the framebuffer/LCD** if physical flicker is to be avoided. The passed palette pointer must represent the currently selected OrcSDR shared waterfall palette (256 RGB565 colors). If the same pointer is repainted in place after a global `P` palette cycle, call `invalidate_palette()`. When no palette is supplied, the preview uses grayscale intensity only.

The real FFT has 1,024 bins at the actual input rate. Plot columns show a 640 kHz **zoom**, centered on the selected carrier (−320..+320 kHz); the 10 kHz AM passband markers therefore occupy approximately 4 pixel-columns. There is no fake animated waterfall when disconnected. The DEMO label is set only by explicit `set_demo(true)`; production must leave it false for live input and never use generated IQ for a LIVE display.

## Required shared-file changes (COORDINATOR, not this commit)

1. `apps/orcsdr-cardputer-zero/src/gui_main.cpp`: add selector route, lifecycle transitions, typed action/entry and renderer dirty-blit path, preserving concurrent Home/FM edits.
2. The app-owned receiver/session dispatch (currently in `src/fm_session.cpp` and GUI code): make the **single** RtlReceiver producer selectable between FM and Airband consumers, rather than starting an independent long-running RTL receiver. Reuse its IQ queue, readback and generation scheme. A narrowly-scoped Airband session adapter belongs **outside** the staged delivery once main/FMs changes stabilize.
3. Parent app CMake/GUI display adapter: compile/link this staged module and pass the global palette; keep `src/linux_audio.cpp` as sole ALSA owner.

No source under these three paths was edited by the staged implementation. **Required hardware gates:** antenna + VHF reception, sustained 2.56 MHz USB and CPU/drop test, actual audio device selection and latency, physical keyboard behavior, LCD region-blit/no flicker, and safe exit/re-entry/USB reconnect.
