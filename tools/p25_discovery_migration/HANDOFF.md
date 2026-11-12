# P25 v2 / Radio Discovery host migration preview — handoff

**Ownership:** only `tools/p25_discovery_migration/**` was added. No P25/firmware/shared data model code changed.

## Purpose / status

Read the original P25 v2 profile tree and emit a fully auditable, deterministic JSON **proposal** for per-control-frequency `DirectoryRecord` candidates while retaining the original P25-specific configuration in an explicit sidecar. Exclude unknown/invalid/malformed and conflict/ambiguous/over-capacity records from creates. No existing data or journal is written; `plan.can_apply` is always false.

## Commands

```sh
python3 -m unittest discover -s tools/p25_discovery_migration/tests -v
python3 tools/p25_discovery_migration/migration.py tools/p25_discovery_migration/fixtures/sample_sd \
  --directory-capacity 8 --existing-directory-count 1
```

Use `--output` **only** for a NEW JSON path outside the input tree. CLI exit 0 means preview produced, not migration accepted. Windows: `py -3` works with Python 3.10+.

## Critical integration gaps

1. Existing `Library<RF,DIR,NOTE>` cannot persist profile-level `auto_follow`, `encryption_skip`, hold talkgroup, TG aliases, CQPSK gains, or active profile selection. No data is dropped from the **plan**, but it cannot yet be applied losslessly to the shared library.
2. `DirectoryRecord` is a staging destination with `Origin::directory_import`, `Proof::candidate`. It MUST retain local-P25 provenance, never RF truth or third-party provider authority. Persistent `Origin::user_entry` has no library insertion API today.
3. This preview's JSON existing-candidate snapshot is an independent host-only exchange contract; do not call it a live journal reader. Occupied/capacity numbers are operator assertions, not storage measurements.
4. Need a separate, reviewed adapter that revalidates SHA-256, actual destination snapshot counts/IDs, exact protocol and provenance semantics, and implements atomic settings and talkgroup-alias retention with rollback before applying any migration.
5. The parser deliberately rejects version 1, malformed/ambiguous v2, unknown fields and unsafe source paths. Recover backups explicitly before using the importer, never silently fall back to `.bak`.

**Tests:** 38 deterministic Python stdlib unit tests; synthetic only, no ESP32/SD hardware, no production P25 files. See README for full contract and field-level mapping.
