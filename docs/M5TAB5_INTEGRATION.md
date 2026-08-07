# M5Tab5 Integration

## Truth status

**Decision:** M5Tab5 is first-class in OrcLink as both an edge node/gateway and a portable operator console.

**Verified:** the target is a Tab5 with ESP32-P4 v1.3, 16 MiB flash, 32 MiB octal PSRAM, built-in USB Serial/JTAG, a 1280x720 display, working GT911 touch, read-only battery level/voltage/current, and an ESP32-C6 ESP-Hosted station interface that can inventory nearby networks without blocking USB auth. A native M5Unified UI, stable node identity, physical-USB pairing, HMAC-authenticated session, bounded NVS journal, degraded state, reconnect replay, signed bounded offline status workflow, journal-pressure recovery, and credential rotation have run on COM17.

**Still unverified:** the authenticated Wi-Fi provisioning path is implemented but needs an explicitly supplied network credential for association and reboot-persistence proof. TLS transport, AP loss/roaming, production-protected key storage, sudden-power-loss/storage-full journal behavior, power characteristics, and vendor update/recovery semantics also remain open. The measured development unit has no secure boot or flash encryption, so its NVS credentials are extractable. Detailed evidence is in `M5TAB5_VALIDATION_REPORT.md`.

## Roles

### Edge node and gateway

An M5Tab5 may expose onboard or attached sensors, buttons, indicators, local controls, and Bluetooth/Wi-Fi/serial-connected peripherals. It normalizes these as components and namespaced capabilities beneath one stable node identity. It may execute bounded, installed workflows during an upstream outage.

The gateway does not turn every attached peripheral into a trusted device. New peripherals are discovered as quarantined components or child nodes until approved.

### Portable operator console

The touch console shows topology summaries, node health, active commands, alerts, pending approvals, and a manual-control surface. Emergency controls must be explicit, always visible when relevant, resistant to accidental activation, and backed by device-native safety mechanisms. A disconnected console clearly displays stale/offline state and cannot imply that an approval reached the daemon.

Both roles may run together, but edge collection must continue if the UI crashes. UI rendering must not own device I/O or durable command state.

## Logical split

```text
M5Tab5
├── platform/firmware layer
│   ├── boot, drivers, network, storage, watchdog, update/recovery
│   └── device identity key access and physical I/O safety
├── OrcLink edge agent
│   ├── pairing, protocol, capability handlers, telemetry, local journal
│   └── approved offline workflow executor
└── operator console
    └── topology, status, approvals, alerts, and manual controls
```

The exact packaging boundary is unresolved: separate processes, firmware tasks, or one application with isolated modules are all possible.

## Communication with `orclinkd`

Preferred semantics are a long-lived, mutually authenticated session with:

- registration and capability negotiation;
- full state snapshot followed by sequenced telemetry/events;
- command invocation, acknowledgement, progress, result, and cancellation;
- heartbeats and explicit degraded status;
- resumable synchronization using session ID and last acknowledged sequence;
- schema and protocol-version negotiation.

**Assumption:** TLS over local Wi-Fi is viable. If the validated platform cannot support it safely, a trusted local gateway may terminate transport security, but end-device commands still require signed identity and anti-replay protection. Serial may support provisioning or a directly attached fallback; Bluetooth transport is deferred until validated.

The console can connect directly to `orclinkd` or consume a local edge-agent cache. Direct daemon connection is authoritative for approvals. Cached views are labeled with age and connection state.

## Identity, pairing, and trust

1. Factory reset/unpaired device creates or imports a device key using the strongest verified protected storage available.
2. The daemon discovers a pairing candidate but quarantines it.
3. An operator compares a short-lived code/QR or equivalent proof on both endpoints.
4. The daemon issues a local trust credential bound to node ID and public key.
5. The operator assigns roles and permitted capability namespaces separately.
6. Credential rotation, revocation, and factory-reset recovery are auditable.

MAC address, IP address, hostname, and serial port are discovery hints, never sole identity. If protected key storage is unavailable, documentation and UI must state the increased cloning/extraction risk.

## Offline and degraded operation

The agent retains:

- its identity and trust chain;
- last approved configuration and capability policy;
- bounded last-known state;
- an append-only local event journal with monotonic sequence numbers;
- installed offline workflows and their expiry;
- pending results, never newly invented approvals.

While disconnected it may read sensors, preserve local safety behavior, operate manual controls, and run explicitly installed offline workflows. It may not perform centrally-approved-only actions, accept stale one-time approvals, broaden capability arguments, or claim central success.

The UI displays `online`, `degraded`, `offline`, or `resynchronizing`, plus last authoritative contact time. Safety functions fail to their device-defined safe state, not a universal guessed state.

## Reconnect and synchronization

1. Establish authenticated session and negotiate versions.
2. Exchange boot ID, agent instance ID, last acknowledged event sequence, and configuration revision.
3. Upload unsynchronized events in order with stable IDs; daemon deduplicates.
4. Reconcile commands by ID and idempotency key; never blindly replay an uncertain physical action.
5. Resolve configuration by daemon-authoritative revision, except device-local calibration and safety limits that policy marks non-overridable.
6. Request a full snapshot after gaps, journal truncation, reset, or schema mismatch.
7. Mark synchronization complete only after both sides acknowledge durable checkpoints.

Conflicting state is recorded, not silently overwritten. Clock differences do not reorder source events when a source sequence exists.

## Initial capability surface

- `m5tab5.device.info`, `health.read`, `power.read` where supported.
- `m5tab5.telemetry.subscribe` and `event.journal.sync`.
- `m5tab5.display.alert.show` and `.clear`.
- `m5tab5.input.state.read` and operator-action events.
- `m5tab5.peripheral.list` and read-only discovery metadata.
- `m5tab5.workflow.install`, `.list`, `.start`, `.cancel` with strict policy.
- `m5tab5.console.approval.respond` only while connected to the authoritative daemon.
- Transport-specific sensor, GPIO, serial, or Bluetooth capabilities only after hardware validation.

The example catalog is in `schemas/examples/capabilities-m5tab5.yaml`.

## UI modes

### Native application — selected for the platform proof

Pioarduino 55.03.38-1 with M5Unified/M5GFX is the measured working path for display, touch, NVS, and predictable full-screen behavior. The minimal ESP-IDF build remains useful for low-level serial/recovery proof. The vendor demo's current ESP-IDF component graph failed from incompatible codec and LVGL API versions, so it is not the selected UI path without corrected upstream pins.

### Installed/local web application

Potentially fastest to share with Control Room. It requires a verified browser/webview, safe credential storage, offline caching behavior, and kiosk/full-screen support. Browser origin security must not be bypassed for convenience.

### Device-served web UI

Useful as a fallback from another phone/tablet, but it does not itself provide an on-device console. Limit configuration exposure and require authenticated access.

Select one only after a thin proof measures startup time, touch behavior, memory, reconnect, offline persistence, and secure credential access.

## Hardware validation checklist

- [x] Record exact model, chip revision, vendor documentation, and available unit.
- [x] Identify processor/architecture, RAM, persistent storage, and initial app budget.
- [x] Confirm ESP-IDF/FreeRTOS SDK and compiler path, boot partitions, and firmware packaging.
- [x] Confirm 1280x720 landscape display, mapped touch points, and native drawing; gesture, brightness, accessibility, and burn-in measurement remain open.
- [x] Inventory ESP-Hosted Wi-Fi station versions/mode and passive scan behavior; Bluetooth, reconnect, and roaming remain open.
- [x] Confirm built-in USB Serial/JTAG and inventory documented external interfaces; electrical validation remains open.
- [ ] Battery level/voltage/current telemetry is verified; PMIC identity, VBUS/charging detection, other sensors, buttons, speakers, LEDs, and watchdog behavior remain open.
- [ ] Verify TLS, modern cipher suites, and certificate validation over the ESP32-C6 network path; hardware random generation and HMAC are practical.
- [x] Verify stable MAC-derived node ID and persistent NVS pairing key; the key is extractable because secure boot and flash encryption are disabled.
- [x] Record current secure-boot, flash-encryption, JTAG, and ROM-download eFuse state; production hardening remains open.
- [ ] Measure cold boot, UI startup, idle/peak power, battery runtime, thermal behavior, and sleep/wake.
- [x] The bounded NVS journal survives host loss and an app rewrite/reboot, reports pressure, and clears pressure after host acknowledgement; wear, sudden power loss, and destructive storage-full behavior remain open.
- [ ] Test WebSocket or chosen session transport under packet loss, AP loss, daemon restart, and clock drift.
- [x] Select native M5Unified/M5GFX for the platform proof; browser/PWA and accessibility evaluation remain open.
- [ ] Verify physical mounting, connector strain, ESD, and safe use around controlled equipment.
- [x] Produce a minimal hardware-in-loop display, touch, authenticated-session, journal, and reconnect proof.
