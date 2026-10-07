#!/usr/bin/env bash
# Linux/CI port of tools/release/build-hosted-c6.ps1: builds the pinned ESP-Hosted C6 image
# that the Tab5 P4 firmware embeds. Never flashes anything.
#   tools/ci/build-hosted-c6.sh [--patch-idf] <output-dir> [<esp-hosted-source-dir>]
# --patch-idf: run ESP-Hosted's `eh.py patch-idf` on $IDF_PATH first (lifts the 4092-byte SDIO
#   send cap in sdio_slave.c that SW aggregation needs). The released v0.3.0-beta.2 C6 image was
#   built with a patched IDF (its strings contain "len <= 0", not "(0, 4092]"). CI's IDF is a
#   throw-away container, so CI passes this; a developer's IDF is never patched implicitly.
# Needs ESP-IDF 5.5.4 exported (espressif/idf:v5.5.4 container, or . $IDF_PATH/export.sh).
# Writes <output-dir>/esp_hosted_tab5_c6.bin and c6-provenance.json (same fields as the .ps1).
set -euo pipefail
patch_idf=0
if [ "${1:-}" = --patch-idf ]; then patch_idf=1; shift; fi
out=${1:?usage: build-hosted-c6.sh <output-dir> [<source-dir>]}
repo=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
lock="$repo/tools/release/hosted-c6-release.json"
src=${2:-"${RUNNER_TEMP:-/tmp}/OrcSDR-esp-hosted-$(jq -r .hosted_version "$lock")"}
idf.py --version | grep -q 'v5\.5\.4' || { echo "ESP-IDF 5.5.4 is required (got: $(idf.py --version))" >&2; exit 2; }

rev=$(jq -r .source_revision "$lock")
project="$repo/$(jq -r .source_project "$lock")"
# Same local-source fingerprint as the .ps1 (hash of "name=sha256" lines, Windows-style names).
local_sha=$(cd "$project" && for f in CMakeLists.txt main/CMakeLists.txt main/main.c sdkconfig.defaults partitions_eh_cp_ota_4m.csv; do
  printf '%s=%s\n' "${f//\//\\}" "$(sha256sum "$f" | cut -d' ' -f1)"; done | head -c -1 | sha256sum | cut -d' ' -f1)

if [ ! -d "$src/.git" ]; then
  git clone -q "$(jq -r .source_repository "$lock")" "$src"
fi
[ -z "$(git -C "$src" status --porcelain --untracked-files=no)" ] || { echo "pinned ESP-Hosted source has tracked changes" >&2; exit 1; }
if [ "$(git -C "$src" rev-parse HEAD)" != "$rev" ]; then
  git -C "$src" fetch -q origin "$rev"
  git -C "$src" checkout -q --detach "$rev"
fi
[ "$(git -C "$src" rev-parse HEAD)" = "$rev" ] || { echo "ESP-Hosted revision mismatch" >&2; exit 1; }
git -C "$src" submodule update -q --init --recursive

if [ "$patch_idf" = 1 ]; then
  python "$src/tools/eh.py" patch-idf --idf-path "$IDF_PATH"
fi

comp_root="$src/.orcsdr-components"
mkdir -p "$comp_root"
if [ -L "$comp_root/esp_hosted" ]; then
  [ "$(readlink -f "$comp_root/esp_hosted")" = "$(readlink -f "$src")" ] || { echo "refusing to replace $comp_root/esp_hosted" >&2; exit 1; }
else
  ln -s "$src" "$comp_root/esp_hosted"      # the .ps1 uses an NTFS junction here
fi

(cd "$project"
 idf.py -B build -D "EXTRA_COMPONENT_DIRS=$comp_root" set-target esp32c6
 idf.py -B build -D "EXTRA_COMPONENT_DIRS=$comp_root" build)
mkdir -p "$out"
name=$(jq -r .output_name "$lock")
cp "$project/build/orcdial_hosted_c6.bin" "$out/$name"
# Same SDIO behaviour as the shipped C6 image: the send-cap guard must be the patched one.
if grep -a -q -F '(0, 4092]' "$out/$name" || ! grep -a -q -F 'len <= 0' "$out/$name"; then
  echo "C6 image was built against an ESP-IDF without the SDIO send-cap patch (use --patch-idf)" >&2; exit 1
fi
sha=$(sha256sum "$out/$name" | cut -d' ' -f1)
jq -n --slurpfile l "$lock" --arg local "$local_sha" --arg tc "$(riscv32-esp-elf-gcc --version | head -1)" \
      --arg sdk "$(sha256sum "$project/sdkconfig" | cut -d' ' -f1)" --arg fw "$name" \
      --argjson bytes "$(stat -c %s "$out/$name")" --arg sha "$sha" '
  $l[0] as $l | {hosted_version: $l.hosted_version, source_repository: $l.source_repository,
   source_revision: $l.source_revision, source_project: $l.source_project, local_source_sha256: $local,
   target: $l.target, transport: $l.transport,
   board_configuration: "M5Stack Tab5 internal ESP32-C6; P4 host uses ESP32P4_TAB5_C6_BOARD and qualified 4-bit SDIO at 10 MHz",
   idf_version: $l.idf_version, toolchain: $tc, sdkconfig_sha256: $sdk, firmware: $fw, bytes: $bytes, sha256: $sha}' \
  > "$out/c6-provenance.json"
echo "HOSTED_C6_BUILD_OK version=$(jq -r .hosted_version "$lock") bytes=$(stat -c %s "$out/$name") sha256=$sha"
