# Implementation plan — close OrcSDR gaps from peer research

Date: 2026-08-06  
Depends on: `COMPETITIVE_ANALYSIS_ESP32_RTLSDR.md`, `PORTING.md`, clean-room spec.

This document turns peer lessons into **executable OrcSDR work** without
violating the clean-room driver boundary.

---

## Principles

1. **RTL-SDRv4-ESP** remains the only USB/tuner owner. No librtlsdr source import.
2. **Architecture may match peers** (dual-core, ring, rtl_tcp) while **transfer
   sequences stay ours** (observed tables + public R820 math we already use).
3. **Apps own product features** (WebSDR, ADS-B, rtl_tcp, FM UI). Driver owns IQ.
4. Every gate needs **measured** evidence on named hardware (chip, board, rate, drops).

---

## Gap map (OrcSDR vs peers)

| Gap | Peer proof | OrcSDR today | Priority |
|---|---|---|---|
| USB owner ≠ UI/DSP core | wifi-rtlsdr Core0/1; ADS-B task work | Single-task / partial | **P0** |
| PSRAM SPSC IQ ring | wifi-rtlsdr 2–8 MB | Not in driver | **P0** |
| Full stream in component | both ports stream in-driver | Skeleton `start()` NOT_FINISHED | **P0** |
| Hot-plug + STALL recovery | ADS-B Scope | Partial / app-level | **P1** |
| rtl_tcp (public protocol) | xtrsdr + wifi-rtlsdr | None | **P1** |
| Ethernet high-rate path | both claim multi-MS/s on eth | None | **P1** |
| Safe retune (no bulk conflict) | implied by their stacks | Fixed once; must stay | **P0** |
| WiFi IQ streaming | wifi-rtlsdr 1 MS/s class | None | **P2** |
| SpyServer / SoapyRemote | wifi-rtlsdr | None | **P2** |
| WebSDR browser | wifi-rtlsdr | On-device UI only | **P2** |
| ADS-B decode | ADS-B Scope | None | **P3** |
| Bias-tee / Blog HF path | wifi-rtlsdr gaps doc | Unverified / not public API | **P2** (measure first) |
| PIE SIMD FFT | wifi-rtlsdr | Soft FFT in app | **P2** |
| FS ESP32-S2/S3 matrix | xtrsdr S2 limits | P4 only measured | **P3** |

---

## Workstream A — Finish standalone RTL-SDRv4-ESP (P0)

**Goal:** Any ESP-IDF P4 project can `start()` Blog V4 and receive IQ callbacks without Tab5 UI.

### A1. Extract USB session owner task into component

**Files:** `components/rtl_sdr_v4_esp/src/*`  
**Source of behavior (not code to paste blindly):** `apps/orcsdr-tab5/ui/main.cpp` capture path.

| Step | Deliverable | Done when |
|---|---|---|
| A1.1 | USB host client register/install options (shared vs owned host lib) | Matches public API |
| A1.2 | V4 identity filter (0bda:2838 + Blog strings) | Reject others with clear error |
| A1.3 | Claim iface 0, clean-room init + expected STALLs | Same serial ladder as Tab5 |
| A1.4 | Sample rate 960 kS/s (and later allowlisted rates) | Measured sps ≥ 95% of request |
| A1.5 | Tune: presets + custom Hz (existing PLL pack) | KZEL/NOAA + free tune |
| A1.6 | Bulk IN multi-buffer, stop, cleanup | Continuous ≥ 60 s, 0 fatal USB errors |
| A1.7 | Metrics API filled | bytes, drops, effective_sps, min/max/mean |

**Invariant:** No EP0 while bulk URB outstanding (OrcSDR crash lesson).

### A2. IQ delivery ring (from wifi-rtlsdr architecture)

| Step | Deliverable |
|---|---|
| A2.1 | SPSC or FreeRTOS queue of fixed IQ blocks (internal RAM for USB DMA; copy to PSRAM consumer buffers if needed) |
| A2.2 | `RTL_SDR_V4_ESP_EVT_IQ_BLOCK` callback with acquire/release rules |
| A2.3 | Overrun counter when consumer slow (drop **oldest** or newest — pick and document) |
| A2.4 | Optional dual-core: USB task Core 0 prio high; optional helper Core 1 for app |

**Depth:** start with 3 × 32 KiB (Tab5 proven); Kconfig up to larger PSRAM pools.

### A3. Command queue for retune/stop

| Step | Deliverable |
|---|---|
| A3.1 | Bounded command queue on USB owner task |
| A3.2 | `retune_hz` only between bulks |
| A3.3 | Fail soft: keep previous LO, emit `EVT_ERROR` |

### A4. Tab5 app consumes component

| Step | Deliverable |
|---|---|
| A4.1 | `apps/orcsdr-tab5` links `rtl_sdr_v4_esp` |
| A4.2 | Delete in-app EP0 bulk path once parity proven |
| A4.3 | Regression: KZEL audio + scope scroll + volume |

**Exit Gate A:** `examples/p4_serial_smoke` streams 960 kS/s to UART metrics (no M5 UI) for 5 minutes.

---

## Workstream B — Network streaming apps (P1)

**Goal:** Optional IQ export without contaminating the driver.

### B1. `apps/orcsdr-rtltcp` (inspired by xtrsdr + wifi-rtlsdr)

| Item | Spec |
|---|---|
| Protocol | Public rtl_tcp (DongleInfo + 5-byte commands) — implement from protocol docs, not by copying peer servers blindly |
| Transport | Ethernet first on Waveshare-class boards; WiFi second |
| Rate targets | Eth: approach 2 MS/s if USB+driver allow; WiFi: start 1 MS/s class |
| Client tests | SDR++, GQRX, or `rtl_tcp` CLI |

### B2. Ring + network decoupling

Mirror wifi-rtlsdr lesson: **never** have TCP send block the USB owner.

```text
USB Core0 → driver ring → rtl_tcp task (Core1) → lwIP
```

### B3. mDNS

Advertise `_rtl_tcp._tcp` service name (configurable hostname).

### B4. Explicit non-goals for v1

- SpyServer / SoapyRemote (P2)
- Multi-client fanout (P2; single client first)

**Exit Gate B:** SDR++ over Ethernet at ≥ 1 MS/s for 10 minutes; document drops %.

---

## Workstream C — Reliability (P1) from ADS-B Scope lessons

| Item | Implementation note |
|---|---|
| Hot-plug | Detect disconnect; flush URBs; return to Enumerated; no reboot required |
| STALL policy | Expected init STALLs vs unexpected mid-stream STALLs |
| DMA/cache | Document P4 cache-safe buffer ownership; add fence if corrupt IQ appears at high rate |
| Status enum | Disconnected / Ready / Streaming / Error surfaced to OrcSDR UI |
| Adaptive gain | App-level policy on top of driver gain API once gain is exposed |

Do **not** import ADS-B Scope’s librtlsdr; re-derive recovery in our state machine.

**Exit Gate C:** Unplug/replug V4 during stream recovers without hard reset (Tab5 + one other P4 board).

---

## Workstream D — On-device radio quality (P1, OrcSDR app)

OrcSDR already has speaker + waterfall. Peers reinforce:

| Item | Action |
|---|---|
| Dual-core split | USB vs demod/UI (align with Workstream A) |
| AGC / soft limit | Keep and tune (clipping was measured at peak 16000) |
| Scroll tune | Hot retune only via driver command queue |
| Optional audio-only mode | Disable spectrum when drops rise |
| Local FM path | Remain first-class; network is additive |

**Exit Gate D:** Operator-subjective “smooth FM” + serial `audio_dropped≈0` for 5 minutes with spectrum on.

---

## Workstream E — Optional advanced (P2)

| Feature | Peer inspiration | OrcSDR placement |
|---|---|---|
| WebSDR / waterfall in browser | wifi-rtlsdr | New app or module, not driver |
| SpyServer | wifi-rtlsdr | App protocol adapter |
| Bias-tee GPIO | wifi-rtlsdr | Driver API + board BSP callback |
| Blog V4 HF path | wifi-rtlsdr gap list | **Measure first** with clean-room captures; then extend tables |
| PIE SIMD FFT | wifi-rtlsdr | Optional DSP component in app |
| ADS-B 1090 | ADS-B Scope | Separate OrcSDR app later |

---

## Workstream F — Target matrix honesty (P3)

| Target | Plan |
|---|---|
| ESP32-P4 HS | Primary; all gates above |
| ESP32-S3 FS | Build component with `#error` or runtime reject of rates > policy; optional 240 kS/s experiment only if someone measures |
| Classic ESP32 | Out of scope |

Document every board: USB connector, VBUS enable, HS vs FS, max sustained sps, transport.

---

## Suggested milestone order

```text
M0  (done)   Component skeleton + OrcSDR monorepo + peer analysis docs
M1  (2–4 w)  Gate A: full start/stop/IQ in rtl_sdr_v4_esp; Tab5 uses it
M2  (2–3 w)  Gate C: hot-plug + STALL + dual-core ring default
M3  (2–4 w)  Gate B: orcsdr-rtltcp on Ethernet P4 board
M4  (ongoing) Gate D audio/UI polish; optional WebSDR / SpyServer
```

---

## Concrete file checklist for M1

```text
components/rtl_sdr_v4_esp/
  src/rtl_sdr_v4_esp_usb.c       # host client, bulk, control
  src/rtl_sdr_v4_esp_tune.c      # presets + PLL pack (from our math)
  src/rtl_sdr_v4_esp_session.c   # state machine + command queue
  src/rtl_sdr_v4_esp_ring.c      # IQ ring
  private/rtl_sdr_v4_transfers.hpp  # existing
  test/host_build/               # pure logic tests where possible

apps/orcsdr-tab5/ui/
  # thin consumer of rtl_sdr_v4_esp_* only

examples/p4_serial_smoke/
  # print metrics once/sec while streaming
```

---

## Legal / license checklist

| Action | Rule |
|---|---|
| Read peer READMEs / architecture docs | OK |
| Implement rtl_tcp from public protocol descriptions | OK |
| Copy `tuner_r82xx.c` / `librtlsdr.c` from peers | **Forbidden** for RTL-SDRv4-ESP |
| Dual-license AGPL component + GPL app example | Review if mixing; keep driver AGPL-clean |
| Cite peers in docs | Required when we adopt ideas |

---

## Success metrics (numeric)

| Metric | Target |
|---|---|
| Continuous 960 kS/s, local Tab5 | ≥ 95% effective sps, 5 min, 0 USB fatal |
| rtl_tcp Ethernet | ≥ 1.0 MS/s, 10 min, drop rate documented |
| Hot-plug recovery | ≤ 3 s to Ready after reinsert |
| Hot retune | No crash; LO settles ≤ 100 ms typical |
| Component usable | Third-party IDF project builds with only `EXTRA_COMPONENT_DIRS` |

---

## Explicit non-goals (next 90 days)

- Becoming a full librtlsdr replacement for all tuners (E4000, FC00xx, …)
- Claiming S2/S3 production IQ rates equal to P4
- Folding OrcLink daemon into OrcSDR
- Shipping SpyServer before rtl_tcp is solid

---

## References (peer repos)

- https://github.com/jstockdale/T-Display-P4 (branch `adsb`)
- https://github.com/r4d10n/esp32p4-wifi-rtlsdr
- https://github.com/XTR1984/xtrsdr

Internal: `COMPETITIVE_ANALYSIS_ESP32_RTLSDR.md`, `PORTING.md`, `RTL_SDR_V4_CLEAN_ROOM_SPEC.md`.
