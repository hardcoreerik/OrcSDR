# Wi-Fi recovery for a remote receiver (the "wedge" and its fix)

Applies to the Tab5 build on `claude/orcsdr-rtl-tcp-interop-f12b5d` (PR 240). A remote `rtl_tcp` receiver depends on Wi-Fi the whole time it runs, so a Wi-Fi outage must end by itself.

## The problem

After a long run the Tab5's Wi-Fi sometimes stopped coming back: scans failed, connecting a saved network did nothing, and only a full reset recovered it. Two defects caused this.

1. **Bounded retries.** After a link loss the Tab5 retried Wi-Fi 5 times (3 s, 10 s, 30 s, 60 s, 120 s: about 3.7 minutes), then logged `RTL_WIFI_RECONNECT_GIVE_UP` and stopped. The C6 link recovery has its own budget of 3 tries. Both budgets refilled only after the link had been up for 5 minutes, so an outage longer than about four minutes (an access point restarting, a phone hotspot switched off) left the Tab5 offline for good, even when the network came back. A remote receiver then sat idle until someone reset the unit.
2. **"Radio pause failed".** Every Wi-Fi scan or connect first pauses the radio and waits up to 5 s for the receiver to stop. With a remote source selected and Wi-Fi down, the receiver could be left marked `queued`: nothing can start a stream with no usable source, and only the driver task clears `queued`, so the wait always timed out. Each failure stalled the main loop for 5 s, used up one reconnect attempt, and Wi-Fi could not be set up from the touch screen at all (seen after a reboot with no saved network).

## The fix

1. **Keep Wi-Fi alive** (Settings > RTL_TCP, serial `RTL_TCP WIFIKEEP 0|1`). **On by default.** While a remote receiver is selected (source set to remote, or automatic with no dongle plugged in, and a server configured), each 5 minutes Wi-Fi stays down re-arms both retry budgets. It reuses the existing reconnect and C6 link-recovery code and adds no new reset path. It does nothing while a local USB dongle is the receiver. A Wi-Fi power-off or disconnect chosen by the user disarms the reconnect, and the keep-alive does not re-arm it.
2. **A stream nothing can start no longer blocks the pause.** The Wi-Fi pause waits only on a stream that is running, or queued with a usable source; with no usable source it clears `queued`. After a Wi-Fi operation the radio resumes only when a source can use it.
3. **Evidence.** While a remote receiver may need Wi-Fi the Tab5 logs `RTL_WIFI_HEALTH` once a minute (connected, ready, link failed, reconnect attempts, recoveries, seconds down, internal DMA RAM, stream state). `RTL_WIFI_KEEPALIVE rearm` marks each re-arm, and `RTL_RADIO_PAUSE_FAILED` records the receiver state when a pause gives up.

Timing: a keep-alive re-arm needs 5 minutes of downtime and 5 minutes since the previous re-arm, so the worst case from "network back" to "Tab5 back" is about 5 minutes plus the reconnect delay (3 s, then the normal backoff). The cost of leaving it on is one Wi-Fi attempt every few minutes while the network is down.

## Evidence (2026-11-10, COM17, phone hotspot as the access point)

| Run | Setup | Result |
|---|---|---|
| 1 | Keep-alive OFF, hotspot off for about 9 minutes | Link lost 14:22:02, 5 retries, `GIVE_UP` at 14:26:01. Hotspot back at about 14:31: still offline at 14:33 (`reconnect_attempts=5`, down 614 s). Wedge reproduced. |
| 2 | Keep-alive ON, started from that stuck state with the hotspot already back | `RTL_WIFI_KEEPALIVE rearm down_s=670`, reconnected 7 s later (`RTL_WIFI_CONNECTED`). |

Limits of that evidence: run 2 recovered from the given-up state; it was not a fresh outage of its own from start to finish, and the stream resuming afterwards was not part of it (the Pi was on another network). The repeatable test below covers both. The pause defect was seen once and fixed from the code; a reboot with no saved network was not rerun after the fix, so it is not proven. The unit also reset once while diagnosing (cause not established).


## Repeatable outage test (no router involved)

`tools/rtl_tcp_wifi_outage_test.py` runs the whole scenario from the PC over serial. It uses the firmware test command `RTL_WIFI_OUTAGE <seconds>` (authenticated, 0 cancels, at most 1800): the access point "vanishes" for that long. A live link is dropped with a real disassociation, and every connect request asks the C6 to join a network that does not exist, so the C6 runs its real scan-and-fail path (reason 201, no AP found); when the time is up the next real connect works. The reconnect, give-up, keep-alive re-arm and stream-reconnect code all run for real. What is simulated is only "the access point is gone"; the C6 link recovery (`link_failed`) is not exercised. Nothing is persisted and a reset clears it.

```
python tools/rtl_tcp_wifi_outage_test.py --keepalive 1 --outage 360                 # expect: recover
python tools/rtl_tcp_wifi_outage_test.py --keepalive 0 --outage 360 --expect wedge  # expect: gives up (the old bug)
```

It needs the Tab5 on Wi-Fi with the remote stream streaming, opens the FT8 dashboard on 14.074 MHz, prints a timeline, exits 0 when the outcome matches `--expect`, and restores the lab (keep-alive ON, waits for the Tab5 to come back). `--log FILE` keeps every serial line. About 11 minutes per run.

| Run (2026-11-10, COM17) | Result |
|---|---|
| Keep-alive ON, 360 s outage | Link lost 2.8 s, gave up at 238 s, keep-alive re-armed at 303 s, **Wi-Fi back at 417 s (57 s after the outage ended), stream streaming again at 430 s**, 30 s later `streaming`, link 100%, bytes increasing. PASS |
| Keep-alive OFF, 360 s outage | Gave up at 236 s, no re-arm, still offline 150 s after the outage ended. PASS (the old bug, reproduced on demand) |

What the test found while it was being built:

- **The stream did not restart after Wi-Fi recovered.** Wi-Fi came back with keep-alive ON, but the remote stream stayed idle. The Wi-Fi work pauses the radio and records that it should resume; with no usable source (Wi-Fi down) the resume request was dropped, and nothing restarted the stream once Wi-Fi returned. Fixed: the resume now stays pending (`RTL_RADIO_RESUME deferred stream restarted`) until a source is usable. This is the gap the manual hotspot test could not see, because the Pi was not reachable from the hotspot.
- **The C6 channel can stop answering.** An earlier version of the simulation skipped the radio's connect request. In 2 of 3 runs the ESP-Hosted channel to the C6 then stopped replying (`eh_host_feat_rpc: request: no response ... (5000 ms)` every few seconds, each stalling the main loop 5 s) and never recovered without a hardware reset; `link_failed` did not detect it, so the C6 link recovery never ran. That simulation was unfaithful, and with the faithful one (0 hangs in 2 runs) it did not recur, so this may have been caused by sending repeated disconnects with no connect in between. **But it matches the original field symptom** (scans failing, connect doing nothing, only a reset helping). It is not understood and not fixed: the Tab5 has no detection for "Hosted RPC stopped answering". It deserves its own issue and a test that makes the C6 stop answering.

## What it does not do

- It does not make Wi-Fi itself more reliable; it only stops the Tab5 from giving up.
- It does not touch a Wi-Fi network the user turned off or disconnected on purpose.
- Re-joining after a reset needs a saved network. A forgotten network is gone, and the Tab5 does not guess.

Details: [control surface](RTL_TCP_CONTROL_SURFACE.md), [test plan iterations 47 to 49](RTL_TCP_RELEASE_TEST_PLAN.md).
