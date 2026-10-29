# ESP32-P4 board evidence protocol — draft for OrcSDR branch

## Scope and ordering

Board/PCB revision, screen assembly variant and product bundle are separate identities. Document one revision-specific field at a time; no default mappings inherited from other boards. Prefer (1) exact-revision schematic/netlist (2) matching BSP code (3) vendor revision-specific errata (4) official wiki / release notes (5) physical device log. Log both sides of disagreements. A vendor's word “validated” may mean *example supplied*, not a compiled or physically tested example.

## Status vocabulary

- `UNVERIFIED`: no primary evidence for this specific hardware field.
- `DOCUMENTED_IN_SDK`: constant or driver initialization read from pinned SDK; **not a continuity measurement**.
- `VERIFIED_DOCUMENTARY`: supported by official vendor statement/doc for an identified product or revision; verify physical revision match.
- `PREVIOUS_REA_RESULT`: a prior published RSA/REA observation, with source/date; do not double count as new analysis.
- `CONFLICT`: simultaneous non-equivalent facts in code/schematic or revision docs; both retained until test.
- `BUILD_PASSED`: pinned `idf.py build` exited 0 with logged environment and artifacts (NONE in this handoff).
- `VERIFIED_RUNTIME`: actually observed on an identified physical board, with logged hardware and firmware identity (NONE in this handoff).
- `NOT_APPLICABLE`: only after authoritative confirmation that a feature is absent, **not** used for unresearched features.

## Field evidence schema

Each nonempty `fields` entry in `ESP32P4_CATALOG_HANDOFF.json` includes `value`, `status`, `source_url`, `source_file`, `source_ref` (when GitHub), `board_revision`, `evidence_type`, and `observed_on_hardware`. Some facts reference a revision-specific vendor guide synthesizing source+schematic rather than an independently inspected raw `.sch`; those are flagged as such.

## Stage gates

`DISCOVERY_PENDING` → `IDENTIFIED` → `SOURCE_CAPTURED` → `PINMAP_EXTRACTED` → `SDK_BUILD_CHECKED` → `HARDWARE_TESTED` → `PROFILE_READY`. An entry may be at a stage while each feature remains individually unverified; entries in this handoff are not production-ready. For every stage change, log operator, date, material SHA, exact board marking and what was measured. No positive `PROFILE_READY` without powered RTL-SDR host enumeration + sustained I/Q, working touch/panel, C6 Host/CP version match, and clean ESP-IDF 5.5 run.

## Hardware safety and publication

No board flashing, cable rewiring, PoE/USB power injection, battery experimentation or reverse-engineering of proprietary binaries without separate authorization. Third-party sources remain gitignored in RSA targets/source; publish generated metrics through `rsa.py publish`, not hand-written metrics. Closed vendor source requires explicit approval before import.