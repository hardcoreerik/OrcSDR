# D1-H read-only integration contract — future work, NOT wired

## Source and ownership

1. The existing OrcSDR receiver/session owner chooses local USB or rtl_tcp,
   manages rate, retunes, generations and stop/reconnect. No probe method may
   call `esp_rtl_sdr_*`, USB Host, `rtl_tcp_*`, SD APIs, P25, audio or M5GFX.
2. The existing `rtl_dsp_task` remains the sole consumer of the current CU8
   `rtl_filled_q` ring. There is **no second RTL owner or reading of driver
   borrowed buffers after callback return**.
3. A future approved channelizer on that same decoder-dispatch path creates a
   **centered, anti-aliased, continuous 48k complex CF32** channel no wider
   than the selected DMR 12.5 kHz RF lane. Implement/qualify channelizer
   ownership independently; `p25core::process_channel_iq()` and experimental
   240k DSP lab are not safe substitutes.
4. Opt-in must be `OFF` by default. Producer passes only `const ComplexSample*`
   plus count, exact rate and the receiver's monotonically changing 32-bit
   **generation**; never passes a mutable global pointer to the worker.

## Memory / handoff

- `Attachment<d1::ComplexSample,d1::integration::Receiver>` keeps the real
  frontend, D1 decoder, timestamp map and **8 x 240** copied IQ blocks.
  The only producer operation is `offer(ptr, count, 48000, epoch)`;
  atomic 32-bit queue indexes use release/acquire publication. Caller memory
  can be immediately recycled. `std::atomic<uint32_t>` must be lock-free on
  the selected toolchain/target; compiler will fail otherwise.
- `offer()` is nonblocking, no allocation, no RTOS operations, no callback,
  no active receiver processing; accepts 1..960 samples as an all-or-nothing
  group (up to four 240-sample frames). **Never pass arbitrary-size input**:
  split source-channel output into <=960 chunks on the owned DSP path.
- If **any** offer is rejected due to full queue, entire offer is counted
  dropped, its sequence numbers are skipped and the worker resets decoder
  continuity before the next accepted block. Wrong rates and invalid inputs
  likewise invalidate continuity. For upstream losses not visible to `offer`,
  the sole producer **must** call `note_gap(missing_complex_samples)`; do not
  supply zeros to fill holes or silently resume.
- When the receiver is **retuned, changed source, stopped, restarted, or
  the channelizer resynchronizes**, increase `epoch` **before** publishing
  the first sample from the new stream. Only the worker invokes
  `Receiver::discontinuity()` when epoch or sequence changes, so no worker/
  producer concurrent mutation of DMR DSP state. Reset of an idle stream
  will occur on the next accepted block (there are no events in between).
- Exactly one worker invokes `drain_one()` and retrieves only its own
  `Receiver::stats()` / `frontend_stats()`. Data lifetime ends at `release()`.
  Synchronous event callbacks, if later enabled by a new adapter, may only
  increment bounded counters or copy into a separate bounded queue; no
  logging/network/SD/tuner/P25 updates from that callback.
- **No unbounded retries or growing queues.** Queue: 8 blocks, 40 ms and
  15,360 bytes of IQ payload. One 960-sample offer fits half the queue.
  Under overload, drop and reset rather than delaying USB/spectrum/audio.
  The default PSRAM placement is a hypothesis requiring device verification:
  cross-core atomic/cache behavior, worst-case memcpy, and DSP speed must be
  measured on the actual Tab5.

## Evidence, timing and qualification

- Only `integrity_valid` + D1 `Evidence::identity_valid` may be promoted to
  a decoded radio/talkgroup identity. Correlation, CACH TACT, FEC and sync
  alone are weaker evidence. D1-S CACH cadence and D1-P measured pre-roll
  protections remain unchanged.
- The target workload is **48,000 complex IQ/s**. An 8-byte CF32 complex
  sample costs 384,000 payload bytes/s to the queue, excluding channelizer
  memory traffic. A 240-sample block represents 5 ms. The worker should
  complete a block comfortably below 5 ms, including jitter and competing
  USB/DSP/UI activity; measure worker max and distribution (p99 bucket),
  contiguous loss and free memory on ESP32-P4, not only host averages.
- `sizeof(Frontend)`, `sizeof(Decoder)`, `sizeof(Receiver)`, queue and full
  attachment are reported separately. The frontend 1,024 float history is
  included in `sizeof(Frontend)`; the bridge includes its 256-entry
  timestamp map. `sizeof(Receiver)-sizeof(Frontend)-sizeof(Decoder)` is
  a **rough bridge state difference**, including padding. The protocol/FEC
  and frontend CPU cost cannot be isolated accurately without separate
  instrumented phases **inside** the actual code, which this owned folder
  must not change. On-device timing reported is combined D1 receiver cost.
- Synthetic 4FSK is a bounded workload generator, **not** live 4FSK RF or a
  statistically valid RF decoder acceptance corpus. Real-air capture
  validation and live long-duration RSSI/USB bandwidth checks remain open.
- A definitive non-interference gate must monitor `rtl_iq_pipeline_drops`,
  `rtl_iq_pipeline_drop_samples`, USB URB overruns, live audio latency,
  per-dashboard update cadence, tuner generation and P25 profile hashes,
  and SD paths **before/after** any expressly approved future integration.
  This detached probe cannot itself verify live-USB non-interference.

## Integration pseudocode (not production code)

```cpp
// existing radio service task, only if opted in and 48k channelizer exists:
// channelized IQ is immutable and owned by the current radio service.
if (opt_in && channelized_rate_hz == 48000) {
  for (size_t at = 0; at < count; at += 960) {
    const size_t n = std::min<size_t>(960, count - at);
    // Bounded copy; on full count/drop, NEVER block the source task.
    (void)probe.offer(iq + at, n, 48000, owner_generation);
  }
}
// All upstream skipped IQ is reported by probe.note_gap(lost_samples).
// Low-priority worker alone calls while (probe.drain_one()) {}.
```

No code in this folder exports a hook into the installed Tab5 application.
`apps/orcsdr-tab5/main/CMakeLists.txt`, central dispatcher, driver, firmware,
P25 and microSD files are intentionally unmodified.
