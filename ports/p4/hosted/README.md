# LilyGO Hosted reset adapter

ESP-Hosted 3.0.6 requires an MCU reset GPIO in its SDIO bring-up and its IDF
reset implementation drives that GPIO directly. LilyGO instead connects C6 EN
to XL9535 vendor IO14. A GPIO number cannot represent this connection.

The LilyGO build replaces two source entries with generated copies, leaving
the managed dependency unchanged. Exact upstream byte hashes guard the patch;
a dependency change stops configuration for review. The generated reset path
uses the board-control API to assert EN for 20 ms, releases it through the
board pull-up, and propagates I2C errors. Other profiles retain upstream sources.

Run `python tests/test_hosted_board_reset.py` from the repository root. The
configure tests verify source replacement, unchanged managed files, default
behavior for other profiles, and rejection of dependency drift. They do not
prove hardware reset or Wi-Fi operation; those require a full build and device
boot evidence.
