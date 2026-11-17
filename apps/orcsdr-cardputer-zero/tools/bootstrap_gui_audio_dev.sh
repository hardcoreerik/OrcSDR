#!/usr/bin/env bash
# OrcSDR Cardputer Zero development toolchain for Debian 13 / Ubuntu.
# Run this only on a developer workstation or disposable CI runner, not as a
# boot service and never with automatic OS flashing or BSP replacement.
set -euo pipefail
if ! command -v apt-get >/dev/null; then
  echo "Requires Debian/Ubuntu apt; no packages installed" >&2
  exit 2
fi
if [[ ${EUID} -eq 0 ]]; then SUDO=(); elif command -v sudo >/dev/null; then SUDO=(sudo); else
  echo "Install with apt privileges; sudo was not found" >&2; exit 2
fi
export DEBIAN_FRONTEND=noninteractive
"${SUDO[@]}" apt-get update
"${SUDO[@]}" apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build git pkg-config ca-certificates \
  libsdl2-dev libasound2-dev libevdev-dev libdrm-dev \
  libfreetype-dev libpng-dev libjpeg-dev zlib1g-dev libfmt-dev \
  libfftw3-dev librtlsdr-dev xvfb python3 python3-pil dpkg-dev
if [[ ${1:-} == --cross ]]; then
  "${SUDO[@]}" apt-get install -y --no-install-recommends \
    gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
fi
echo "=== Verified dev dependencies ==="
cmake --version | head -1
c++ --version | head -1
for package in sdl2 alsa libevdev libdrm freetype2 libpng librtlsdr fftw3f; do
  pkg-config --modversion "$package"
done
if [[ ${1:-} == --cross ]]; then aarch64-linux-gnu-g++ --version | head -1; fi
echo "LVGL is supplied as a separately pinned build dependency; do not confuse system SDL with the vendor LVGL source."
