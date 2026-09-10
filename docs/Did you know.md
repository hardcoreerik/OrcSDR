# OrcSDR — Did You Know? / Quick Tips

Source list for the startup quick-tip popup. 50 entries, mixing user tips and
short "did you know?" SDR-theory notes tied to specific OrcSDR screens.

The popup:

- Appears about 1 second after the UI is up.
- Is non-blocking — the user can dismiss it or leave it and start tapping
  immediately.
- Has a "Don't show this again" checkbox that disables it from future boots.
- Cycles a different randomly-selected entry each launch.

## Getting started

1. **Tip:** Your first "it works" moment is FM — pick a strong local station,
   look for a peak on the spectrum, and raise the header volume.
2. **Tip:** Make sure your RTL-SDR dongle is plugged in.
3. **Did you know?** An RTL-SDR turns radio waves into a stream of *IQ samples*
   — complex numbers OrcSDR then demodulates in software. Nothing about the
   radio is fixed to one mode; the same hardware does FM, ADS-B, LoRa, POCSAG,
   and P25.
4. **Tip:** OrcSDR boots straight to Home — no wait screen. If a dashboard
   looks blank at first, give the SDR one full second to enumerate on USB.
5. **Did you know?** Everything the touchscreen does can also be driven over
   serial. See the **Serial Command Reference** in the wiki if you want to
   script tuning, scans, or file transfers.

## Spectrum & waterfall theory

6. **Did you know?** The **spectrum** plot shows *how much energy is present
   at each nearby frequency, right now*. A peak means a signal; a flat green
   floor is noise.
7. **Did you know?** The **waterfall** is the spectrum over time — new lines
   at the top, older lines scrolling down. A steady vertical stripe = a
   station that's been on the whole time; a diagonal stripe = something
   drifting or moving.
8. **Did you know?** Levels are shown in **dBFS** (decibels relative to full
   scale). 0 dBFS is the strongest the receiver can measure without clipping;
   every -6 dBFS is about half the amplitude.
9. **Did you know?** **SNR** (signal-to-noise ratio) is just "how far above
   the noise floor is my signal, in dB." OrcSDR's health screens show this so
   you can tell if a decode failure is *no signal* or *too much noise*.
10. **Tip:** Widen the spectrum **span** to hunt for activity. Narrow it back
    down once you've found a target — narrower span = finer resolution.

## FM Radio

11. **Tip:** **Step ±** moves one channel at a time; **Seek ±** hunts for the
    next usable station. Use Seek when you're new to an area.
12. **Tip:** Tap a visible peak on the spectrum to jump straight to it —
    faster than typing a frequency.
13. **Did you know?** FM broadcast is **wideband FM** (~200 kHz per station).
    OrcSDR down-mixes the RTL's IQ stream, filters to one station's bandwidth,
    and demodulates the audio — the same steps a car radio does, just in
    software.
14. **Did you know?** **RDS text** rides on a 57 kHz subcarrier inside the FM
    signal. It only shows up after a clean lock for a few seconds — weak
    stations may never provide reliable text.
15. **Tip:** If audio stutters, open FM's **RF Health**. Rising USB overruns,
    audio underruns, or consumer drops all mean the RF path is falling
    behind — not that the station is bad.
16. **Tip:** Home remembers your recent dashboards and keeps live spectrum,
    waterfall, frequency, and volume close by. It's the safe place to return
    to.

## ADS-B Aircraft

17. **Tip:** Set your **latitude & longitude** in Settings before opening
    ADS-B — range and bearing depend on it.
18. **Did you know?** ADS-B is **Mode-S Extended Squitter** on **1090 MHz** —
    aircraft transmit position, altitude, velocity, and their **ICAO 24-bit
    hex address** every second or so. OrcSDR decodes those messages and plots
    them.
19. **Tip:** For a homebrew **1090 MHz dipole**, each leg is a quarter
    wavelength ≈ **6.9 cm** (2.7"). Two of those, back-to-back, oriented
    vertically, with a clear sky view, will get you real traffic.
20. **Tip:** The callsign in the ADS-B card is the **flight number / ICAO
    callsign**, not the FAA **N-number** tail registration. Cross-reference
    the ICAO hex on the FAA registry to find the tail number.
21. **Tip:** Cross-check what you're seeing on **flightradar24.com** for your
    area — if OrcSDR shows the same aircraft, your receiver is working
    end-to-end.
22. **Tip:** Radar empty? Check **Stats** first — it tells you whether Mode-S
    messages are arriving at all. Zero messages = antenna or placement issue,
    not decoder.

## LoRa Monitor

23. **Did you know?** OrcSDR's LoRa Monitor is **receive-only**. It does not
    join a mesh, does not transmit, and **cannot decrypt user-to-user
    Meshtastic DMs** — you'll see channel/protocol frames and public info,
    not private message content.
24. **Tip:** An empty **Nodes** page just means no verified node has been
    received yet. Nothing's broken.
25. **Tip:** Match your region, spreading factor, and bandwidth to local
    Meshtastic traffic before expecting decodes.
26. **Did you know?** LoRa uses **chirp spread spectrum**. A higher
    **spreading factor** = longer range and better weak-signal decode, but
    slower throughput. That's why matching SF matters.

## POCSAG Pager Monitor

27. **Tip:** Edit `/orcsdr/pocsag_scan.cfg` on the SD card to build your own
    regional frequency profile — one Hz frequency per line, with `#` comment
    lines for source/date.
28. **Tip:** During a scan, OrcSDR dwells **4 seconds per candidate** and
    only locks after seeing BCH-valid data. A "corrected-only" result is
    weak evidence, not a confirmed channel.
29. **Did you know?** **POCSAG** is a simple FSK paging protocol. Pagers
    listen for their **CAPCODE** (a unique address) and only wake up when
    their code appears in the stream — that's why one channel serves
    thousands of pagers.
30. **Tip:** The Session tab is intentionally **RAM-only** — it clears on
    restart. Retained history lives in the main inbox.
31. **Did you know?** The CAPCODE directory is built **only** from pages the
    Tab5 has decoded locally — it never phones home and never assumes a
    transmitter identity.

## P25

32. **Did you know?** P25 Phase I **clear voice** is verified. Encrypted
    calls are **detected and muted** — OrcSDR does not decrypt them.
33. **Did you know?** P25 systems use a **control channel** that assigns
    talkgroup calls to voice channels on the fly. That's why OrcSDR follows
    the control channel first, then hops to the assigned voice frequency for
    each call.
34. **Tip:** **HOLD** binds to the displayed talkgroup. **HOLD NEXT** waits
    until the next eligible grant arrives — useful when a channel is quiet.
35. **Tip:** Use the Spectrum view to visually locate a strong control
    channel before tuning.

## RF Lab

36. **Did you know?** RF Lab is an **implemented test bench**, not a
    calibrated instrument — values are dBFS unless you've done the reference
    workflow.
37. **Tip:** **Never** enable bias tee with a DC-short load connected. The UI
    requires a deliberate hold before the request queues, for exactly this
    reason.
38. **Did you know?** Bias tee sends **~4.5 V DC up the coax** to power an
    active antenna or LNA. Great for weak signals — dangerous for anything
    DC-shorted.
39. **Tip:** RF Lab sessions land under `/orcsdr/rf_lab/<session>/` on the SD
    card — CSV readings, screen bitmap, and a session JSON, all together.
40. **Did you know?** If the source disappears mid-run, RF Lab marks the
    session **inconclusive** rather than silently keeping bad data.

## Driver, hardware & antennas

41. **Did you know?** OrcSDR is powered by the open-source
    [esp-rtl-sdr driver](https://github.com/hardcoreerik/esp-rtl-sdr) — a
    clean-room ESP-IDF component maintained as its own project.
42. **Did you know?** OrcSDR does **not** use `librtlsdr` source, register
    tables, or GPL driver ports. It's a clean-room implementation based on
    public docs and independently observed behavior.
43. **Tip:** The single biggest upgrade for any band is a **better antenna,
    of the right type, in a better spot** — not a settings tweak. A resonant
    antenna at a window beats any amount of gain fiddling on the wrong one.
44. **Did you know?** A **half-wave dipole** is two quarter-wave legs,
    back-to-back. Quarter-wave length in meters ≈ 75 / freq(MHz). At 100 MHz
    FM ≈ 75 cm per leg; at 433 MHz ≈ 17 cm; at 1090 MHz ≈ 6.9 cm.
45. **Tip:** RF noise is worse indoors than out, and near USB-C chargers,
    laptops, and LED lights. If a signal looks weak, moving 2 meters can
    beat any config change.

## Settings & the project

46. **Tip:** SDR not detected? Power off, reseat the receiver on the **USB
    Host** port, power on, and try FM first before a specialist dashboard.
47. **Did you know?** OrcSDR is designed to work as a **standalone radio**.
    Companion Wi-Fi and web-console features are optional add-ons, not
    required for reception.
48. **Tip:** Reporting a problem? A screenshot of the relevant **RF Health**
    page usually explains more than a paragraph of description.
49. **Did you know?** Everything you touch on-screen also has a **serial
    command** — the touch handlers and the CLI call the same underlying
    functions, so you can automate anything you can tap.
50. **Did you know?** **PPM correction** in RF Lab compensates for the RTL's
    crystal being slightly off frequency. If ADS-B messages are close but not
    decoding, or FM stations sit slightly off center, a small PPM tweak can
    fix it.

## RF Visualizer — modes & navigation

51. **Tip:** The **VIS** button in the top-right of every dashboard opens the
    **RF Visualizer** — twelve different ways to look at the same live IQ
    stream coming off your dongle.
52. **Tip:** Inside the visualizer, tap **◀ / ▶** in the HUD to step through
    modes, or tap the **title bar** to pop up the full **3×4 chooser grid**
    and jump straight to any view.
53. **Did you know?** The twelve visualizer views are **Spectrum**,
    **Waterfall**, **Phosphor Persistence**, **3D Spectrum History**, **I/Q
    Constellation**, **I/Q Oscilloscope**, **Polar / Phase**, **Channel
    Occupancy**, **Peak Hold / Average**, **Doppler / Drift**, **Channelized
    Tiles**, and **Audio Spectrogram**. Each is a different lens on the same
    IQ data.
54. **Tip:** Tap **FREEZE** in the visualizer HUD to pause the display without
    stopping reception — great for reading a fleeting peak or comparing two
    moments. Audio and decoding keep running underneath.
55. **Tip:** **Long-press anywhere** in the visualizer (about ⅔ of a second)
    to lock the HUD open and enter **inspect mode** — the HUD stops
    auto-hiding so you can read values, tap controls, or open the drawer
    without racing the timeout. Long-press again to unlock.

## Handy serial commands

56. **Tip:** `RTL_HELP` prints the built-in command list. It's the fastest
    way to see what your firmware supports right now — if a feature isn't in
    the list, it isn't in the build.
57. **Tip:** `RTL_STATUS` reports whether the RTL-SDR dongle is actually
    enumerated (VID, PID, USB speed, serial). Use it as the first check
    whenever a dashboard looks blank — it tells you "dongle problem" vs
    "software problem" in one line.
58. **Tip:** `RTL_TUNE FM 96100000` (or `AM`, `WX`, `LORA`, `ADSB`, `P25`)
    tunes the radio from a laptop over USB. Within the same band, use the
    lighter `RTL_FREQ 96100000` — it's the fast path for stepping and
    scanning without restarting the capture.
59. **Tip:** `RTL_REC_START` begins a WAV recording of the demodulated audio
    and `RTL_REC_STOP` writes it to the SD card. Great for capturing a
    weather net, a POCSAG burst, or a P25 clear-voice call for later review.
60. **Tip:** `RTL_UI OPEN HOME|FM|ADSB|LORA|P25|RF_LAB` drives the app to
    any dashboard over serial — the same handlers the touchscreen uses. Pair
    with `RTL_UI ACTION` to script the exact button taps a workflow needs.

## Which visualizer for which job

61. **Tip:** **Waterfall** is for **identifying what a signal is by its
    shape**. A steady vertical stripe = a station always on; a diagonal
    stripe = something drifting or moving; a fat pillar of hash = wideband
    (FM, chirp, spread spectrum); short horizontal dashes = bursts (LoRa,
    paging, digital voice grants).
62. **Tip:** **Phosphor Persistence** reveals **weak or intermittent signals
    the plain spectrum misses**. Bright pixels are frequencies that keep
    coming back; a faint smear that stays lit across many frames is
    something real hiding under the noise floor.
63. **Tip:** **I/Q Constellation** is your **digital signal quality meter**.
    A tight, well-separated cluster of dots = clean decodable signal; a
    smeared or rotating cloud = fading, interference, or the wrong
    demodulator settings. Especially useful with P25 and other digital
    modes.
64. **Tip:** **Channel Occupancy** answers "**which channels around here
    actually carry traffic?**" It shows what percentage of time each slice
    of the band was active. Perfect for building a POCSAG or LoRa scan list
    without guessing.
65. **Tip:** **Peak Hold vs Average**: Peak stacks the loudest level ever
    seen at each frequency — use it to catch **intermittent** transmitters
    (a pager burst, a control-channel grant). Average smooths the trace —
    use it to see **steady** stations clearly through noise.
