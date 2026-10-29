#!/usr/bin/env bash
# Host-only Pluto/WEFAX generator + current production WEFAX core replay.
# No RF, hardware, serial ports, firmware flashes, or radio TX are invoked.
set -euo pipefail
RUN_DIR="$PWD"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOG="$RUN_DIR/wefax_pluto_host_test.log"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
{
  echo "OrcSDR Pluto-WEFAX host test UTC $(date -u +%FT%TZ)"
  echo "Source branch: $(git -C "$ROOT" branch --show-current 2>/dev/null || true)"
  echo "Working directory: $RUN_DIR"
  python3 -m py_compile "$ROOT/tools/wefax_pluto_lab.py"
  g++ -std=c++17 -O2 -Wall -Wextra -Werror \
    -I"$ROOT/apps/orcsdr-tab5/ui" \
    "$ROOT/tools/wefax_wav_replay.cpp" \
    "$ROOT/apps/orcsdr-tab5/ui/wefax_core.cpp" -o "$TMP/wefax-replay"
  for cfg in "576 120" "288 90"; do
    read -r ioc lpm <<<"$cfg"
    out="$TMP/$ioc-$lpm"
    python3 "$ROOT/tools/wefax_pluto_lab.py" selftest --ioc "$ioc" --lpm "$lpm" --rows 6 \
      --out-dir "$out"
    # With zero RF/audio latency, the first chart sample starts at 35 s.
    "$TMP/wefax-replay" "$out/wefax_reference_48k.wav" "$out/received.pgm" 35 6 "$ioc" "$lpm"
    python3 - "$out/wefax_reference.pgm" "$out/received.pgm" <<'PY'
import sys
from pathlib import Path
def pgm(path):
    b = Path(path).read_bytes()
    assert b.startswith(b'P5\n'), path
    header, width, height, _ = b.split(b'\n', 3)
    head = len(header)+len(width)+len(height)+len(b'255')+4
    w,h=int(width.split()[0]),int(width.split()[1])
    return w,h,b[head:]
w,h,a=pgm(sys.argv[1])
w2,h2,b=pgm(sys.argv[2])
assert (w,h)==(w2,h2), ((w,h),(w2,h2))
assert len(a)==len(b)==w*h, (len(a),len(b),w*h)
err=sum(abs(a[i]-b[i]) for i in range(len(a))) / len(a)
print(f'WEFAX_PGM_COMPARE width={w} rows={h} MAE={err:.3f}/255')
assert err < 40.0, f'Excessive synthetic host image error: {err:.2f}'
PY
  done
  # Must refuse conducted RF without mandatory operator confirmation (without
  # opening/using a Pluto device). A missing guard must be a nonzero exit code.
  if python3 "$ROOT/tools/wefax_pluto_lab.py" transmit --rows 2 \
        --start-seconds 0.2 --phasing-seconds 0.2 --stop-seconds 0.2 \
        --out-dir "$TMP/guard"; then
    echo "FAIL: transmitted without confirmations"
    exit 1
  fi
  echo "PASS: RF mode was refused without explicit conducted-chain confirmation"
  echo "ALL PLUTO-WEFAX HOST TESTS PASSED"
} 2>&1 | tee "$LOG"
