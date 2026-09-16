import hashlib
import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).parent))
import replay_lora_orciq


class FakeSerial:
    def __init__(self, chunk=3):
        self.stream = bytearray()
        self.commands = []
        self.payload = bytearray()
        self.chunk = chunk

    def _line(self, value):
        self.stream.extend(value.encode("ascii") + b"\n")

    def write(self, value):
        if value.startswith(b"RTL_"):
            command = value.decode("ascii").strip()
            self.commands.append(command)
            if command.startswith("RTL_LORA_REPLAY_BEGIN "):
                self._line(f"RTL_LORA_REPLAY_READY chunk={self.chunk} bytes=6")
            elif command.startswith("RTL_LORA_REPLAY_CHUNK "):
                self._line("RTL_LORA_REPLAY_DATA")
            return len(value)
        self.payload.extend(value)
        if len(self.payload) == 6:
            self._line("RTL_LORA_REPLAY_QUEUED bytes=6")
        else:
            self._line(f"RTL_LORA_REPLAY_ACK bytes={len(self.payload)}")
        return len(value)

    def flush(self):
        pass

    def readline(self):
        newline = self.stream.find(b"\n")
        if newline < 0:
            return b""
        value = bytes(self.stream[: newline + 1])
        del self.stream[: newline + 1]
        return value


class ReplayUploadTests(unittest.TestCase):
    def test_capture_header_rejects_invalid_lora_parameters(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "invalid.orciq"
            for rate, sf, bandwidth in ((0, 11, 250000), (960000, 31, 250000),
                                        (960000, 11, 0), (125000, 11, 250000)):
                with self.subTest(rate=rate, sf=sf, bandwidth=bandwidth):
                    decoder = replay_lora_orciq.decode_orciq
                    path.write_bytes(decoder.HEADER.pack(
                        decoder.MAGIC, decoder.HEADER.size, rate, 906875000,
                        2, 1, sf, 0, bandwidth, 0,
                    ) + b"\x7f\x7f")
                    with self.assertRaisesRegex(ValueError, "invalid LoRa parameters"):
                        replay_lora_orciq.decode_orciq.read_capture(path)

    def test_upload_rejects_zero_device_chunk(self):
        with self.assertRaisesRegex(RuntimeError, "invalid replay chunk"):
            replay_lora_orciq._upload_iq(
                FakeSerial(chunk=0), b"abcdef", rate=960000,
                frequency_hz=906875000, sf=11, bandwidth_hz=250000,
            )

    def test_upload_is_chunked_and_content_bound(self):
        connection = FakeSerial()
        replay_lora_orciq._upload_iq(
            connection, b"abcdef", rate=960000, frequency_hz=906875000,
            sf=11, bandwidth_hz=250000,
        )

        digest = hashlib.sha256(b"abcdef").hexdigest()
        self.assertEqual(
            connection.commands[0],
            f"RTL_LORA_REPLAY_BEGIN 6 {digest} 960000 906875000 11 250000",
        )
        self.assertEqual(connection.payload, b"abcdef")
        self.assertEqual(connection.commands[1:], [
            "RTL_LORA_REPLAY_CHUNK 3", "RTL_LORA_REPLAY_CHUNK 3",
        ])

    def test_no_preamble_result_has_no_trace(self):
        self.assertEqual(
            replay_lora_orciq._read_traces(None, "RTL_LORA_NATIVE_DONE preambles=0"),
            [],
        )

    def test_parse_fields_converts_profile_values(self):
        self.assertEqual(
            replay_lora_orciq._parse_fields(
                "RTL_LORA_MEMORY stage=after_init decoder_psram=657184 "
                "fft_table=PSRAM fft_bytes=131072 recovery=PSRAM recovery_bytes=1856"
            ),
            {"stage": "after_init", "decoder_psram": 657184,
             "fft_table": "PSRAM", "fft_bytes": 131072,
             "recovery": "PSRAM", "recovery_bytes": 1856},
        )

    def test_wait_line_retains_nonmatching_telemetry(self):
        connection = FakeSerial()
        connection._line("RTL_LORA_MEMORY stage=replay_before decoder_psram=657160")
        connection._line("RTL_LORA_NATIVE_DONE packets=1")
        observed = []

        self.assertEqual(
            replay_lora_orciq._wait_line(
                connection, ("RTL_LORA_NATIVE_DONE",), observed=observed
            ),
            "RTL_LORA_NATIVE_DONE packets=1",
        )
        self.assertEqual(
            observed,
            ["RTL_LORA_MEMORY stage=replay_before decoder_psram=657160"],
        )

    def test_parse_records_keeps_packet_identity(self):
        self.assertEqual(
            replay_lora_orciq._parse_records(
                ["noise", "RTL_LORA_NATIVE_PACKET sequence=2 packet_id=4219089354"],
                "RTL_LORA_NATIVE_PACKET ",
            ),
            [{"sequence": 2, "packet_id": 4219089354}],
        )

    def test_trace_reader_keeps_symbol_alternates(self):
        connection = FakeSerial()
        connection._line("RTL_LORA_NATIVE_TRACE sequence=1")
        connection._line("RTL_LORA_NATIVE_ALTERNATES sequence=1 count=1 values=8:12:1.025")
        connection._line("RTL_LORA_NATIVE_SYMBOLS sequence=1 count=1 values=11")

        self.assertEqual(
            replay_lora_orciq._read_traces(
                connection, "RTL_LORA_NATIVE_DONE preambles=1"
            ),
            [
                "RTL_LORA_NATIVE_TRACE sequence=1",
                "RTL_LORA_NATIVE_ALTERNATES sequence=1 count=1 values=8:12:1.025",
                "RTL_LORA_NATIVE_SYMBOLS sequence=1 count=1 values=11",
            ],
        )

    def test_symbol_difference_reports_first_and_histogram(self):
        self.assertEqual(
            replay_lora_orciq._symbol_difference([1, 2, 3], [1, 3, 2]),
            {"first": 1, "indices": [1, 2], "different": 2, "largest": 1,
             "histogram": {"-1": 1, "0": 1, "+1": 1}},
        )

    def test_symbol_difference_records_unequal_lengths(self):
        self.assertEqual(
            replay_lora_orciq._symbol_difference([1, 2], [1]),
            {"first": 1, "indices": [1], "different": 1, "largest": 0,
             "histogram": {"0": 1},
             "length_mismatch": {"reference": 2, "native": 1}},
        )

    def test_alternate_coverage_reports_correct_runner_up_symbols(self):
        line = (
            "RTL_LORA_NATIVE_ALTERNATES sequence=4 count=3 "
            "values=8:12:1.025,9:22:1.400,10:31:1.010"
        )

        alternates = replay_lora_orciq._parse_symbol_alternates(line)

        self.assertEqual(alternates, {
            8: {"symbol": 12, "ratio": 1.025},
            9: {"symbol": 22, "ratio": 1.4},
            10: {"symbol": 31, "ratio": 1.01},
        })
        self.assertEqual(
            replay_lora_orciq._alternate_coverage(
                [1] * 8 + [12, 20, 30],
                [1] * 8 + [11, 20, 32],
                alternates,
            ),
            {"error_indices": [8, 10], "covered_indices": [8], "covered": 1,
            "errors": 2},
        )

    def test_alternate_coverage_records_unequal_lengths(self):
        self.assertEqual(
            replay_lora_orciq._alternate_coverage([1], [1, 2], {}),
            {"error_indices": [1], "covered_indices": [], "covered": 0,
             "errors": 1, "length_mismatch": {"reference": 1, "native": 2}},
        )

    def test_symbol_alternates_keep_native_peak_magnitudes(self):
        self.assertEqual(
            replay_lora_orciq._parse_symbol_alternates(
                "RTL_LORA_NATIVE_ALTERNATES sequence=4 count=1 "
                "values=8:12:1.025:82.000:80.000"
            ),
            {8: {"symbol": 12, "ratio": 1.025,
                 "primary_magnitude": 82.0, "alternate_magnitude": 80.0}},
        )


if __name__ == "__main__":
    unittest.main()
