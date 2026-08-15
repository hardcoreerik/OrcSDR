# OrcSDR roadmap

This file tracks externally-sourced review feedback and the gaps it surfaces,
as accepted roadmap items. `PROJECT_STATUS.md` remains the authoritative
current-state and evidence-boundary document — this file exists to hold
review-derived findings that are not yet folded into that snapshot, and to
point at `phasing.md` for how each item gets implemented.

## Evidence labels

Same convention as `PROJECT_STATUS.md`: **Hardware-verified**,
**Build-verified**, **Implemented**, **Planned**, **Deferred**.

## External review: Grok code review (2026-08-10)

A third-party review (Grok, reviewing `hardcoreerik/OrcSDR` on GitHub) assessed
the driver, the Tab5 application, and project documentation. It rated the
driver design, documentation discipline, and evidence-labeling practice as
strong — comparable to production embedded driver contracts — and flagged
five structural gaps. None of these are DSP-correctness bugs; they are
maintainability, portability, and process gaps that get more expensive to fix
the longer they're deferred.

### Gap 1 — `main.cpp` is a single ~266 KB translation unit

DSP (demodulators, AGC, filter state), UI drawing (spectrum, waterfall,
dashboards, controls, touch routing), the radio/band/capture state machine,
and the serial host protocol (SD transfer, journal, auth) all live in one
file. The review's framing: this is the embedded equivalent of collapsing an
entire MVC stack — model, view, controller, and service layer — into one
class. Concretely, it already makes review diffs hard to scope (a touch-
handler change and a DSP change land in the same file with no compiler-
enforced boundary), and it will only get worse as more radio modes/tools are
added (RDS decode is the next one in the queue, per `docs/lora/README.md` and
recent stereo-decoder work).

**Status: Planned.** See `phasing.md` Phase 1.

### Gap 2 — dual USB code paths (`RTL_USE_LEGACY_USB`)

`PROJECT_STATUS.md` already tracks this correctly under P1 ("Remove the
legacy in-app USB path only after a soak build passes") — the review confirms
the *sequencing* is right (soak-test before removal) but flags the *cost* of
carrying both paths in the meantime: every driver-facing change has to be
reasoned about twice.

**Status: Already tracked** — `PROJECT_STATUS.md` P1. No new tracking needed
here beyond linking it into the module-split work (Phase 1), since splitting
the USB path into its own translation unit makes the eventual deletion a
one-file diff instead of a main.cpp-wide search.

### Gap 3 — no CI

There is no `.github/workflows` directory, no automated build of the driver
component, no automated build of `examples/p4_serial_smoke`, and no unit or
integration test suite for the driver. For a clean-room USB driver claiming a
public API contract (`docs/API_RTL_SDR_V4_ESP.md`), an unbuilt example is a
real regression risk — nothing currently prevents a header change from
silently breaking the example that's supposed to prove the contract.

**Status: Planned.** See `phasing.md` Phase 2.

### Gap 4 — open performance gates (already tracked, cross-referenced here)

The review independently re-derived the same P0 gates already open in
`PROJECT_STATUS.md`: graphics+audio simultaneous FPS/drop-rate measurement,
the live-speaker-vs-recorded-PCM discrepancy, and hot-plug/second-board
validation. This is a **confirmation**, not a new finding — it's listed here
only so the review is fully accounted for; the tracked item and its evidence
boundary remain in `PROJECT_STATUS.md` P0/P1 unchanged.

**Status: Already tracked** — `PROJECT_STATUS.md` P0/P1. No duplicate entry.

### Gap 5 — monolithic touch/draw logic; no tool-shell abstraction

Touch hit-testing, button dispatch, spectrum drawing, and per-band dashboard
painting are interwoven inside the same functions per band (e.g.
`handle_cb_touch`, `handle_lora_touch`, `handle_fm_touch`, each hand-rolling
its own hit-test geometry against shared screen constants). The review's
concern: every new tool (Analyzer, Gain Lab, IQ dump, the in-flight RDS
panel) keeps growing the same call sites rather than plugging into a shared
shape. This is the same root cause as Gap 1, scoped down to the touch/draw
surface specifically.

**Status: Planned.** See `phasing.md` Phase 1 (module split) and Phase 3
(tool-shell abstraction, sequenced after the split lands so it has stable
module boundaries to plug into).

## Product initiative — ADS-B 1090 dashboard

The supplied four-panel concept is accepted as the product contract for an
on-device **1090 MHz Mode-S/ADS-B** tool: Radar, Aircraft List, selected
Target, and RF Statistics share one aircraft snapshot and one selected ICAO.
Settings is a fifth control view for receiver coordinates and radar range,
not another data panel. The first implementation is deliberately marked
`DEMO`; it exercises navigation and persistence without claiming live RF.

Peer projects establish useful boundaries:

- [T-Display-P4 ADS-B](https://github.com/jstockdale/T-Display-P4) proves the
  ESP32-P4 + RTL-SDR vertical-app shape and informs product/reliability goals.
- [dump1090](https://github.com/antirez/dump1090) and
  [readsb](https://github.com/wiedehopf/readsb) inform frame validation,
  bounded recently-seen aircraft state, and replay-first decoder checks.
- [tar1090](https://github.com/wiedehopf/tar1090) reinforces the separation
  between decoder state and selectable radar/list/detail views.

These are architecture and behavior references only. OrcSDR does not copy
GPL tuner, decoder, or UI source, and it does not bundle airline logos or
aircraft photos. The 2.048 MS/s path is implemented and hardware-measured. The
clean-room decoder, bounded state table, and live snapshot path are flashed;
the dashboard keeps its `DEMO` badge until a newly received frame passes CRC,
so replay and build evidence cannot be mistaken for current live aircraft.

**Status: Live pipeline flashed; physical acceptance pending.** See `phasing.md`
Phase 6.1 for the shell and Phase 6.2–6.6 for high-rate input, decoding,
enrichment, and hardware acceptance.

## Product initiative — global Settings and connectivity

OrcSDR will provide one on-device Settings application for connectivity,
receiver location, data/maps, display/audio, radio defaults, storage,
optional Companion integration, and system information. A persistent gear
opens it without stopping ordinary reception; disruptive SD/network work must
use an explicit pause/resume confirmation. The existing `orclink` NVS namespace
is retained, including migration of the legacy single Wi-Fi credential into
the first of four bounded profile slots.

Phone, BLE, GPS, map building, and TheOrc/HIVE are not prerequisites. BLE is
shown only after the Tab5 ESP32-C6 SDIO path is proven, and M5Launcher remains
the firmware/partition owner.

**Status: Build-verified foundation; hardware acceptance and service phases
pending.** See `phasing.md` Phase 7.

### U.S. data catalog and SD sync

Settings will expose a user-initiated U.S. data catalog backed by signed GitHub
Release assets, not a permanent OrcSDR data service. Each approved pack carries
a compact runtime index and its source archive, provenance, source date,
license/redistribution review, size, and SHA-256. The initial packs are FAA
aircraft registration, FAA aviation/NASR, NOAA Weather Radio, and FCC FM/AM.
P25 remains a user-owned editable SD profile unless a future source explicitly
permits redistribution; maps remain imported/validated separately.

**Status: Build-verified catalog client and pack-publishing tooling; no public
catalog release or hardware install evidence yet.** See `phasing.md` Phase 7.2.

## Product initiative — automated guide and media

OrcSDR will maintain one versioned screen manifest covering every usable view.
Authenticated serial capture writes exact 1280x720 frames to SD, while a local
Windows pipeline retrieves and hashes them, creates annotated PNGs, builds the
Material for MkDocs guide, and renders captioned Kokoro-narrated videos. Live
data is preferred only when a bounded condition is met; otherwise the capture
is visibly labeled `DEMO`. Generated MP4 files remain outside Git history.

**Status: Build-verified tooling and firmware interface; hardware capture,
privacy review, and voice approval pending.** See `phasing.md` Phase 8.

## Summary table

| Gap | New or already tracked | Phase |
|---|---|---|
| `main.cpp` monolith | New | Phase 1 |
| Dual USB paths | Already tracked (`PROJECT_STATUS.md` P1) | Linked into Phase 1 |
| No CI | New | Phase 2 |
| Open performance gates | Already tracked (`PROJECT_STATUS.md` P0/P1) | Unchanged |
| No tool-shell abstraction | New | Phase 3 |
| ADS-B 1090 dashboard | Product initiative | Phase 6 |
| Global Settings and connectivity | Product initiative | Phase 7 |
| Automated guide and media | Product initiative | Phase 8 |
