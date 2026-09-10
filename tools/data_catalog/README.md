# Data-catalog staging

`faa_aviation_atc_lane_county.csv` is release-source input, not a device pack.
Before every catalog release, review each row against the current FAA Chart
Supplement or terminal procedure, regenerate `faa_aviation.idx` with
`build_faa_aviation_index.py`, and archive the FAA source with that release.

The initial Eugene rows were reviewed against the current FAA Northwest Chart
Supplement edition, effective 9 July through 3 September 2026, and the Eugene
One Departure in terminal-procedure cycle 2607. The tower frequencies are
118.900 MHz for Runway 16R/34L and 124.150 MHz for Runway 16L/34R:
<https://aeronav.faa.gov/Upload_313-d/supplements/CS_NW_20260709.pdf>
<https://aeronav.faa.gov/d-tpp/2607/00140EUGENE.PDF>.


## World coastlines (embedded ORCMAP1)

Firmware embeds `apps/orcsdr-tab5/main/world_coastlines.idx` as the default
basemap (Natural Earth 110m coastlines, 640/32 caps). Regional detail packs
install to `/orcsdr/data/regional_map.idx` and override the embed when present.
The legacy `/orcsdr/data/lane_county_map.idx` path remains a load fallback.

Rebuild the embedded pack:

```bash
python tools/data_catalog/build_world_coastlines_map.py \
  --geojson ne_110m_coastline.geojson \
  --out apps/orcsdr-tab5/main/world_coastlines.idx
```

Lane County (and future regions) stay signed catalog assets. Match Home
lat/lon to a pack via the `map_region_packs` nearest-hint scaffold, then
install one ~30 KB ORCMAP1 pack — do not ship Lane County as the global default.


The committed embed may be a longest-first subset of Natural Earth 110m
within the flash/tooling budget; rebuild with `--segment-cap 640` for the full pack.
