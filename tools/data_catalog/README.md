# Data-catalog staging

`faa_aviation_atc_lane_county.csv` is release-source input, not a device pack.
Before every catalog release, review each row against the current FAA Chart
Supplement or terminal procedure, regenerate `faa_aviation.idx` with
`build_faa_aviation_index.py`, and archive the FAA source with that release.

The initial Eugene rows were reviewed against the FAA Eugene One Departure,
effective 11 June through 9 July 2026:
<https://aeronav.faa.gov/d-tpp/2606/00140EUGENE.PDF>.
