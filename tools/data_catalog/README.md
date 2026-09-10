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


## Lane County offline map (ORCMAP1)

The Tab5 basemap remains `ORCMAP1` at `/orcsdr/data/lane_county_map.idx`
(640 segments / 32 labels). Rebuild a readable pack from the Overpass JSON
inside `lane_county_map-source.zip`:

```bash
python tools/data_catalog/build_lane_county_map.py \
  --osm lane_county_osm_source.json \
  --out lane_county_map.idx
```

Then refresh the signed `data-catalog-v1` release assets
(`lane_county_map-runtime.idx` + source zip) with the existing catalog
signing flow. Map *colors* are firmware-side (ADS-B / LoRa `draw_base`
palette); regenerating the pack updates geometry/labels only.
