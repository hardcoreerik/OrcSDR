# OrcSDR radio profile import preview — host-only

A **fully offline**, Python 3.10+ standard-library CLI that reads CSV radio-channel lists or a **versioned OrcSDR JSON interchange snapshot**, checks records and previews a selective create-only plan. This is a preparation/review workstream for CFG-01/CFG-03; **there is no firmware integration or application of a plan**. The tool does **not** read the live SD/P25 profile store, write live configurations, call provider APIs, or connect to the network.

## Quick test (Linux, macOS, Windows PowerShell)

From the OrcSDR repository root:

```sh
python3 -m unittest discover -s tools/radio_profile_import/tests -v
python3 tools/radio_profile_import/import_preview.py tools/radio_profile_import/examples/channels.csv
python3 tools/radio_profile_import/import_preview.py tools/radio_profile_import/examples/incoming.json --existing tools/radio_profile_import/examples/baseline.json
```

On Windows, substitute `py -3` for `python3` if required. For machine-readable output:

```sh
python3 tools/radio_profile_import/import_preview.py tools/radio_profile_import/examples/incoming.json \
  --existing tools/radio_profile_import/examples/baseline.json --format json
```

To create an **exclusively new** preview JSON file, add `--output preview.json`. The file **must not already exist**. Use a different destination each run or delete an obsolete preview yourself. The application refuses to write to either input path and offers no mode that writes P25 profiles, user settings or radio libraries.

## CSV (documented portable subset)

An ordinary UTF-8 CSV with a header row. `Frequency`, `Freq`, `RX Frequency`, `Frequency MHz` are **MHz**; `Frequency Hz` and `frequency_hz` are **Hz**. Numeric values may explicitly end in `MHz` or `Hz`. Resolution must be an integral number of hertz. `Mode` / `Protocol` is required (DMR, P25, NXDN, FM, NFM, WFM, AM or OTHER). Unsupported source fields are reported rather than silently mapped. Case-insensitive common headers recognize:

| Radio field | Typical CSV headers | Rules |
|---|---|---|
| `frequency_hz`, `frequency_mhz` | Frequency Hz, Frequency, RX Frequency | One receive frequency, integer Hz canonical form |
| `protocol`, `modulation`, `role` | Mode, Modulation, Role | Role defaults to `unknown`; accepted: `repeater`, `simplex`, `control`, `traffic`, `base`, `mobile`, `unknown` |
| `name`, `region`, `network_id`, `site_id` | Name, Location, Network, Site | Text only; region is a user/directory hint, **not** an RF transmitter fix |
| `color_code`, `slot`, `nac`, `ran` | CC, Time Slot, NAC, RAN | DMR CC 0–15, DMR TS 1–2, P25 NAC 0–4095, NXDN RAN 0–63; decimal integers |
| `talkgroup_id`, `radio_id` | Talkgroup, Radio ID | Positive decimal identifier 1–16,777,215 |
| `tx_frequency_hz`, `notes`, `scan_list`, `skip`, `hold` | TX Frequency Hz, Notes, Scan List, Skip, Hold | TX frequency not evidence of received RF; bool = yes/no, true/false or 1/0 |

For nonstandard headers, map explicitly (the value **must match the original CSV header exactly**):

```sh
python3 tools/radio_profile_import/import_preview.py custom.csv \
  --map frequency_mhz=Dial --map protocol=Digital --map name=FriendlyName \
  --source-name 'My saved CSV' --source-license 'user-owned' \
  --source-retrieved-at 2026-11-11
```

`--source-url` records a user-asserted URL without fetching it. These fields are **not** verified provider authority or RF truth. An invalid CSV cell produces a row-specific error and cannot enter the create plan. A row with extra CSV cells is invalid rather than silently truncating data; unknown named columns are reported as unsupported. Duplicate/ambiguous headers fail the file. This is **not a universal CHIRP/CPS/codeplug importer**; vendor tone, repeater offset, contacts, proprietary IDs and binary profiles require a separately tested adapter before automatic conversion.

## OrcSDR JSON interchange format v1

This is a **new standalone interchange schema**, not existing P25 `profile.cfg` v2, and not an on-device storage schema. JSON input **must** declare the exact version; unknown versions are rejected without guessing or migrating.

```json
{
  "schema": "orcsdr.radio-profiles",
  "version": 1,
  "source": {
    "name": "user-supplied file",
    "url": "https://example.invalid/original-source",
    "license": "user-supplied; check redistribution terms",
    "retrieved_at": "2026-11-11"
  },
  "profiles": [
    {
      "frequency_hz": 145500000,
      "protocol": "DMR",
      "role": "repeater",
      "name": "Sample Site",
      "region": "OR",
      "site_id": "site-1",
      "color_code": 3,
      "slot": 1,
      "talkgroup_id": 91
    }
  ]
}
```

`profiles` is an array; records have the canonical CSV fields above. `source` is optional, but if included it must be an object. Input source metadata is included as `asserted_*` fields, not as independently verified facts. A path basename, source SHA-256, source row/index (and CSV line), and evidence label `directory_candidate` are attached to every proposed addition. Timestamp and source URL are never invented. No local file is interpreted as an on-air observation or proof of RF reception. All file reads require UTF-8 (a UTF-8 BOM is tolerated), and each file is capped at 8 MiB / 50,000 records.

## Deterministic matching and selective planning

Supply `--existing saved-library.json` to compare against a **read-only, versioned JSON snapshot**. An invalid baseline fails the whole operation instead of hiding existing data. Comparison is independent of filename and source row order:

- **`duplicate`**: identical normalized identity **and** full normalized content, even across files or reordered headers. Suppressed from the plan.
- **`conflict`**: identical normalized identity, differing content (e.g., existing personal name versus imported label). Default action `skip`; **never update** the original.
- **`review`**: same carrier/protocol with compatible but incomplete/unknown identity, so overlap cannot be disproven. Default action `skip` rather than guessing.
- **`new`**: no equal or ambiguous identity. Distinct explicitly identified sites on the same RF frequency remain separate.
- **`invalid`**: one or more failing checks; never selectable into a plan.

Identity uses `(protocol, role, receive frequency Hz, known region/network/site/color-code/NAC/RAN/slot/talkgroup/radio ID)`. Unknown information is not treated as a matching value; distinct explicit site identifiers are not collapsed merely because frequencies agree. `tx_frequency_hz`, `name`, `notes`, `scan_list`, `skip`, `hold` and modulation participate in content comparison, not site identity. Matched existing and earlier incoming row numbers are listed in the preview. Existing records are **never merged, overwritten, deleted or renamed**.

Select only desired 1-based input rows, and explicitly opt into preserving conflicting or ambiguous entries **as separate records**:

```sh
python3 tools/radio_profile_import/import_preview.py tools/radio_profile_import/examples/incoming.json \
  --existing tools/radio_profile_import/examples/baseline.json \
  --select 1-3 --on-conflict keep-both --rename '1=Imported alternate (review me)' \
  --format json --output preview-new.json
```

`--on-review keep-both` is similarly explicit. `--rename INDEX=NAME` renames **only the proposed new copy**, not an existing profile. Default selections cover all input rows, but only unambiguous `new` rows are planned. The generated JSON is `orcsdr.radio-import-preview` version 1, containing all row verdicts and the `plan.creates` array; `plan.updates=[]` and `plan.deletes=[]` are **always empty**. There is **no import/apply command**. A future authenticated implementation must revalidate the current destination snapshot, reserve unique destination identifiers and commit atomically with rollback before using this plan; do not feed it directly into the P25 v2 store.

## Error behavior and limitations

- Exit **0**: preview successfully generated, even when individual rows are invalid (see row errors). Exit **2**: invalid file/schema/mapping/baseline/selection or refused output. No partial import can occur.
- Range check defaults to 24–1766 MHz as an *informational receiver hint*, not a statement that every OrcSDR receiver covers it. Override with `--min-hz` and `--max-hz`. Out-of-range rows warn rather than being quietly discarded.
- Only explicitly mapped, supported fields appear in the plan; unknown columns are reported. This intentionally does not silently preserve unvalidated vendor-specific semantics or validate provider licensing.
- The tool performs no runtime firmware integration, P25 v2 profile conversion, network access, device testing, RF decoder verification, SD atomic writes, backups, or rollbacks. Those are separate integration gates.

Run `python3 tools/radio_profile_import/import_preview.py --help` for all options.