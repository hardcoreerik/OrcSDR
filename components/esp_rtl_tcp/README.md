# esp-rtl-tcp

An rtl_tcp-compatible IQ server for ESP32 boards (AGPL-3.0, like the esp-rtl-sdr driver). Application layer: no dependency on OrcSDR or on the driver.
Written from the observed behaviour of rtl_tcp, not from anyone's code. Design and decisions: `docs/hardware/ESP_RTL_TCP_DESIGN.md`.

- `include/rtl_tcp_proto.hpp`: wire format (12-byte header sent once, 5-byte big-endian commands, command classification). Pure C++; `tests/proto_test.cpp` runs on a host.
- `include/esp_rtl_tcp.hpp`, `src/esp_rtl_tcp.cpp`: the server (one client, a second waits), the `Host` interface the application implements, counters, and `push()` for IQ blocks.
- Listen-only by default: tuning commands are counted and ignored; a change of centre or rate by another owner ends the session with `receiver_changed`.
