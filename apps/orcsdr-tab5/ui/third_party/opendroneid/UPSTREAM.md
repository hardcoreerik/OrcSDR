# Pinned Open Drone ID decoder dependency

This directory contains **unaltered** `libopendroneid/opendroneid.c`,
`libopendroneid/opendroneid.h` and the original Apache License 2.0 text from
`opendroneid/opendroneid-core-c` at commit:

`849c03adbe0cdd68997a462fafab397edcfe53f3`

Original project: https://github.com/opendroneid/opendroneid-core-c

Original author notice and SPDX headers are preserved. The core is licensed
Apache-2.0; the surrounding OrcSDR application retains its own license.
Review combined distribution notices if publishing firmware.

`drone_watch_rid.cpp` is OrcSDR's independent bounded input adapter and
presentation/evidence policy. The vendor decoder does NOT receive over BLE,
Wi-Fi or an RTL-SDR by itself; only precisely extracted 25-byte Open Drone ID
messages enter this component. No firmware C6 interface was modified.

Do not replace this revision with latest/master without redoing fixture,
API, compiler, memory and licensing review.
