# OrcDial accessory

OrcDial is an **optional** M5Dial controller for OrcSDR. The standard OrcSDR build includes the dormant Dial bridge and its ESP-NOW-capable ESP-Hosted C6 image. No separate OrcSDR download is needed. The normal Tab5 radio, Wi-Fi, and dashboard paths remain authoritative when no Dial is paired.

The Dial shows the Orc badge for five seconds, then OrcSDR Home and a side carousel of current dashboard destinations. The selector embeds illustrated dashboard pictures, while the full dashboard views retain their code-drawn artwork. Tuner views offer Reel, Dial, Odometer, Tape, and Split frequency graphics. The renderer uses a small buffered canvas; the user confirmed the selector pictures are readable and rotation stays smooth.

## Build

Build the M5Dial with PlatformIO, and optionally the ESP32 receiver stub for bench tests:

```powershell
cd F:\AI\OrcSDR-TEMP\m5dial-vfo\orcdial
& 'C:\Users\hardc\.platformio\penv\Scripts\pio.exe' run -e dial
& 'C:\Users\hardc\.platformio\penv\Scripts\pio.exe' run -e receiver
```

Build the standard Tab5 image with its embedded OrcDial-capable C6 image:

```powershell
cd F:\AI\OrcSDR-TEMP\m5dial-vfo
./apps/orcsdr-tab5/tools/build-tab5-idf.ps1
```

The accessory must work without requiring a C6 firmware update. The existing experimental bridge uses a custom C6 relay; that approach is under review against the no-update requirement. Keep the user's installed C6 firmware intact while investigating its supported interfaces and verifying channel alignment. The build resolver currently caches the experimental image by a hash of the local C6 sources; embedding it does not install it on the device.

Local USB automation: send `RTL_WIFI_CHANNEL` and `RTL_ORCDIAL_PAIR START` to the Tab5, then `ORCDIAL_PAIR START` to the Dial. Query the Dial with `ORCDIAL_STATUS` (link, pairing, channel, frequency). `RTL_ORCDIAL_PAIR STOP` closes the Tab5 window. Other authenticated Tab5 controls remain protected. Run `python orcdial/tests/serial_smoke.py --port COM14` for the Dial command and overflow checks; it starts discovery but never tunes.

Run `python orcdial/tests/host_probe.py --port COM17` to check the Tab5 commands and send a discovery packet through the experimental relay. `RTL_ORCDIAL_PROBE` reports the C6 relay RPC result; even `ESP_OK` does not prove over-air transmission or pairing. Neither script updates firmware.

For connected-device control tests, `ORCDIAL_ROTATE <delta>` accepts nonzero detents from -20 to 20 and uses the same dashboard/focus mapping as the physical encoder, without acceleration. `ORCDIAL_FOCUS NEXT` cycles the same focus choices as a short encoder press on a tuner. Both refuse offline control. A queued response is not proof of application: verify the acknowledgment and returned state in `ORCDIAL_STATUS`, then compare the Tab5 state. Status includes dashboard, focus, step, volume, last acknowledgment, and pending-command state.

## Controls and state

From Home, turn or press to open the dashboard carousel; turn to choose and press to open. On tuners, rotation emits frequency deltas in the dashboard's configured step, with acceleration only for frequency. A short press cycles the available frequency, step, gain, and volume focus. On channel and content dashboards, rotation emits semantic channel, radar range, candidate, slot, target, message, AP, or setting actions, never a generic RF tune. The Tab5 acknowledges only actions backed by a real handler; an unsupported action returns `ERROR`. The Dial keeps its center readout on the last authoritative Tab5 state while a command is pending.

The current Tab5 bridge handles dashboard open, FM/AM and generic Browse tuning, FM/AM step, CB channel, P25 candidate, LoRa slot, ADS-B range, and shared volume. Weather and Marine channel plans, content selection for aircraft/nodes/messages/APs, RF Lab parameters, and full Settings navigation are still pending. Their controls must not imply a successful action or display invented results. `docs/ORCDIAL_CONTROL_MATRIX.md` is the behavioral contract.

The Dial labels disconnected state `OFFLINE` and never treats its offline preview frequencies as live RF measurements. `ORCDIAL_DEMO=1` builds are explicitly labeled `DEMO` and do not start ESP-NOW. The Tab5 publishes a v3 state packet periodically and after accepted commands; its `signal_valid` bit remains clear because the available dBFS reading is not calibrated dBm.

## Pairing and current validation

On the Tab5, open Settings → Connectivity → **CONNECT ORCDIAL** to open a 60-second pairing window. The local USB serial equivalent is `RTL_ORCDIAL_PAIR START`; `RTL_ORCDIAL_STATUS` is read-only. On the M5Dial Home/Settings screen, hold the encoder for four seconds or use its Connect touch screen. Both sides save the paired MAC. This prototype is MAC-gated but **not encrypted or cryptographically authenticated**; do not treat it as a production trust boundary.

The M5Dial and receiver PlatformIO builds, C6 ESP-IDF build, and earlier P4 builds succeeded locally. Host protocol/controller assertions passed earlier. The current Dial UI was flashed on COM14 and the user confirmed it looks good. The single-build packaging path and Tab5/C6 radio link still need verification. Pairing, channel changes, reconnect, dashboard sync, radio action results, Wi-Fi coexistence, and recovery still need hardware acceptance. See [PROTOCOL.md](PROTOCOL.md) and [ORCSDR_ESPNOW_INTEGRATION_PLAN.md](ORCSDR_ESPNOW_INTEGRATION_PLAN.md).
