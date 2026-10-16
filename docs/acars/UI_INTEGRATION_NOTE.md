# What the decoder owner must provide to activate LIVE reception

Audience: whoever owns `acars_core.*` and the receiver runtime. The UI is complete without any of this and
changes nothing in the decoder. This note is the original seam description; `acars_receiver.*` now implements it for the
RTL-SDR (see `DASHBOARD_README.md`, "Live reception"). Without a receiver LIVE shows `RECEIVER FEED NOT CONNECTED`.

## The seam

```cpp
acars_ui::Model* m = orcsdr::acars_app::model();   // null until the dashboard has been opened once
m->post_message(const acars::Message&, uint32_t uptime_ms, uint32_t utc_epoch);  // 0 = clock not verified
m->post_stats(const acars::Stats&);
m->post_receiver_state(acars_ui::ReceiverState);
m->post_capabilities(acars_ui::Capabilities);
m->report_tuned(uint32_t hz, bool ok);              // UI task only, see below
```

The four `post_*` calls are lock-free and bounded (single producer): a decoder task or callback may call them.
They never touch M5GFX, SD or the screen. If the 16-event queue is full the message is dropped and counted
(`UI QUEUE DROPS` on ACTIVITY), never blocked on.

## Checklist

1. **Own the receiver.** Use the existing RTL-SDR pipeline (no second driver). Run `Frontend2400k` /
   `Decoder` outside the UI task and outside the IQ callback's hot path where possible.
2. **Callback to queue.** In the `MessageCallback`, call `post_message` with `Message::frequency_hz` set
   (`Decoder::set_frequency`), `uptime_ms` from `millis()`, and `utc_epoch` from the verified clock or 0.
   Only CRC-verified blocks reach the callback, which is what the UI labels `CRC OK` / `FIX n`.
3. **Publish stats about once a second** with `post_stats(decoder.stats())`. Counters are shown as given.
4. **Publish receiver state** (`running`, `paused`, `unavailable`, `error`; `disconnected` is the default).
5. **Advertise capabilities honestly.** Set `live_reception` when real reception works; leave
   `signal_measurements`, `multi_channel`, `archive`, `application_decoding`, `message_reassembly` and
   `adsb_correlation` false until they exist. `live_reception` flips the tune status from "unavailable" to
   "pending".
6. **Tuning.** Set `acars_app::Hooks::request_tune` (in `main.cpp`'s `acars_app_hooks()`) to retune the radio
   to the requested Hz. After the radio actually moves, call `report_tuned(hz, true)` from the UI task (or
   `false` on failure). The UI shows TUNED only after that call.
7. **Not in the UI task:** do not call `acars_dashboard`, `M5.Display`, or SD from the decoder side.

## Not provided by the UI, to be added when they exist

SNR / RSSI / lock / timing (the ACTIVITY diagnostics card is reserved), a channel-activity measure
(`ChannelRecord::activity_measured`), multiple channels (`Capabilities::multi_channel`; presets already model
one request per row), an archive provider (ARCHIVE is wired to nothing; adding one means implementing the
`archive_*` query in the model against the SD store), message reassembly using `Message::end_marker`
(ETB / ETX are preserved and displayed), application parsing (`MessageRecord::interpretation` is shown only
when `has_interpretation` is set), and ADS-B correlation.

If any of this needs a decoder API change, report the proposal first; an adapter or optional field is
preferred to changing `acars_core.*`.
