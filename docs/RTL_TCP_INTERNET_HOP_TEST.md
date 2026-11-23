# rtl_tcp over a real internet path: test setup and pass record

Purpose: show that the Tab5 decodes live signals from a **stock, unmodified `rtl_tcp` server that is not on its LAN**. We cannot assume control of the server, so every fix is client-side.

## Result (2026-11-10): PASS

Live FT8 on 20 m (14.074 MHz), 8 minutes, stock RTL-SDR Blog `rtl_tcp` 1.4.0 on a Raspberry Pi 4B behind a cellular connection:

| Measure | Value |
|---|---|
| Slots decoded | 31 of 31 (none skipped) |
| Decodes | 194 (mean 6.3 per slot, min 3, max 12) |
| Wire rate | 480 kB/s (240 kS/s) |
| Link | mean 100 %, per-sample range 88-109 % |
| Dropped bytes / reconnects | 0 / 0 |

Same session, same band, home LAN, 4 minutes: 105 decodes in 15 slots (7.0 per slot). The internet path costs no decodes at this rate.

Not covered: a server on a public address with no relay, durations beyond 8 minutes, rates above 240 kS/s, ADS-B (needs about 4 MB/s).

## Layout

```
 Tab5 (home Wi-Fi) --TCP--> PC relay (LAN :1236) --SSH forward over Tailscale--> Pi (phone hotspot, cellular) rtl_tcp :1234
```

- **Server:** Pi with a V4L dongle and an antenna, running the stock `rtl_tcp` as a systemd service. It binds to its own Wi-Fi address (not 127.0.0.1, not 0.0.0.0).
- **Link under test:** the Pi joined to a phone hotspot, so its traffic crosses a real cellular network. Typical round trip 40-100 ms.
- **Relay:** the Pi has no public address, so the PC reaches it over a Tailscale VPN and re-offers it on the LAN with an SSH local forward. The Tab5 cannot join the VPN, so it connects to the relay as if it were the server.
- **Client:** the Tab5 (source `remote`), FT8 dashboard.

The relay adds an extra encrypted hop and a second TCP connection. It is a stand-in for the internet, not a measurement of it; the earlier bug (iteration 38) only showed up when something sat between the Tab5 and a stock server.

## Procedure

Placeholders: `<pi-vpn-ip>` the Pi's VPN address, `<pi-wifi-ip>` its address on the hotspot, `<pc-lan-ip>` the PC's LAN address. Keep the real VPN address out of documents and commits.

1. **Safety net on the Pi.** Before moving it, schedule a fall-back to the home network so a failed hotspot cannot strand it: `sudo sh -c 'nohup sh -c "sleep 1500; nmcli connection up <home-profile>" >/dev/null 2>&1 &'`. Cancel it at the end with `sudo pkill -f 'sleep 1500'`.
2. **Move the Pi to the hotspot:** `sudo nmcli connection up <hotspot-profile>`. The LAN address disappears; reconnect over the VPN address. Confirm `ip -4 addr show wlan0` shows the hotspot address.
3. **Restart the server** if it started before the address changed: `sudo systemctl restart rtl-tcp`. It logs `serving rtl_tcp on <pi-wifi-ip>:1234`.
4. **Start the relay on the PC:** `ssh -N -o ServerAliveInterval=15 -o ExitOnForwardFailure=yes -L <pc-lan-ip>:1236:<pi-wifi-ip>:1234 <user>@<pi-vpn-ip>`. The target must be the Pi's Wi-Fi address; `127.0.0.1` is refused because the server does not listen there.
5. **Point the Tab5 at the relay:** serial `RTL_STOP`, wait a few seconds, then `RTL_TCP HOST <pc-lan-ip> 1236` (a host change is refused while a stream is active).
6. **Check the hop before measuring.** From the PC, open a socket to the relay: a 12-byte greeting (`RTL0` plus tuner type and gain count) must arrive within a second. If it does not, a client is holding the server's single slot (often the Tab5 reconnecting) or the forward target is wrong.
7. **Measure.** Open the FT8 dashboard, `FT8 BAND 14074000`, and record at least 8 minutes of slots plus the stream counters (`RTL_TCP STATUS`: bytes, dropped, reconnects, link). The helper used here, `ft8_live.py`, polls `ORC_FT8_STATUS` per slot; any script that records decodes per slot and the byte rate is equivalent.
8. **Restore.** `RTL_STOP`, `RTL_TCP HOST <pi-lan-ip> 1234`, stop the relay, `sudo nmcli connection up <home-profile>` on the Pi, restart `rtl-tcp`, cancel the safety net.

## Pass criteria

- Greeting received through the relay (step 6).
- Link at or above 95 % averaged over the run, zero dropped bytes, zero reconnects.
- Decodes per slot within the same range as the same band on the LAN (the band, not the link, sets the count).
- For a synthetic variant (no live band needed) use `tools/rtl_tcp_replay_server.py`; see the test plan, iterations 37 and 38.

## Gotchas found

- The server takes **one client**. A Tab5 left in its reconnect loop will take the slot the moment a test client lets go.
- The server binds to the Wi-Fi address. After the Pi changes network, restart it or it keeps listening on a dead address.
- A host change on the Tab5 needs the stream stopped first.
- Gain state belongs to the server and survives clients; the Tab5 sends AUTO on every new band (iteration 39). Check `iq_dbfs` (-5 to -15 dBFS is healthy on FT8) before blaming the link.
- The Pi's ssh session over the LAN address drops when it changes network; use the VPN address for all management during the test.

Related records: [test plan, iterations 38 and 44](RTL_TCP_RELEASE_TEST_PLAN.md), [changelog](../CHANGELOG.md).
