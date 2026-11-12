#!/usr/bin/env python3
"""Host-only validation and deterministic chunked replay of private digital-voice IQ.

Only Python stdlib. No firmware, radios, network fetching, or decoder integration.
All IQ format decisions are explicit; raw IQ bytes have no self-describing header.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import struct
import sys
from dataclasses import dataclass
from typing import Callable, Iterator

HERE = Path(__file__).resolve().parent
MANIFEST = HERE / "manifest.json"
SCHEMA = "orcsdr-digital-voice-replay-fixtures-v1"
BYTES_PER_COMPLEX = {"cf32-le": 8, "cs16-le": 4}
KNOWN_SUFFIXES = {".cfile": "cf32-le", ".cf32": "cf32-le", ".cs16": "cs16-le"}
HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")


class FixtureError(ValueError):
    """Bad metadata, corrupted IQ or unsafe operation."""


@dataclass(frozen=True)
class Fixture:
    id: str
    path: str
    protocol: str
    format: str
    sample_rate_hz: int
    bytes: int
    git_blob_sha1: str
    sha256: str | None
    origin: str
    source: dict
    upstream_expected: dict

    @property
    def sample_count(self) -> int:
        return self.bytes // BYTES_PER_COMPLEX[self.format]


def _relative_path(path: str) -> PurePosixPath:
    if not isinstance(path, str) or not path or "\\" in path:
        raise FixtureError(f"invalid POSIX relative fixture path: {path!r}")
    p = PurePosixPath(path)
    if p.is_absolute() or any(part in (".", "..") for part in path.split("/")) or not p.parts:
        raise FixtureError(f"unsafe relative fixture path: {path!r}")
    if ":" in p.parts[0] or "//" in path:
        raise FixtureError(f"unsafe relative fixture path: {path!r}")
    return p


def detect_sample_format(path: str, declared: str | None = None) -> tuple[str | None, str]:
    """Return (format, evidence). Suffix is only a convention, never a bytes-based claim.

    A '.cfile' is CF32 by *this collection's* source convention, not a global
    guarantee. An unknown extension requires a manifest or explicit format.
    """
    suffix = PurePosixPath(path).suffix.lower()
    hint = KNOWN_SUFFIXES.get(suffix)
    if declared is not None and declared not in BYTES_PER_COMPLEX:
        raise FixtureError(f"unsupported declared format: {declared}")
    if declared and hint and declared != hint:
        raise FixtureError(f"format conflict: {path} implies {hint}, manifest says {declared}")
    if declared:
        return declared, "manifest+suffix" if hint else "manifest"
    return hint, "filename_hint" if hint else "unknown"


def load_manifest(filename: Path = MANIFEST) -> tuple[dict, list[Fixture]]:
    try:
        obj = json.loads(Path(filename).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise FixtureError(f"could not read manifest: {exc}") from exc
    if not isinstance(obj, dict) or obj.get("schema") != SCHEMA:
        raise FixtureError(f"manifest must declare schema {SCHEMA}")
    files = obj.get("files")
    if not isinstance(files, list) or not files:
        raise FixtureError("manifest needs a nonempty files array")
    root_keys = ("fixture_repository", "fixture_branch", "source_repository", "source_commit")
    for key in root_keys:
        if not isinstance(obj.get(key), str) or not obj[key]:
            raise FixtureError(f"manifest missing {key}")
    seen_ids: set[str] = set()
    seen_paths: set[str] = set()
    fixtures: list[Fixture] = []
    for index, row in enumerate(files):
        if not isinstance(row, dict):
            raise FixtureError(f"files[{index}] must be object")
        fid = row.get("id")
        if not isinstance(fid, str) or not re.fullmatch(r"[a-z][a-z0-9-]*", fid):
            raise FixtureError(f"files[{index}] has invalid id")
        if fid in seen_ids:
            raise FixtureError(f"duplicate fixture id: {fid}")
        seen_ids.add(fid)
        rel = row.get("path")
        _relative_path(rel)
        if rel in seen_paths:
            raise FixtureError(f"duplicate fixture path: {rel}")
        seen_paths.add(rel)
        fmt, _ = detect_sample_format(rel, row.get("format"))
        if fmt is None:
            raise FixtureError(f"ambiguous IQ format for {rel}; declare format")
        size = row.get("bytes")
        if type(size) is not int or size <= 0 or size % BYTES_PER_COMPLEX[fmt]:
            raise FixtureError(f"invalid or unaligned byte length for {fid}")
        rate = row.get("sample_rate_hz")
        if type(rate) is not int or rate <= 0:
            raise FixtureError(f"missing/invalid sample rate for {fid}")
        sha1 = row.get("git_blob_sha1")
        sha256 = row.get("sha256")
        if not isinstance(sha1, str) or not HEX40.fullmatch(sha1):
            raise FixtureError(f"missing/invalid pinned Git blob SHA-1 for {fid}")
        if sha256 is not None and (not isinstance(sha256, str) or not HEX64.fullmatch(sha256)):
            raise FixtureError(f"invalid SHA-256 for {fid}")
        protocol = row.get("protocol")
        if not isinstance(protocol, str) or not protocol:
            raise FixtureError(f"missing protocol for {fid}")
        expected = row.get("upstream_expected", {})
        if not isinstance(expected, dict):
            raise FixtureError(f"expected metadata must be object for {fid}")
        source = {
            "fixture_repository": obj["fixture_repository"],
            "fixture_branch": obj["fixture_branch"],
            "source_repository": obj["source_repository"],
            "source_commit": obj["source_commit"],
            "capture_stage": row.get("capture_stage", "channelized_iq"),
        }
        if source["capture_stage"] != "channelized_iq":
            raise FixtureError(f"unsupported capture stage for {fid}: {source['capture_stage']}")
        fixtures.append(Fixture(fid, rel, protocol, fmt, rate, size, sha1, sha256,
                                row.get("origin", "real_air"), source, expected))
    return obj, fixtures


def fixture_path(root: Path, fixture: Fixture) -> Path:
    """Do not follow a fixture symlink outside the data root."""
    root = Path(root).resolve()
    rel = _relative_path(fixture.path)
    candidate = root.joinpath(*rel.parts).resolve()
    try:
        candidate.relative_to(root)
    except ValueError as exc:
        raise FixtureError(f"fixture escapes root: {fixture.path}") from exc
    return candidate


def hashes_for_file(path: Path) -> tuple[int, str, str]:
    """Hash full file streaming; Git blob SHA-1 includes the Git blob header."""
    size = path.stat().st_size
    git = hashlib.sha1()
    git.update(f"blob {size}\0".encode("ascii"))
    sha = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            block = f.read(1024 * 1024)
            if not block:
                break
            git.update(block)
            sha.update(block)
    return size, git.hexdigest(), sha.hexdigest()


def verify_fixture(fixture: Fixture, root: Path) -> dict:
    result = {
        "id": fixture.id, "path": fixture.path, "format": fixture.format,
        "sample_rate_hz": fixture.sample_rate_hz,
        "expected_bytes": fixture.bytes, "expected_samples": fixture.sample_count,
        "status": "missing", "verified": False, "hash_algorithm": "git_blob_sha1",
        "sha256_pinned": fixture.sha256 is not None,
    }
    path = fixture_path(root, fixture)
    if not path.is_file():
        result["error"] = "file not found"
        return result
    size, git_sha, sha = hashes_for_file(path)
    result.update(actual_bytes=size, actual_git_blob_sha1=git_sha, actual_sha256=sha)
    if size != fixture.bytes:
        result.update(status="mismatch", error="byte length mismatch")
    elif size % BYTES_PER_COMPLEX[fixture.format]:
        result.update(status="mismatch", error="unaligned complex sample length")
    elif git_sha != fixture.git_blob_sha1:
        result.update(status="mismatch", error="Git blob SHA-1 mismatch")
    elif fixture.sha256 and sha != fixture.sha256:
        result.update(status="mismatch", error="SHA-256 mismatch")
    else:
        result.update(status="ok", verified=True, actual_samples=size // BYTES_PER_COMPLEX[fixture.format])
    return result


def iter_iq(path: Path, fmt: str, chunk_samples: int = 4096) -> Iterator[list[tuple[float, float]]]:
    """Yield normalized (I,Q) float pairs, in bounded chunks, without changing rate.

    CS16 normalization is division by 32768.0 (max 32767/32768); CF32 is
    untouched. Little-endian I/Q interleave is explicit in both cases.
    """
    if fmt not in BYTES_PER_COMPLEX:
        raise FixtureError(f"unsupported input format: {fmt}")
    if chunk_samples <= 0:
        raise FixtureError("chunk_samples must be positive")
    frame_bytes = BYTES_PER_COMPLEX[fmt]
    layout = "<ff" if fmt == "cf32-le" else "<hh"
    with Path(path).open("rb") as inp:
        header = inp.read(12)
        if header[:4] in (b"RIFF", b"RIFX", b"RF64") or (header[:4] == b"FORM"):
            raise FixtureError("container/WAV detected; these helpers accept headerless IQ only")
        inp.seek(0)
        while True:
            block = inp.read(chunk_samples * frame_bytes)
            if not block:
                break
            if len(block) % frame_bytes:
                raise FixtureError("truncated complex IQ frame")
            decoded = []
            for i, q in struct.iter_unpack(layout, block):
                if fmt == "cs16-le":
                    decoded.append((i / 32768.0, q / 32768.0))
                else:
                    if not (math.isfinite(i) and math.isfinite(q)):
                        raise FixtureError("non-finite CF32 sample")
                    decoded.append((i, q))
            yield decoded


def _quantize_cs16(value: float) -> int:
    if not math.isfinite(value):
        raise FixtureError("non-finite IQ sample cannot be converted")
    return min(32767, max(-32768, int(round(value * 32768.0))))


def encode_iq(samples: list[tuple[float, float]], fmt: str) -> bytes:
    if fmt == "cf32-le":
        if any(not (math.isfinite(i) and math.isfinite(q)) for i, q in samples):
            raise FixtureError("non-finite IQ sample cannot be encoded")
        return b"".join(struct.pack("<ff", i, q) for i, q in samples)
    if fmt == "cs16-le":
        return b"".join(struct.pack("<hh", _quantize_cs16(i), _quantize_cs16(q)) for i, q in samples)
    raise FixtureError(f"unsupported output format: {fmt}")


def replay_iq(fixture: Fixture, root: Path, consumer: Callable[[list[tuple[float, float]]], None],
              chunk_samples: int = 4096) -> dict:
    """Verify integrity before streaming IQ to a host-only consumer callback."""
    check = verify_fixture(fixture, root)
    if not check["verified"]:
        raise FixtureError(f"cannot replay {fixture.id}: {check.get('error', check['status'])}")
    count = 0
    peak = 0.0
    mean_square = 0.0
    total_i = total_q = 0.0
    for chunk in iter_iq(fixture_path(root, fixture), fixture.format, chunk_samples):
        consumer(chunk)
        count += len(chunk)
        for i, q in chunk:
            total_i += i
            total_q += q
            mag_squared = i * i + q * q
            mean_square += mag_squared
            peak = max(peak, math.sqrt(mag_squared))
    if count != fixture.sample_count:
        raise FixtureError(f"sample-count mismatch during replay: {count} != {fixture.sample_count}")
    return {
        "id": fixture.id, "status": "iq_replayed", "decoded": False,
        "source_format": fixture.format, "sample_rate_hz": fixture.sample_rate_hz,
        "samples": count, "duration_s": count / fixture.sample_rate_hz,
        "peak_magnitude": peak, "rms_magnitude": math.sqrt(mean_square / count),
        "mean_i": total_i / count, "mean_q": total_q / count,
        "hash_verified": True, "git_blob_sha1": fixture.git_blob_sha1,
        "note": "IQ transport/statistics only; no protocol synchronization, CRC, FEC, or audio decoding",
    }


def convert_fixture(fixture: Fixture, root: Path, output: Path, target_fmt: str,
                    chunk_samples: int = 4096) -> dict:
    """Hash-gated conversion, exclusive output creation; never overwrites recordings."""
    if target_fmt not in BYTES_PER_COMPLEX:
        raise FixtureError(f"unsupported output format: {target_fmt}")
    if chunk_samples <= 0:
        raise FixtureError("chunk_samples must be positive")
    check = verify_fixture(fixture, root)
    if not check["verified"]:
        raise FixtureError(f"cannot convert {fixture.id}: {check.get('error', check['status'])}")
    output = Path(output)
    # Exports cannot be placed in the committed/test source tree (not even
    # via symlinks) to guard against accidental raw IQ publication.
    try:
        output.resolve().relative_to(HERE)
    except ValueError:
        pass
    else:
        raise FixtureError("IQ output inside tests/digital_voice_replay is forbidden")
    fmt_hint, _ = detect_sample_format(output.name)
    if fmt_hint and fmt_hint != target_fmt:
        raise FixtureError(f"output suffix conflicts with {target_fmt}")
    count = 0
    try:
        with output.open("xb") as out:
            for chunk in iter_iq(fixture_path(root, fixture), fixture.format, chunk_samples):
                out.write(encode_iq(chunk, target_fmt))
                count += len(chunk)
    except Exception:
        # Only delete if our exclusive-create successfully made the file.
        # A pre-existing destination must never be altered.
        if 'out' in locals() and not out.closed:
            out.close()
        if 'out' in locals():
            output.unlink(missing_ok=True)
        raise
    return {
        "id": fixture.id, "from_format": fixture.format, "to_format": target_fmt,
        "sample_rate_hz": fixture.sample_rate_hz, "samples": count,
        "output": str(output), "output_bytes": count * BYTES_PER_COMPLEX[target_fmt],
        "source_hash_verified": True,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    sub = parser.add_subparsers(dest="command", required=True)
    listing = sub.add_parser("list", help="list pinned public metadata (never reads IQ)")
    listing.add_argument("--json", action="store_true")
    verification = sub.add_parser("verify", help="verify size, Git blob SHA-1 and optional SHA-256")
    verification.add_argument("--fixtures-root", required=True, type=Path)
    verification.add_argument("--id", action="append", dest="ids")
    verification.add_argument("--json", action="store_true")
    replay = sub.add_parser("replay", help="hash-gated chunked IQ statistics (NOT a decoder)")
    replay.add_argument("--fixtures-root", required=True, type=Path)
    replay.add_argument("--id", required=True)
    replay.add_argument("--chunk-samples", type=int, default=4096)
    conversion = sub.add_parser("convert", help="convert CF32 <-> CS16 into a NEW local file")
    conversion.add_argument("--fixtures-root", required=True, type=Path)
    conversion.add_argument("--id", required=True)
    conversion.add_argument("--to", required=True, choices=sorted(BYTES_PER_COMPLEX))
    conversion.add_argument("--output", required=True, type=Path)
    conversion.add_argument("--chunk-samples", type=int, default=4096)
    args = parser.parse_args(argv)
    try:
        metadata, fixtures = load_manifest(args.manifest)
        by_id = {f.id: f for f in fixtures}
        if args.command == "list":
            records = [{"id": f.id, "protocol": f.protocol, "path": f.path, "format": f.format,
                        "rate_hz": f.sample_rate_hz, "samples": f.sample_count, "origin": f.origin,
                        "sha256_pinned": f.sha256 is not None} for f in fixtures]
            if args.json:
                print(json.dumps({"source": metadata["fixture_repository"], "fixtures": records}, indent=2))
            else:
                for x in records:
                    print(f"{x['id']:30} {x['format']:8} {x['rate_hz']:6} Hz  {x['samples']:6} samples  {x['protocol']}")
            return 0
        if args.command == "verify":
            if args.ids:
                unknown = set(args.ids) - set(by_id)
                if unknown:
                    raise FixtureError(f"unknown fixture id(s): {', '.join(sorted(unknown))}")
            selected = [f for f in fixtures if not args.ids or f.id in args.ids]
            results = [verify_fixture(f, args.fixtures_root) for f in selected]
            if args.json:
                print(json.dumps({"results": results, "all_verified": all(r["verified"] for r in results)}, indent=2))
            else:
                for r in results:
                    print(f"{r['id']}: {r['status']}" + (f" ({r['error']})" if r.get('error') else ""))
                print(f"Verified: {sum(r['verified'] for r in results)}/{len(results)}")
            return 0 if all(r["verified"] for r in results) else 2
        if args.id not in by_id:
            raise FixtureError(f"unknown fixture id: {args.id}")
        fixture = by_id[args.id]
        if args.command == "replay":
            result = replay_iq(fixture, args.fixtures_root, lambda _: None, args.chunk_samples)
        else:
            result = convert_fixture(fixture, args.fixtures_root, args.output, args.to, args.chunk_samples)
        print(json.dumps(result, indent=2))
        return 0
    except (FixtureError, OSError, OverflowError, struct.error) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
