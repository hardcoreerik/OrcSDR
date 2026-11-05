# Testing a remote rtl_tcp receiver with OrcSDR (community procedure)

For the harbour receiver in [issue 179](https://github.com/hardcoreerik/OrcSDR/issues/179).
This is a test build feature, not a release. Please report what you see, including
failures. Nothing here needs your Wi-Fi password, router login or any account.

## What you need

- An M5Stack Tab5 running an OrcSDR build that contains the `rtl_tcp` client.
- Your receiver already serving `rtl_tcp` on a **local** network the Tab5 can join.
- The server's local IPv4 address (for example `192.168.1.50`) and port (default 1234).

## 1. Keep the server off the internet

`rtl_tcp` has no password. Anyone who can reach the port can tune your radio and read
its samples.

- Bind it to your local address only: `rtl_tcp -a 192.168.1.50 -p 1234`.
  Do not use `-a 0.0.0.0`, and do **not** forward the port on your router.
- If the Tab5 must be on a different network, use a VPN you already trust. Do not
  open the port to the internet.

## 2. Check the server first (no Tab5 needed)

From any Linux or Windows-with-WSL machine on the same network, using the probe in
`apps/orcsdr-tab5/tools/rtl_tcp_probe.cpp`:

```bash
g++ -std=c++17 -O2 -pthread -Iapps/orcsdr-tab5/ui apps/orcsdr-tab5/tools/rtl_tcp_probe.cpp \
  apps/orcsdr-tab5/ui/rtl_tcp_session.cpp apps/orcsdr-tab5/ui/rtl_tcp_protocol.cpp -o rtl_tcp_probe
./rtl_tcp_probe 192.168.1.50 1234 100100000 960000 8
```

Expected: `PROBE OK`, the tuner name, and an effective rate close to the one requested.
Send me those lines. If it prints `NO_IQ` or "not an rtl_tcp server", fix that before
trying the Tab5.

Known server behaviour: some `rtl_tcp` builds need a few seconds after a client
disconnects before they accept the next one, and some stop if the sample rate is changed
while streaming. If a second attempt hangs, restart the server.

## 3. Point the Tab5 at it

On the Tab5: **Settings > Radio Defaults**. Under *Remote server* press **EDIT**, type the
server's address (and `:port` if it is not 1234) and press **SAVE**. Then press **CHANGE** next
to *Receiver source* until it reads **REMOTE RTL_TCP**. The line under the rows shows
STREAMING once connected (or BEHIND REAL TIME if the link is too slow for the rate). The new
source takes effect the next time a stream starts.

The same can be done over the USB serial console (115200 baud, same port used for flashing),
after pairing as usual:

```
RTL_TCP HOST 192.168.1.50 1234
RTL_TCP SOURCE remote
RTL_TCP STATUS
```

`SOURCE` takes `auto`, `usb` or `remote`. `auto` prefers a plugged-in dongle and only
falls back to the saved server after about 8 seconds without one. Unplug any USB
receiver for a clean test. The settings are saved; the Tab5 starts the remote stream by
itself once Wi-Fi is up.

## 4. What to report

- `RTL_TCP STATUS` after a minute: state, `bytes`, `dropped`, `reconnects`.
- Which dashboard you used and whether audio or the spectrum looked right.
- The Wi-Fi band and signal strength at the Tab5.

## Known limits (please read)

- **Rate is the main limit.** The Tab5 reaches the network through its Wi-Fi coprocessor.
  Our measurement on the bench was about 1 MB/s with the stock settings, so bands that
  want 2.4 MS/s (4.8 MB/s) will stutter. Slower bands (960 kS/s) are the realistic target.
- `rtl_tcp` never confirms a command. A tune is "sent", not "applied".
- A TCP link that never drops does not prove the radio lost nothing. Samples the
  server itself discarded are invisible to us.
- Only one client at a time can use a standard `rtl_tcp` server.
