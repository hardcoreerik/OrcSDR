# OrcDial accessory

OrcDial is an **optional** M5Dial controller for OrcSDR. The standard OrcSDR build includes the dormant Dial bridge and its ESP-NOW-capable ESP-Hosted C6 image. No separate OrcSDR download is needed. The normal Tab5 radio, Wi-Fi, and dashboard paths remain authoritative when no Dial is paired.

The Dial shows the Orc badge for five seconds, then OrcSDR Home and a side carousel of current dashboard destinations. The selector embeds illustrated dashboard pictures, while the full dashboard views retain their code-drawn artwork. Tuner views offer Reel, Dial, Odometer, Tape, and Split frequency graphics. The renderer uses a small buffered canvas; the user confirmed the selector pictures are readable and rotation stays smooth.

## Build

Build the M5Dial with PlatformIO:

```powershell
cd orcdial
pio run -e dial
```

Build the standard Tab5 image with its embedded OrcDial-capable C6 image:

```powershell
cd <repository root>
./apps/orcsdr-tab5/tools/build-tab5-idf.ps1
```

The accessory must work without requiring a C6 firmware update. The existing experimental bridge uses a custom C6 relay; that approach is under review against the no-update requirement. Keep the user's installed C6 firmware intact while investigating its supported interfaces and verifying channel alignment. The build resolver currently caches the experimental image by a hash of the local C6 sources; embedding it does not install it on the device.

Local USB automation: send `RTL_WIFI_CHANNEL` and `RTL_ORCDIAL_PAIR START` to the Tab5, then `ORCDIAL_PAIR START` to the Dial. Query the Dial with `ORCDIAL_STATUS` (link, pairing, channel, frequency). `RTL_ORCDIAL_PAIR STOP` closes the Tab5 window. Other authenticated Tab5 controls remain protected. Run `python orcdial/tests/serial_smoke.py --port COM14` for the Dial command and overflow checks; it starts discovery but never tunes.

Run `python orcdial/tests/host_probe.py --port COM17` to check the Tab5 commands and queue a discovery packet through the experimental relay. `RTL_ORCDIAL_PROBE` reports queue acceptance and the preceding completed C6 RPC result; neither proves over-air transmission or pairing. Outgoing replies use a bounded worker queue so Hosted timeouts do not block DSP or UI. Neither script updates firmware.

For connected-device control tests, `ORCDIAL_ROTATE <delta>` accepts nonzero detents from -20 to 20 and uses the same dashboard/focus mapping as the physical encoder, without acceleration. `ORCDIAL_FOCUS NEXT` cycles the same focus choices as a short encoder press on a tuner. Both refuse offline control. A queued response is not proof of application: verify the acknowledgment and returned state in `ORCDIAL_STATUS`, then compare the Tab5 state. Status includes dashboard, focus, step, volume, last acknowledgment, and pending-command state.

The temporary direct-Wi-Fi bench transport has been removed following reported audio and spectrum stalls. OrcDial uses ESP-NOW only. Periodic host state updates require a packet from the paired Dial within the preceding three seconds; saving a peer does not activate background sends. Both devices now have the ESP-NOW-only firmware installed.

## Controls and state

From Home, turn or press to open the dashboard carousel; turn to choose and press to open. On tuners, rotation emits frequency deltas in the dashboard's configured step, with acceleration only for frequency. A short press cycles the available frequency, step, gain, and volume focus. On channel and content dashboards, rotation emits semantic channel, radar range, candidate, slot, target, message, AP, or setting actions, never a generic RF tune. The Tab5 acknowledges only actions backed by a real handler; an unsupported action returns `ERROR`. The Dial keeps its center readout on the last authoritative Tab5 state while a command is pending.

The current Tab5 bridge handles dashboard open, FM/AM and generic Browse tuning, FM/AM step, CB channel, P25 candidate, LoRa slot, ADS-B range, and shared volume. Weather and Marine channel plans, content selection for aircraft/nodes/messages/APs, RF Lab parameters, and full Settings navigation are still pending. Their controls must not imply a successful action or display invented results. `docs/ORCDIAL_CONTROL_MATRIX.md` is the behavioral contract.

The Dial labels disconnected state `OFFLINE` and never treats its offline preview frequencies as live RF measurements. `ORCDIAL_DEMO=1` builds are explicitly labeled `DEMO` and do not start ESP-NOW. The Tab5 publishes its state periodically and after accepted commands, inside the authenticated version-4 session; its `signal_valid` bit remains clear because the available dBFS reading is not calibrated dBm.

## Pairing and current validation

On the Tab5, open Settings → Companion → **CONNECT ORCDIAL** (or **RE-PAIR ORCDIAL** for a saved peer) to open a 60-second pairing window. The local USB serial equivalent is `RTL_ORCDIAL_PAIR START`; `RTL_ORCDIAL_STATUS` is read-only. On the M5Dial Home/Settings screen, hold the encoder for four seconds or use its Connect touch screen. Pairing uses a numeric comparison: both screens show a six-digit code and you confirm on both devices. Each device then stores a unique identity and a per-pair secret, and every session is authenticated and encrypted (see [secure pairing](docs/SECURE_PAIRING.md)). The published `v0.1.0-beta.1` tag still uses the earlier MAC-only prototype, which is **not encrypted or authenticated**; do not mix its firmware with this version.

The Dial PlatformIO build and Tab5/P4 and C6 ESP-IDF builds succeeded locally and were flashed. Host protocol/controller assertions passed earlier. On the clean startup-recovery build, selecting FM and rotating one detent changed the authoritative Tab5 frequency from 96.2 to 96.3 MHz; the Dial reported acknowledgment 266 with no command pending. The single-build release packaging, channel changes, offline-router recovery, and prolonged operation still need acceptance. See [PROTOCOL.md](PROTOCOL.md) and [ORCSDR_ESPNOW_INTEGRATION_PLAN.md](ORCSDR_ESPNOW_INTEGRATION_PLAN.md).

Startup recovery: the Tab5 defers accessory RPCs until after its splash and initial startup, and the C6 drops incoming accessory discovery until the P4 announces readiness. The C6 handles Wi-Fi station stop and start events to rebuild its ESP-NOW endpoint. Router Wi-Fi association temporarily defers accessory handling. Individual restart/rejoin tests passed on channel 11. An earlier synchronous-relay build reset after simultaneous startup. The later worker-queue build passed pairing, acknowledged FM tuning, and a simultaneous startup observed through 128 seconds with both devices connected and active audio counters reporting zero drops. This does not establish long-term stability or explain the earlier watchdog reset. Settings screenshots now verify the Companion pairing control and Connectivity spacing; their capture path was fixed after a reproducible stack fault. Offline-router behavior and audible audio remain unverified.

USB lifecycle control: `ORCDIAL_RESTART` restarts the Dial and resumes normal discovery without clearing its saved peer.
