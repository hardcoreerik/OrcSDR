# RTL-SDRv4-ESP public API (best-practice contract)

**Header:** `components/rtl_sdr_v4_esp/include/rtl_sdr_v4_esp.h`  
**Version:** 0.2.0

This document is the human contract for a **professional, deterministic** driver API.
The implementation must not break these rules when USB streaming is fully extracted.

---

## Design goals

| Goal | Mechanism |
|---|---|
| Works every time | Strict validation; no half-open USB; idempotent stop/uninstall |
| Safe under concurrency | Per-handle mutex; documented callback rules |
| Stable ABI growth | `struct_size` on config structs |
| Clear failures | Component error codes + `err_to_name` + `get_last_error` |
| Feature discovery | `get_capabilities` + rate allowlist |
| No silent over-claim | CAP_STREAM/RETUNE off until extraction; start returns UNSUPPORTED |

---

## Lifecycle

```text
install → IDLE
start   → STREAMING   (or error, stay IDLE / FAULT)
retune  → STREAMING   (queued; never during bulk)
stop    → IDLE        (idempotent)
uninstall → destroyed (NULL-safe, always frees)
reset   → IDLE from FAULT if not streaming
```

### Idempotence

| Call | Behavior |
|---|---|
| `stop` when IDLE | `ESP_OK` |
| `uninstall(NULL)` | `ESP_OK` |
| `uninstall` twice | second → `STALE_HANDLE` |
| `start` while STREAMING | `ERR_BUSY` |

---

## Threading

1. Public API is serialized per handle (mutex).
2. Event callbacks must not call `install` / `uninstall` / `start` / `stop` on the same handle (deadlock risk).
3. IQ payload pointers are **borrowed** until callback returns.
4. Never invoke API from USB completion ISR.

---

## Validation rules

### Config

- `struct_size == sizeof(config)`
- `transfer_bytes` in [512, 262144] and **multiple of 512**
- `transfer_count` in [2, 8]
- `control_timeout_ms` in (0, 30000]
- `usb_task_core_id` in {0, 1, 0xFF}

### Stream

- `struct_size` match
- `sample_rate_sps` allowlisted (`960k`, `1024k`, `2048k` for now)
- `CUSTOM_HZ`: frequency in [24 MHz, 1766 MHz], quantized to 1 kHz
- `max_bytes` even (IQ pairs) when non-zero

---

## Errors

Prefer component codes over generic `INVALID_STATE` when the app can branch:

| Code | Meaning |
|---|---|
| `NO_DEVICE` | No Blog V4 attached |
| `NOT_V4` | USB device present but identity mismatch |
| `BUSY` | Already streaming / stop in progress |
| `NOT_STREAMING` | retune without stream |
| `BAD_RATE` / `BAD_FREQ` | Policy reject |
| `USB` / `TIMEOUT` / `FAULT` | Hardware path |
| `UNSUPPORTED` | Feature not built yet (extraction) |
| `STALE_HANDLE` | Use after uninstall |

---

## Capabilities (0.2.0 binary)

| Flag | Status |
|---|---|
| `METRICS` | On |
| `CUSTOM_HZ` | Policy on (stream path pending) |
| `STREAM` | Off until Gate 2 |
| `RETUNE` | Off until Gate 2 |
| `HOTPLUG` | Off until implemented |
| `BIAS_TEE` / `DIRECT_SAMPLING` | Reserved off |

Apps must:

```c
if (rtl_sdr_v4_esp_get_capabilities() & RTL_SDR_V4_ESP_CAP_STREAM) {
    /* start guaranteed to attempt USB */
} else {
    /* expect UNSUPPORTED; use Tab5 app path or wait for release */
}
```

---

## Recommended app pattern

```c
rtl_sdr_v4_esp_config_t cfg;
rtl_sdr_v4_esp_config_default(&cfg);
cfg.event_cb = on_evt;
ESP_ERROR_CHECK(rtl_sdr_v4_esp_config_validate(&cfg));

rtl_sdr_v4_esp_handle_t sdr = NULL;
esp_err_t err = rtl_sdr_v4_esp_install(&cfg, &sdr);
if (err != ESP_OK) { /* sdr is NULL */ }

rtl_sdr_v4_esp_stream_config_t st;
rtl_sdr_v4_esp_stream_config_default(&st);
st.preset = RTL_SDR_V4_ESP_PRESET_CUSTOM_HZ;
st.frequency_hz = 100100000;
err = rtl_sdr_v4_esp_start(sdr, &st);
/* handle UNSUPPORTED until extraction */

/* teardown always */
rtl_sdr_v4_esp_stop(sdr, 3000);
rtl_sdr_v4_esp_uninstall(sdr);
```

---

## Implementation checklist (streaming fill-in)

When USB code lands, preserve:

- [ ] No EP0 while bulk outstanding  
- [ ] start failure never leaves interface claimed  
- [ ] stop always releases interface (best effort)  
- [ ] metrics updated under lock briefly or atomics  
- [ ] EVT_IQ_BLOCK never holds mutex across app callback  
- [ ] retune queued to USB owner task only  

---

## Versioning policy

- **Patch:** bugfix, no API change  
- **Minor:** new fields at end of structs (requires `struct_size`), new functions  
- **Major:** break ABI or semantics  

Bump `RTL_SDR_V4_ESP_VERSION_*` and `idf_component.yml` together.
