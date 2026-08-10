# RTL-SDRv4-ESP public API (best-in-class contract)

**Header:** `components/rtl_sdr_v4_esp/include/rtl_sdr_v4_esp.h`  
**Version:** 0.4.1

This document is the human contract for a **professional, deterministic** driver API.
The implementation must not break these rules as streaming and retune evolve.

---

## Design goals

| Goal | Mechanism |
|---|---|
| Works every time | Strict validation; no half-open USB; fail closed to IDLE/FAULT |
| Safe under concurrency | Per-handle mutex; RAII locks; short timeouts on queries |
| No callback re-entry | `in_callback_depth` → `ERR_REENTRANT` on lifecycle APIs |
| Events outside lock | Callbacks run only after mutex release |
| Stable ABI growth | `struct_size` on config structs; new fields only at end |
| Clear failures | Component error codes + `err_to_name` + `get_last_error` |
| Feature discovery | `get_capabilities`, rate allowlist, `get_supported_rates` |
| No silent over-claim | Capability bits are enabled only for implemented paths |
| Idempotent teardown | `stop` when idle; `uninstall(NULL)`; skip events during destroy |

---

## Lifecycle

```text
install → IDLE
start   → STREAMING   (or error, stay IDLE / FAULT — never half-open)
retune  → STREAMING   (queued; never EP0 while bulk outstanding)
stop    → IDLE        (idempotent; emits EVT_STOPPED once when leaving stream)
uninstall → destroyed (NULL-safe; always frees; second use → STALE_HANDLE)
reset   → IDLE from FAULT if not streaming (clears metrics)
```

### Idempotence and safety

| Call | Behavior |
|---|---|
| `stop` when IDLE | `ESP_OK` |
| `stop(NULL)` | `ESP_OK` |
| `uninstall(NULL)` | `ESP_OK` |
| `uninstall` twice | second → `STALE_HANDLE` |
| `start` while STREAMING | `ERR_BUSY` |
| `start` / `stop` / `retune` / `reset` from event callback | `ERR_REENTRANT` |
| `get_*` with NULL/stale handle | safe; no crash |
| failed `start` | handle remains **IDLE**; no interface claim |

---

## Threading

1. Public API is serialized per handle (mutex).
2. Event callbacks must not call `install` / `uninstall` / `start` / `stop` / `retune` / `reset` on the same handle (`ERR_REENTRANT`).
3. Callbacks **may** call `get_state`, `get_metrics`, `get_device_info`, `get_last_error`, `release_iq_block`.
4. IQ payload pointers are **borrowed** until the callback returns (unless acquire mode + `release_iq_block` when CAP_IQ_ACQUIRE is set).
5. Never invoke API from a USB completion ISR.
6. Do not call other APIs concurrent with `uninstall` on the same handle (single owner task).

---

## Validation rules

### Config (`config_validate`)

- `struct_size == sizeof(config)`
- `transfer_bytes` in [512, 262144] and **multiple of 512**
- `transfer_count` in [2, 8]
- `control_timeout_ms` in (0, 30000]
- `usb_task_core_id` in {0, 1, 0xFF}

### Stream (`stream_config_validate`)

- `struct_size` match
- `sample_rate_sps` allowlisted (`960k`, `1024k`, `2048k` for now)
- `CUSTOM_HZ`: frequency in [24 MHz, 1766 MHz], quantized to 1 kHz
- Named presets ignore `frequency_hz` (driver LO constants)
- `max_bytes` even (IQ pairs) when non-zero
- `timeout_ms` ≤ 30000

### Helpers

| Function | Purpose |
|---|---|
| `config_default` / `stream_config_default` | Zero + safe defaults + `struct_size` |
| `is_rate_supported` | Single-rate allowlist check |
| `get_supported_rates` | Copy or size-query the allowlist |
| `normalize_frequency` | Clamp/quantize policy |
| `preset_frequency_hz` | Named preset LO (not CUSTOM) |
| `state_to_name` / `err_to_name` | Logging without sprintf tables in apps |
| `release_iq_block` | No-op in borrow mode (safe unconditional call) |

---

## Errors

Prefer component codes over generic `INVALID_STATE` when the app can branch:

| Code | Meaning |
|---|---|
| `NO_DEVICE` | No Blog V4 attached |
| `NOT_V4` | USB device present but identity mismatch |
| `BUSY` | Already streaming / stop in progress / concurrent uninstall |
| `NOT_STREAMING` | retune without stream |
| `BAD_RATE` / `BAD_FREQ` | Policy reject |
| `USB` / `TIMEOUT` / `FAULT` | Hardware path |
| `UNSUPPORTED` | Feature not built yet (extraction) |
| `STALE_HANDLE` | Use after uninstall / bad pointer |
| `REENTRANT` | Lifecycle API called from event callback |
| `NOT_CLAIMED` | Device present but not claimed (future) |
| `NOT_READY` | Transient not-ready (future hotplug) |

---

## Capabilities (0.4.1 binary)

| Flag | Status |
|---|---|
| `METRICS` | On |
| `CUSTOM_HZ` | On |
| `STREAM` | On |
| `RETUNE` | On; bulk drains before EP0 apply |
| `HOTPLUG` | On; events implemented, unplug/replug recovery soak pending |
| `IQ_ACQUIRE` | Off until acquire mode ships |
| `BIAS_TEE` / `DIRECT_SAMPLING` | Reserved off |

Apps must:

```c
if (rtl_sdr_v4_esp_get_capabilities() & RTL_SDR_V4_ESP_CAP_STREAM) {
    /* start guaranteed to attempt USB */
} else {
    /* feature is unavailable in this build */
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
/* handle hardware and validation errors; check CAP_STREAM first */

/* teardown always */
(void)rtl_sdr_v4_esp_stop(sdr, 0); /* 0 = default timeout */
(void)rtl_sdr_v4_esp_uninstall(sdr);
sdr = NULL;
```

### Event callback rules (copy-paste)

```c
static void on_evt(rtl_sdr_v4_esp_event_t ev, const void *payload, void *ctx)
{
    switch (ev) {
    case RTL_SDR_V4_ESP_EVT_IQ_BLOCK: {
        const rtl_sdr_v4_esp_iq_block_t *iq = payload;
        /* copy or process quickly; pointer invalid after return */
        (void)rtl_sdr_v4_esp_release_iq_block((rtl_sdr_v4_esp_handle_t)ctx, iq);
        break;
    }
    case RTL_SDR_V4_ESP_EVT_ERROR: {
        const rtl_sdr_v4_esp_error_info_t *e = payload;
        ESP_LOGE("app", "%s", e ? e->message : "?");
        break;
    }
    default:
        break;
    }
    /* NEVER call start/stop/uninstall here on this handle */
}
```

---

## Streaming invariants

Preserve these implemented invariants:

- [x] No EP0 while bulk is outstanding
- [x] Start failure has a cleanup path
- [x] Stop releases the interface best effort
- [x] Metrics use bounded synchronization
- [x] `EVT_IQ_BLOCK` does not hold the API mutex across the app callback
- [x] Retune drains bulk before the USB control sequence
- [x] `CAP_STREAM | CAP_RETUNE` match working code paths
- [x] Reentrancy guard rejects lifecycle calls from callbacks

Hardware soak and recovery acceptance remain tracked in `PROJECT_STATUS.md`.

---

## Versioning policy

- **Patch:** bugfix, no API change  
- **Minor:** new fields at end of structs (requires `struct_size`), new functions  
- **Major:** break ABI or semantics  

Bump `RTL_SDR_V4_ESP_VERSION_*`, `RTL_SDR_V4_ESP_VERSION_STRING` (via macros), and `idf_component.yml` together.

Packed runtime version: `(major << 16) | (minor << 8) | patch` from `get_version()`.
