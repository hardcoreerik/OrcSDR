# P4 Home action owner boundary

`home_app::request_web_action(name, value, &request_id)` admits a bounded Home
VFO request. Success means **queued**, not a changed receiver or successful RF
tune. A four-entry FreeRTOS queue is drained by the existing UI task and calls
the same `handle_action` used by native touch. Capabilities are rechecked on
that task, including actual tuner gain steps, mode selection, fixed step/filter
restrictions and the active screen. Speaker volume/mute and screen changes are
not browser Home actions; browser audio must have its own stream.

The same path is available through authenticated serial commands:

```
RTL_HOME ACTION tune 96100000
RTL_HOME ACTION step_up
RTL_HOME ACTION step_down
RTL_HOME ACTION step_size_up
RTL_HOME ACTION span_down
RTL_HOME ACTION mode_set 0
RTL_HOME ACTION filter_set 0
RTL_HOME ACTION gain_auto
RTL_HOME ACTION gain_tenth_db -99
RTL_HOME ACTION rtl_agc 0
RTL_HOME RESULT <request-id>
```

Available names are the twelve entries in `home_web_actions.hpp`. A manual gain
must match a step reported by the connected tuner; the example is not a claim
that every tuner supports -9.9 dB. A zero filter resets to the native standard
when that band has configurable filters. Unsupported or malformed requests
must not silently become other actions.

`RTL_HOME_ACTION_QUEUED id=N` acknowledges admission.
`RTL_WEB_HOME_ACTION id=N ... dispatch=...` reports owner validation/dispatch.
`RTL_HOME RESULT N` queries the last eight completed dispatches in this boot.
Unknown/pending/expired results are explicit and are never treated as success.
Clients must correlate their request ID, not assume that a global last-result
belongs to them. Inspect `applied_hz`/receiver state separately: native dispatch
can precede an asynchronous driver tune failure or successful retune.

Off-task Home status readers receive one critical-section-protected published
copy. UI-owner callers can read current owner state. `ui_ready` and `ui_failed`
distinguish startup from usable UI and unsupported-viewport failure. Before the
UI is ready, requests are unavailable. The failed-start path clears the owner
before its task handle can be recycled and releases its pre-admission queue.
The queue and eight completion records have fixed storage; no screen-sized
buffer or new per-request worker is allocated.

## Verification boundary

`bash tools/test-p4-home-actions.sh` compiles real parsing/capability logic with
the existing filter presets. It checks forbidden speaker/navigation controls,
unknown/malformed actions, tune limits, fixed controls, mode and filter values,
negative tuner steps and unplugged-device restrictions. It is not an RTOS queue,
HTTP authentication or physical-radio test. Native compilation and serial
device checks must be recorded separately. The web adapter and remaining web
endpoints are a separate integration slice; this API alone is not a web UI.

Native ESP-IDF 5.5.4 build and COM32 application flash passed with esptool hash
verification. The private `home-owner-device-test-applied.log` records the real
driver at 96.1 -> 96.3 -> 96.1 MHz, correlated owner receipts, invalid filter and
malformed mode rejection, Home-context refusal while LoRa owned the screen,
and unchanged speaker mute/volume. It does not establish sustained audio,
unsupported-viewport runtime recovery, HTTP authentication or browser parity.
