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

Limits of that evidence: run 2 recovered from the given-up state; it was not a fresh outage of its own from start to finish, and the stream resuming afterwards was not part of it (the Pi was on another network). The pause defect was seen once and fixed from the code; a reboot with no saved network was not rerun after the fix, so it is not proven. The unit also reset once while diagnosing (cause not established).

## What it does not do

- It does not make Wi-Fi itself more reliable; it only stops the Tab5 from giving up.
- It does not touch a Wi-Fi network the user turned off or disconnected on purpose.
- Re-joining after a reset needs a saved network. A forgotten network is gone, and the Tab5 does not guess.

Details: [control surface](RTL_TCP_CONTROL_SURFACE.md), [test plan iterations 47 to 49](RTL_TCP_RELEASE_TEST_PLAN.md).
