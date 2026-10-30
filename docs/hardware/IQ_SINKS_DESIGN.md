# IQ consumers in radio_service: registered-sinks design

Status: design only, 2026-10-29. No code written or built. P4-only: `radio_service.*` and its callers live in
`apps/orcsdr-p4-hosyond-console/main` and are not compiled into the Tab5 build, so this does not touch Tab5.

## Today (read from the code)

- `radio_service.cpp:181-183`: one receiver task (`"radio"`, core 1, priority 5, stack 8192) reads `kIqBytes = 16384` bytes of CU8
  from `esp_rtl_sdr_read` (40 ms timeout), then calls `g_sink(iq, n)` and `g_aux_sink(iq, n)` **inline**.
- `g_sink` is taken by `audio_service.cpp:198` (demodulates inline in the receiver task; its state is in PSRAM by measurement).
- `g_aux_sink` is taken by `iq_tap.cpp:63` (one global stream buffer plus one task), `screen_rflab.cpp:67`, `screen_acars.cpp:55`
  and `screen_lora.cpp:514`. Each sets it on entry and clears it with `nullptr` on exit.
- The callback type is `void(*)(const uint8_t*, size_t)`: no context pointer, no sample rate, no sequence number, no receiver index.
- Findings that matter for the design:
  1. A slow aux sink slows the read loop for everyone, and the audio path shares that loop.
  2. The sink pointers are plain globals. Clearing one does not wait for a call already running in the receiver task, so a consumer
     that frees its buffer right after clearing can race the receiver task. I have not seen this fail; it is a risk by inspection.
  3. `iq_tap` can serve only one consumer at a time (single global stream buffer). Only one aux user can run, which is why the browser
     channel cannot coexist with RF Lab, ACARS or LoRa.

## Goals (owner, 2026-10-29)

Keep browser IQ streaming and decoder functionality; not be stuck with two sinks; keep multi-receiver possible even if the first build
uses one dongle; reuse the existing radio service; leave Tab5 alone.

## Design

### Registry

A small fixed table (proposed 8 slots) inside `radio_service`:

```
struct IqBlock {               // what every consumer receives
  const uint8_t* data;         // CU8, valid only during the call
  size_t bytes;
  uint32_t sample_rate;        // S/s of this block
  uint32_t center_hz;
  uint32_t sequence;           // increments per block per receiver: consumers detect gaps
  uint64_t time_us;            // esp_timer_get_time() at read completion
  uint8_t  receiver;           // 0 now; index of the dongle later
};
using IqConsumer = void (*)(const IqBlock&, void* context);

enum class SinkKind { inline_fast, queued };
struct SinkSpec {
  const char* name;            // diagnostics ("audio", "rflab", "browser-ch0")
  IqConsumer fn;               // inline_fast only
  void* context;
  SinkKind kind;
  uint8_t receiver;            // which dongle to subscribe to (0 until multi-receiver)
  size_t queue_bytes;          // queued only; buffer lives in PSRAM
};
SinkHandle add_sink(const SinkSpec&);       // returns invalid handle when the table is full
void remove_sink(SinkHandle);               // returns only after no call to this sink is running
bool sink_stats(SinkHandle, SinkStats*);    // blocks, bytes, drops, longest inline call (us)
```

- **Dispatch** walks the occupied slots after each read. The slot table is read without a lock: each slot has a state word
  (`free`, `active`, `removing`) and the receiver task publishes a per-block epoch counter. `remove_sink` marks the slot `removing`,
  then waits until the epoch has advanced twice (bounded, 100 ms), so no call is in flight and the caller may free its context.
- **`inline_fast`** sinks run in the receiver task. Allowed only where the work is a few microseconds or already proven (audio
  demodulation). The registry times each call with `esp_timer` and keeps the longest as a stat, so a slow sink shows itself.
- **`queued`** sinks (the default for everything else) get a PSRAM stream buffer owned by the registry. Dispatch does a non-blocking
  send of the block; if the buffer is full it drops that block for that sink only, counts it, and carries on. The consumer pulls from
  its own task at its own pace. This is `iq_tap`'s pattern, one buffer per sink instead of one global. A slow browser connection
  can then cost only its own blocks, not the receiver or the audio.

### Compatibility

`set_iq_sink` and `set_aux_iq_sink` stay as thin wrappers that register or remove slots named `"primary"` and `"aux"`, with the old
bare-pointer signature adapted. Every existing caller (`audio_service`, `iq_tap`, RF Lab, ACARS, LoRa) compiles and behaves as before
without edits. They move to the new API one at a time later, each with its own test.

### What the registry does not do

- **No channelizing or decimation.** Sinks get the raw CU8 block. The browser's 240/300 kS/s channel, the POCSAG 960 kS/s feed and
  so on are consumer-side work (the other branch's `channel_stream` and `ssb_demod` slot in as consumers).
- **No rate arbitration.** Today `set_sample_rate_override` (RF Lab) and the Home span policy fight by "last writer". A request-by-handle
  rule (an exclusive holder wins, otherwise the highest rate any sink asks for) is a follow-up and needs its own decision, because a
  sink that needs 960 kS/s and one that needs 2.4 MS/s cannot both be satisfied by one stream. The block header's `sample_rate` lets
  a consumer see what it actually got.
- **No cross-dongle mixing.** `receiver` selects which instance's blocks a sink sees; with one dongle it is always 0.

### Multi-receiver

Multi-receiver capability is kept by putting `receiver` in `IqBlock` and `SinkSpec` now, and by making the registry a member of a
future per-dongle `Receiver` object. The first integrated build instantiates one. Nothing in the registry assumes one dongle.

### Multi-dongle: reuse the proven OrcNode manager (owner, 2026-10-29)

The multi-dongle code already exists and is validated, so the per-dongle layer is lifted from it, not designed again. Read from
`OrcSDR-Headless` `main/receiver_manager.cpp/.hpp` (tip 67e535e) and `docs/validation/FOUR_DONGLE_2560K.md`:

- Four receivers (`ORCNODE_MAX_RECEIVERS 4`), one driver handle, drain task and state per slot, with per-slot want/retune/stall-restart
  bookkeeping and hot-plug polling.
- Stable physical identity by USB hub port (`usb_path`), because VID/PID/serial are not unique across identical dongles and the USB
  address moves on replug.
- Validation on COM18 (Waveshare ESP32-P4-WIFI6): four dongles at 2,560,000 S/s for 120 s, zero drops and USB errors, no reboot
  (the document itself says it is not a long soak or a sensitivity test). Driver `a5dc9d88`.
- Configuration that run depended on: 2 x 16384-byte internal USB DMA transfers per receiver, USB client cores 0,1,0,1, a 256 KiB
  pull ring per receiver in PSRAM.

How it fits here: `radio_service` becomes a set of up to four receiver instances built on that manager's open/claim/recovery code.
Each instance's drain task feeds its own sink table; the console's tune/gain/rate calls take a receiver index (0 by default).
What must be checked before it is trusted in the console:

1. **Internal DMA memory.** Four receivers need about 128 KiB of internal DMA buffers (2 x 16 KiB x 4). COM8 with the full console
   and Wi-Fi up reports `dma_free=126,671` B, largest block 94,208 B (measured 2026-10-29). One dongle fits easily; four may not fit
   beside the console's other internal-RAM use. Measure with the console running before promising four.
2. **Driver read path.** The manager uses the driver's event/pull-ring delivery; `radio_service` reads with `esp_rtl_sdr_read`. The merged
   per-dongle layer must keep one of them, and the one chosen is proven on four dongles (the manager's), so it should win.
3. **Driver pin.** The proof used `a5dc9d88`; this console and the Tab5 use `31a159df` (both v0.9.3, documentation-only difference
   per their notes, not diffed by me). Same caveat as before: no pin change touches Tab5 without approval and a test.
4. **Core and priority layout.** The manager splits USB clients across both cores; the console's receiver, audio and UI tasks use
   core 1. The two must be reconciled with `RTL_CPU` measurements.

## Implementation status (branch `claude/p4-multi-dongle`, 2026-10-29)

Implemented in `apps/orcsdr-p4-hosyond-console` (P4-only; no Tab5 file touched). **Builds only; not flashed, not run on any board.**

- `radio_service.cpp` state is now one `Rx` object per receiver (`CONFIG_ORCSDR_MAX_RECEIVERS`, 1 to 4, default 1), one task per
  receiver (`radio`, `radio1`, ...), each with its own driver handle, tune/gain/rate/bias requests, spectrum bins and sink table.
- Receiver 0's loop is the previous code unchanged in behaviour. Receiver k installs with `host_library_already_installed = true`
  once receiver 0 has installed, starts only when more than k dongles are attached, and claims through a mutex. The driver's
  `ESP_RTL_SDR_BIND_ANY` hands each handle the first USB address no other handle owns.
- With one receiver the driver config is as before (six 16 KiB transfers). With more than one, each receiver uses two 16 KiB
  transfers and USB clients alternate between cores 0 and 1, the settings of the OrcNode four-dongle validation.
- Sinks: `add_sink` / `remove_sink` / `sink_stats` with inline and queued kinds, 8 per receiver, per-sink block, drop and longest-call
  counters. `set_iq_sink` and `set_aux_iq_sink` are wrappers registering inline sinks on receiver 0, so audio, `iq_tap`, RF Lab,
  ACARS and LoRa are unchanged. `remove_sink` waits (bounded, about 300 ms) for the receiver loop to move on, which closes the
  free-while-running window.
- New: `RTL_RX STATUS` (read-only, one line per receiver: state, frequency, rate, drops, USB hub-port path, device), `Info.usb_path`.
- Build overlay `sdkconfig.multi-dongle` sets two receivers.
- **Not implemented:** screens and the web layer still use receiver 0 only; rate arbitration between consumers; the web IQ channel.

**How many dongles this console can run at once is not measured.** The OrcNode four-dongle run (2.56 MS/s, 120 s, zero drops) was
OrcNode firmware without this UI, Wi-Fi and audio load. Treat 2 as the first test and 4 as a structural maximum.

### Build and test (nothing here has been run)

```
# one receiver (default; same behaviour as before)
idf.py -B build-multi1 -D IDF_TARGET=esp32p4 -D SDKCONFIG=build-multi1/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults" build
# two receivers
idf.py -B build-multi2 -D IDF_TARGET=esp32p4 -D SDKCONFIG=build-multi2/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.multi-dongle" build
```
(add `-D C6_FIRMWARE_BIN=...` and the board overlay as for any build; v3 silicon boards also need `sdkconfig.silicon-v3` last.)

1. Single-receiver regression (one dongle, `build-multi1`): `python tools/p4_health.py --port <port>`; `RTL_RX STATUS` shows receiver 0 streaming;
   compare `RTL_CPU`, `RTL_MEM STATUS` and the `IQ hz= sps= drops=` log with the numbers before this change; open and close RF Lab,
   ACARS and LoRa a dozen times (the sink add/remove path).
2. Two receivers (two dongles, `build-multi2`): `RTL_RX STATUS` lists two streaming receivers with different `usb_path`; run 10 minutes;
   `drops` stay 0 on both; `RTL_MEM STATUS` dma_free stays above a safe margin; `RTL_CPU` core 1 keeps idle headroom.
3. One dongle on the two-receiver build: receiver 1 stays at state 0 (scanning) and nothing else changes.
4. Hot-plug the second dongle in and out; receiver 1 comes and goes, receiver 0 is undisturbed.
5. Raise to three, then four, only after steps 2 to 4 pass with the memory numbers recorded.

## Cost

- Static RAM: slot table about 8 x ~48 bytes plus counters, under 500 bytes of internal RAM, plus the epoch word. Queue buffers and the
  consumers' own state go in PSRAM. COM8 had 166,259 bytes of free internal RAM (largest block 94,208) with Wi-Fi up (measured
  2026-10-29), so this is affordable; it is still checked after the change.
- Receiver-task time: one pass over at most 8 slots per 16 KB block, plus `esp_timer` reads for inline sinks. Measure with `RTL_CPU`
  before and after.

## Tests before any merge of this

All on a P4 board with a dongle plugged in (COM8 currently has none, so the owner must connect one). Nothing flashed without the
owner's go-ahead.

1. Build only: the console builds for the default and the other board profiles; Tab5 build untouched and not rebuilt.
2. Behaviour unchanged with wrappers: audio plays; RF Lab, ACARS and LoRa screens open and close repeatedly (the add/remove path);
   `RTL_CPU` and `info().overruns / consumer_drops` compared with the numbers recorded before the change.
3. Many sinks: audio plus two queued test sinks for 10 minutes at 960 kS/s and 2.4 MS/s; zero receiver overruns attributable to the
   sinks; per-sink drop counts plausible.
4. Slow-sink isolation: a deliberately slow queued sink must raise only its own drop counter.
5. Remove race: a loop that adds and removes a sink every few milliseconds for several minutes with the receiver streaming, no crash.
6. `tools/p4_health.py` passes after the flash.

## Clarifications (owner questions, 2026-10-29)

- A **sink** is a consumer of one dongle's raw IQ: a function that receives every block of samples that dongle produces (audio
  demodulator, RF Lab analysis, a decoder, the browser IQ channel, a recorder).
- The **slots are consumers per dongle, not dongles.** Each dongle gets its own receiver task and its own table of consumer slots.
  Four dongles at once means four receiver instances (the driver has run four), each with up to 8 consumers.
- Today's `radio_service` is a single instance with one driver handle and one task, so it serves one dongle. The design makes it a
  per-dongle object; that is the multi-receiver requirement, and it is not met by the current code.
- `iq_tap` today can serve **one consumer at a time** (single global buffer and task; `start()` refuses while it is running). Several
  users are possible one after another, never together. The queued-sink design removes that limit for each dongle.
- Inline versus queued: inline runs inside the receiver task, so a slow consumer delays reading the dongle and the audio; queued
  copies the block into the consumer's own buffer and a separate task handles it. Recommendation: only audio inline.

## Decisions

1. Slots per dongle: 8 proposed (not yet confirmed by the owner).
2. Only audio may be `inline_fast` (recommended; awaiting owner confirmation).
3. The sample-rate arbitration rule is still open. It decides how the browser, RF Lab and a decoder share one dongle's stream.
4. **Decided by the owner:** screen-owned (node) versus browser-owned decoding is a checkbox/selector in the browser UI, chosen per
   decoder. The other branch already has a Node / Browser / Auto switch (`PHASE4_DECODER_PLACEMENT.md`) and one event ring tagged by
   placement, which fits. Still to define: the setting lives on the device (per decoder, per dongle) so two browsers see the same choice.
