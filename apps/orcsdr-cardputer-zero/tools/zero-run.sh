#!/bin/sh
# Run the compact GUI while the vendor launcher is frozen.
#
# M5CardputerZero-APPLaunch keeps /dev/fb0 open and repaints its home screen and
# status bar roughly twice a second. OrcSDR erases those pixels on its next pass,
# about 80ms later, so the panel flickers with the launcher's clock and battery
# figure. Measured on a Zero: 266 top-row changes in 150s with the launcher
# running, 0 in 90s with it stopped.
#
# This freezes the launcher with SIGSTOP instead of stopping its systemd unit.
# A stopped process does not take its children with it, so this stays safe even
# when the launcher spawned the app; stopping APPLaunch.service would kill a
# child that sits in the same cgroup. On exit the launcher is resumed with
# SIGCONT and redraws itself.
#
# Usage: zero-run.sh [gui options...]
#   ORC_ZERO_BIN  override the binary path
#                 (default: <repo>/build/zero/orcsdr_cardputer_zero_gui)
#
# If this script is killed with SIGKILL the launcher stays frozen. Recover with
#   kill -CONT $(pgrep -f M5CardputerZero-APPLaunch)
# or systemctl --user restart APPLaunch.service

set -u

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
bin=${ORC_ZERO_BIN:-}
if [ -z "$bin" ]; then
  for candidate in \
    "$repo/build/zero/orcsdr_cardputer_zero_gui" \
    "$repo/build/ux/orcsdr_cardputer_zero_gui" \
    "$repo/build/zero-release/orcsdr_cardputer_zero_gui"; do
    if [ -x "$candidate" ]; then bin=$candidate; break; fi
  done
fi

if [ -z "$bin" ] || [ ! -x "$bin" ]; then
  echo "zero-run: no GUI binary found; build one or set ORC_ZERO_BIN" >&2
  exit 1
fi

launcher=""
for pid in $(pgrep -f 'M5CardputerZero-APPLaunch' 2>/dev/null); do
  [ "$pid" = "$$" ] && continue
  launcher="$launcher $pid"
done
launcher=${launcher# }

thaw() {
  [ -n "${launcher:-}" ] || return 0
  # shellcheck disable=SC2086
  kill -CONT $launcher 2>/dev/null
  launcher=""
}

trap 'thaw' EXIT
trap 'thaw; exit 130' INT
trap 'thaw; exit 143' TERM

if [ -n "${launcher:-}" ]; then
  # shellcheck disable=SC2086
  kill -STOP $launcher 2>/dev/null
fi

"$bin" "$@"
status=$?
thaw
trap - EXIT INT TERM
exit $status
