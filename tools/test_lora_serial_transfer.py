import hashlib
import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).parent))
import decode_orciq


class FakeIqSerial:
    payload = b"abcdef"

    def __init__(self, timeout_first_read=False):
        self.stream = bytearray()
        self.attempt = 0
        self.chunk = 0
        self.aborts = 0
        self.released = False
        self.timeout_first_read = timeout_first_read
        self.timed_out = False

    def _line(self, value: str):
        self.stream.extend(value.encode("ascii") + b"\n")

    def write(self, value: bytes):
        for command in value.decode("ascii").splitlines():
            if command == "RTL_IQ_RETRIEVE_BEGIN":
                self._line(
                    "RTL_IQ_RETRIEVE_READY storage=psram bytes=6 rate=250000 "
                    "frequency_hz=906875000 sf=9 bw=125000"
                )
            elif command == "RTL_IQ_GET_BEGIN":
                self.attempt += 1
                self.chunk = 0
                self._line("RTL_IQ_GET_READY chunk=3 bytes=6")
            elif command == "RTL_IQ_GET_CHUNK":
                start = self.chunk * 3
                data = self.payload[start : start + 3]
                self.chunk += 1
                self._line(f"RTL_IQ_GET_DATA bytes={len(data)}")
                self.stream.extend(data)
                if self.chunk == 2:
                    digest = "0" * 64 if self.attempt == 1 else hashlib.sha256(self.payload).hexdigest()
                    self._line(f"RTL_IQ_GET_DONE bytes=6 sha256={digest}")
            elif command == "RTL_IQ_GET_ABORT":
                self.aborts += 1
                self._line("RTL_IQ_GET_ABORTED ready=true bytes=6")
            elif command == "RTL_IQ_RETRIEVE_END":
                self.released = True
                self._line("RTL_IQ_RETRIEVE_DONE")
        return len(value)

    def readline(self):
        newline = self.stream.find(b"\n")
        if newline < 0:
            return b""
        result = bytes(self.stream[: newline + 1])
        del self.stream[: newline + 1]
        return result

    def read(self, count: int):
        if self.timeout_first_read and self.attempt == 1 and not self.timed_out:
            self.timed_out = True
            return b""
        result = bytes(self.stream[:count])
        del self.stream[:count]
        return result

    def reset_input_buffer(self):
        self.stream.clear()


class SerialTransferTests(unittest.TestCase):
    def test_sha_mismatch_aborts_retries_and_releases_only_after_success(self):
        connection = FakeIqSerial()
        name, capture = decode_orciq._retrieve_latest_iq(connection)

        self.assertEqual(connection.aborts, 1)
        self.assertEqual(connection.attempt, 2)
        self.assertTrue(name.endswith("_906875000_sf9_bw125000.orciq"))
        self.assertEqual(capture[decode_orciq.HEADER.size :], connection.payload)
        self.assertFalse(connection.released)

        connection.write(b"RTL_IQ_RETRIEVE_END\n")
        self.assertEqual(
            decode_orciq._wait_line(connection, ("RTL_IQ_RETRIEVE_DONE",)),
            "RTL_IQ_RETRIEVE_DONE",
        )
        self.assertTrue(connection.released)

    def test_meshtastic_pki_direct_message(self):
        import struct
        from cryptography.hazmat.primitives import hashes
        from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
        from cryptography.hazmat.primitives.ciphers.aead import AESCCM
        from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

        sender, destination, message_id = 0xF66F81BC, 0xF670A224, 0x12345678
        sender_private = X25519PrivateKey.from_private_bytes(bytes(range(1, 33)))
        remote_private = X25519PrivateKey.from_private_bytes(bytes(range(33, 65)))
        remote_public = remote_private.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
        shared = sender_private.exchange(remote_private.public_key())
        digest = hashes.Hash(hashes.SHA256())
        digest.update(shared)
        session_key = digest.finalize()
        plaintext = decode_orciq._data_message(1, b"PKI direct test")
        extra_nonce = b"\x11\x22\x33\x44"
        nonce = bytearray(struct.pack("<QI", message_id, sender) + b"\0" * 4)
        nonce[4:8] = extra_nonce
        encrypted = AESCCM(session_key, tag_length=8).encrypt(
            bytes(nonce[:13]), plaintext, None
        ) + extra_nonce
        frame = decode_orciq.MESH_HEADER.pack(
            destination, sender, message_id, 0x6B, 0, 0, sender & 0xFF
        ) + encrypted

        decoded = decode_orciq.decode_mesh_packet(
            frame, [], [("test-pair", bytes(range(1, 33)), remote_public)]
        )
        self.assertEqual(decoded["encryption"], "pki")
        self.assertEqual(decoded["payload"], b"PKI direct test")

    def test_timeout_aborts_and_retries_from_byte_zero(self):
        connection = FakeIqSerial(timeout_first_read=True)
        _, capture = decode_orciq._retrieve_latest_iq(connection)

        self.assertEqual(connection.aborts, 1)
        self.assertEqual(connection.attempt, 2)
        self.assertEqual(capture[decode_orciq.HEADER.size :], connection.payload)


if __name__ == "__main__":
    unittest.main()
