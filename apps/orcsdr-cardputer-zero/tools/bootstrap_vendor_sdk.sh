#!/usr/bin/env bash
# Optional pinned Cardputer Zero reference SDK checkouts. This SDK is a
# reference/toolchain input; the OrcSDR native framebuffer renderer does not
# silently depend on a fork of the vendor LVGL application.
set -euo pipefail
root="${1:-${PWD}/.cache/orc-zero-sdk}"
mkdir -p "$root"
pin() {
  local repo="$1" dest="$2" sha="$3"
  if [[ ! -d "$dest/.git" ]]; then git clone -q --filter=blob:none "$repo" "$dest"; fi
  git -C "$dest" fetch -q --depth 1 origin "$sha"
  git -C "$dest" checkout -q --detach "$sha"
  [[ "$(git -C "$dest" rev-parse HEAD)" == "$sha" ]] || exit 3
  printf 'verified %s %s\n' "$dest" "$sha"
}
pin https://github.com/CardputerZero/Template.git "$root/Template" 477fa6d217b683e3084c361a757f03dc8ca9f753
pin https://github.com/lvgl/lvgl.git "$root/lvgl" 85aa60d18b3d5e5588d7b247abf90198f07c8a63
pin https://github.com/CardputerZero/AppBuilder.git "$root/AppBuilder" 18996b694ae305306fc5d457d982cfba0b0e7cc5
[[ -f "$root/lvgl/lvgl.h" && -f "$root/Template/CMakeLists.txt" ]]
echo 'Pinned vendor GUI SDK and LVGL v9.5.0 sources ready (no OS changes).'