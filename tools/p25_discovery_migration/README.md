# P25 v2 → Radio Discovery migration **preview** (host-only, read-only)

This Python 3.10+ standard-library tool **does not migrate, write, or repair** existing OrcSDR P25 profiles or the shared Radio Discovery journal. It reads local `profile.cfg` **version 2** files and generates a deterministic, self-describing JSON **review plan**. All implementation, tests and fixtures live under `tools/p25_discovery_migration/`.

This is **not** the generic `tools/radio_profile_import/` CSV/JSON importer and not an RF decoder. It is a compatibility analysis specifically for the existing P25 v2 profile store. Nothing in this directory is linked into firmware.

## Inputs and limits (read-only)

- A local SD-card root with `/orcsdr/p25/<profile-id>/profile.cfg` (up to **16** active profiles in P25 v2) and optional `/orcsdr/p25/active.txt`.
- The historical `/orcsdr/P25.cfg` is included as a separately tagged source even if it coexists with the newer store; legacy **v1** contents are rejected as unsupported until a specific adapter is written.
- A *single* `profile.cfg` file may be passed for inspection. Backup-only `profile.cfg.bak` and `.part` files are **not** silently promoted: the preview identifies backups requiring an operator decision. A single-file path cannot be used to infer an active profile.
- P25 v2 supports at most eight unique `control_channel_hz` values per profile, eight unique `talkgroup=ID,alias` pairs, a 47-byte system name, and 31-byte talkgroup aliases. The preview records over-capacity input and **blocks** unsafe proposals, rather than dropping entries.
- File read limit: 64 KiB per profile, 256 directory entries hard scan ceiling, UTF-8, 127-byte source lines. Unsupported keys and repeated scalars are retained as `raw_entries`/`unsupported_fields` and flagged as blocking. Invalid values, duplicate channel/TG IDs, and invalid line lengths are surfaced, not repaired.
- A file SHA-256, relative path, byte size, ordered original `key=value` entries, explicitly present fields, and the active-profile marker preserve provenance. Actual comments/formatting remain in the original source, which is never modified. The plan is **not** a byte-for-byte reverse exporter of the original profile.

### Quick start from OrcSDR repository root

```sh
# Deterministic host tests (no hardware, network, or external dependencies)
python3 -m unittest discover -s tools/p25_discovery_migration/tests -v

# Inspect the committed synthetic sample, stdout JSON; capacity unknown -> zero creates
python3 tools/p25_discovery_migration/migration.py \
  tools/p25_discovery_migration/fixtures/sample_sd

# Operator-asserted directory capacity/occupied count, then export to a NEW file
python3 tools/p25_discovery_migration/migration.py \
  tools/p25_discovery_migration/fixtures/sample_sd \
  --directory-capacity 32 --existing-directory-count 0 \
  --output /tmp/p25-migration-preview.json
```

On Windows use `py -3` instead of `python3` and a normal absolute output path outside the SD-card tree, e.g. `C:\Temp\p25-migration-preview.json`. The output file **must not exist**. The tool creates it exclusively, does not follow the final symlink, and refuses destinations inside the input tree (including through a symlinked parent). Exit status **0** means a preview was produced, **not** that every record is safe or that migration is possible. Exit **2** means the scan/plan/export was refused. Review `summary`, `discovery_issues`, every `profiles[].errors`, and `records[].status` before using any output.

For an optional **externally exported** set of candidate records (a specially versioned JSON snapshot, *not* the binary `ORAD` journal):

```sh
python3 tools/p25_discovery_migration/migration.py \
  tools/p25_discovery_migration/fixtures/sample_sd \
  --existing-candidates tools/p25_discovery_migration/fixtures/existing_candidates.json \
  --directory-capacity 32 --existing-directory-count 1
```

`--existing-candidates` expects exactly `{"schema":"orcsdr.radio-discovery-candidates","version":1,"records":[...]}`; each record has `protocol`, `frequency_hz`, `identity`, `label`, `source_sha256`. `identity.known_fields` is the **numeric** bitmask used by `evidence.hpp`, and optional `access_code`, `system_id`, `site_id` values must match set flags. This exchange shape is an **input-only host preview contract authored here**, not an existing public firmware export API. A snapshot may be incomplete: `snapshot_completeness_unverified` is always true. The asserted `--existing-directory-count` must be at least the number of supplied rows and no greater than `--directory-capacity`; neither number is measured against an actual running journal.

## Mapping into Radio Discovery F0b

References are **read-only**: `apps/orcsdr-tab5/ui/p25_config.hpp/.cpp`, `components/orcsdr_radio_discovery/include/orcsdr_radio_discovery/evidence.hpp` and `library.hpp` (branch `feature/dmr-digital-voice-foundation` at implementation time).

| P25 v2 source | `DirectoryRecord` candidate projection | Rules |
|---|---|---|
| Each `control_channel_hz` | One `Observation.frequency_hz` | Exact Hz, no rounding; multiple channels become multiple candidates |
| P25 profile | `Protocol::p25_phase1` | Only the currently configured P25 v2 receiver profile is in scope; **not** evidence of P25 Phase II or actual reception |
| Profile file origin | `Origin::directory_import`, `Proof::candidate` | **Import staging** from a local P25 user profile, *not* a trusted external directory or RF observation; local provenance is explicitly labeled |
| Explicit `nac` (including 0) | `access_code_known` mask **1** + `access_code` | An absent NAC cannot be inferred from zero initialized storage |
| Explicit `wacn` **and** `system_id` | `system_known` mask **4** + `(wacn << 12) \| system_id` | Reversible 20+12-bit composite; partial pairs retained only in sidecar |
| Explicit `rfss` **and** `site` | `site_known` mask **8** + `(rfss << 8) \| site` | Reversible 8+8-bit composite; no invented site identity if partial |
| `system_name` | `DirectoryRecord.label` | 47 UTF-8 byte limit; no truncation |
| Original profile bytes | `Source.name="Local P25 v2 profile"`, SHA-256 | `Source.sha256` is represented as a 64-char lowercase HEX string in JSON; any future adapter must decode it into 32 bytes. Empty URL/license are explicitly *unknown*, not invented. Relative source path is separately retained. |

The emitted candidate properties are sufficient to populate the bounded `Observation` and `Source` value objects with default-initialized unspecified fields. They **are not** a serialized `ORAD` snapshot or executable `Library::import_candidate` calls. Before importing, an integration adapter must revalidate the current store, capacity, global IDs, evidence types, and source hashes. The `Library` directory collection is the *only* current collection that can persist user-supplied candidates without mislabeling them as measured RF; hence this **staging-only** mapping. It does not claim the original P25 profile was a provider-supplied directory entry. `Library` does not currently offer a native persistent `Origin::user_entry` record API.

### Fields that the current shared library **cannot** represent faithfully

The plan's `profiles[].sidecar` (also `plan.sidecars`) preserves, without truncation:

- Original ID, name, active-profile selection, all control channel frequencies, selected last channel;
- WACN, SYSID, NAC, RFSS, site as original separate P25 values, even when identity incomplete;
- Every configured `talkgroup=ID,alias` pair (including order), plus `hold_talkgroup`;
- `auto_follow`, `encryption_skip`, modulation mode, `cqpsk_timing_gain`, `cqpsk_carrier_gain`;
- Explicit source-field presence, original `key=value` lines, unknown fields, source file path and SHA-256 (the original raw entries are under each parent `profile`).

There is **no target-native scan policy, hold-talkgroup selector, alias-by-talkgroup table, tuning-gains table or active-profile pointer** in `Library<RF,DIR,NOTE>` yet. `Annotation` is an alias **for a saved RF/directory record**, not a talkgroup-ID keyed contact table; fabricating per-talkgroup annotations would be wrong. No such operations are proposed. The plan therefore always sets `plan.requires_manual_adapter=true`, `plan.can_apply=false`, and `summary.not_ready_to_apply=true`, even if all directory candidates say `new`.

## Deterministic matching and capacity behavior

- `new`: disjoint carrier/protocol/known identity and a proven operator-supplied capacity slot. Proposed **create only**, never applied.
- `duplicate`: same carrier, same numeric known fields/values and identical full P25 semantic configuration (or an identical matching existing source SHA plus label). The second is suppressed. Filenames are not part of duplicate identity.
- `conflict`: same fully specified discovery identity but different profile contents/labels/provenance. Both conflicting incoming rows are blocked (or the incoming row is blocked against an existing candidate).
- `ambiguous`: same frequency/protocol and compatible overlapping *partially known* identities; neither incoming row is silently selected. A known differing NAC, system composite or site composite proves two records distinct on the same frequency.
- `blocked_source`: invalid P25 v2 data, unsupported keys, exceeded v2 input capacity or case-folded profile ID collisions; no candidate is generated for import, but source entries and issues are retained for review.
- `blocked_capacity`: directory create count would exceed asserted `Library<RF,DIR,NOTE>::DIR` remaining capacity, with records still reported.
- `capacity_unverified`: no explicit `--directory-capacity` plus `--existing-directory-count`; **zero creates**, even for otherwise valid input.

The tool estimates **directory candidate** slots only. It does not allocate RF records or annotations and cannot validate the live journal's generation, `next_id` exhaustion, memory budget, existing source policy, concurrent edits, P25 runtime behavior, or unprovided baseline records. All result arrays and the `plan_sha256` are deterministic for the same bytes and declared capacity/baseline. The preview never writes to `/orcsdr/p25`, `P25.cfg`, `active.txt`, `.bak`, `.part`, firmware, or Discovery storage.

## Security and validation boundary

- No network, external packages, shell execution, firmware import, SD mutation, persistent library writer, or automatic conflict resolution.
- Symlinks are rejected for input profile files/roots; no recursive scanning, no hidden filename fallback, bounded reads and strict schema/version parsing. Local host trust and concurrent filesystem mutation are **not** a security boundary; verify source hashes again in the eventual import process.
- All frequencies and identities are *declared configuration*, never elevated to proof of RF reception or site confirmation.
- No claim of on-device or actual-user-profile compatibility: all included fixtures are synthetic. Real-world source data must be inspected with the same tools and separately signed off.
- Future integration should introduce a versioned P25-settings/alias sidecar owner with explicit revalidation and atomic rollback before any migration. This directory deliberately **does not implement that**.
