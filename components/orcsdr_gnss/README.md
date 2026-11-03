# OrcSDR GNSS sentence parser

This platform-independent C parser accepts checksummed NMEA RMC and GGA fixes
from GP, GN, and GL talkers. It consumes one byte at a time, uses a fixed
128-byte sentence buffer, and allocates no memory. It requires `*HH` checksums
and CRLF framing. Unsupported valid sentences are ignored; malformed, truncated,
oversized, or checksum-invalid sentences are rejected. A completed RMC `V` or
GGA quality-zero sentence returns `NO_FIX` with zero coordinates so the caller
can clear the live fix. The parser does not retain a last-known fix or combine
RMC/GGA fields across sentences. Estimated, manual, simulated and invalid-mode
solutions are reported as no-fix rather than a verified GNSS position.

Coordinates are signed degrees times 10^7. UTC time validity and date validity
are reported separately. GGA has no date and therefore never fabricates one.
RMC preserves the supplied two-digit year without applying a century pivot.
Fractional UTC seconds are validated and truncated to milliseconds. The parser
accepts February 29 as structurally possible without inferring a century.

## Source and license provenance

The implementation is original OrcSDR C code; no parser source was copied or
translated. The following primary sources informed framing, checksums, sentence
fields, and hardware expectations:

- [TinyGPSPlus parser source](https://github.com/mikalhart/TinyGPSPlus/blob/master/src/TinyGPS%2B%2B.cpp), LGPL-2.1-or-later. Its parser XORs sentence bytes and commits data only after checksum success. Used as a behavioral reference only.
- [GPSD NMEA driver](https://github.com/ukyg9e5r6k7gubiekd6/gpsd/blob/master/driver_nmea0183.c), GPSD project BSD-2-Clause. It documents the RMC/GGA field positions, optional fractional UTC precision, and fix-status handling. Used as a behavioral reference only.
- [Quectel L76K protocol specification](https://forums.quectel.com/uploads/short-url/2YgtXqGBqOrRPLKzPYGR7Nxu7D2.pdf), Quectel vendor document. Consulted for the L76K's supported NMEA sentence family; it is not source code.
- [LilyGO T-Display-P4 board configuration](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h) and [L76K initialization](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_driver.cpp#L800-L867), LilyGO GPL-3.0 sources. Consulted for board pins and the vendor's baud/configuration sequence only; no driver code was copied.

Run the focused host test with `bash tools/test-p4-gnss.sh`. This checks the C
parser only and does not qualify UART, receiver fixes, or physical GNSS behavior.

## Optional P4 UART service

The P4 application uses `main/gnss_service.cpp` only when its board profile
explicitly declares GNSS. The shared expander owner applies the documented
startup latch before the passive UART reader starts. LilyGO declares UART1 RX23,
TX22 and 115200 baud from its vendor configuration; the reader leaves TX
unconfigured and sends no receiver commands. The installed unit's baud and
actual reception must be verified separately.

`RTL_GNSS STATUS` reports startup, baud, byte/sentence counts, current usable
fix, retained-last-fix availability/age and transport/cleanup errors. It emits
neither coordinates nor raw NMEA. A no-fix sentence, read error or five seconds
without a fresh fix makes the current fix unusable. Last-valid coordinates are
retained separately and must not be mistaken for a live position. Saved receiver
location, map selection and clock are not changed automatically.

`bash tools/test-p4-gnss-service.sh` compiles the actual service with UART,
timer and task fakes. It checks ownership/setup failures and fix lifecycle;
it does not establish hardware baud, satellite reception, concurrent task
scheduling or a physically accurate fix.
