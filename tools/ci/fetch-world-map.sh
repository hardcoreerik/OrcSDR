#!/usr/bin/env bash
# Fetches the reviewed Natural Earth 5.1.2 z0-z4 world archive that releases embed at orcmaps_world
# (0x650000), for CI builds. Release builds pass the same file to build-tab5-idf.ps1 -WorldMap
# (ORCSDR_WORLD_MAP) and tools/release finalize-m5burner-maps.py; both pin this SHA-256, as does
# apps/orcsdr-tab5/CMakeLists.txt. The archive is not published on its own, so it is taken from the
# v0.5.0-beta.1 release's local M5Burner zip (firmware/orcmaps_world_0x650000.pmtiles), whose bytes
# are exactly the pinned artifact.
#   tools/ci/fetch-world-map.sh <dir>   -> <dir>/world-overview-z4.pmtiles
set -euo pipefail
dir=${1:?usage: fetch-world-map.sh <dir>}
sha=9aea08772bacf1f024d1da90cc52aa8fcf0b0e37405415dc7c91e75e56596f0f
size=871343
tag=v0.5.0-beta.1
asset=OrcSDR-Tab5-v0.5.0-beta.1-local-m5burner.zip
member=firmware/orcmaps_world_0x650000.pmtiles
repo=${GITHUB_REPOSITORY:-hardcoreerik/OrcSDR}
out="$dir/world-overview-z4.pmtiles"
mkdir -p "$dir"
check() { [ -f "$out" ] && [ "$(stat -c %s "$out")" = "$size" ] && echo "$sha  $out" | sha256sum -c --status; }
if check; then echo "world map cached ($sha)"; exit 0; fi
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
url="https://github.com/$repo/releases/download/$tag/$asset"
curl -fsSL --retry 3 -o "$tmp/$asset" "$url"
python3 -c "import sys,zipfile; open(sys.argv[3],'wb').write(zipfile.ZipFile(sys.argv[1]).read(sys.argv[2]))" "$tmp/$asset" "$member" "$out"
check || { echo "world map from $url does not match SHA-256 $sha" >&2; rm -f "$out"; exit 1; }
[ "$(head -c 8 "$out" | od -An -c | tr -d ' ')" = 'PMTiles003' ] || { echo "world map lacks a PMTiles v3 header" >&2; exit 1; }
echo "world map fetched from $tag ($sha)"
