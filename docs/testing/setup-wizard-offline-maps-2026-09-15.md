# Offline setup wizard and map provisioning — verification record

Date: 2026-09-15
Branch: `claude/setup-wizard-offline-maps`
Base: `codex/v3c-iq-asymmetry-investigation` @ `ae96a5681c519e70fc8c3970a77de76beb30e3fc`

OrcMaps commits used:

- `c195822` `provision_pack.py`
- `381183c` `LocationPicker`

## What problem this addresses

OrcSDR's two existing location paths both require the network:
`ui/location_estimate.cpp` calls Nominatim for an address search or
`ipwho.is` for an approximate fix, and `request()` refuses outright without
`wifi_connected`. A field receiver that cannot establish its own position
offline is a gap, and the offline map it then needs is a second gap: the
current offline map is a single hard-coded file,
`/orcsdr/data/lane_county_map.idx`, from the ORCMAP1 prototype.

## The flow

```
pin on a locally stored map            (device, offline)
  -> wizard validates + persists       (device)
  -> wizard checks installed packs     (device)
  -> wizard prints a provision command  (device screen)
  -> provision_pack.py cuts the pack    (PC, no network needed if the
                                         source pack is already staged)
  -> files copied to <card>/orcmaps/    (PC)
  -> device rediscovers and reports covered
```

## Round trip, executed

The point of this run is that no step was hand-fitted to the next: the
command came out of the wizard, the pack came out of that command, and the
verdict came back from the same wizard code.

**Step 1 — the device's output.** Eugene pin (44.0521, -123.0867), radar
range 25 nm, blank card:

```
covered=0 radius_km=58
python tools/pack-builder/provision_pack.py --source-manifest data/local/oregon.manifest.json \
  --lat 44.0521000 --lon -123.0867000 --radius-km 58 --name home --sd-root <card> \
  --min-zoom 1 --max-zoom 13 --builder-commit $(git rev-parse HEAD)
```

58 km is derived, not chosen: 25 nm × 1.852 km/nm × 1.25 margin = 57.9,
rounded up so the pack cannot stop short of the range ring OrcSDR draws.

**Step 2 — running it** (only `--sd-root` and `--pmtiles-cli` substituted):

| | |
| --- | --- |
| Pack size | 9,553,371 bytes (9.11 MB) |
| Bounds | -123.81811, 43.53108 .. -122.35529, 44.57312 |
| Files staged | 3 (`home.pmtiles`, `home.manifest.json`, `home.sha256`) |

`orcmap_pack_verify` on the resulting directory:

```
INSTALLED home
    manifest: z1-13  bounds -123.81811,43.53108 .. -122.35529,44.57312
    archive:  z1-13  bounds -123.81811,43.53108 .. -122.35529,44.57312
    credit:   © OpenMapTiles  https://openmaptiles.org/
    credit:   © OpenStreetMap contributors  https://www.openstreetmap.org/copyright
RESULT: OK -- all 1 pack(s) would install.
```

Manifest bounds equal archive bounds, which is the check the device applies
before installing a pack. Both required credits are present; only the
OpenStreetMap one was passed in, and the OpenMapTiles schema credit was added
by the builder's own obligation table.

**Step 3 — back to the device.** The pack's real bounds, read from the
generated manifest and converted to 1e-7 degrees, fed to the same wizard:

```
pack bounds e7: lat 435310795..445731205  lon -1238181053..-1223552947  z1-13
covered=1 covering_pack=home recommendation_needed=0
msg=Local map found for this location
```

**Negative controls**, so the check is not trivially true:

| Case | Result |
| --- | --- |
| Portland pin vs the Eugene pack | `covered=0`, recommendation needed |
| Eugene pin vs the same pack capped at z10 | `covered=0`, recommendation needed |

The second is the rule that matters most: geographic containment is not map
coverage. A pack must also reach `kMinDetailZoom` (11), or a world overview
pack — which contains every point on Earth — would report a user's map as
fine while being unable to draw their town.

## Pack sizes by radius

Measured against the 84 MB staged Oregon source, z1–13, go-pmtiles 1.28.2:

| Radius | Tiles | Pack size | Time |
| --- | --- | --- | --- |
| 25 km | 384 | 4.06 MB | 276 ms |
| 50 km | 1,218 | 7.34 MB | 249 ms |
| 58 km (25 nm range) | — | 9.11 MB | 294 ms |
| 100 km | 4,526 | 20.30 MB | 295 ms |

Generation is effectively instant at any of these radii, so pack size — not
build time — is the only budget worth designing around.

## Host tests

| Suite | Result |
| --- | --- |
| `tools/test-setup-wizard.sh` (g++ -O2 -Werror) | 23 test groups pass |
| same, ASan + LSan + UBSan | pass |
| OrcMaps host suite (incl. 16 `LocationPicker` groups) | 163 functions pass |
| OrcMaps Python suite (incl. 16 provisioner tests) | 95 pass |

Run the wizard tests with:

```bash
bash tools/test-setup-wizard.sh
```

## What is NOT verified here

- **No hardware run.** Nothing in this record was flashed to a Tab5. The
  wizard core, the picker and the provisioner are all host-verified only.
- **No on-device UI exists yet.** The wizard core and `LocationPicker` are
  the logic behind a "choose your location" screen; the screen itself, its
  touch handling and its integration into `ui/main.cpp` are not written.
- **OrcSDR does not yet depend on OrcMaps.** `apps/orcsdr-tab5/main/idf_component.yml`
  has no `orcmap` entry, so the OrcMaps side of this is not yet compiled into
  the firmware. Adding it needs the OrcMaps commits above pushed first, since
  the dependency is pinned by `git:` + full SHA (the same pattern as
  `esp_rtl_sdr`).
- **`lane_county_map.idx` is untouched.** Replacing the ORCMAP1 offline map
  with OrcMaps pack discovery is a separate change.
- **The driver pin is untouched.** This branch leaves
  `esp_rtl_sdr` at `e1ca40e` as its base had it, rather than coupling a map
  change to two unrelated RF tuner fixes.
