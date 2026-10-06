# Devices and secure pairing (unreleased)

Open **Devices** in the Dial carousel and **Settings → Accessories & Companion**
on the Tab5. Pair on both within 60 seconds. Compare all six digits and confirm
on both devices. Cancel if they differ. Each device trusts one peer.

Pair establishes persistent trust. Connect authenticates a fresh session.
Disconnect retains trust and pauses automatic attempts for the current boot.
Explicit Connect can resume either peer. Forget & Re-pair requires confirmation,
revokes the old credential locally, and opens only the local pairing window.
The other device must also enter Pair and confirm again.

Boot connection defaults on after pairing; each device has its own preference.
The tablet waits for splash/Wi-Fi staging, with a bounded router startup wait.
Only the Dial scans channels. OrcDial remains an optional accessory.

## Version 4

Both applications must be upgraded. Legacy MAC-only records show **Pairing
upgrade required** and cannot authorize commands. No insecure fallback exists.
The existing C6 relay remains unchanged.

The shared headers under `src/control/secure_*` implement:

- Fixed 64-byte frames, logical messages up to 192 bytes, 48-byte fragment
  payloads, bounded reassembly pinned to one MAC, and a two-second expiry.
- mbedTLS P-256, Bluetooth f4/f5/f6/g2 numeric-comparison primitives, plus a
  key-confirmation transcript binding full device identities, roles, version,
  public keys, MACs and nonces. This adaptation uses ESP-NOW, not Bluetooth.
- Fresh authenticated connection nonces, distinct direction keys, AES-256-GCM,
  session identity and monotonic counters. Only authenticated plaintext enters
  the dashboard command queue. The control payload CRC is not authentication.
- Random persistent device identities and per-pair secrets. One versioned NVS
  blob is committed and read back before authorization. No secrets are logged,
  embedded in releases, or claimed resistant to physical flash extraction.
- A low-priority worker for crypto/storage/fragments. Radio callbacks enqueue;
  the tablet's existing Hosted TX task isolates slow RPCs from UI/DSP.

Fragment retirement remembers eight recent exchange IDs for up to two seconds.
It reduces disruption from delayed fragments; it is not authentication or a
comprehensive defense against wireless denial of service. Authenticated session
counters reject replay before controls are applied.

## Local USB commands

Use `ORCDIAL_` on the Dial or `RTL_ORCDIAL_` on the tablet:

```
ORCDIAL_PAIR START
ORCDIAL_PAIR CANCEL
ORCDIAL_PAIR CONFIRM 123456
ORCDIAL_CONNECT
ORCDIAL_DISCONNECT
ORCDIAL_FORGET
ORCDIAL_STATUS
```

Supply the displayed code after comparing BOTH screens. FORGET alone does not
open Pair. Status reports trust, connection, boot preference and failure reason.
The Tab5 requires its existing serial PAIR/AUTH session for mutations and to
print the comparison code. A failed deletion blocks control and replacement
pairing until Forget succeeds; storage failure can leave old trust on flash,
so it must be retried before restarting.

## Verification and rollout

```
cmake -S tests -B .pio/security-tests -DMBEDTLS_SOURCE_DIR=<mbedtls-3.6-LTS-source>
cmake --build .pio/security-tests --config Debug --target runtime_test security_test protocol_test controller_test
ctest --test-dir .pio/security-tests -C Debug --output-on-failure
pio run -e dial
```

Host tests require Mbed TLS 3.6 LTS (validated with 3.6.5); CMake rejects
other source versions. Assertions remain enabled in every build configuration.

Host tests include published CMAC/Bluetooth/GCM vectors; both approvals; invalid
keys; cancellation, timeout, restart and storage failure; dropped/duplicate
flights; malformed/reordered fragments; wrong MAC, modified ciphertext, replay,
stale sessions; manual stop/resume, boot preference and offline revocation.
Fault injection covers 700 queue/NVS/task initialization failures, failed
revocation with retry, and lost encrypted Forget notices.
Actual 240×240 display-only Devices captures are under `docs/screenshots/devices`.

Host tests and firmware builds are not hardware acceptance or a security audit.
Before a subsequent beta: complete independent security review, both startup
orders, absent router/accessory, channel stability, acknowledged tuning, stable
Wi-Fi, moving spectrum/waterfall and unchanged audio. Preserve recovery firmware
and serial logs. Keep `v0.1.0-beta.1` immutable.

References: [Bluetooth Core Security Manager](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host/security-manager-specification.html),
[RFC 4493](https://www.rfc-editor.org/rfc/rfc4493),
[NIST SP800-38D](https://csrc.nist.gov/pubs/sp/800/38/d/final).
