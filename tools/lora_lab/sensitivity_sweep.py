#!/usr/bin/env python3
"""Decode success versus SNR for a real Meshtastic packet, per preamble-search mode.

Takes a real ORCIQ capture (see capture_packet.py), crops it to one burst, adds
seeded white noise at a series of SNRs and replays each variant into the Tab5's
native decoder over serial. Each SNR/seed is replayed once for every
`RTL_LORA_DECODER SEARCH_PAD` mode so the fast 1x search can be compared with
the legacy 4x search on identical input.

SNR is the repo's wideband definition (active signal RMS against added noise
across the whole 960 kS/s capture). In-band SNR in the 250 kHz LoRa channel is
that value plus 10*log10(960/250) = 5.84 dB. SF11 needs about -17.5 dB in-band.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import time
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import decode_orciq  # noqa: E402
import replay_lora_orciq as replay  # noqa: E402
from help_media import Tab5  # noqa: E402

INBAND_OFFSET_DB = 10 * __import__("math").log10(960 / 250)


def fields(line: str) -> dict:
    result = {}
    for key, value in re.findall(r"\b([a-z_]+)=([^\s]+)", line):
        try:
            result[key] = int(value)
        except ValueError:
            result[key] = value
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path, help="ORCIQ capture of one real packet")
    parser.add_argument("--start-s", type=float, default=0.0)
    parser.add_argument("--end-s", type=float, default=0.0, help="0 = to the end")
    parser.add_argument("--timing-bias", type=int, default=0,
                        help="diagnostic: shift the decoder's header timing search by N samples")
    parser.add_argument("--auto-crop", action="store_true",
                        help="crop to the strongest burst plus 0.35 s either side")
    parser.add_argument("--snrs", type=float, nargs="+",
                        default=[-8, -12, -16, -18, -20, -22, -24])
    parser.add_argument("--seeds", type=int, nargs="+", default=[1, 2, 3])
    parser.add_argument("--modes", type=int, nargs="+", default=[1, 4])
    parser.add_argument("--port", default="COM17")
    parser.add_argument("--pairing-key", type=Path,
                        default=TOOLS.parent / ".orclink" / "ui-doc.key")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    rate, frequency_hz, sf, bandwidth_hz, iq = decode_orciq.read_capture(args.capture)
    if args.auto_crop:
        import numpy as np
        values = np.frombuffer(iq, dtype=np.uint8).astype(np.float32) - 127.5
        samples = (values[0::2] + 1j * values[1::2]) / 127.5
        block = rate // 100
        power = np.array([10 * np.log10(np.mean(np.abs(samples[i * block:(i + 1) * block]) ** 2) + 1e-12)
                          for i in range(samples.size // block)])
        floor = float(np.median(power))
        peak = int(np.argmax(power))
        lo = hi = peak
        while lo > 0 and power[lo - 1] > floor + 4:
            lo -= 1
        while hi < power.size - 1 and power[hi + 1] > floor + 4:
            hi += 1
        args.start_s = max(0.0, (lo - 35) * 0.01)
        args.end_s = min(power.size * 0.01, (hi + 36) * 0.01)
        print(f"AUTOCROP burst={lo * 0.01:.2f}-{(hi + 1) * 0.01:.2f}s floor={floor:.1f}dB "
              f"peak=+{power[peak] - floor:.1f}dB crop={args.start_s:.2f}-{args.end_s:.2f}s")
    first = int(args.start_s * rate) * 2
    last = int(args.end_s * rate) * 2 if args.end_s else len(iq)
    iq = iq[first:last]
    print(f"CROP samples={len(iq) // 2} seconds={len(iq) / 2 / rate:.2f} sf={sf} bw={bandwidth_hz}")

    rows = []
    # Opening the Tab5's native USB port resets it and the port disappears for a
    # few seconds while it re-enumerates, so retry the open.
    tab5 = None
    for attempt in range(1, 16):
        try:
            tab5 = Tab5(args.port, args.pairing_key)
            break
        except (OSError, TimeoutError, __import__("serial").SerialException) as error:
            if attempt == 15:
                raise
            print(f"WAIT port not ready ({attempt}): {error}", flush=True)
            time.sleep(3)
    try:
        tab5.authenticate()
        tab5.send("RTL_SERIAL VERBOSITY NORMAL")
        tab5.wait(("RTL_SERIAL_VERBOSITY_OK",), 5)
        # Ambient energy can start a live automatic capture, which blocks replay.
        tab5.send("RTL_LORA_AUTO OFF")
        tab5.wait(("RTL_LORA_AUTO OFF",), 5)
        tab5.send(f"RTL_LORA_DECODER TIMING_BIAS {args.timing_bias}")
        tab5.wait(("RTL_LORA_DECODER_OK",), 5)
        tab5.send("RTL_STOP")
        stopped = tab5.wait(("RTL_STOP_RESULT", "RTL_STOP_ERROR"), 20)
        if stopped != "RTL_STOP_RESULT ESP_OK":
            raise RuntimeError(stopped)
        for snr in args.snrs:
            for seed in args.seeds:
                noisy, impairment = replay._impair_iq(iq, snr, seed, 0.05)
                for mode in args.modes:
                    tab5.send(f"RTL_LORA_DECODER SEARCH_PAD {mode}")
                    tab5.wait(("RTL_LORA_DECODER_OK",), 5)
                    started = time.monotonic()
                    for attempt in range(1, 7):
                        try:
                            replay._upload_iq(tab5.serial, noisy, rate=rate,
                                              frequency_hz=frequency_hz, sf=sf,
                                              bandwidth_hz=bandwidth_hz)
                            break
                        except (TimeoutError, RuntimeError) as error:
                            if attempt == 6:
                                raise
                            print(f"RETRY upload after: {error}", flush=True)
                            tab5.send("RTL_LORA_REPLAY_ABORT")
                            time.sleep(4)
                            tab5.serial.reset_input_buffer()
                            if "busy" in str(error):
                                # a live automatic capture is holding the buffer: stop the radio again
                                tab5.send("RTL_STOP")
                                time.sleep(2)
                                tab5.serial.reset_input_buffer()
                    observed: list[str] = []
                    done = replay._wait_line(tab5.serial, ("RTL_LORA_NATIVE_DONE",), 180, observed)
                    profile = replay._wait_line(tab5.serial, ("RTL_LORA_NATIVE_PROFILE",), 15, observed)
                    if not rows:
                        print("DEBUG " + done, flush=True)
                        print("DEBUG " + profile[:300], flush=True)
                    done_fields = fields(done)
                    profile_fields = fields(profile)
                    row = {
                        "wideband_snr_db": snr,
                        "inband_snr_db": round(snr + INBAND_OFFSET_DB, 1),
                        "seed": seed,
                        "search_padding": mode,
                        "crc_ok": done_fields.get("crc_ok", 0),
                        "preambles": done_fields.get("preambles", 0),
                        "header_failures": done_fields.get("header_failures", 0),
                        "decode_ms": done_fields.get("decode_ms", done_fields.get("decode_millis")),
                        "preamble_ms": profile_fields.get("preamble_ms"),
                        "packets": sum(1 for line in observed if line.startswith("RTL_LORA_NATIVE_PACKET ")),
                        "seconds": round(time.monotonic() - started, 1),
                    }
                    rows.append(row)
                    print(json.dumps(row), flush=True)
    finally:
        # Best effort, also after a failed sweep: leave the device as it was found.
        for command, marker in (("RTL_LORA_DECODER SEARCH_PAD 1", "RTL_LORA_DECODER_OK"),
                                ("RTL_LORA_DECODER TIMING_BIAS 0", "RTL_LORA_DECODER_OK"),
                                ("RTL_LORA_AUTO ON", "RTL_LORA_AUTO ON")):
            try:
                tab5.send(command)
                tab5.wait((marker,), 5)
            except Exception as error:  # noqa: BLE001 - restore must not mask the original error
                print(f"WARN restore failed for {command!r}: {error}", flush=True)
        tab5.close()

    print("SUMMARY inband_snr_db  mode  success/trials")
    for snr in args.snrs:
        for mode in args.modes:
            sel = [r for r in rows if r["wideband_snr_db"] == snr and r["search_padding"] == mode]
            ok = sum(1 for r in sel if r["crc_ok"] > 0)
            print(f"SUMMARY {snr + INBAND_OFFSET_DB:7.1f}  x{mode}  {ok}/{len(sel)}")
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
