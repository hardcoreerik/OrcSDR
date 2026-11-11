# DMR and Digital Voice — product vision

**Status:** proposed / planned; this document is a product direction, not a claim of implemented DMR or NXDN decoding.
**Established:** 2026-11-11. **Scope:** DMR first, shared discovery and configuration experience for P25 Phase I/II, DMR and NXDN.
**Current capability authority:** [PROJECT_STATUS.md](../../PROJECT_STATUS.md). **Execution tracker:** [TRACKING.md](TRACKING.md).

## Product promise

> OrcSDR should not require users to know what is on the air before they can discover it.

OrcSDR should be as useful for exploring an unfamiliar radio environment as it is for monitoring a familiar one. A user sets a location (or leaves it unset), chooses a search scope, and OrcSDR discovers actual activity, describes what it can prove, and remembers what it learned—**without an internet requirement**. Curated directory data is an optional accelerator, never the source of an invented reception result.

## Four system-wide requirements

1. **Discover first, configure second.** Search selected frequencies and classify DMR, P25 and NXDN when the corresponding host- and RF-validated decoder supports it. Learn frequency, DMR color code and time slot, P25 NAC/system/site/talkgroup, NXDN RAN, radio IDs, and other identity fields *only* from successfully decoded signaling. Permit discovery with no imported profile.
2. **Offline by default.** Signal discovery, decoding, saved profiles, receiver observations, logbook and installed directory lookups must work without Wi-Fi, subscription, account, cloud API or telemetry upload. RepeaterBook, RadioReference, RadioID and network-directory services are **optional** enrichment if their terms permit the contemplated operation.
3. **A growing local radio database.** Persist the user-approved discoveries with frequencies, measurements, timestamps, configured receiver location, protocol identifiers and original evidence. Provide editable user names/notes alongside immutable observation provenance. Make export, backup, restore and offline transfer first-class workflows.
4. **Evidence over assumptions.** Distinguish *imported/listed candidate*, *energy/signature observed*, *valid protocol lock*, *validated decoded system*, *user annotation* and *matched directory hypothesis*. Never call a guessed transmitter location confirmed, translate a license into actual RF activity, or label network-routed callers as long-distance RF reception.

### DX credibility and honesty

- Weak-signal screens show relative power (not calibrated dBm without calibration), measured frequency offset, sync confidence, valid CRC/FEC results, slot activity, lock/relock counters and IQ discontinuities.
- A channel may have two DMR time slots but still occupies **one RF carrier**. One receiver cannot promise simultaneous monitoring of widely separated frequencies or control and traffic channels outside available sample bandwidth.
- Distinguish the **locally received repeater/site** from a remotely routed DMR network caller; do not imply the caller's reported geography proves an RF DX path.
- Distinguish a candidate repeater matching frequency/area from a confirmed site identity; maintain confidence and corroborating evidence instead of silently auto-confirming matches.
- Receive-only; encrypted traffic is labeled as encrypted/unknown and not decrypted or represented as intelligible audio.
- No decoder performance advantage is claimed until a common IQ corpus and equal test conditions demonstrate it.

## Experience: DISCOVER → IDENTIFY → SAVE → ENRICH

**Discover:** Select a location and optional radius, manual frequency, local-band scan plan or preloaded candidate list. With no database pack, scan only user-approved, supported ranges; live IQ remains the evidence. The user sees scan progress, dwell/settle, signal status and receiver limitations.

**Identify:** Detect energy first, then protocol-like signatures, then verified frames. For DMR: mode (base/mobile/direct), color code, correct TS1/TS2 activity, source and destination talkgroup/radio IDs, privacy state and error metrics as available. For P25 and NXDN, show their protocol-specific identifiers without conflating them with DMR fields.

**Save:** One-touch save of a validated channel and independent user alias. First/last heard, observation count, measurement summary, location *at time of reception*, evidence level and source/receiver provenance persist locally. Allow explicitly saving an unconfirmed candidate with a clearly lower trust level.

**Enrich:** Optionally match against an installed, attribution-bearing local directory; download/import additional packs only on user request. Keep imported aliases, operator observations and user edits separate. Offline lookup continues after installation.

## Bring Your Radio Setup — simple migration and provider import

A user with years of repeater memories, a codeplug, a spreadsheet or a RadioReference subscription should not have to recreate their setup on the touchscreen.

**Offline-first file import (first delivery path):**

- Import/export OrcSDR P25 v2 profiles, portable CSV and a documented OrcSDR JSON schema through SD card or an authenticated local web transfer. Provide a clear downloadable template and human-readable errors with row numbers.
- Build explicit mappers for known, documented file shapes such as common CHIRP CSV exports where feasible; do not claim universal support for vendor CPS codeplugs, encrypted/proprietary formats, or binary backups.
- Support channels/frequencies, modes, repeater location, color code/NAC/RAN, TS, TG IDs, source/radio IDs, friendly labels, scan lists, skip/hold preferences, and provenance when the source contains them. Preserve raw original files as optional user-owned backups, not as opaque trusted database state.
- **Preview before import**: counts, parse errors, duplicates, unsupported rows, tuner-range warnings, conflicts and the exact resulting objects. Allow select/skip/rename/merge/keep-both. No automatic destructive overwrite; save atomically with rollback and a resumable error report.
- Deduplicate by canonicalized **content/identity**, not by filename. Frequency alone is insufficient: two systems/sites can legitimately share a channel. Compare mode, role, frequency, site/network identity and region as available. Preserve distinct transmitter candidates and user annotations.
- Provide export/backup with schema version and source metadata, and a safe upgrade path from current P25 profiles (16 profiles, eight control channels and eight talkgroup aliases per profile in v2). Expand database capacity in a backward-compatible migration rather than silently truncating data.

**Optional connected providers (future, per-provider approval):**

- Investigate RepeaterBook for amateur DMR/P25/NXDN repeater candidates; RadioID for ID/callsign/talkgroup data; RadioReference for public-safety trunked-site/talkgroup listings; BrandMeister for optional network context; FCC ULS for **licensed candidate** frequencies/site records.
- Use official documented APIs or user-provided, authorized exports; do not scrape around access restrictions. Check each provider's commercial use, offline caching, redistribution, subscription, rate-limit, attribution and account-consent conditions before shipping an adapter or bundled database.
- Where account integration is supported, the user explicitly connects it through a secure, least-privilege flow. Never request passwords for direct device storage or upload accounts/contacts to OrcSDR's infrastructure by default. Provide disconnect, cache-expiry, and deletion controls.
- An import is a **snapshot**, not automatic syncing; show the import date and data freshness. Connected synchronization is opt-in and cannot be required for offline functionality. Network-routed talkgroup availability may change independently of the saved pack.
- Never mix imported source labels with RF-proven IDs: UI must distinguish `directory-listed`, `observed`, `decoded`, and `user-labeled` records.

## Shared architecture, independent protocols

- Keep standalone, hardware-independent protocol cores for P25 Phase I, Phase II, DMR and NXDN. Reuse DSP primitives, bounded interfaces, receiver/session ownership, measured RF statistics, typed events and local storage **only where unit and regression tests prove compatibility**.
- DMR targets 4FSK Tier II conventional/base and direct mode, dual-slot burst framing, CACH, FEC and LC/CSBK metadata first; AMBE+2 clear voice requires its own technical/licensing gate.
- NXDN48 and NXDN96 use related 4FSK primitives but independent framing (including RAN/LICH/SACCH). P25 Phase II is not DMR and must retain its distinct TDMA and modulation path. Preserve working Phase I P25 behavior.
- Shared storage: `observation`, `verified_decoder_event`, `directory_record`, `user_annotation`, `site_match`, and `receiver_location` have separate identities and clear schemas. A typed `RadioEvent` can power native UI, local web views, optional exports and SD-backed history.
- Receiver coordinates are **user-set location context**, not automatic proof of transmitter coordinates. Do not silently change the user's saved location or broadcast it to providers.
- Reuse OrcSDR's existing P25 SD profiles, radio-session/scan engine, signed data catalog, map projection, receiver location and storage safety guarantees; avoid growing `main.cpp` and retain stable persisted dashboard identifiers.

## External evidence and foundation

- DMR RSA/REA research (private, unmerged):
  [`research/dmr-competitor-20261111`](https://github.com/hardcoreerik/REA-Results/tree/research/dmr-competitor-20261111).
  Includes the five-application analysis, constants comparison, source and REA metrics. **It did not run decoders or demonstrate RF superiority.**
- Standards baseline: ETSI TS 102 361-1 (DMR air interface), TS 102 361-2 (voice), TS 102 361-3 (data), TS 102 361-4 (trunking). P25/NXDN have separate protocol specifications and validations.
- Existing project documentation: [radio profiles](../RADIO_CONFIGURATION.md), [data catalog](../DATA_CATALOG.md), [source eligibility ledger](../DATA_SOURCE_LEDGER.md), [P25 work phase](../P25_PHASE2_WORKPHASE.md), and [native decoder inventory](../NATIVE_DECODER_INVENTORY.md).
- No provider adapter, regional DMR/NXDN directory, unified digital-voice auto-discovery, or native DMR/NXDN receiver is claimed present by creating this document.

Follow the [DMR development tracker](TRACKING.md) for stage gates, ownership and measurable acceptance.
