# LoRa payload entry contract

`orcsdr::lora_native::decode_payload` interprets one Meshtastic mesh payload
after the caller has accepted the LoRa physical-layer CRC. It does not calculate
or verify that CRC, and successful parsing does not authenticate the sender.
Callers must keep the hardware CRC result and any sender trust decision separate
from this decoded packet.

Input is bounded to the physical LoRa payload range of 17 through 255 bytes.
Rejected input clears the output packet when an output pointer is available.
The entry uses the existing mesh parser/decryptor and does not allocate or
initialize IQ capture workspace. SDR capture decoding retains its own CRC and
CFO processing path.

Run the focused actual-translation-unit fixture with
`apps/orcsdr-tab5/tools/run-lora-payload-wasm-test.ps1`. In an isolated worktree
without the portable decoder shims, pass `-SupportRepo` pointing at a checkout
that contains `apps/orcsdr-web/decoders/shim_host` and `shim_portable`.

The parent integration checkout passed the default runner after importing the
CRLF normalization fix. The fixture compiles the real decoder translation unit
and checks plaintext, a known default-key ciphertext, unknown encrypted content,
null/invalid lengths, cleared rejected output, and unchanged IQ workspace bytes.
The mounts are read-only; compilation/execution use the container's temporary
directory. These software checks do not prove CRC reception on the radio or
end-to-end message display; the hardware RX adapter is still to be wired.
