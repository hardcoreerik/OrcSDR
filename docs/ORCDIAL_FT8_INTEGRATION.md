# OrcDial FT8 delivery — development handoff, 2026-10-30

Active development; not hardware accepted, merged, or released.

## Implemented path

`ft8_native_on_decode` queues confirmed backend results as before. The UI task's
`ft8_native_drain_pending` appends them to `DecodeStore` and copies the latest
record into `dial_preview::Delivery`. `orcdial_poll` sends its summary/text via
the existing secure runtime and unchanged C6 relay only while FT8 is active and
the paired Dial has explicitly opted into schema 1. No additional DSP callback
work, network lookups, framebuffer, task, or dynamic allocation was added.

The standalone OrcDial branch is authoritative for Dial firmware. The receiver
uses the existing `orcdial/` shared protocol/runtime headers; only those shared
headers receive the matching extension declarations/session queue safeguards.
The embedded Dial UI is not synchronized or substituted for the standalone app.

## Negotiation and compatibility

- Keep secure protocol 4, trust records, 64-byte control payloads, command IDs
  1–16, crypto, and C6 firmware unchanged. Schema-1 metadata remains types 17/18.
- Receiver `RADIO_STATE.capabilities` bit 31 (`0x80000000`) advertises schema-1
  FT8 summary/text support globally. Existing dashboard-local bits 0–10 retain
  their meanings; the receiver state revision is still its ordinary revision.
- A new Dial that has received that offer opts in using an ordinary authenticated
  HEARTBEAT: capabilities exactly `0x80000000`, revision `0x46543801`.
  Unknown/missing schemas fail closed. This heartbeat does not enter the command
  sequence or command acknowledgement cursor.
- Fresh, nonzero heartbeat sender/sequence required; duplicate/old heartbeats
  cannot renew the 3500 ms lease. Sender changes and every secure runtime
  generation change clear negotiation. The secure replay checks remain in force.
- Old Dials send zero in those existing heartbeat fields: no metadata is sent.
  Old receivers offer no bit 31: new Dials send normal heartbeats and retain
  ordinary controls. No extension probes or unknown packet types reach old peers.
- Queued outbound controls carry the runtime generation and are discarded after
  a generation change. This prevents pre-reconnection records entering a later
  session. Receiver-owned types from a Dial are ignored before command ordering.

## Bounds, accuracy, and loss

Only the latest decode is retained, with a separate nonzero monotonic record ID.
No strings or rows are fabricated. The capture-time dial, decoder mode, UTC (or
zero), and SNR validity are retained. A location is associated only when the CQ
message's parsed callsign and locator match the supplied fields. The locator is
an area; the Dial plots its approximate center, not a transmitter coordinate.
Source arrays are bounded/ASCII-checked before parsing. Golden vectors remain
unchanged and represent synthetic fixtures, not actual reception.

One summary/text pair is attempted at most every 1500 ms. Control processing
comes first; metadata requires an idle secure input/action queue and at least
eight spare relay slots. Enqueue never waits for Hosted I/O. A partial/lost pair
is retried with the same identity; newer decodes replace it without backlog.
This is bounded best-effort delivery, not application-level acknowledgement.
Each 64-byte application record uses two existing secure outer fragments.

The producer stops retrying 60 seconds after UI-task observation. The Dial expires
its cache 60 seconds after first accepting that record; duplicate summaries and
text never extend that timer or resurrect an expired record. Connection, sender,
dashboard, and offered-capability changes clear the cache. Re-entry can obtain a
still-eligible latest record. There is no cross-device monotonic clock claim.

The new producer and peer negotiation use a fixed-size Decode and small counters;
there are no new PSRAM/DMA buffers. Existing queue capacities and task priorities
are unchanged. Physical throughput, DSP/audio continuity and memory headroom
remain hardware gates, not consequences of host burst tests.

## Validation and remaining work

Exact final commands, results, source commits and firmware hashes are recorded
in the companion validation report when available. Primary checks:

```bash
bash tools/test-ft8.sh
cmake -S <OrcDial>/tests -B <build> -DCMAKE_BUILD_TYPE=Debug \
  -DMBEDTLS_SOURCE_DIR=<Mbed-TLS-3.6> -DORCSDR_SOURCE_DIR=<this-repo>
cmake --build <build> -j 2
ctest --test-dir <build> --output-on-failure
```

The optional CMake setting links the actual receiver encoder into the standalone
Dial security tests: offer/heartbeat, encrypted summary/text, corrupted/replayed
ciphertext, tuning after metadata, and reconnect replay rejection. Portable tests
also cover missing fields, unavailable UTC/SNR, truncation, wrong identity,
out-of-order records, expiry, schema mismatch, and a 10,000-decode burst.

Build production Dial with PlatformIO `dial`; build Tab5 only with the pinned
`apps/orcsdr-tab5/tools/build-tab5-idf.ps1`. No hardware flashing is authorized by
this handoff. The COM14 Dial still has the earlier slice-1 build until separately
authorized. Two-device pairing, real on-air decode, legacy firmware matrix,
RF/audio coexistence, packet-loss stress, physical map/text readability, and
display smoothness are unclosed acceptance gates.

Selected-history records, multiple map points, receiver location/distance,
Dead Air telemetry and actions, ADS-B/ACARS/SSTV projections remain future work.
They should use explicit schema capabilities and bounded receiver-owned
snapshots. Ambient animation is decorative; no new event-driven animation is
claimed here.

## Research/provenance

Local secure-session/runtime and C6 bridge source are the primary architecture
evidence. [Espressif's ESP-NOW guide](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/network/esp_now.html)
supports retaining small frames and queued work outside high-priority callbacks.
This is documentation research only: no external source code or data was copied,
and no external redistribution/licensing dependency was introduced. No shipped
binary reverse engineering or broad RSA/REA analysis was needed for this source
integration.
