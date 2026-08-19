You are redesigning the **OrcSDR LAN Console for a large-screen Android TV**.

This is NOT simply a larger version of the M5Stack Tab5 interface.

The goal is to create a **premium, cinematic, fluid, instrument-grade SDR command-center experience** that takes advantage of a 16:9 television display and visually exceeds the M5Tab5 interface in polish, motion, information density, readability, and overall “wow” factor.

The current TV display already works and receives live OrcSDR data from the Tab5 over LAN. The existing implementation proves the transport and basic data flow work. Your job is to transform the visual layer into something that feels like a purpose-built **OrcSDR Mission Control display**.

Think:

- high-end spectrum analyzer
- modern RF laboratory instrument
- aircraft avionics
- premium automotive digital cockpit
- space mission telemetry
- cyberpunk command center
- professional broadcast monitoring console

But it must remain **clean, readable, elegant, and believable**.

Do NOT turn it into a cheesy hacker movie UI.

The final result should make someone walking into the room immediately ask:

> “What the hell is that?”

The experience should feel significantly more advanced than the Tab5 while still clearly belonging to the same OrcSDR ecosystem.

---

# PRIMARY CONCEPT

Treat the devices as two different classes of interface.

## M5Stack Tab5

The Tab5 remains the primary SDR appliance and direct-control surface.

It handles:

- RTL-SDR hardware
- DSP
- tuning
- demodulation
- SDR modes
- local UI
- touch controls
- operational interaction

The Tab5 is the **radio**.

## Android TV

The Android TV becomes the:

# ORCSDR MISSION CONTROL DISPLAY

The television should emphasize visualization, telemetry, situational awareness, live RF behavior, metadata, historical information, and dramatic real-time presentation.

The TV is not constrained by the Tab5's 1280×720 embedded UI architecture.

Use the huge display.

Do not simply reproduce Tab5 panels at 2× scale.

---

# DESIGN PRIORITY

In this order:

1. Fluidity
2. Visual sophistication
3. Information hierarchy
4. RF visualization quality
5. Readability at 6–12 feet
6. Real-time responsiveness
7. Efficient rendering
8. OrcSDR visual identity
9. Graceful handling of unavailable telemetry

Every animation must support comprehension or atmosphere.

Never sacrifice performance for decoration.

---

# DISPLAY TARGET

Primary target:

- 16:9 Android TV
- likely 1920×1080
- viewed several feet away
- browser/WebView-style frontend
- LAN connection to OrcSDR Tab5

The layout must scale gracefully to:

- 1280×720
- 1920×1080
- 2560×1440
- 3840×2160

Use responsive sizing rather than hardcoding one resolution.

The existing TV currently displays a basic card interface with values such as:

- FREQUENCY
- BAND
- SIGNAL
- RECEIVING
- SCREEN
- DEVICE / TAB5

Do not discard these concepts.

Transform them into something dramatically more sophisticated.

---

# VISUAL IDENTITY

Create a dedicated **OrcSDR TV visual language**.

The visual theme should be:

- very dark navy / near-black background
- deep layered surfaces
- cyan / electric-blue instrument lighting
- selective green for positive/live states
- amber for warnings or marginal conditions
- red only for real alert conditions
- bright off-white for critical typography
- subtle transparency
- soft luminous borders
- restrained glow
- extremely subtle gradients
- extremely subtle animated noise / RF texture if performance allows

Avoid:

- giant flat blue rectangles
- generic Bootstrap dashboard appearance
- Material Design cards everywhere
- exaggerated neon glow
- excessive rounded corners
- random rainbow colors
- clutter
- fake scrolling hex dumps
- meaningless animation

Every visual element should look deliberate.

---

# THE INTERFACE SHOULD FEEL ALIVE

Nothing should feel like a static HTML status page.

The screen should constantly—but subtly—communicate that a real radio receiver is operating.

Examples:

- spectrum energy moving
- waterfall scrolling
- signal meter breathing naturally
- tiny frequency drift movement if telemetry exists
- live timestamps
- metadata entering smoothly
- mode changes transitioning rather than snapping
- signal peaks leaving brief persistence trails
- receiver activity indicators
- tiny micro-animations
- connection health animation
- data values tweening instead of teleporting

Think **60 FPS instrumentation**, not web page refreshes.

---

# PERFORMANCE TARGET

Aim for 60 FPS animation.

Never redraw or reconstruct the whole DOM unnecessarily.

Prefer:

- requestAnimationFrame
- CSS transforms
- opacity transitions
- Canvas
- WebGL where useful
- bounded updates
- interpolation of telemetry
- efficient data structures

Avoid:

- huge DOM trees
- constant layout reflow
- full-page React rerenders
- heavyweight animation libraries unless truly justified
- expensive blur effects everywhere
- dozens of overlapping shadows

If spectrum/waterfall rendering is high-frequency, use Canvas or WebGL.

The UI must remain smooth even when telemetry updates rapidly.

---

# SCREEN ARCHITECTURE

Create a layered dashboard rather than a grid of equal cards.

Suggested structure:

+---------------------------------------------------------------+
| ORCSDR        MODE / RECEIVER       STATUS        TIME       |
+---------------------------------------------------------------+
|                                                               |
|              HERO RECEIVER / FREQUENCY AREA                  |
|                                                               |
+---------------------------------------------------------------+
|                                                               |
|                  LIVE SPECTRUM ANALYZER                      |
|                                                               |
+---------------------------------------------------------------+
|                                                               |
|                        WATERFALL                              |
|                                                               |
+---------------------------------------------------------------+
| SIGNAL / AUDIO / METADATA / DEVICE / NETWORK / HISTORY       |
+---------------------------------------------------------------+

However, do not mechanically follow this wireframe if a stronger composition emerges.

Use cinematic hierarchy.

---

# 1. TOP STATUS BAR

Create a very clean persistent top bar.

Left side:

**OrcSDR**

Potential subtitle:

**RF Mission Control**

or

**Live Receiver Console**

Do not make the subtitle overly theatrical.

Center or near-center:

Current operating mode such as:

FM BROADCAST  
SHORTWAVE  
AIRBAND  
P25  
ADS-B  
WEATHER  
GHOSTSDR  
etc.

Right side:

- Tab5 connection status
- SDR status
- LAN latency
- telemetry rate
- clock
- recording status if applicable

Example:

TAB5 ● LIVE     SDR ● STREAMING     12 ms     21:43:16

Use small status dots and concise labels.

Green should indicate actual healthy status.

If disconnected, animate a graceful state change rather than flashing aggressively.

---

# 2. HERO FREQUENCY AREA

Frequency is the single most important value.

Give it dramatically more visual priority.

Example:

96.110
MHz

The numbers should look like premium instrumentation.

Not seven-segment retro unless deliberately used.

Use a clean modern technical typeface.

Possible supporting labels:

FM BROADCAST

CENTER 96.110 MHz

BW 200 kHz

STEREO

RDS LOCK

SIGNAL -14.9 dBFS

If station metadata exists:

KLCC
Eugene, Oregon

or:

Artist — Track

Integrate this elegantly beneath or adjacent to the frequency.

Frequency changes should animate smoothly.

Do not roll every digit like a slot machine.

Use a fast restrained transition.

---

# 3. LARGE LIVE SPECTRUM ANALYZER

This should become one of the visual centerpieces of the TV experience.

The spectrum should feel vastly more sophisticated than what can practically be displayed on the Tab5.

Requirements:

- high-resolution trace
- smooth line
- optional filled region
- peak hold
- configurable persistence
- frequency markers
- center frequency indicator
- tuned bandwidth region
- dB scale
- signal peak annotations
- adaptive scaling
- subtle grid

If actual FFT data is available from OrcSDR, use it.

Do NOT generate fake spectrum activity when real data exists.

If no raw spectrum stream currently exists, architect the component so real FFT data can be plugged in later.

A tuned channel should be visually identifiable.

Example:

95.8     96.0     96.1     96.2     96.4 MHz
                     ▲
                  TUNED

For FM, show neighboring stations if detectable.

For shortwave, show narrowband signals.

For P25, show channel energy.

For weather radio, center the active NOAA channel.

The same component should adapt intelligently by mode.

---

# 4. WATERFALL

Give the TV a gorgeous real-time waterfall.

This can potentially become the single strongest visual element of the whole experience.

Requirements:

- smooth vertical scrolling
- adjustable persistence
- time axis
- frequency axis aligned with spectrum
- signal intensity mapped intelligently
- center-frequency marker
- optional signal labels
- restrained scientific color palette

Do not automatically use the classic rainbow SDR palette.

Create an OrcSDR-specific waterfall palette.

Consider something like:

black
deep navy
blue
cyan
white

with perhaps subtle amber/red only at extreme intensity.

The result should look sophisticated on a dark television.

Use WebGL or Canvas if appropriate.

---

# 5. SIGNAL TELEMETRY

Create a dedicated receiver telemetry cluster.

Possible values:

SIGNAL
-14.9 dBFS

SNR
32.4 dB

RSSI
-61 dBm

NOISE
-94 dBm

GAIN
28.0 dB

PPM
+0.8

AGC
AUTO

Do not show unsupported values as fake data.

For unavailable values use:

—

or hide the field gracefully.

Signal strength should also have an animated graphical representation.

Avoid an old-fashioned huge analog meter unless deliberately chosen.

Possible approaches:

- horizontal RF level meter
- radial arc
- segmented bar
- micro history sparkline

I'd prefer a modern linear instrument with history.

---

# 6. SIGNAL HISTORY

Since we have a big TV, show behavior over time.

Create a rolling signal-history graph:

Last 60 seconds by default.

Possible traces:

- signal level
- SNR
- noise floor

This should be understated and extremely smooth.

Provide peak/min/average indicators.

Example:

SIGNAL HISTORY — 60 SEC

AVG -18.3
PEAK -11.7
MIN -31.2

---

# 7. FM MODE — MAKE IT SPECIAL

The current screenshot is FM.

FM should be gorgeous.

When OrcSDR is in FM Broadcast mode, expose FM-specific visualization.

Potential features:

- station frequency
- station name
- RDS PS
- RDS RadioText
- PI code
- stereo / mono
- pilot lock
- audio level
- RDS lock
- signal level
- adjacent channel visualization
- station history
- currently playing metadata if available

Create a **Now Receiving** presentation.

Example:

96.1 FM

KLCC

PUBLIC RADIO

STEREO ● RDS ●

Artist
Track

Do not make it resemble Spotify.

It must still look like an RF receiver.

---

# 8. AUDIO VISUALIZATION

If audio samples or levels are available, add restrained audio instrumentation.

Possible elements:

LEFT / RIGHT meters

Stereo correlation

Audio waveform

Small vectorscope

Do not dominate the screen.

For FM, the RF spectrum remains more important than the audio visualization.

---

# 9. MODE-AWARE DASHBOARD

This is critical.

The television must NOT show the exact same widgets for every OrcSDR mode.

Create a reusable shell but allow the center content to transform based on mode.

Examples:

## FM

- giant station frequency
- RDS
- stereo
- spectrum
- waterfall
- audio meters

## SHORTWAVE

- exact frequency
- band
- modulation
- filter bandwidth
- S-meter
- spectrum
- waterfall
- station metadata/log

## AIRBAND

- frequency
- AM mode
- squelch
- active carrier
- spectrum
- recent channel activity
- optional decoded metadata if available

## ADS-B

- aircraft count
- strongest aircraft
- message rate
- receiver stats
- map-centric visualization

## P25

- frequency
- NAC
- talkgroup
- system
- site
- signal quality
- decode state
- spectrum
- activity history

## WEATHER

- NOAA channel
- SAME / alerts
- weather metadata
- RF signal
- alert state
- spectrum

## GHOSTSDR

Maintain the more experimental personality but still remain consistent with OrcSDR.

---

# 10. TRANSITIONS BETWEEN MODES

When the Tab5 changes mode, the television should not abruptly replace everything.

Create elegant transitions.

Examples:

Current dashboard elements fade/slide subtly.

Frequency hero changes.

Spectrum scale transitions.

New mode-specific panels enter.

Entire transition should take approximately:

250–500 ms.

It should feel like changing modes on a premium instrument.

No page reload.

---

# 11. CONNECTION EXPERIENCE

When the TV first opens OrcSDR, do not show a boring blank webpage.

Create an OrcSDR startup sequence.

Keep it short.

Maybe:

ORCSDR

Connecting to receiver...

TAB5 FOUND
192.168.x.x

TELEMETRY LINK ESTABLISHED

Then smoothly enter the dashboard.

Total startup animation:

~1–2 seconds.

Do not make users wait unnecessarily.

If the Tab5 is unavailable:

ORCSDR

RECEIVER OFFLINE

Searching for Tab5...

Show an elegant low-key animation.

Automatically reconnect.

---

# 12. DATA STALENESS

Important.

The TV must know whether data is fresh.

Track timestamps.

If updates stop:

After a short threshold:

DATA STALE

Then:

RECONNECTING

Do not leave stale information looking live.

Gradually dim stale telemetry.

When the connection returns, smoothly restore.

---

# 13. MICRO-INTERACTIONS

Use subtle motion throughout.

Examples:

LIVE indicator has a very slow breathing glow.

Signal meter interpolates rather than jumps.

Spectrum peaks leave slight persistence.

Frequency updates animate over ~100 ms.

New RDS text crossfades.

Status changes have short transitions.

Recording indicator pulses gently.

Network latency indicator updates smoothly.

Do not animate everything.

Motion must be calm.

---

# 14. DEPTH

Use visual layering to make the dashboard feel physical without becoming skeuomorphic.

Consider:

background layer

panel layer

instrument layer

data layer

annotation layer

Very subtle gradients and highlights can make panels feel premium.

Example panel:

near-black translucent surface

1px cyan/blue border at low opacity

tiny inner highlight

slight shadow

Do NOT put giant shadows around everything.

---

# 15. TYPOGRAPHY

Typography is extremely important.

Use clear hierarchy.

Example:

OrcSDR
34 px

96.110
96–130 px

MHz
28 px

-14.9
44 px

dBFS
18 px

Panel labels:
14–18 px uppercase

Telemetry values:
24–38 px

Use responsive scaling.

At TV distance, labels must remain readable.

Avoid tiny text.

---

# 16. INFORMATION DENSITY

Use the extra TV space intelligently.

Do not make every element huge.

A TV allows both:

- large critical information
- additional secondary telemetry

The user should understand the receiver state instantly from across the room.

But approaching the TV should reveal deeper detail.

Think:

10-foot view
+
3-foot view

---

# 17. RF GRID AND INSTRUMENT CHROME

Create subtle RF-inspired structure.

Examples:

frequency tick marks

thin graph grids

center markers

bandwidth overlays

peak markers

channel markers

signal threshold indicators

Use these rather than generic dashboard decoration.

This is a radio instrument.

Its visuals should communicate radio concepts.

---

# 18. AMBIENT BACKGROUND

The large empty TV surface can support an extremely subtle ambient background.

Potential ideas:

soft radial gradients

slow moving RF-like texture

extremely faint spectrum ghost lines

subtle scan texture

very faint grid

Do not make it distracting.

The background should remain darker than all functional components.

---

# 19. IDLE MODE

If no active receiver activity occurs for a period, consider an optional OrcSDR ambient mode.

For example:

ORCSDR

Receiver Ready

current time

Tab5 status

very subtle animated spectrum background

When activity returns, transition immediately to live mode.

Do not let idle mode interfere with normal monitoring.

---

# 20. FULLSCREEN

The TV interface should be optimized for fullscreen operation.

Avoid browser-looking UI.

No scrollbars.

No accidental overflow.

Everything must fit within the viewport.

Use:

100vw
100vh

Respect overscan / safe areas.

Leave approximately 2–3% display-edge margin for television overscan.

---

# 21. REMOTE CONTROL / D-PAD SUPPORT

Even though the first version may remain read-only, build the frontend with Android TV navigation in mind.

Potential future controls:

UP/DOWN
change panel focus

LEFT/RIGHT
change dashboard

OK
expand panel

BACK
return

Do not require a mouse.

Focus indicators should be tasteful.

However:

**Do not turn the TV into a competing primary controller yet.**

The Tab5 remains the control authority.

---

# 22. TV DASHBOARD PAGES

Instead of forcing everything into one screen, consider multiple TV views.

Potential pages:

### LIVE
Primary receiver dashboard.

### SPECTRUM
Huge spectrum and waterfall.

### SIGNAL
Signal metrics and history.

### METADATA
Decoded station / protocol information.

### SYSTEM
Tab5 / RTL-SDR / network telemetry.

These can rotate manually or optionally auto-cycle.

However:

The default LIVE page should already feel complete.

---

# 23. SPECTRUM-FOCUSED VIEW

Create an optional visualization mode where:

Spectrum occupies roughly 40% of the screen.

Waterfall occupies roughly 40%.

Bottom status strip occupies the remaining area.

Frequency remains overlaid subtly.

This should make OrcSDR look like a serious laboratory spectrum instrument.

---

# 24. SYSTEM TELEMETRY

Use a small area for hardware status.

Potential metrics:

TAB5
ESP32-P4

SDR
RTL-SDR V4

USB
CONNECTED

SAMPLE RATE
2.4 MSPS

BUFFER
92%

DSP
ACTIVE

LAN
12 ms

UPTIME
03:41:52

TEMP
if available

MEMORY
if useful

Only show useful telemetry.

Do not fill the screen with useless system trivia.

---

# 25. REAL TELEMETRY ONLY

Critical rule:

Do not invent capabilities.

If OrcSDR currently provides:

frequency
band
screen
signal
connection state

use those.

If it does not yet provide:

SNR
RDS
FFT
waterfall
audio levels
latency
device temperature

architect placeholders/interfaces for them, but do not fabricate live values.

Use graceful unavailable states.

The dashboard should encourage expanding the telemetry API cleanly.

---

# 26. TELEMETRY ARCHITECTURE

Create a clean central state model.

For example:

ReceiverState

Properties could include:

connection
device
frequency
band
mode
screen
signal
snr
rssi
noise
sampleRate
bandwidth
gain
ppm
audio
rds
fft
waterfall
system
network
recording
timestamp

Components subscribe only to the data they need.

Avoid scattering raw WebSocket parsing throughout UI components.

---

# 27. NETWORK TRANSPORT

If the current LAN console polls HTTP, evaluate whether WebSockets or Server-Sent Events would provide smoother telemetry.

Do not change transport unnecessarily if the existing system already performs well.

But design for:

low latency

efficient incremental updates

reconnection

heartbeat

stale-data detection

sequence numbers if useful

timestamping

---

# 28. COMPONENT ARCHITECTURE

Break the UI into reusable components.

Possible structure:

AppShell

TopStatusBar

ReceiverHero

FrequencyDisplay

ModeBadge

SignalMeter

SpectrumAnalyzer

Waterfall

SignalHistory

MetadataPanel

AudioMeters

ReceiverStatus

SystemTelemetry

ConnectionOverlay

ModeDashboard

FMView

ShortwaveView

AirbandView

P25View

WeatherView

ADSBView

GhostSDRView

Keep the architecture clean.

---

# 29. RESPONSIVENESS

Do not assume only one TV.

Use CSS Grid / Flexbox thoughtfully.

Define breakpoints for:

720p

1080p

1440p

4K

At 4K, do not simply make everything microscopic relative to available space.

Scale typography and graph resolution appropriately.

---

# 30. GPU ACCELERATION

Use GPU-friendly transforms.

For spectrum/waterfall:

prefer Canvas/WebGL.

Avoid repeatedly creating DOM nodes for graph points.

A waterfall should use a rolling texture/buffer rather than hundreds of div rows.

---

# 31. ANIMATION SYSTEM

Use a coherent motion language.

Suggested timing:

Micro state:
100–150 ms

Panel state:
200–300 ms

Mode transition:
350–500 ms

Startup:
1000–1500 ms

Easing:

smooth ease-out

Avoid bounce.

Avoid elastic animation.

Avoid cartoon motion.

This should feel like instrumentation.

---

# 32. ERROR STATES

Design error states with the same care as the live dashboard.

Examples:

RTL-SDR DISCONNECTED

USB STREAM LOST

TAB5 OFFLINE

TELEMETRY TIMEOUT

DSP STOPPED

NETWORK DEGRADED

Do not throw ugly browser alerts.

Use contextual overlays/panels.

---

# 33. ORCSDR BRAND

Make OrcSDR unmistakable.

Use the real OrcSDR icon/badge assets where appropriate.

Do not substitute unrelated orca whale imagery.

The logo should appear cleanly in the header/startup sequence.

Keep branding restrained.

---

# 34. VISUAL TARGET

The screen should look good enough that screenshots could be used as official OrcSDR promotional images.

At first glance it should appear like a commercial high-end RF instrument.

At second glance it should clearly have OrcSDR personality.

At third glance the user should realize an ESP32-P4 Tab5 is actually powering the radio.

That contrast is part of what makes this project cool.

---

# 35. DO NOT SIMPLY RESKIN THE EXISTING PAGE

This is extremely important.

The current LAN console proves functionality.

Do NOT simply change:

colors
font
border radius
card spacing

and call it redesigned.

Reconsider the entire visual hierarchy.

Replace the generic card dashboard concept.

The TV should be a completely evolved experience.

---

# 36. DO NOT COPY TAB5 PIXEL-FOR-PIXEL

The Tab5 interface is optimized for:

1280×720

touch

embedded rendering

M5GFX

limited screen size

The TV has different strengths.

Use them.

The TV should display deeper visualization and ambient telemetry that would be wasteful or crowded on Tab5.

However, preserve:

terminology

mode naming

status meaning

visual DNA

so they still feel connected.

---

# 37. BIG-SCREEN WOW MOMENTS

I specifically want a few elements that make the TV experience jaw-dropping.

Potential examples:

A beautiful waterfall occupying nearly half the screen.

A huge smoothly animated frequency readout.

RF signals rising and falling in real time.

Subtle peak persistence.

RDS metadata smoothly appearing.

Mode transitions that transform the dashboard.

A spectral “glow” corresponding to actual RF energy.

A clean center-frequency reticle.

A narrow signal-history trace running continuously.

A tiny animated network waveform showing live Tab5 telemetry.

Choose only effects that feel believable and professional.

---

# 38. DEVELOPMENT APPROACH

Before changing code:

Inspect the existing OrcSDR LAN console implementation.

Determine:

how telemetry currently arrives

what data exists

refresh/update rate

current frontend architecture

where HTML/CSS/JS lives

whether Canvas already exists

whether WebSocket/SSE exists

whether assets are shared with Tab5

Then make a concrete implementation plan.

Do not rewrite the transport layer blindly.

Preserve working functionality.

---

# 39. IMPLEMENT IN PHASES

Suggested development order:

## Phase 1 — Shell

Create:

fullscreen layout

top bar

receiver hero

signal status

responsive architecture

## Phase 2 — Motion

Add:

telemetry interpolation

micro-transitions

connection animation

mode transitions

## Phase 3 — Spectrum

Implement high-performance FFT renderer.

## Phase 4 — Waterfall

Implement performant scrolling waterfall.

## Phase 5 — FM Experience

Implement FM-specific metadata/audio/state visualization.

## Phase 6 — Additional Modes

Create mode-aware views for other OrcSDR dashboards.

## Phase 7 — Polish

Optimize:

60 FPS

spacing

typography

TV viewing distance

overscan

latency

reconnection

---

# 40. TESTING

Test specifically for:

60 FPS rendering

no visible stutter

no page reloads

no viewport overflow

no scrollbars

no memory leak from waterfall history

no runaway arrays

stable reconnection

clean stale-data behavior

responsive scaling

1080p television rendering

720p fallback

4K scaling

long-running operation

The dashboard should be able to sit on the television **for hours**.

---

# 41. IMPORTANT UX PRINCIPLE

The TV should tell a story.

At any moment the viewer should intuitively understand:

WHAT frequency is being received

WHAT radio mode is active

HOW strong the signal is

WHAT the RF environment looks like

WHETHER decoding/audio is successful

WHAT the receiver is doing

WHETHER the Tab5/SDR connection is healthy

Everything else is secondary.

---

# 42. FINAL EXPERIENCE

Imagine walking into a dark room.

The Tab5 sits on the desk connected to an RTL-SDR V4.

Above it, the television shows:

ORCSDR

96.110 MHz

FM BROADCAST

A wide live RF spectrum moves smoothly.

A bright center signal rises from the noise floor.

Below it, a waterfall continuously paints the RF history downward.

Signal telemetry updates fluidly.

RDS information fades into view.

A tiny green indicator says:

TAB5 LIVE

Another says:

RTL-SDR V4

A thin signal-history trace slowly scrolls.

Everything moves gently.

Nothing jitters.

Nothing screams “webpage.”

Nothing looks generic.

It feels like a real RF command center.

That is the target.

---

# YOUR TASK

1. Inspect the current OrcSDR TV/LAN-console implementation.
2. Identify exactly what telemetry is currently available.
3. Identify the current frontend architecture and rendering path.
4. Preserve all existing working connectivity.
5. Design the new TV-specific Mission Control architecture.
6. Replace the basic card dashboard with a dramatically more sophisticated layout.
7. Build a fluid real-time visualization framework.
8. Implement polished motion and telemetry interpolation.
9. Add spectrum and waterfall visualization using real OrcSDR data when available.
10. Build graceful interfaces for telemetry that will be added later.
11. Keep the Tab5 as the primary control surface.
12. Optimize specifically for 16:9 Android TV use.
13. Make it stable enough for continuous operation.
14. Make it visually superior to the already-polished M5Tab5 UI.
15. Make the final result genuinely beautiful.

Do not stop at “functional.”

Do not stop at “clean.”

Do not stop at “modern.”

**Push this until OrcSDR on the television feels like a premium purpose-built RF Mission Control system that happens to be powered by our Tab5.**

I want this to be the visual showcase for OrcSDR.