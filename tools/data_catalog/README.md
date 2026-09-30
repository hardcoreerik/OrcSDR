# Data-catalog staging

Runtime radio databases are generated from archived/reviewed source snapshots;
the source dumps themselves are not committed to the firmware tree.

## Aviation

The preferred runtime file is `/orcsdr/data/aviation.idx` using `ORCAIR2`.
The catalog-v1 pack slot is named `aviation`; firmware also accepts the legacy
manifest id `faa_aviation` and legacy runtime path `/orcsdr/data/faa_aviation.idx`.

### Worldwide baseline — OurAirports

OurAirports publishes worldwide airport and airport-frequency CSV exports as
Public Domain. Download `airports.csv` and `airport-frequencies.csv`, preserve
the exact source snapshot used for a release, then normalize it with:

```bash
python tools/data_catalog/build_ourairports_aviation_index.py \
  --airports airports.csv \
  --frequencies airport-frequencies.csv \
  --country SG \
  --output aviation.idx
```

`--country` and `--region` are repeatable. `--center-lat`, `--center-lon`, and
`--radius-nm` can build a location-bounded pack. OurAirports output is tagged
`COMMUNITY` even though its redistribution license is Public Domain, because
provenance class describes authority rather than copyright status.

### Official enrichment

National AIP/eAIP or other authority feeds should normalize to the same
`ORCAIR2` fields and use source class `OFFICIAL` when redistribution terms
permit. Licensed feeds use `LICENSED`. Keep source URL, effective/source date,
and redistribution terms in the release catalog/archive. Do not scrape and
redistribute an AIP merely because it is publicly viewable; country-specific
terms still apply.

### Legacy FAA pack

`faa_aviation_atc_lane_county.csv` and `build_faa_aviation_index.py` remain for
backward-compatible `ORCCAT1` generation. Before an FAA release, review rows
against current FAA publications and archive the source used for that release.

The initial Eugene rows were reviewed against the FAA Northwest Chart
Supplement and terminal procedures. Existing `ORCCAT1` packs continue to load,
but new international work should target `ORCAIR2`.

## Provenance rule

The runtime database is contextual metadata, not RF-decoded identity. OrcSDR
may display a nearby database association alongside measured frequency/signal,
but it must not claim that a transmission belongs to an airport/controller or
aircraft solely because the frequency matches a database record.
