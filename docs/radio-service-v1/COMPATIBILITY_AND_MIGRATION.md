# Radio Service v1 — compatibility, migration and agent handoff

**Implementation scope on this branch:** schema + worked examples + host-only reference adapters/guards. The original products remain untouched and operate independently. No merge into `main`.

## Existing interfaces and non-breaking mapping

| Product / concrete source | Observed source behavior | Adapter to canonical v1 | Limit / status |
| --- | --- | --- | --- |
| OrcSDR Tab5 `apps/orcsdr-tab5/ui/radio_session.hpp/.cpp` | `Owner`, `Band`, `Token{owner,generation}`, state and frequency/samples | Lease owner/receiver state, 32-bit generation widened under boot+attachment fencing | `retuned()` only changes session state; requires driver ACK before v1 `applied`; **adapter not integrated** |
| OrcSDR Tab5 `main.cpp` and optional LAN console | Native IQ callbacks, DSP task, `/api/action`, telemetry/spectrum/audio on port 80 | Local provider adapter above driver, preserve old LAN endpoints | LAN HTTP has no TLS/app authentication. Do not expose externally; **not upgraded** |
| Headless `main/receiver_manager.hpp` | 4 receiver slots, 0..4 states, `usb_path` and volatile `usb_addr`, rate/center, counters | `headless_state()`, `headless_health()` pure snapshot conversions; runtime supplies new attach/session/generation IDs | No reliable generation/ownership in legacy slots. **Reference conversion implemented, not deployed** |
| Headless `GET /api/v1/receivers` | Indexed array, `center_hz`, `sample_rate`, mixed-unit `drops` and separate USB/drain counters | Local polling to snapshot+health, never convert `drops` to complex samples | Poll gaps, stale receiver slot/index; authenticate control separately |
| Headless `POST /api/v1/receivers/tune`, `/rate` | Indexed control with legacy confirmation and `ok/rc` | Command adapter validates lease/caps and waits for readback / retune confirmation | Legacy `ok` alone insufficient for applied proof |
| Headless `GET /api/v1/tuner`, `POST /tuner/gain`, `/agc`, `/bias` | Report tuner ranges, gain steps, AGC, bias caps | Build runtime caps from measured tuner + source provenance; settings via command/result | Tuner bounds may be generalized; device and external-converter accuracy must be verified |
| Headless `GET /api/v1/spectrum`, `/fm/spectrum` | Binary whole-span waterfall `orcnode_waterfall_row_t`; 512 encoded levels, not dBFS | Convert to explicitly `relative` unit with true frequency axis and window unknown unless known | Never claim 1–255 intensity as calibrated dBFS; **not deployed** |
| Headless `GET /api/v1/fm` and WS `/api/v1/fm/stream` | FM status and binary paced visualization rows | Map session/sequence/format to canonical spectrum metadata | Existing WS frames preserved; binary ABI is not v1 envelope |
| Headless port 81 WS `/api/v1/fm/iq` | OIQ1 data blocks; `main/channel_stream.hpp` describes narrow/wide (mixer/decimator) sessions | Sideband metadata maps receiver center, requested channel offset, stream sample rate | OIQ1 is an existing transport; no invented replacement; must verify packet details / timestamps |
| Headless `GET /api/v1/channel` and `POST /channel`, `/stop` | One channel session, narrow or wide, desired Hz vs actual hardware center | Keep `requested.frequency_hz` and effective `applied.center_frequency_hz` distinct; annotate mixer/offset at transport edge | One listener slot / receiver conflict; not arbitrary multi-client service |
| Headless `/api/v1/decode/events`, `/status`, `/ingest`, `/decoder` | Native or browser-origin decoder events; test/inject routes exist | Map integrity/provenance/origin; do not reclassify browser/test data as live RF | Source event shape differs; ingestion/injection must be privileged |
| Headless `GET /api/v1/events` | Scanner anomaly events and baseline quality, not decoded modulation messages | Map as typed observation extension in future minor, **not** `decoder_event` without a decoder | No fabricated signal protocol classification |
| Mobile `mobile_ui/lib/data/radio_service.dart` | `connect/disconnect/tune/setGain/setSquelch/setVolume` return bool; typed `RadioEvent` streams | Keep public methods, insert canonical adapter below view-model; add future `TuneResult` stream sidecar | Bool maps to bridge command completion, not proof of RF reception |
| Mobile `services/radio_channel.dart` and Kotlin `RadioBridge.kt` | MethodChannel `orcsdr/radio`; EventChannel `orcsdr/radio/events`; `RadioSnapshot` | `mobile_state()` pure conversion; attach/session/monotonic metadata must come from native bridge | Snapshot lacks hardware generation, UTC validity and loss units |
| Mobile `data/bridge_radio_service.dart` and `models.dart` | Flutter-facing receiver, spectrum, waterfall, stats; `timestampMs` taken from wall clock; `dropCount` from starved pulls | Immutable v1 metadata before throttled Dart events; null for unavailable counters; frame generation from native | No raw 2.4 MSPS IQ on MethodChannel; remote Mobile client is **planned** |
| Linux ARM64 or x86-64 | No v1 provider verified here | Native backend implements same interface independent of UI/USB | **Future** |
| Web/WASM | Browser rendering and wasm modules exist in Headless reference branch | Client adapter consumes normalized events; DSP in WASM only when explicitly deployed | **v1 adapter future** |

Specific `/api/v1` paths above belong to the **Headless reference repo**, not the Tab5 LAN console and not the new v1 namespace. Do not turn them into a competing public v1 HTTP API or claim a compatibility guarantee that current servers do not offer.

## Streaming transport decision record

1. **Direct native memory** should be first choice for same-process Tab5/Linux/Android DSP. No JSON serialization, copy or network dependency on high-rate IQ path.
2. **rtl_tcp** remains a useful, already-understood external IQ distribution option where receiver control and receiver format semantics are adequate. It is not a universal session/auth/decoder-event protocol: add a separate authenticated radio-service control plane and ensure retune/attach generation is fenced.
3. **Existing OIQ1/WS and binary visual streams** must remain functional for existing Headless/Web clients. A v1 adapter can translate metadata without rewriting packet formats. Do not claim timestamp precision absent in source packets.
4. **OrcStream** should be evaluated as a possible future data-plane transport that can carry canonical sideband metadata, but this branch does **not** define OrcStream wire compatibility, SDK implementations, compression effectiveness, or security. No premature duplicate IQ packet format.
5. If a transport lacks attribution for drops, exact first sample or authoritative time, publish `null`/unknown and lower confidence rather than synthesize exactness.

No claim here that rtl_tcp alone authenticates sessions or that OrcStream is already production-ready.

## Safe staged migration

| Gate | Implementation | Required proof before completion |
| --- | --- | --- |
| M0 — complete (this branch design) | V1 schema, examples, reference guards, legacy snapshot adapters, host contract tests | Host Python validation only; no runtime integration |
| M1 — Tab5 local provider | Wrap existing `radio_session` and driver callback, bind attachment/session generations, acknowledge hardware operations | ESP-IDF host build, hotplug/retune/drop tests and physical receiver acceptance |
| M2 — Headless provider | Read-only sidecar for receiver, health and decoder events; add command bridge behind real auth | Multi-RX ownership races, stale index and replug tests, no breakage of existing `/api/v1` clients |
| M3 — Mobile local | Native bridge mints identities/generation and confirmed source timestamps; Dart surfaces immutable v1 snapshots alongside old API | Host Dart/Kotlin tests, Android emulator and real USB (distinct) |
| M4 — Linux | Native receiver adapter, no UI/display/network dependency | Linux x86-64/ARM64 CI and real local USB / Ethernet receiver test |
| M5 — secure remote | Negotiated versions/scopes, transport security and stream sidebands | Auth bypass, read-only vs tune/record, replay, revocation, TLS configuration, packet loss and load |
| M6 — multi-device parity | One dashboard contract against direct USB, remote, local/headless and mixed tuner types | Verified same RF evidence fixture, timestamp/source/decoder parity, non-coherent warning |

## Breaking-change and quality risks

- The existing Tab5 32-bit `generation` / owner model and Headless no-generation control differ from v1; migration must fence stale owner and avoid wrap/reconnect reuse.
- Existing Mobile bool setters and frequency defaults can be misread as hardware-applied; new result semantics may require UI status adjustments **without removing existing methods**.
- Headless receiver indices, weak duplicate serials and port topology must not be treated as authentication or unchanging attachment identity.
- Headless binary waterfall levels differ from calibrated dBFS; audio and IQ WS framing must not be relabeled without parsing/test evidence.
- Current Web/Tab5 controls are not remotely secure; turning on external access before authentication would be a security regression.
- Different tuner RF ranges, rate quantization, direct sampling, external converter support and gain steps can make a “shared frequency” impossible; feature-gate and report unsupported.
- Timestamps and drops differ between source systems; preserve units and nullable unknown values. JS integer precision for 64-bit byte counters requires care.
- The new JSON schema is **strict**. This is a designed validation boundary, not a command to make production clients immediately consume this envelope.

## Required downstream interfaces — Agents 3, 4, 5

These are **responsibility contracts**, not assertions about those agents' independently assigned work:

- **Agent 3 / any remote-control and security owner:** implement authenticated principal+scope negotiation, per-receiver lease checks, command idempotency and audit, and explicit restrictions on `observe`/`tune`/`stream`/`record`/`admin`/`tx`. Do not trust confirmation headers. Produce denied/expired/replayed request tests.
- **Agent 4 / any streaming transport owner:** adapt native IQ block and actual existing rtl_tcp/OIQ1/OrcStream-compatible data paths to v1 `iq_block`/`spectrum_frame`/`audio_frame` metadata, with generation fencing, bounded backpressure, explicit format and measured drop units. Do not invent a new incompatible stream or send 2.4 MSPS through Dart MethodChannel.
- **Agent 5 / any UI/client parity owner:** consume `capabilities`/`receiver_state`/`command_result`/`decoder_event`, keep requested and applied settings separate, reject stale frames, preserve event integrity/provenance, and exercise local disconnected/no-display/no-network states.
- **All agents:** share the exact schema/fixture set, extend by negotiated minor version only, run `python -m unittest discover -s tests/radio_service_v1 -v` from repo root with `jsonschema>=4.21,<5`, and document implemented versus measured evidence. **Do not modify the other repositories from this branch.**

## Tests and evidence

Schema uses JSON Schema Draft 2020-12. CI workflow `.github/workflows/radio-service-contract.yml` runs fixture and negative-case unittest with `jsonschema` pinned by range. Reference Python modules are deliberately host-only examples, not compiled firmware artifacts and not claims of full decoder portability. New test artifacts contain invented/example identifiers, never real capture content, personal serials, secrets or internal agent prompts.
