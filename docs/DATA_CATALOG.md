# OrcSDR data catalog

The Tab5 checks a signed `catalog-v1.json` from the `data-catalog-v1` GitHub
Release only when the user chooses **Settings → Data & Maps → Check for
Updates**. It never sends Wi-Fi credentials, receiver coordinates, private
labels, or live traffic data to the catalog.

Each pack has a compact runtime index and the corresponding unmodified source
archive. It is streamed to `*.part`, SHA-256 verified, checked for readability,
then activated with an on-SD `.bak` rollback copy. A failed update leaves the
active pack in place. P25 configuration is deliberately outside this mechanism:
`/orcsdr/P25.cfg` is user-owned and is never created, replaced, or removed by a
catalog operation.

## Publishing a catalog

Keep the P-256 signing key outside the repository. On this development machine
the initial key is deliberately stored under `F:\Ai\temp`; move it to an
offline secrets store before publishing anything. Firmware embeds only
`apps/orcsdr-tab5/main/catalog_public_key.pem`.

Prepare normalized runtime files plus preserved source archives, update a copy
of `tools/data_catalog/catalog-input.example.json`, then run:

```powershell
python .\tools\data_catalog\build_catalog.py .\my-catalog-input.json `
  --out .\artifacts\data-catalog-v1 `
  --private-key F:\secure\orcsdr-catalog-signing-key.pem `
  --verify-public-key .\apps\orcsdr-tab5\main\catalog_public_key.pem `
  --release-base https://github.com/hardcoreerik/OrcSDR/releases/download/data-catalog-v1 `
  --openssl "C:\Program Files\Git\usr\bin\openssl.exe"
```

The asset URL prefix is included before signing, so GitHub Release upload cannot
alter the signed manifest. Do not publish a pack until its source-rights entry
is complete.

## Supported first-release pack IDs

| ID | Runtime data | Source archive | Refresh |
| --- | --- | --- | --- |
| `faa_aircraft` | ICAO/registration index | FAA Releasable Aircraft ZIP | daily source |
| `faa_aviation` | airport/ATC/frequency index | FAA NASR ZIP | 28-day AIRAC |
| `noaa_weather` | NWR transmitter/SAME index | source capture | source-dependent |
| `fcc_broadcast` | FM/AM station index | FCC LMS dump | source-dependent |

Maps are imported and validated separately. HF schedules, LoRa regional
profiles, and any P25 directory require a separate rights and format review.
