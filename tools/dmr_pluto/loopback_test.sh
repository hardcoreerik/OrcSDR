#!/bin/sh
# No-RF proof that the generator and the production receive chain agree: generate a call, make a CU8 (optionally noisy), decode it with
# dv_voice_replay and compare the decoded frames with the frames that were sent. usage: loopback_test.sh <dv_voice_replay> [snr_db]
set -e
B=$1; SNR=$2; D=$(mktemp -d); H=$(dirname "$0")
python3 "$H/dmr_gen.py" --frames "$H/test_frames_18.txt" --src 4106008 --dst 6011885 --cc 8 --slot 1 --superframes 8 --out "$D/call.npz" >/dev/null
python3 "$H/cu8_loopback.py" "$D/call.npz" "$D/call.cu8" $SNR >/dev/null
"$B" "$D/call.cu8" 915000000 "$D" lb | head -1
python3 - "$D" <<'PY'
import sys, json, numpy as np
d = sys.argv[1]
sent = json.loads(str(np.load(d + '/call.npz', allow_pickle=True)['label']))['frames_sent']
got = [x.strip().upper() for x in open(d + '/lb_ts1_call1.frames.txt') if x.strip()]
print('frames sent', len(sent), 'decoded', len(got), 'EXACT' if got == sent else 'MISMATCH')
sys.exit(0 if got == sent else 1)
PY
