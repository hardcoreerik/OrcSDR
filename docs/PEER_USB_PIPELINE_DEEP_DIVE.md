# Peer USB pipeline deep-dive (for RTL-SDRv4-ESP Gate 2)

**Date:** 2026-08-07  
**Sources reviewed (source-level):**

| Project | Repo | Core USB / stream files |
|---|---|---|
| ADS-B Scope | [jstockdale/T-Display-P4](https://github.com/jstockdale/T-Display-P4) `adsb` | `main/examples/adsb_receiver/{esp_libusb.c,class_driver.c,librtlsdr.c,tuner_r82xx.c}` |
| Related ADS-B fork | [SAMS0N1TE/esp32p4-rtl-sdr-v4](https://github.com/SAMS0N1TE/esp32p4-rtl-sdr-v4) | `main/{usb_host_lib_main.c,class_driver.c,esp_libusb.c}` |
| esp32p4-wifi-rtlsdr | [r4d10n/esp32p4-wifi-rtlsdr](https://github.com/r4d10n/esp32p4-wifi-rtlsdr) | `components/rtlsdr/rtlsdr.c`, `components/rtltcp/rtltcp.c`, `main/main.c`, `docs/BUGS_AND_FIXES.md` |
| xtrsdr | [XTR1984/xtrsdr](https://github.com/XTR1984/xtrsdr) | `librtlsdr/src/{libusb.c,librtlsdr.c}`, `esp32s2/*`, `esp32p4/*` |
| OrcSDR reference app | this repo | `apps/orcsdr-tab5/ui/main.cpp`, clean-room tables |

**License warning for OrcSDR:** r4d10n and xtrsdr are **librtlsdr ports** (GPL). ADS-B Scope also embeds librtlsdr + R82xx. OrcSDR’s driver is **clean-room measured transfers** — take **architecture and ESP-IDF call sequences** only. Do **not** copy tuner PLL math, init arrays, or register tables from those trees.

**IQ format correction:** RTL2832U bulk EP `0x81` delivers **unsigned 8-bit interleaved CU8** (`I0,Q0,I1,Q1,...`), **not** 16-bit I/Q. All three projects treat the buffer as `uint8_t *` of even length. Convert to signed: `si = (int16_t)buf[2*i] - 127`.

---

## Project 1 — ADS-B Scope (jstockdale / SAMS0N1TE)

### 1. Where is the USB Host logic?

| File | Role |
|---|---|
| `esp_libusb.c` / `.h` | libusb-shaped wrapper: control + bulk sync on ESP-IDF `usb_host_*` |
| `class_driver.c` | Client register, device open, `rtlsdr_open`, ADS-B loop, bias-T UI hooks |
| `librtlsdr.c` | Full librtlsdr port (open/init/tune/bias) |
| `tuner_r82xx.c` | R820T/R828D tuner (V4 uses `CHIP_R828D`) |
| SAMS0N1TE `usb_host_lib_main.c` | `usb_host_install` + host lib event task |

### 2. Client registration / open / claim sequence

**Host install** (SAMS0N1TE `usb_host_lib_main.c` — same ESP-IDF pattern ADS-B uses):

```c
usb_host_config_t host_config = {
    .skip_phy_setup = false,
    .intr_flags     = ESP_INTR_FLAG_LEVEL1,
    .peripheral_map = BIT0,   /* P4 HS PHY map — platform specific */
};
ESP_ERROR_CHECK(usb_host_install(&host_config));

// Dedicated task (MUST run continuously):
while (has_clients) {
    uint32_t event_flags;
    ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &event_flags));
    // handle NO_CLIENTS / ALL_FREE for uninstall
}
```

**Client register** (SAMS0N1TE `class_driver.c`):

```c
usb_host_client_config_t cfg = {
    .is_synchronous = false,
    .max_num_event_msg = 5,
    .async = {
        .client_event_callback = client_event_cb,  /* NEW_DEV / DEV_GONE */
        .callback_arg = &driver_obj,
    },
};
ESP_ERROR_CHECK(usb_host_client_register(&cfg, &hdl));
// class_driver_task loops: usb_host_client_handle_events(hdl, ...)
```

**Device open** (on `USB_HOST_CLIENT_EVENT_NEW_DEV`):

```c
ESP_ERROR_CHECK(usb_host_device_open(client_hdl, dev_addr, &dev_hdl));
// then spawn setup task OUTSIDE the callback (critical — see pitfalls)
xTaskCreatePinnedToCore(rtlsdr_setup_task, "rtlsdr_setup", 8192, ..., 0);
```

**Interface claim:** done inside librtlsdr `rtlsdr_open` (interface 0; bulk EP `0x81`).

**Deadlock fix (SAMS0N1TE README):** never call `rtlsdr_open()` / control-heavy init **from inside** `client_event_cb`. Init needs the event pump to complete EP0 transfers → use a separate task.

### 3. Bulk IN — URB style

ADS-B Scope is **mostly single-URB synchronous**, not multi-URB pipelining:

```c
// esp_libusb.c
#define RTLSDR_BUF_LEN (16384 + 512)

// Pre-allocate once (heap fragmentation fix):
usb_host_transfer_alloc(RTLSDR_BUF_LEN + 512, 0, &adsbdev->transfer);

int esp_libusb_bulk_transfer(..., endpoint, data, length, transferred, timeout) {
    adsbdev->transfer->num_bytes = length;
    adsbdev->transfer->device_handle = driver_obj->dev_hdl;
    adsbdev->transfer->bEndpointAddress = endpoint;  // 0x81
    adsbdev->transfer->callback = bulk_transfer_read_cb;
    adsbdev->is_done = false;
    usb_host_transfer_submit(adsbdev->transfer);
    while (!adsbdev->is_done)
        usb_host_client_handle_events(driver_obj->client_hdl, portMAX_DELAY);
    *transferred = adsbdev->bytes_transferred;
    memcpy(data, adsbdev->transfer->data_buffer, *transferred);
}
```

**SAMS0N1TE improvements:**

| Issue | Fix |
|---|---|
| `vTaskDelay(1)` spin-wait → WDT / cross-core thrash | Binary semaphore in bulk complete CB |
| USB + DSP same core contention | `adsb_rx` on **CPU1**; USB host/client on **CPU0** |
| `MAX_PACKET_SIZE 128000` → transfer failures | **16384** bytes; assemble 2× into 32 KB demod buffer |

### 4. IQ parsing

CU8 buffer fed to Mode-S demod (`demod1090` / mode-s). No float conversion in USB path:

```c
// Pattern used across ADS-B ports:
// buffer[i] is uint8_t; sample pair at (2*k, 2*k+1)
// magnitude / magnitude-squared demod, not complex float at USB edge
```

### 5. V4 / R828D / bias-tee

- `librtlsdr.c`: if `RTLSDR_TUNER_R828D` → `R828D_I2C_ADDR`, `CHIP_R828D`
- Bias-T: `rtlsdr_set_bias_tee(dev, on)` → GPIO0 via SYSB `GPO`/`GPOE`/`GPD` (librtlsdr path)
- V4-specific: **EP0 STALL on some demod read-backs** — ADS-B documents: do **not** close/reopen device (kills bulk EP); yield ~50 ms for USBH daemon; treat STALL as non-fatal like desktop libusb
- V4 also uses bias-T GPIO5 in R828D path for input mux (cable 1/2) in tuner code

### ADS-B: A–E extraction summary

| Item | Value |
|---|---|
| A Init | install → host task → client_register → NEW_DEV → **setup task** → device_open → rtlsdr_open/claim |
| B URB | **1** bulk transfer, **16 KB+512**, submit + pump events until CB |
| C IQ | CU8 pairs; ADS-B magnitude path |
| D Tune | librtlsdr + r82xx (GPL) |
| E Ring | App-level queues / SD buffers; **not** multi-URB SPSC IQ ring |

---

## Project 2 — esp32p4-wifi-rtlsdr (r4d10n)

### 1. Clean-room or librtlsdr port?

**Direct port of librtlsdr register/protocol encoding**, adapted to ESP-IDF USB Host. Header says “Ported from librtlsdr.” **GPL-2.0-or-later.**

**Reusability for OrcSDR:** architecture, URB counts, dual-task model, bug list — **yes**. Source copy of `tuner_r82xx.c` / demod init arrays — **no** (clean-room + AGPL dual-license strategy).

### 2. Ring buffer design

```c
// components/rtltcp/rtltcp.c
srv->ring_buf = xRingbufferCreate(srv->ring_size, RINGBUF_TYPE_BYTEBUF);  // often PSRAM

// USB async CB → ring (non-blocking send = drop on full):
uint32_t rtltcp_push_samples(...) {
    if (xRingbufferSend(srv->ring_buf, data, len, 0) == pdTRUE) ...
}

// Network task:
uint8_t *data = xRingbufferReceiveUpTo(srv->ring_buf, &item_size,
                                       pdMS_TO_TICKS(5), 32768);
// send(...); vRingbufferReturnItem(...);
```

Architecture doc describes **Core0 USB + ring write**, **Core1 TCP/UDP/WebSDR read**, lock-free SPSC conceptual model; implementation uses FreeRTOS **byte ring buffer** (mutex inside FreeRTOS API — fine at ≤1–2 MSPS).

### 3. USB issues and resolutions (docs/BUGS_AND_FIXES.md)

High-value pitfalls for OrcSDR:

| # | Bug | Lesson for OrcSDR |
|---|---|---|
| 1 | Zero-IF reg `0xB1` = `0x1A` not `0x1B` | Use **measured** clean-room table, not guessed bits |
| 2/15 | demod wIndex/wValue encoding | Clean-room tables already encode observed packets |
| 3 | I2C repeater must be on before tuner | Measured init sequence order matters |
| 4 | I2C max **8-byte** chunks | Any I2C via EP0 must chunk |
| 5/6 | HS EPA regs at **0x2148**, MAXPKT HS | OrcSDR tables already include HS EPA path |
| 7 | R82xx IF **3.57 MHz** in librtlsdr | **OrcSDR measured LO offset differs** (`kRtlIfOffsetHz ≈ 1.814972e6`) — keep measured, not peer IF |
| 10 | Missing **client event task** → ctrl hangs | Always run `usb_host_client_handle_events` on a task |
| 14 | Bias-T GPIO addresses SYSB | Optional feature; CAP_BIAS_TEE off until measured |

Also: never call heavy open from USB event CB (same as SAMS0N1TE).

### 4. HS vs FS

- Project targets **ESP32-P4 HS** primarily
- EPA HS address space `0x2000+` vs FS `0x0000+` documented in bugs
- No full dual-target conditional tree like `#if CONFIG_IDF_TARGET_ESP32S2` for bulk MPS — P4 assumes **512-byte** bulk MPS

### 5. Multi-URB continuous streaming (best reference)

```c
// rtlsdr.c
#define DEFAULT_BUF_NUM  6
#define DEFAULT_BUF_LEN  (32 * 512)   /* 16 KiB */
#define RTLSDR_BULK_EP   0x81

static void bulk_xfer_cb(usb_transfer_t *xfer) {
    rtlsdr_dev_t *dev = xfer->context;
    if (xfer->status == USB_TRANSFER_STATUS_COMPLETED && xfer->actual_num_bytes > 0) {
        if (dev->async_cb)
            dev->async_cb(xfer->data_buffer, xfer->actual_num_bytes, dev->async_ctx);
    }
    if (dev->async_running)
        usb_host_transfer_submit(xfer);   /* re-arm same URB */
    else
        xSemaphoreGive(dev->xfer_sem);
}

esp_err_t rtlsdr_read_async(dev, cb, ctx, buf_num, buf_len) {
    // default 6 x 16KB, round buf_len down to multiple of 512
    for (i...) {
        usb_host_transfer_alloc(buf_len, 0, &dev->xfers[i]);
        dev->xfers[i]->device_handle = dev->dev_hdl;
        dev->xfers[i]->bEndpointAddress = RTLSDR_BULK_EP;
        dev->xfers[i]->num_bytes = buf_len;
        dev->xfers[i]->callback = bulk_xfer_cb;
        dev->xfers[i]->context = dev;
    }
    rtlsdr_reset_buffer(dev);
    dev->async_running = true;
    for (i...) usb_host_transfer_submit(dev->xfers[i]);
    while (dev->async_running) vTaskDelay(pdMS_TO_TICKS(100));
    // wait counting semaphore for all to finish after stop
}
```

**Client init (same file):**

```c
usb_host_client_register(&client_config, &dev->client_hdl);
xTaskCreatePinnedToCore(usb_client_event_task, "usb_client", 4096, dev, 10, ..., 0);
// task body: while (running) usb_host_client_handle_events(client_hdl, 50ms);

usb_host_device_open(...);
usb_host_interface_claim(client, dev, 0, 0);
// optional claim interface 1
```

**Control transfer encoding (librtlsdr-compatible):**

```c
// write_reg:  bmRequestType=0x40, bRequest=0, wValue=addr, wIndex=(block<<8)|0x10
// read_reg:   bmRequestType=0xC0, bRequest=0, wValue=addr, wIndex=(block<<8)
// demod_write: wValue=(addr<<8)|0x20, wIndex=page|0x10, then dummy IN read
// STALL recovery: halt/flush/clear EP0, retry ≤2
```

**set_center_freq:**

```c
rtlsdr_set_i2c_repeater(dev, true);
r82xx_set_freq(dev, freq);   // I2C over vendor EP0
rtlsdr_set_i2c_repeater(dev, false);
```

### r4: A–E summary

| Item | Value |
|---|---|
| A | install (app) + client_register + client event task + open + claim 0 (+1) |
| B | **6 × 16 KB**, CB resubmits, background client task pumps events |
| C | Pass raw CU8 to ring / decoders; no in-driver float |
| D | Full librtlsdr encode + R82xx |
| E | FreeRTOS `xRingbuffer` bytebuf (MB-scale PSRAM), non-blocking push |

---

## Project 3 — xtrsdr (XTR1984)

### 1. What does “adapted librtlsdr” mean?

**Rewrite of the libusb backend** (`librtlsdr/src/libusb.c`) to call ESP-IDF USB Host, while keeping **stock librtlsdr + tuner sources** almost intact. Not a thin wrapper around host libusb — it **is** librtlsdr with a new USB OS layer.

GPL; same reusability rule as r4.

### 2. S2 rates: 240k WiFi / 300k Ethernet — bottleneck?

- **USB FS** theoretical bulk ~1.2 MB/s; at CU8 2 bytes/sample → ~**600 kSPS theoretical**, but host stack + WiFi overhead dominate
- Author: WiFi stream **unstable with distance** (“не получилось”)
- Ethernet W5500 SPI path better than WiFi but still **CPU + SPI + stack** limited → **300 kSPS** practical
- Bottleneck order: **network path first**, then **FS USB**, then CPU

### 3. P4 full 2 MSPS — what changes?

- Same librtlsdr async path; **HS USB** allows sustained bulk near 2 MSPS CU8 (~4 MB/s)
- P4 app: `esp32p4/rtl_tcp_eth_esp32p4` uses **Ethernet** (not WiFi) for backhaul
- `DEFAULT_SAMPLE_RATE` P4 path: **2000000**; S2 WiFi: **240000**
- EPA programmed for **512-byte** HS (`USB_EPA_MAXPKT = 0x0002` comment in librtlsdr)
- Async defaults in xtr librtlsdr: `DEFAULT_BUF_NUMBER=15`, `DEFAULT_BUF_LENGTH=(16*16*25)` (= 6400) — small buffers; P4 rtl_tcp may pass larger `buf_len`

Key USB layer (`libusb.c`):

```c
usb_host_install(&config);
usb_host_client_register(&client_config, &uh_handle);
xTaskCreate(usbhost_daemon_task, ...);   // usb_host_lib_handle_events
xTaskCreate(usbhost_one_client_task, ...); // usb_host_client_handle_events
// control: alloc per call + binary semaphore CB (alloc churn — ADS-B improved this)
```

Async (librtlsdr.c): multi-URB submit, `_libusb_callback` resubmits, halt/flush/clear on cancel.

### 4. rtl_fm + I2S audio pipeline

`esp32s2/rtl_fm_i2s_example`: classic rtl_fm structure:

- Dongle task: `rtlsdr_read_async` → CB fills demod input
- Demod task: FM demod → audio samples
- I2S to MAX98357A at audio rate (default 16 kHz class)

**Implication for OrcSDR dual-core:** USB producer core ≠ demod/audio/UI consumer core; soft queue between them (OrcSDR already has free/filled queues for IQ blocks).

### 5. Stability

- **Experimental** hobby project; README candid about WiFi instability
- P4 2 MSPS claim is “full samplerate” over Ethernet — production readiness not claimed
- Useful proof points: FS vs HS, multi-URB pattern, rtl_tcp command handling

### xtrsdr: A–E summary

| Item | Value |
|---|---|
| A | Global `usbhost_begin()`: install + client + **two** event tasks |
| B | **15 × ~6.4 KB** default (or app-supplied); EP `0x81`; resubmit in CB |
| C | CU8 → app CB (tcp or demod) |
| D | Full librtlsdr + r82xx |
| E | rtl_tcp linked list / sockets (S2); not shared SPSC with OrcSDR app |

---

## Side-by-side comparison

| Function | ADS-B Scope | r4d10n wifi-rtlsdr | xtrsdr | OrcSDR Tab5 app (today) |
|---|---|---|---|---|
| **Client init** | class_driver + host task | `rtlsdr_init` owns client + client task | Global `usbhost_begin` | In-app `usb_host_install` + client in UI |
| **Open/claim** | librtlsdr open; setup task off-CB | device_open + claim 0/1 | librtlsdr open | device_open + claim; clean-room init table |
| **URB model** | **1 sync** URB, 16 KB | **6 async** 16 KB, resubmit | **15 async** ~6 KB, resubmit | Continuous bulk **32 KB**, ring depth **3** (partial dual-core) |
| **Event pump** | client_handle in wait loop | dedicated client task | daemon + client tasks | pump in wait_for_flag |
| **IQ parse** | CU8 → ADS-B demod | CU8 → ring → net/decoders | CU8 → rtl_tcp / rtl_fm | CU8 → FM/AM demod + spectrum |
| **Freq tune** | librtlsdr R828D | `r82xx_set_freq` + I2C repeater | same | Measured PLL pack / hot retune between bulks |
| **Ring** | app queues | FreeRTOS ringbuf MB | tcp lists | free_q + filled_q SPSC-like |
| **HS/FS** | P4 HS | P4 HS + bug notes | S2 FS vs P4 HS apps | P4 HS only claimed |
| **License** | GPL (librtlsdr) | GPL port | GPL port | Clean-room AGPL component |

---

## Recommended architecture for `rtl_sdr_v4_esp`

```text
                    ┌─────────────────────────────────────┐
                    │  App (orcsdr-tab5, rtl_tcp, tests)   │
                    │  event_cb / SPSC consumer only      │
                    └─────────────────┬───────────────────┘
                                      │ public C API
                    ┌─────────────────▼───────────────────┐
                    │  rtl_sdr_v4_esp (component)         │
                    │  install/start/stop/retune/metrics  │
                    │  mutex + state machine (v0.3 API)   │
                    └─┬───────────────┬───────────────┬───┘
                      │               │               │
           Core0 USB owner     SPSC IQ ring      Core1 optional
           task (pinned)       (slots or         delivery task
                      │         FreeRTOS q)            │
           usb_host_*  │               │          event_cb(IQ)
           multi-URB   │               │
           EP0 tables  │               │
           (clean-room)│               │
```

### Design rules (non-negotiable)

1. **No Tab5 / M5 symbols** inside the component (GPIO/VBUS via config hooks if needed).
2. **No librtlsdr source** — only measured tables + observed EP0/bulk behavior.
3. **CAP_STREAM** only after multi-URB start works on measured hardware.
4. **Never EP0 while bulk outstanding** (OrcSDR hot-retune rule + peer STALL lessons).
5. **Never open/init from client event callback** — queue address, open on owner task.
6. **Pre-allocate** ctrl + bulk transfers early (`MALLOC_CAP_DMA` / internal) — ADS-B fragmentation lesson.
7. **Targets:**
   - `CONFIG_IDF_TARGET_ESP32P4`: bulk MPS **512**, transfer_bytes multiple of 512, rates up to measured 960k–2M
   - `ESP32S2/S3`: MPS **64**, transfer_bytes multiple of 64, rates allowlist capped (e.g. ≤300k) until measured — do not claim HS

### Dual-core / SPSC

Prefer **fixed slot ring** (what Tab5 already sketches) over copying every URB into a huge byte ring:

```c
// Producer (USB CB or USB task after CB):
//  - take free slot index (queue free_q, 0 timeout → overrun++)
//  - memcpy URB → slot (or zero-copy if acquire mode later)
//  - push to filled_q

// Consumer (DSP / event delivery task on other core):
//  - receive filled_q
//  - emit EVT_IQ_BLOCK (borrow) or copy
//  - return slot to free_q
```

Alternatively FreeRTOS `xRingbuffer` like r4 for rtl_tcp-style apps — keep **behind** the public API as an internal option.

### `retune_hz()` pure driver

```text
App: retune_hz(handle, f)
  → normalize f
  → queue request on USB owner task
  → owner waits until in_flight_bulk_count == 0
  → apply clean-room tune EP0 sequence
  → EVT_RETUNED
  → never from UI paint path
```

---

## Step-by-step implementation plan (Gate 2)

### Step 1 — Move USB Host client ownership into the component

**ESP-IDF APIs:**

- `usb_host_install` / `usb_host_uninstall` (if `!host_library_already_installed`)
- `usb_host_client_register` / `deregister`
- `usb_host_lib_handle_events` (host task)
- `usb_host_client_handle_events` (client task)
- `usb_host_device_addr_list_fill` / `device_open` / `device_close`
- `usb_host_interface_claim` / `release` (iface 0; optional 1)

**Structure (adapted from r4 + xtr, clean-room identity filter):**

```c
// On install():
if (!cfg.host_library_already_installed) {
  usb_host_config_t hc = { .skip_phy_setup = false,
                           .intr_flags = ESP_INTR_FLAG_LEVEL1 };
  ESP_ERROR_CHECK(usb_host_install(&hc));
  xTaskCreatePinnedToCore(host_lib_task, "usb_lib", 4096, h, 20, ..., 0);
}
usb_host_client_config_t cc = {
  .is_synchronous = false,
  .max_num_event_msg = 8,
  .async = { .client_event_callback = on_client_evt, .callback_arg = h },
};
usb_host_client_register(&cc, &h->client);
xTaskCreatePinnedToCore(client_evt_task, "usb_cli", 4096, h, 19, ..., 0);
// Pre-alloc: ctrl xfer 64+setup; bulk pool N x transfer_bytes (DMA)

// on_client_evt: only set pending_addr / gone flags — NO open/init here
```

**Pitfalls:** open from CB (SAMS0N1TE deadlock); missing event tasks (r4 bug #10); heap fragment — pre-alloc DMA early (ADS-B).

### Step 2 — `rtl_sdr_v4_esp_start()` with URB submission

**Sequence:**

1. Validate stream config (already v0.3).
2. Wait/open device if not open: VID `0x0BDA` PID `0x2838`, mfg/product match.
3. `usb_host_interface_claim(client, dev, 0, 0)`.
4. Run **clean-room init table** via pre-allocated control transfers (existing tables).
5. Apply sample-rate + tune records (measured 960k path).
6. `reset streaming endpoint` cleanup subset if needed.
7. Allocate/submit **N bulk IN** URBs:

```c
#define EP_BULK_IN 0x81
// N = config.transfer_count (2..8), default 3
// len = config.transfer_bytes, default 32768, % 512 == 0 on P4
for (i = 0; i < N; i++) {
  usb_host_transfer_alloc(len, 0, &h->bulk[i]);
  h->bulk[i]->device_handle = h->dev;
  h->bulk[i]->bEndpointAddress = EP_BULK_IN;
  h->bulk[i]->num_bytes = len;
  h->bulk[i]->callback = bulk_done_cb;
  h->bulk[i]->context = h;
  usb_host_transfer_submit(h->bulk[i]);
}
h->state = STREAMING;
// CAP_STREAM becomes true only in this build path after validation
```

**Pitfalls:** transfer size too large (128 KB fails — use 16–32 KB); EP0 during bulk; claim without matching release on fail; STALL on V4 init reads — allow documented stalls (OrcSDR tables already mark stall range).

### Step 3 — Bulk IQ callback + ring

```c
static void bulk_done_cb(usb_transfer_t *xfer) {
  handle *h = xfer->context;
  if (xfer->status == COMPLETED && xfer->actual_num_bytes > 0) {
    // DO NOT hold API mutex across app callback
    // Copy/push to ring or deliver via delivery task
    ring_push(h, xfer->data_buffer, xfer->actual_num_bytes);
  }
  if (h->streaming)
    usb_host_transfer_submit(xfer);
}
```

**IQ view for app (already in API):**

```c
// CU8: for (size_t i = 0; i + 1 < bytes; i += 2) {
//   int16_t I = (int16_t)data[i] - 127;
//   int16_t Q = (int16_t)data[i+1] - 127;
// }
```

**Pitfalls:** heavy work in CB; recursive start/stop from CB (`ERR_REENTRANT`); ring full → count overrun, drop oldest or newest (document).

### Step 4 — `retune_hz` / center frequency via control transfers

**Do not** port r82xx source. Two OrcSDR-legal options:

1. **Measured tune table pack** per frequency quanta (what Tab5 already builds for scroll-tune).
2. Later: derive pack from published math offline, regenerate tables — still no GPL paste.

**Driver-side apply (owner task only):**

```c
// Pseudocode
wait_until(h->bulk_in_flight == 0);  // or pause resubmit briefly
for (record in tune_records_for(freq_hz))
  submit_control(record);  // bmRequestType 0x40/0xC0, bRequest 0,
                           // wValue/wIndex/wLength/data from clean-room table
h->frequency_hz = freq_hz;
emit EVT_RETUNED;
resume bulk resubmit;
```

**Peer control encoding (for understanding only):**

| Kind | bmRequestType | bRequest | wValue | wIndex |
|---|---|---|---|---|
| USB/SYS write | `0x40` | `0` | `addr` | `(block<<8)\|0x10` |
| USB/SYS read | `0xC0` | `0` | `addr` | `block<<8` |
| Demod write | `0x40` | `0` | `(addr<<8)\|0x20` | `page\|0x10` |
| Demod read | `0xC0` | `0` | `(addr<<8)\|0x20` | `page` |

OrcSDR already stores **fully observed** packets — prefer replaying those over re-implementing block encoding.

**Pitfalls:** I2C repeater windows; EP0 STALL recovery (yield or halt/flush/clear EP0 — **never** device close); don’t retune from UI core without queue.

### Step 5 — Dual-core architecture

| Task | Core | Priority (example) | Work |
|---|---|---|---|
| `usb_lib` | 0 | 20 | `usb_host_lib_handle_events` |
| `usb_cli` / USB owner | 0 | 19 | client events, EP0, URB submit, retune queue |
| bulk CB | 0 (ISR-like context) | — | minimal: ring push + resubmit |
| DSP / event delivery | 1 | 10 | IQ process, `event_cb`, audio |
| UI | 1 | 5 | M5 paint only |

**Tab5 already has:** `kRtlRingDepth = 3`, free/filled queues — **lift into component** as default delivery path feeding `EVT_IQ_BLOCK`.

**Verify:** no `vTaskDelay(1)` poll for bulk complete at high rate (SAMS0N1TE WDT); pin USB and DSP to opposite cores.

---

## Mapping to current OrcSDR public API (v0.3)

| Peer function | OrcSDR API |
|---|---|
| init/open | `rtl_sdr_v4_esp_install` + lazy open on start / hotplug |
| read_async | internal to `start` + `EVT_IQ_BLOCK` |
| set_center_freq | `rtl_sdr_v4_esp_retune_hz` (+ start stream_config) |
| cancel/stop | `rtl_sdr_v4_esp_stop` |
| close | `rtl_sdr_v4_esp_uninstall` |

Enable `RTL_SDR_V4_ESP_CAP_STREAM | CAP_RETUNE` only after Step 2–4 pass on Tab5 measured path at **960 kSPS**.

---

## Immediate next coding actions

1. Implement USB owner task + host/client install path inside `components/rtl_sdr_v4_esp/src/`.
2. Wire clean-room control runner (from Tab5 `run_control_record`) as private `ctrl_submit()`.
3. Multi-URB bulk with resubmit; ring → events.
4. Port Tab5 continuous listen off in-app USB to component `start/stop`.
5. Gate CAP bits; integration test on COM-attached Tab5 + Blog V4.
