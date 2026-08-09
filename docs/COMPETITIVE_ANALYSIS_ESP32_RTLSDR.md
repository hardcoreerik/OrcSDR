# Peer project analysis for OrcSDR / RTL-SDRv4-ESP

> Research snapshot from 2026-08-06. Its OrcSDR baseline is historical; use
> [`../PROJECT_STATUS.md`](../PROJECT_STATUS.md) for current implementation state.

Date: 2026-08-06  
Audience: OrcSDR maintainers deciding architecture, not a green light to port librtlsdr.

**Clean-room boundary (non-negotiable):** OrcSDR’s **RTL-SDRv4-ESP** driver is built from independently observed USB behavior and public hardware docs. We may adopt *architecture*, *protocols*, *board lessons*, and *measured performance targets* from peers. We must **not** copy librtlsdr / rtl-sdr-blog register sequences, I2C tables, or source files from these projects into the driver.

| Project | Repo | License (GitHub) | Stars | Last push (API) |
|---|---|---|---|---|
| ADS-B Scope (T-Display-P4) | [jstockdale/T-Display-P4](https://github.com/jstockdale/T-Display-P4) `adsb` | GPL-3.0 | 34 | 2026-04-10 |
| esp32p4-wifi-rtlsdr | [r4d10n/esp32p4-wifi-rtlsdr](https://github.com/r4d10n/esp32p4-wifi-rtlsdr) | GPL-2.0 (docs claim; GitHub license field empty) | 4 | 2026-04-21 |
| xtrsdr | [XTR1984/xtrsdr](https://github.com/XTR1984/xtrsdr) | GPL-2.0 | 11 | 2026-04-17 |

OrcSDR baseline for comparison: measured Tab5 V4 HS path, 960 kS/s continuous listen, on-device WBFM/NFM + spectrum, clean-room EP0 tables, component skeleton `rtl_sdr_v4_esp` (stream not fully extracted).

---

## Project 1 — ADS-B Scope (T-Display-P4, `adsb` branch)

### What it is

Portable **1090 MHz ADS-B** receiver on **LILYGO T-Display-P4** (ESP32-P4 + C6), with RTL-SDR on USB Host, LVGL UI, SD aircraft DB, adaptive gain, hot-plug recovery, optional Meshtastic/LoRa mesh. Firmware binaries under `firmware/jstockdale-adsb_receiver/`; source under `main/examples/adsb_receiver/`.

### 1. Architecture and code structure

| Question | Finding |
|---|---|
| USB host driver | Custom path: `class_driver.c/h`, `esp_libusb.c/h` wrapping ESP-IDF USB host for libusb-like semantics, plus vendored **`librtlsdr.c`** in the example. |
| librtlsdr integration | **Direct**: librtlsdr is in-tree and adapted to `esp_libusb`. Not clean-room. |
| V4 / R828D / bias / clock | Uses librtlsdr stack (R82xx path inside librtlsdr). Commits mention EP0 STALL recovery, hot-plug (10 consecutive read errors), AXI DMA fence fixes — production-hardened host lifecycle. Bias-tee / Blog V4 HF switching: not the product focus (ADS-B is 1090 MHz). |

App structure is a **monolithic IDF example**: RF + decode (`mode-s`) + LVGL + SD + web companion, not a separable “driver-only” component.

### 2. ESP32 chip support

| Chip | Role |
|---|---|
| **ESP32-P4** | Main SoC: USB HS host + dual RISC-V + display |
| **ESP32-C6** | Wi-Fi/BT companion (SDIO), board-level |

USB: P4 High-Speed matches Blog V4 bulk MPS 512 B. ADS-B is a **narrowband / message-rate** workload, not multi-MS/s continuous IQ to the network.

### 3. Performance and features

| Area | Status (from README / commits) |
|---|---|
| RF product | ADS-B decode 15–30 msg/s, ~12–30 aircraft, ~30 nm anecdotal with small antenna |
| Adaptive gain | Two-phase AGC with quality gating |
| USB reliability | Hot-plug, STALL recovery, DMA fence work (v1.0.8) |
| UI | LVGL table + radar scope, web companion, aircraft DB |
| Streaming protocols | Not the point — decode on-device |
| Sample rate | Not published as a continuous IQ marketing number; tuned for Mode-S, not GQRX bridge |

**Maturity:** Actively released firmware (v1.0.8 era commits Apr 2026). Closer to a **shipping vertical app** than a generic driver.

### 4. Lessons for OrcSDR

| Reuse / adapt (architecture only) | Do not copy into RTL-SDRv4-ESP |
|---|---|
| USB hot-plug and STALL recovery state machine | `librtlsdr.c` / register tables |
| AXI/DMA ownership and “fence” discipline on P4 | Full ADS-B decoder stack (unless as separate Orc app later) |
| Clear disconnected / connected / error UI status | Meshtastic/mp3 product surface |
| Adaptive gain *policy* ideas for FM (measure ourselves) | Their libusb shim if it embeds GPL’d librtlsdr patterns |

**Best for OrcSDR as:** reliability and productization patterns on T-Display-P4-class hardware, not as the generic V4 driver foundation.

---

## Project 2 — esp32p4-wifi-rtlsdr

### What it is

Waveshare **ESP32-P4-WIFI6** + RTL-SDR as a **wireless multi-protocol IQ platform**: `rtl_tcp`, SpyServer, SoapyRemote, UDP, embedded **WebSDR** (FFT/waterfall/WBFM), WiFi Manager, optional Ethernet. Self-described **work in progress** with many “production” feature rows on `main`.

### 1. Architecture and code structure

| Question | Finding |
|---|---|
| USB host driver | Component `components/rtlsdr/`: `rtlsdr.c` + `tuner_r82xx.c`, ESP-IDF USB host, bulk EP **0x81**, vendor EP0 by block (demod/USB/sys/tuner/I2C). |
| librtlsdr integration | **Port of librtlsdr / rtl-sdr-blog concepts and structure** (explicitly referenced). Function-table tuner iface planned. **Not clean-room.** |
| V4 specifics | R828D, multi-band claims, HF/VHF/UHF Blog V4 switching called out; **DRIVER_ANALYSIS.md** lists gaps vs rtl-sdr-blog (IF regs, Zero-IF, mux tables, bias GPIO addresses, etc.). Bias-T via GPIO (default GPIO4). Crystal 28.8 MHz assumed. |

**System split (documented):**

```text
Core 0  USB host lib + bulk reader → PSRAM ring (SPSC)
Core 1  TCP/UDP / WebSDR / network (ESP-Hosted C6 or Ethernet)
```

That matches the dual-core plan already sketched for OrcSDR.

### 2. ESP32 chip support

| Chip | Role |
|---|---|
| **ESP32-P4** | USB HS host, ring buffer writer, dual-core pin |
| **ESP32-C6** | WiFi 6 via SDIO (ESP-Hosted) |
| Ethernet | 100 Mbps RMII when present |

High-Speed USB is what makes multi-MS/s bulk practical; WiFi/SDIO becomes the bottleneck before USB does.

### 3. Performance and features

| Metric | Claimed / documented |
|---|---|
| WiFi 6 TCP | ~1.0 MSPS comfortable; ~1.5 advanced |
| Ethernet | 2.4+ MSPS possible |
| Throughput ladder | 214 → 558 → 720 → 891 → 1025 kSPS as USB/demod/SDIO fixed |
| Spectral check | ~96.4% correlation vs direct USB (their metric) |
| Protocols | rtl_tcp, UDP+seq, SpyServer, SoapyRemote |
| WebSDR | FFT (PIE SIMD), waterfall, WBFM/NFM/SSB/CW, decoders |
| DSP | PIE int16 FFT ~4.3× vs ANSI (their bench) |
| Pitfalls | WiFi jitter, SDIO clock tradeoffs, register gaps vs Blog V4 reference |

**Maturity:** Feature-rich, actively developed, labeled WIP; strongest **network SDR appliance** reference of the three.

### 4. Lessons for OrcSDR

| Reuse / adapt | Do not copy |
|---|---|
| **Core0 USB / Core1 consumer** pinning | `rtlsdr.c` / `tuner_r82xx.c` sources |
| **PSRAM SPSC ring** sizing and overrun policy | Full librtlsdr register programs |
| **rtl_tcp command table** and DongleInfo header (public protocol) | GPL-contaminated ports as our driver core |
| Ethernet-first for high rate | Assuming their incomplete Blog V4 gaps are “done” |
| Transport bottlenecks (USB HS ≫ WiFi TCP) | Blind 2 MS/s over WiFi claims |
| mDNS service advertising | — |
| Optional: WebSDR / SpyServer as *OrcSDR app layers* | — |

**Best for OrcSDR as:** transport, dual-core, and protocol **architecture** reference; primary peer for “IQ to the network.”

---

## Project 3 — xtrsdr

### What it is

Experimental **librtlsdr adapted to ESP USB host**, with **rtl_tcp** and **rtl_fm → I2S** demos. Dual tree: `esp32s2/` and `esp32p4/`. GPL-2.0, small commit count (~28), exploratory tone.

### 1. Architecture and code structure

| Question | Finding |
|---|---|
| USB host driver | librtlsdr tree adapted to ESP-IDF USB host (libusb-shaped layer). |
| librtlsdr integration | **Explicit adaptation** of librtlsdr; in-repo `librtlsdr/`. |
| V3/V4 | README: **RTLSDR v3** (RTL2832U + **R820T2**) proven on S2; P4 path claims “full” 2 MS/s on eth board. Blog V4 / R828D not the headline. |
| Bias / clock | Inherited from librtlsdr port; not clean-room documented. |

### 2. ESP32 chip support

| Chip | USB | Notes |
|---|---|---|
| **ESP32-S2** | Full-Speed host only (12 Mbit) | Caps sustainable IQ; no hub; needs PSRAM (e.g. FN4R2) |
| **ESP32-P4** | High-Speed | `esp32-p4-eth` + JST USB adapter; rtl_tcp over Ethernet |

### 3. Performance and features

| Platform | Claimed rate | Features |
|---|---|---|
| S2 WiFi rtl_tcp | **240 kS/s** | GQRX / SDR# / SDR++ clients |
| S2 W5500 SPI eth | **300 kS/s** | Better than WiFi |
| S2 local | rtl_fm → **I2S MAX98357A** | On-device FM audio |
| P4 Ethernet | **2 000 000 S/s** | SDR++ over network |
| Failures | S2 WiFi unstable with distance; phone via WiFi router flaky |

**Maturity:** Proof-of-concept / hobby; valuable for **FS vs HS ceiling** and early P4 eth path, not a polished product.

### 4. Lessons for OrcSDR

| Reuse / adapt | Do not copy |
|---|---|
| Hard numbers: FS ≈ 0.24–0.3 MS/s, HS eth ≈ 2 MS/s | Entire librtlsdr tree |
| rtl_fm + I2S as an **app** pattern (we already have local audio) | Assuming V3 R820T2 path = Blog V4 |
| PlatformIO + IDF hybrid packaging ideas | USB hub assumptions |
| Honesty about WiFi range failure modes | — |

**Best for OrcSDR as:** historical proof that ESP USB host + rtl_tcp works, and that **P4 HS is mandatory** for OrcSDR’s quality bar.

---

## Cross-project comparison

| Topic | ADS-B Scope | esp32p4-wifi-rtlsdr | xtrsdr | OrcSDR today |
|---|---|---|---|---|
| Driver model | librtlsdr + esp_libusb | librtlsdr-style port | librtlsdr adapt | **Clean-room tables** |
| Primary product | ADS-B UI | Network IQ + WebSDR | rtl_tcp / rtl_fm demos | On-device radio UI |
| Dual-core USB/net | App-specific | **Documented Core0/1** | Less formal | Planned / partial |
| PSRAM IQ ring | App buffers | **2–8 MB SPSC** | TCP buffers | Needed |
| rtl_tcp | No | Yes | Yes | Not yet |
| Local FM audio | No (ADS-B) | WebSDR + standalone FM track | **rtl_fm I2S** | Yes (Tab5 speaker) |
| V4 Blog focus | Dongle used | Yes + gap analysis | V3 focus / P4 generic | **Yes, measured** |
| Production feel | High (vertical) | High feature, WIP | Low (PoC) | Mid (measured radio) |

### USB HS impact (shared fact)

- Blog V4 bulk EP **0x81**, **512 B MPS** at High-Speed.
- **ESP32-P4 HS** can feed multi-MS/s IQ into RAM.
- **ESP32-S2 FS** cannot; ~0.25 MS/s class is structural, not “tune the code harder.”
- For network streaming, **Ethernet > WiFi** once rate exceeds ~1 MS/s class TCP budgets.

### Which is the best foundation for a *generic* V4 ESP driver?

| Candidate | Verdict |
|---|---|
| **xtrsdr** | Best early PoC for “librtlsdr on ESP USB host”; wrong legal/architecture base for OrcSDR clean-room driver. |
| **esp32p4-wifi-rtlsdr** | Best **system architecture** (ring, dual-core, protocols, eth/wifi). Wrong base to *copy* as driver source. |
| **ADS-B Scope** | Best **USB reliability / product UX** on a P4 display board. Domain is ADS-B, not generic IQ API. |
| **OrcSDR RTL-SDRv4-ESP** | Best **foundation for OrcSDR’s own driver**: already clean-room, V4-filtered, measured Tab5 path. Peers inform **gaps and targets**, not the transfer body. |

**Recommendation:** Keep **RTL-SDRv4-ESP** as the sole driver core. Treat esp32p4-wifi-rtlsdr as the **primary architectural peer** for IQ delivery + networking; ADS-B Scope as the **reliability peer**; xtrsdr as the **FS ceiling / history peer**.

---

## Common pitfalls (all three + us)

1. **Control vs bulk concurrency** — EP0 while bulk outstanding (OrcSDR already hit this on hot-tune).
2. **WiFi as sole transport** — jitter and range kill sustained IQ (xtrsdr explicit; wifi-rtlsdr uses eth for high rate).
3. **Incomplete Blog V4 path** — missing IF/mux/bias details even in librtlsdr ports (wifi-rtlsdr’s own DRIVER_ANALYSIS).
4. **DMA / cache fences on P4** — ADS-B Scope’s AXI DMA fence work is a warning for high-rate bulk.
5. **Mixing UI and USB on one core** — choppy audio / dropped samples (OrcSDR experience).
6. **License entanglement** — GPL librtlsdr ports cannot be silently folded into AGPL clean-room component without deliberate relicensing review.

---

## What OrcSDR should take next (summary)

1. Finish **driver extraction** (Gate 2) without importing peer register files.  
2. Implement **Core0 USB + ring + Core1 consumer** using peers as design references.  
3. Add **rtl_tcp** (and later optional SpyServer) as **apps**, not inside the driver.  
4. Prefer **Ethernet** example for high-rate IQ; WiFi for control + modest rates.  
5. Steal **hot-plug / STALL recovery** *state machines*, not their librtlsdr guts.  
6. Keep local demod (OrcSDR strength) while adding network streaming as optional product surface.

Detailed work items: **`docs/IMPLEMENTATION_FROM_PEER_RESEARCH.md`**.
