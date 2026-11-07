# OrcSDR Universal Radio Service — contract v1.0

**Status:** additive development-branch contract, schema, examples, reference compatibility adapters and host tests. **Not** a shipped ESP32, Linux, Web, or Android runtime implementation. No existing API has been replaced.

## Scope, source pins and the ownership rule

Authoritative source review on 2026-11-06:

| Source | Commit | Interface inspected |
| --- | --- | --- |
| OrcSDR `main` | `1dbf2d455a5c2bf8bf69ef804336418879b7325f` | `architecture.md`, `PROJECT_STATUS.md`, `apps/orcsdr-tab5/ui/radio_session.hpp/.cpp`, LAN console |
| Headless `feat/m5gfx-web-mirror` | `67e535e3a8562709d190194363f7d81ce77fe2f3` | `main/http_api.cpp`, `receiver_manager.hpp`, `channel_stream.hpp`, `spectrum_tap.hpp` |
| Mobile `main` | `b5fe05d39972c0ee4b1cb6830f903facf58cb3b2` | `mobile_ui/lib/data/radio_service.dart`, `models.dart`, `bridge_radio_service.dart`, `services/radio_channel.dart`, `docs/PARITY_SERVICE_PROPOSAL.md` |

Each deployment owns its **receiver backend**, *one* authoritative radio session and its native DSP/decoder execution. Dashboards, Flutter widgets and Web/WASM views observe the same semantic contract; they do not need to implement different decoders. A device with no network or display remains fully functional locally. Web/WASM is an optional client, not a mandatory processing tier. Multiple receivers are independent unless an adapter explicitly proves synchronization. This specification does not assert working Linux or remote Mobile implementations.

## Envelope and version negotiation

The normative JSON Schema is `../../contracts/radio_service/v1/schema.json` (relative to this document); examples live in `../../contracts/radio_service/v1/examples/`. Every control-plane JSON message and every data-frame **metadata** message carries:

- `contract = "orcsdr.radio-service"`; `version = {major:1, minor:0}`; `kind`; `host_id` and unpredictable `boot_id`.
- `receiver_id` (host-scoped stable *port/slot identity*, not a claim of unique USB serial), `attachment_id` (new for every physical attach), `session_id` (new for each ownership acquisition), `generation` (non-reusable per session), and increasing `sequence`.
- `time.monotonic_ns` since boot, `utc_valid`, `utc_unix_ms` (null when invalid), `clock_source`, and `utc_uncertainty_ms` (null when unknown). No inferred UTC from transport receipt.
- An explicitly typed `payload` matched to `kind`. Unknown top-level or known-kind payload fields are rejected by v1.0 schema. Extension data belongs in an agreed new minor revision, not silently appended.

Handshake `hello` offers supported contract versions, data transports and requested scopes. `hello_result` selects one compatible major/minor and explicitly grants scopes **only after authentication**. Select the highest mutually supported minor within major 1; major mismatch is `VERSION_INCOMPATIBLE`, never silently parse as v1. A non-authenticated session may receive only locally authorized public observation, not control. Transport negotiation is separate from JSON semantics: no mandatory Wi-Fi, Ethernet, TLS stack or display. Local C/FFI calls can carry the same semantic objects without serializing JSON. Firmware may use a preallocated binary representation internally.

New `kind` and fields require a reviewed minor bump if backwards compatible; changed meaning, removed required field, representation changes or reordered binary formats require major 2. Unknown optional capabilities are **not supported** until proven. JSON 64-bit counters are exact integers; JavaScript clients must use an integer-safe representation or an explicitly negotiated string-counter extension rather than truncating values above 2^53-1.

## Identity, generations, ownership and state

- `host_id` is an operator-visible host alias, not a security credential. `boot_id` fences reboots; its value must change every boot.
- `receiver_id` is unique per host at a time. Prefer a persisted USB topology/port key when available. VID/PID, generic EEPROM product and default serial `00000001` are **not** uniquely reliable; device address, index and hub enumeration may shift. A USB topology key is only stable while cabling is stable.
- `attachment_id` changes at unplug/replug, even if the same port and dongle return. `session_id` changes when ownership is acquired or replaced. Avoid reusing IDs after reboot.
- `generation` increases on ownership acquisition and any *stream-semantic* disruption (retune, rate change, reconnect, restart, demod configuration affecting delivered data). A pending transition must immediately fence/quiesce old frames, and only a hardware-confirmed transition can publish an applied setting. On a failed transition, preserve the previous applied settings, mark stream paused/error until reconfirmed and never relabel buffered pre-transition IQ as new-generation IQ.
- The Tab5 `radio::Session::Token` already provides `owner` and a 32-bit generation; an adapter must extend/fence it with `boot_id`, `attachment_id` and `session_id`, and renew the session before wraparound. Do not assume its `retuned()` true means driver ACK: that method updates a session snapshot only.
- Receiver state progression: `absent → present → ready → configuring → streaming → stopping → ready`; `error` can occur from any state. State messages carry `owner` (null when unknown), confirmed `applied` values, and `pending_command_id`. A screen opening/closing is not by itself a receiver acquisition.
- Command preconditions must match **host, boot, receiver, attachment, session, expected generation** and current owner/lease. Stale or conflicted commands fail atomically. An attachment reappearing does not retain prior control rights.

## Capability model and unsupported paths

`capabilities.payload.host` declares display, network interfaces, storage/recording and audio output separately from receiver capabilities. `capabilities.payload.receiver` declares receiver transport, **effective RF** frequency ranges (possibly disjoint), exact and ranged sample rates, manual/auto gain steps, tuner AGC, bias tee, PPM correction, direct sampling, bandwidths, IQ/spectrum/audio/decode/scan/record services, and explicit TX support. No placeholder zero means “supported.” All ranges use inclusive endpoints; implementations must additionally test `min <= max`. A disconnected external upconverter is **not** a capability. For a verified converter, the adapter must identify its RF path and show actual tuner versus effective RF tuning in a compatible negotiated revision before advertising extended coverage.

No capability may be inferred from ESP32-P4 alone: C6 Wi-Fi, SD, Ethernet, touch, native DAC and TX vary by board. USB is not universal either; a network/file/PCIe backend may be used. E4000 must reject unsupported low-frequency requests unless a real converter is detected and modeled. Optional TX is **disabled by default**; it requires separate verified hardware, authorization and regulatory/safety controls. This RX-focused v1 does not authorize any transmission merely by exposing `tx.supported`.

## Command lifecycle

Send `command` with `command_id` (idempotency/deduplication key per session), `operation`, `expected_generation`, `expected_attachment_id` and precise `requested` values. Check role, scope, capability, range, rate and lease **before touching hardware**. Emit exactly one terminal outcome for each accepted ID:

1. `unavailable` with typed reason for missing hardware/capability; or `failed` for bad arguments, stale generation, busy lease, unauthorized, transport or driver failure.
2. `pending` when accepted, carrying only requested values and **no applied snapshot**; controls must label it pending.
3. `applied` after a backend-specific ACK/readback of effective settings, returning `applied`, fresh `result_generation`, and exact `ack_source`. “HTTP 200”, a confirmation header, queue enqueue, or a dashboard-side setpoint is **not** evidence of application.
4. `failed` if ACK fails/times out or receiver detaches; do not optimistically publish requested frequency as applied.

A duplicated `command_id` must return the cached outcome, not repeat a tune or recording action. Concurrent commands for the same lease serialize. A timeout may leave hardware state *unknown*: keep `configuring/error` until verified. The Python reference `preflight_tune()` is a pure preflight, **not** a hardware driver, and `complete_tune()` requires caller-supplied verified ACK.

## Data-plane metadata and fidelity

The schema supplies portable metadata for `iq_block`, `spectrum_frame`, `audio_frame`, `decoder_event` and `receiver_health`. The bytes travel via a local native pointer/bounded queue or an explicitly negotiated transport. The JSON metadata is **not** a newly invented IQ streaming wire protocol.

- IQ: exact `cu8`, `cs8`, `cs16le` or `cf32le` complex interleaved I/Q, true sample rate, center RF frequency, complex count, byte count, first-sample index and missing-complex-sample count (null = not measured). First-sample timestamps are host observations unless the backend supplies hardware sampling time. Sample indices restart on new generation.
- Spectrum: bins carry unit `dbfs`, `relative` or calibrated `dbm`; exact center/span, FFT size, bin count, window, optional source sample index, calibration truth and dropped-frame count. A legacy 1–255 waterfall intensity row is **relative**, never mislabeled dBFS or dBm. Counters must not be silently converted to calibrated quantities.
- Audio: PCM encoding, channel count, sample rate, frames, payload bytes, source generation, actual underrun observations. Local playback does not require a network stream.
- Decoder events: source protocol, versioned/pinned decoder provenance, integrity (`candidate` vs CRC/FEC-valid), `live_rf` vs replay/injected/synthetic origin, confidence nullable, typed payload, timestamps. RF energy is not a validated digital decode; an encrypted frame is not plaintext.
- Health: keep USB bytes received, IQ bytes delivered and IQ bytes consumed **separate**. USB drops, ring overrun bytes, dropped complex samples and available rates/occupancy have distinct meanings. Unknown is `null`, never zero. Counter reset and wrap require a new generation or explicit reset event.
- If a consumer misses sequences or a producer queue overruns, report measured drops; cap producer queues and never block USB reads behind slow browser clients. Late-generation frames are discarded. Multiple receivers cannot be called phase-coherent from host timestamps.

Reference cross-field checks, e.g. IQ bytes matching format/sample count, spectrum bin length, valid capability ranges, are in `contracts/radio_service/v1/compat.py` and are tested. JSON Schema is necessary but alone cannot express all cross-field and runtime lease constraints.

## Authorization and threat boundary

Scopes: `observe` (state, telemetry, redacted events), `tune` (receiver commands/lease), `stream` (IQ/audio subscription), `record` (storage capture/export), `admin` (identity, credentials and maintenance), `tx` (independent high-risk scope and separate device safety). Explicit deny by default; scope and ownership checked per command and per receiver, not merely once on login. Keep a local administrator route that works offline, but do not grant remote network privilege automatically because a local user exists.

Remote transports require a verifiable authenticated principal, authenticated+encrypted transport (e.g. correctly deployed TLS or equivalent), explicit authorization, revocation and audit records. Cross-origin browser protections are not a substitute. Existing Headless `X-OrcSDR-Confirm` / `X-OrcNode-Confirm` headers and open CORS are UX/CSRF-like confirmations only; **not authentication**, confidentiality or permission checks. Current Tab5 trusted-LAN console is likewise not Internet-safe. Never expose raw recording/decoder injection/firmware/clock setting to unauthenticated callers. Capability discovery should avoid sensitive serials, tokens or location without permission. The schema is not a security implementation.

## Interchangeable consumers: minimum contract

A conforming dashboard consumes `capabilities`, `receiver_state`, `command_result` and at least one relevant data/event kind. It must gate controls and display **applied** values, indicate stale/unknown clocks, clear stale waterfall on generation changes, and handle unsupported states honestly. Decoder algorithms remain in shared native C/C++ where portable; the contract defines transport-neutral events, not duplicated Dart/JS decode algorithms. A client can subscribe read-only without acquiring tuning ownership. A local mode must not depend on a remote host or optional network.

## Acceptance gates

The checked-in host suite verifies syntax, canonical fixtures, negative schema cases, preflight authorization/range/session fences, the legacy Headless/Mobile snapshot adapters and cross-field geometry. It does **not** measure RF, compile ESP-IDF/Flutter, authenticate a real network, or prove interoperability with a physical receiver. Physical-host and multi-client/fault-injection gates remain in the migration plan.
