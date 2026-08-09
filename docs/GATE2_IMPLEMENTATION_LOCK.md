# Gate 2 implementation lock (2026-08-07)

> **Historical snapshot.** This file records the v0.4.0 handoff and is not the
> current roadmap. Hot retune subsequently landed in v0.4.1. See
> [`../PROJECT_STATUS.md`](../PROJECT_STATUS.md).

Decisions from operator Q&A:

| Choice | Decision |
|---|---|
| Where | OrcSDR component **+** Tab5 app together |
| Scope | Full pipeline 1–5 (retune is **queue-only**) |
| URB default | **6 × 16 KiB** (Tab5 may override) |
| Retune | Queue / metrics only; no hot EP0 apply yet |
| Dual-core | USB Core0, IQ delivery Core1 |
| Legacy USB | Behind `RTL_USE_LEGACY_USB=0` (default off) |
| Hardware | COM17 flash performed after build |

## Version

Driver **v0.4.0** — `CAP_STREAM | CAP_METRICS | CAP_CUSTOM_HZ | CAP_HOTPLUG`  
`CAP_RETUNE` remains **off** until EP0 apply lands.

## Serial smoke strings (new path)

```
RTL_INSTALL ok v0.4.0 ...
RTL_SDR_PROBE_OK v4=true driver=rtl_sdr_v4_esp ...
RTL_START ESP_OK
RTL_STOP bytes=...
```

## Not done / follow-ups

1. Hot retune EP0 apply between bulk gaps (restore scroll-tune LO)  
2. Audio polish (still app-side demod)  
3. Bounded capture SHA path on driver  
4. Delete legacy USB block after soak  
5. Commit / push OrcSDR when operator requests  
