# OrcSDR data catalog

The Tab5 checks a signed `catalog-v1.json` from the `data-catalog-v1` GitHub
Release only when the user chooses **Settings → Data & Maps → Check for
Updates**. It never sends Wi-Fi credentials, receiver coordinates, private
labels, or live traffic data to the catalog.

Each pack has a compact runtime index and the corresponding unmodified source
archive. Both files are streamed to `*.part`, SHA-256 and format-checked, then
activated together with on-SD `.bak` rollback copies. A failed update leaves the
previous complete pack in place. The five original pack indexes remain stable.
The manifest may also contain bounded `p25_...` packages. Their runtime file
must validate as a P25 profile and install only at the matching
`/orcsdr/p25/<pack-id>/profile.cfg` path.

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

A P25 package represents one verified system. It needs a title, version,
source date, source URL, redistribution statement, a version-2 profile, and a
preserved ZIP source archive. The builder rejects other dynamic IDs or a P25
profile whose destination does not match its ID. Do not publish inferred or
third-party talkgroup labels without documented permission.

For NASR, NOAA, and FCC packs, normalize only reviewed columns into the common
runtime format. For example:

```powershell
python .\tools\data_catalog\build_record_index.py `
  --pack faa_aviation --input .\reviewed-atc.csv --out .\faa_aviation.idx `
  --field airport_id --field frequency_mhz --field service --field location
```

The builder preserves source-row order, writes no hidden metadata, and does not
download a source or decide whether it may be redistributed. The original ZIP
remains the catalog archive; this record file is the device runtime subset.

## Supported first-release pack IDs

| ID | Runtime data | Source archive | Refresh |
| --- | --- | --- | --- |
| `faa_aircraft` | ICAO/registration index | FAA Releasable Aircraft ZIP | daily source |
| `faa_aviation` | airport/ATC/frequency index | FAA NASR ZIP | 28-day AIRAC |
| `noaa_weather` | NWR transmitter/SAME index | source capture | source-dependent |
| `fcc_broadcast` | FM/AM station index | FCC LMS dump | source-dependent |

Additional signed IDs beginning with `p25_` are permitted after the source and
redistribution gate above. None are bundled by this change.

## Map packs (OrcMaps)

Map packs are OrcMaps PMTiles packs, published by OrcMaps as its own GitHub release assets (for example the
`world-overview-1` release). The signed catalog does not copy them; it pins each one by HTTPS URL, size and SHA-256, so
the device downloads straight from the OrcMaps release and refuses anything that differs. A map pack is two files in
the flat `/orcmaps` folder of the SD card, which is the folder the map engine scans:

| ID | Files | Notes |
| --- | --- | --- |
| `orcmaps_<name>` (under 20 characters) | `/orcmaps/<id>.pmtiles` and `/orcmaps/<id>.manifest.json` | `runtime` is the PMTiles archive (must start with `PMTiles`), `archive` is its manifest (a JSON object, at most 64 KB) |

A catalog input entry names the local copies (used to compute the size and hash), the release URLs (`runtime_url`,
`archive_url`), a `title`, and the two fixed destinations; `build_catalog.py` rejects anything else. See the
`orcmaps_world_z7` entry in `catalog-input.example.json`. Settings > Data & Maps lists what the catalog offers, with
DOWNLOAD / REMOVE, and also lists every valid pack found on the card (copied by hand or downloaded) with a RESCAN
button. The Lane County prototype map is no longer listed there.

Maps for ADS-B and LoRa follow the receiver location; HF schedules and LoRa regional profiles require a separate
rights and format review.
