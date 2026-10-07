#!/usr/bin/env bash
# Linux/CI port of apps/orcsdr-tab5/tools/build-tab5-idf.ps1 plus the merge/size gate of
# tools/release/build-m5burner.ps1. Never flashes anything.
#   tools/ci/build-tab5.sh --c6 <esp_hosted_tab5_c6.bin> --out <dir> [--build-id "nightly abc1234 2026-10-07"]
# Needs ESP-IDF 5.5.4 exported. The required-sdkconfig list and the ESP-Hosted patch list are
# read from the .ps1 scripts at run time, so Windows and CI builds can't drift apart.
set -euo pipefail
c6="" out="" build_id="${ORCSDR_BUILD_ID:-}"
while [ $# -gt 0 ]; do
  case "$1" in
    --c6) c6=$2; shift 2 ;;
    --out) out=$2; shift 2 ;;
    --build-id) build_id=$2; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
if [ ! -f "$c6" ] || [ "$(basename "$c6")" != esp_hosted_tab5_c6.bin ]; then echo "--c6 must name an existing esp_hosted_tab5_c6.bin" >&2; exit 2; fi
[ -n "$out" ] || { echo "--out is required" >&2; exit 2; }
idf.py --version | grep -q 'v5\.5\.4' || { echo "ESP-IDF 5.5.4 is required (got: $(idf.py --version))" >&2; exit 2; }

# apps/orcsdr-tab5/dependencies.lock pins esp_rtl_sdr (a git component) by a component_hash that
# was computed on a Windows checkout with core.autocrlf=true (CRLF files). With LF files the
# component manager reports the download as "corrupted". Check the component out the same way
# for this build only (environment-scoped git config; nothing global is changed), so the sources
# match the Windows release builds byte for byte.
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=core.autocrlf GIT_CONFIG_VALUE_0=true

repo=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
app="$repo/apps/orcsdr-tab5"
tools="$app/tools"
build="build-native-hosted3"
c6=$(readlink -f "$c6")
mkdir -p "$out"; out=$(readlink -f "$out")
cd "$app"

# The OrcMaps world basemap embedded in the firmware (setup map picker): the pinned z0-z5 pack, accepted only if its
# size and SHA-256 match (the same pin as apps/orcsdr-tab5/tools/resolve-orcmaps-world.ps1).
world_version=0.2.1
world_sha=6f5c37f6cb505315e4c8a1d74efcf634fdb547b59128422ebf71c8ecf99addcc
world_bytes=1492862
world="$out/orcmaps_world.pmtiles"
curl -fsSL "https://github.com/hardcoreerik/orcmaps/releases/download/v${world_version}/orcmaps-world-z5-${world_version}.pmtiles" -o "$world"
[ "$(stat -c %s "$world")" = "$world_bytes" ] && echo "$world_sha  $world" | sha256sum -c - >/dev/null   || { echo "OrcMaps world pack does not match the pinned release" >&2; exit 1; }

# sdkconfig.defaults is the source; regenerate the per-build cache (same as the .ps1).
mkdir -p "$build"
cp sdkconfig.defaults "$build/sdkconfig"
idf.py -B "$build" -D "SDKCONFIG=$build/sdkconfig" -D SDKCONFIG_DEFAULTS=sdkconfig.defaults \
  -D ORCSDR_DSP_AB=0 -D ORCSDR_DSP_STAGE_TIMING=1 -D ORCSDR_DSP_LAB=0 -D ORCSDR_C6_FAULT_TEST=0 \
  -D "C6_FIRMWARE_BIN=$c6" -D "ORCMAPS_WORLD_BIN=$world" -D "ORCSDR_BUILD_ID=$build_id" reconfigure

# Patches after reconfigure (component manager may re-resolve managed_components/).
rel=$(git rev-parse --show-prefix); rel=${rel%/}
apply_patch() {
  local p="$tools/patches/$1"
  [ -f "$p" ] || { echo "missing patch $p" >&2; exit 1; }
  if git -C "$repo" apply --check --ignore-space-change --directory="$rel" -- "$p" 2>/dev/null; then
    git -C "$repo" apply --ignore-space-change --directory="$rel" -- "$p"
    echo "applied $1"
  elif git -C "$repo" apply --reverse --check --ignore-space-change --directory="$rel" -- "$p" 2>/dev/null; then
    echo "already applied $1"
  else
    echo "installed component does not match $1" >&2; exit 1
  fi
}
apply_patch m5gfx-tab5-pageflip.patch
mapfile -t hosted < <(grep -oP "File = '\K[^']+" "$tools/apply-esp-hosted-trampoline-fix.ps1")
[ "${#hosted[@]}" -gt 0 ] || { echo "could not read the ESP-Hosted patch list" >&2; exit 1; }
for p in "${hosted[@]}"; do apply_patch "$p"; done

# Required sdkconfig lines: the $required = @( ... ) block of build-tab5-idf.ps1.
mapfile -t required < <(awk '/^\$required = @\(/{f=1;next} f&&/^\)/{exit} f' "$tools/build-tab5-idf.ps1" | grep -oP "^\s*'\K[^']+")
[ "${#required[@]}" -ge 10 ] || { echo "could not read the required sdkconfig list" >&2; exit 1; }
for line in "${required[@]}"; do
  grep -qxF -- "$line" "$build/sdkconfig" || { echo "generated sdkconfig disagrees with defaults: $line" >&2; exit 1; }
done
echo "sdkconfig: ${#required[@]} required lines ok"

idf.py -B "$build" build

app_bin="$build/orcsdr_tab5.bin"
size=$(stat -c %s "$app_bin")
# The app partition size comes from the partition table, so this guard cannot drift from it.
factory_field=$(awk -F, '/^[[:space:]]*factory[[:space:]]*,/ {gsub(/[[:space:]]/, "", $5); print $5}' apps/orcsdr-tab5/partitions.csv)
case "$factory_field" in
  *M) app_partition=$(( ${factory_field%M} * 1048576 )) ;;
  *K) app_partition=$(( ${factory_field%K} * 1024 )) ;;
  *) app_partition=$(( factory_field )) ;;
esac
limit=$((app_partition - 0x40000))
[ "$size" -le "$limit" ] || { echo "P4 image $size B leaves less than 256 KiB in the $((app_partition / 1048576)) MiB app partition" >&2; exit 1; }
idf.py -B "$build" merge-bin --format raw --output merged-binary.bin
cp "$build/merged-binary.bin" "$out/merged-binary.bin"
cp "$build/orcsdr_tab5.elf" "$out/orcsdr_tab5.elf"
cp "$build/bootloader/bootloader.bin" "$out/bootloader.bin"
cp "$build/partition_table/partition-table.bin" "$out/partition-table.bin"
cp "$app_bin" "$out/orcsdr_tab5.bin"
echo "TAB5_BUILD_OK app=$size bytes (limit $limit) merged=$(stat -c %s "$out/merged-binary.bin") build_id='$build_id'"
