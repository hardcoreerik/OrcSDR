# rtl_tcp status and control surface (for OrcSDR Web)

What the rtl_tcp code on `claude/orcsdr-rtl-tcp-interop-f12b5d` exposes today, so the web routes can mirror it. Source of truth
is the code named in each section; this page was written from it on 2026-11-07. Nothing here is a web API yet: today the
surface is the authenticated serial/console command set. A web route should call the same module functions, not re-implement
them.

Two independent roles, both optional:

| Role | Module | What it does |
|---|---|---|
| Client ("remote receiver") | `ui/rtl_tcp_source.*`, `ui/rtl_tcp_session.*` | Receive IQ from someone else's `rtl_tcp` server (a Raspberry Pi, a PC) |
| Server | `ui/rtl_tcp_server.*`, `ui/rtl_tcp_server_session.*` | Serve the Tab5's own dongle to SDR++, Gqrx, SatDump |

## Safety rules (hard)

- rtl_tcp has no authentication or encryption. **The server is off by default**, enabling is an explicit, saved, authenticated
  action, and it must never be reachable through, or enabled by, the web login session without a separate explicit action.
- The server binds only to an address the owner names (`wifi` = the Tab5's own Wi-Fi address, or an IPv4). There is no default
  and `0.0.0.0` is refused.
- One client owns the dongle at a time; others are refused. While a client is connected the local UI cannot start its own stream.
- A remote client never switches the bias-T.
- Tune and rate limits come from the driver and the dongle, not from this code: the server checks a client's sample rate with
  `esp_rtl_sdr_is_rate_supported()` and counts refusals; it does not carry a rate table of its own. (The client side asks
  the remote server for rates from its own ladder, below, and cannot know what that server's dongle supports.)

## Server: `RTL_TCPD`

Commands (authenticated): `RTL_TCPD STATUS` | `RTL_TCPD ENABLE <ipv4|wifi> [port]` | `RTL_TCPD DISABLE`.

| Item | Value |
|---|---|
| Default state | disabled (`enabled=0`, state `stopped`) |
| Port | 1-65535, default 1234 |
| Bind | an IPv4 address or `wifi`; never `0.0.0.0` |
| Persistence | enable/disable and bind/port are saved |
| States (`state=`) | `stopped`, `listening`, `serving`, `failed` |
| Errors | `RTL_TCPD ENABLE error=give an IPv4 address or 'wifi' (never 0.0.0.0) and a port 1-65535`; `RTL_TCPD error=commands are STATUS, ENABLE <ipv4\|wifi> [port], DISABLE` |

`RTL_TCPD STATUS` reply fields: `enabled` (0/1), `state`, `bind` (host:port; `wifi` is resolved to the address in use), `client`
(0/1: a client is connected), `rate` (S/s of the live stream), `freq` (Hz), `accepted`, `refused`, `stalled` (clients dropped
for not reading), `applied`, `unknown`, `unsupported` (commands refused as outside the dongle's range), `errors`, `sent`
(bytes), `iq_overrun` (IQ bytes the network could not take), `select`, `accept_fail`, `errno`.

Client-supplied commands handled (rtl_tcp protocol ids 0x01-0x0e): frequency, sample rate, gain mode, gain, frequency
correction, IF gain, test mode, AGC, direct sampling, offset tuning, RTL and tuner xtal, gain by index, bias-T (ignored by
policy). Unknown ids are counted in `unknown`, not fatal.

## Client: `RTL_TCP`

Commands: `RTL_TCP STATUS` | `SOURCE <auto|usb|remote>` | `HOST <ipv4-or-name> [port]` | `BENCH <1-120 s>` | `BENCHUDP <s> [port]`.
`SOURCE` and `HOST` are saved; `SOURCE` takes effect on the next start; `HOST` needs no active stream.

| Item | Value |
|---|---|
| Default mode | `auto` (a USB dongle is used if present) |
| Port | 1-65535, default 1234; host must be an IPv4 address or host name |
| States (`state=`) | `idle`, `connecting`, `streaming`, `failed` (after bounded reconnect: 8 attempts, 0.5 s doubling to 15 s) |
| Errors | `RTL_TCP SOURCE error=use auto, usb or remote`; `RTL_TCP HOST error=need an IPv4 address or host name, a port 1-65535, and no active stream` |

`RTL_TCP STATUS` reply fields: `mode`, `server` (host:port), `state`, `last` (last end reason), `tuner`, `gains`, `bytes`
(received), `dropped` (bytes dropped locally), `connections`, `reconnects`, `errno`, `link` (percent of the needed byte rate
that is arriving; 0 = not measured, below 85 = behind real time).

Live rate: wide bands ask a remote server for 1.44 MS/s, other bands 960 kS/s. When `link` stays under 85% for about 8 s
the rate steps down 1.44 -> 0.96 -> 0.24 MS/s and the stream restarts; after about 60 s healthy it probes one step up (wait
doubles after a failed probe, up to about 16 min). Every rung is a rate an RTL2832 can run (225,001-300,000 and
900,001-3,200,000 S/s) and a whole multiple of 240 kS/s. ADS-B keeps 2.048 MS/s. Serial events: `RTL_TCP_RATE_DOWN`,
`RTL_TCP_RATE_UP` (`link=` and `new_rate=`).

## Suggested web mirror

| Web item | Backing call | Note |
|---|---|---|
| Server on/off, bind, port | `RTL_TCPD ENABLE/DISABLE/STATUS` equivalent | on = a deliberate action with a "no authentication" warning; never implied by login |
| Server clients | `client`, `accepted`, `refused`, `stalled`, `bytes_sent`, `iq_overrun` | one client at a time, so "clients" is 0 or 1 |
| Client source and host | `RTL_TCP SOURCE/HOST` | host change refused while streaming |
| Client connection state | `state`, `last`, `reconnects`, `link`, live rate | the Home header already shows RTL-TCP, LIVE / BEHIND nn% / CONNECTING and the rate |

## Network configuration changes to flag

This branch changed shared network settings; the web server uses the same Wi-Fi and lwIP setup. There is no
`sdkconfig.net-throughput` file on this branch: the changes are in `apps/orcsdr-tab5/sdkconfig.defaults` (Tab5) and
`orcdial/c6/sdkconfig.defaults` (C6 firmware, which changes the release-owned C6 image provenance).

- Wi-Fi power save off (`esp_wifi_set_ps(WIFI_PS_NONE)` after start).
- lwIP: TCP window 262140 with window scaling (scale 3), TCP/UDP/tcpip mailboxes 64, TCP send buffer 65534, SACK out,
  tcpip task on core 1.
- ESP-Hosted SDIO: TX queue 16, RX queue 8, RX staging slots 4, clock 20 MHz (passed two 73 MB HTTPS installs; not a long soak).
- Cost: DMA headroom fell from about 28.1 KB to 25.4 KB (`RTL_DRAM_BUDGET`); internal RAM is the scarce resource.
- C6: static RX 16, dynamic RX/TX 64, AMPDU on, BA windows 32.
- Five local ESP-Hosted patches are applied by `tools/apply-esp-hosted-trampoline-fix.ps1` (3.0.6).

Open: memory right-sizing of these buffers is planned (see `docs/RTL_TCP_INTEROP.md`); nothing here is web-specific.
