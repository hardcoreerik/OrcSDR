#!/usr/bin/env python3
"""Independently validate published AIS NMEA corpus and export MSB-first payloads.

No test data is vendored into OrcSDR. Source data is downloaded only with
--fetch into the caller's local cache, at an immutable commit and blob hash.
NMEA XOR checksums are *not* substitutes for over-the-air RF CRC checks.
Requires Python 3.9+ and only the standard library.
"""
import argparse
import collections
import hashlib
import json
import pathlib
import re
import urllib.request

_ROOT = pathlib.Path(__file__).resolve().parents[2]
_MANIFEST = pathlib.Path(__file__).with_name("corpus_manifest.json")
_JAPAN = _ROOT / "tests" / "fixtures" / "ais_international" / "japan_research_nmea.tsv"


class BadSentence(ValueError):
    pass


def sentence(line):
    """Return NMEA fragment metadata and its MSB-first payload bits."""
    line = line.strip()
    # AIS Hub includes non-AI two-letter NMEA talker identifiers (BS/AB/AN).
    # VDM/VDO is the payload sentence formatter; talker must not be hardcoded.
    if not re.match(r"^![A-Z0-9]{2}VD[MO],", line):
        raise BadSentence("not a VDM/VDO NMEA sentence")
    if line.count("*") != 1:
        raise BadSentence("missing or repeated checksum delimiter")
    text, expected_hex = line.split("*", 1)
    if len(expected_hex) != 2:
        raise BadSentence("invalid checksum text")
    check = 0
    for ch in text[1:]:
        check ^= ord(ch)
    try:
        expected = int(expected_hex, 16)
    except ValueError as exc:
        raise BadSentence("invalid checksum digits") from exc
    if check != expected:
        raise BadSentence("checksum mismatch")
    cells = text.split(",")
    if len(cells) != 7:
        raise BadSentence("invalid field count")
    try:
        total, number, fill = int(cells[1]), int(cells[2]), int(cells[6])
    except ValueError as exc:
        raise BadSentence("invalid integer field") from exc
    if total < 1 or total > 9 or number < 1 or number > total:
        raise BadSentence("invalid fragment index")
    if fill < 0 or fill > 5 or (number != total and fill != 0):
        raise BadSentence("invalid fill bits")
    if cells[4] not in ("", "A", "B", "1", "2"):
        raise BadSentence("invalid channel")
    bits = ""
    for ch in cells[5]:
        value = ord(ch) - 48
        if value > 40:
            value -= 8
        if value < 0 or value > 63:
            raise BadSentence("invalid AIS armoring")
        bits += f"{value:06b}"
    if fill:
        if len(bits) < fill:
            raise BadSentence("too much padding")
        bits = bits[:-fill]
    return total, number, cells[3], cells[4], bits


def combine(parts):
    """Assemble only a complete row of fragments, without guessing missing ones."""
    if not parts:
        return None
    parsed = [sentence(s) for s in parts]
    total = parsed[0][0]
    if len(parsed) != total:
        return None
    indexes = {part[1] for part in parsed}
    if indexes != set(range(1, total + 1)):
        return None
    if any(p[0] != total or p[2] != parsed[0][2] or p[3] != parsed[0][3]
           for p in parsed):
        raise BadSentence("mismatched multipart identifiers")
    return "".join(p[4] for p in sorted(parsed, key=lambda p: p[1]))


def bits_uint(bits, start, width):
    if start + width > len(bits):
        raise BadSentence("truncated AIS payload")
    return int(bits[start:start + width], 2)


def to_hex(bits):
    return bytes(int(bits[i:i + 8].ljust(8, "0"), 2)
                 for i in range(0, len(bits), 8)).hex()


def check_japan():
    """Validate authentic excerpt without changing published invalid checksums."""
    accepted = rejected = 0
    for row, line in enumerate(_JAPAN.read_text(encoding="utf-8").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        expected, nmea = line.split("\t", 1)
        try:
            b = combine([nmea])
            ok = b is not None and 38 <= len(b) <= 1008
        except BadSentence:
            ok = False
        if expected == "accept":
            if not ok:
                raise AssertionError(f"Japanese fixture unexpectedly rejected at {row}")
            accepted += 1
        elif expected == "reject":
            if ok:
                raise AssertionError(f"Japanese fixture unexpectedly accepted at {row}")
            rejected += 1
        else:
            raise AssertionError(f"unknown Japanese fixture expectation at {row}")
    if accepted < 3 or rejected < 2:
        raise AssertionError("insufficient Japanese research excerpts")
    return {"accepted": accepted, "rejected": rejected}


def check_upstream_bytes(path, sha):
    data = path.read_bytes()
    digest = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
    if digest != sha:
        raise AssertionError(f"upstream Git blob hash mismatch for {path.name}: {digest}")
    return data.decode("utf-8-sig", errors="strict")


def world_rows(filename, content):
    lines = content.splitlines()
    header = lines[0].split("|") if lines and lines[0].startswith("NMEA|") else None
    for index, line in enumerate(lines, 1):
        if header is not None and index == 1:
            continue
        if not line.strip():
            continue
        columns = line.split("|")
        if header is not None:
            headers = {key: i for i, key in enumerate(header) if key != "NMEA"}
            parts = [columns[i] for i, name in enumerate(header)
                     if name == "NMEA" and i < len(columns) and columns[i]]
            def field(key):
                at = headers.get(key)
                return columns[at] if at is not None and at < len(columns) else ""
            msg_type, mmsi = field("MSG"), field("MMSI")
            dac, fi = field("DAC"), field("FID")
        else:
            # 2025 type 25 and 26 files have NO header line.
            parts = [columns[0]]
            msg_type = columns[1] if len(columns) > 1 else ""
            mmsi = columns[3] if len(columns) > 3 else ""
            dac = fi = ""
        yield index, parts, msg_type, mmsi, dac, fi



def stitch_type12(records):
    """Reassemble multipart Type 12 rows by NMEA talker, seq, channel, count.

    The original source keeps 2/3-fragment sentences on separate CSV rows.
    Do not join unkeyed or mismatched fragments. Every source fragment is
    consumed once, with full checksum and fill validation.
    """
    pending = {}
    for row, parts, msg_type, mmsi, dac, fi in records:
        if len(parts) != 1:
            raise AssertionError(f"unexpected Type 12 row at {row}")
        sentence_text = parts[0]
        total, number, seq, channel, _ = sentence(sentence_text)
        if total == 1:
            yield row, parts, msg_type, mmsi, dac, fi
            continue
        talker = sentence_text.split(",", 1)[0]
        key = (talker, total, seq, channel)
        group = pending.setdefault(key, {})
        if number in group:
            raise AssertionError(f"ambiguous repeated Type 12 fragment {key}")
        group[number] = (row, sentence_text, msg_type, mmsi, dac, fi)
        if len(group) == total:
            if set(group) != set(range(1, total + 1)):
                raise AssertionError(f"missing Type 12 fragments {key}")
            final = group[total]
            yield (final[0], [group[n][1] for n in range(1, total + 1)],
                   final[2], final[3], final[4], final[5])
            del pending[key]
    for group in pending.values():
        # Unknown/incomplete messages are reported but never fabricated.
        last = max(group.values(), key=lambda part: part[0])
        yield last[0], [part[1] for part in group.values()], last[2], last[3], last[4], last[5]


def check_world(manifest, cache, out):
    summary = {}
    exported = 0
    with out.open("w", encoding="utf-8", newline="\n") as dest:
        dest.write("type\tmmsi\tdac\tfi\tbits\thex\tsource\trow\n")
        for name, details in manifest["worldwide_2025"]["files"].items():
            content = check_upstream_bytes(cache / name, details["git_blob_sha1"])
            records = complete = incomplete = checked = 0
            records_stream = world_rows(name, content)
            if name == "aishub_msg-12.csv":
                records_stream = stitch_type12(records_stream)
            for row, parts, ex_type, ex_mmsi, ex_dac, ex_fi in records_stream:
                records += len(parts) if name == "aishub_msg-12.csv" else 1
                try:
                    bits = combine(parts)  # Checksums validated even if incomplete.
                except BadSentence as exc:
                    raise AssertionError(f"{name}:{row}: {exc}") from exc
                checked += len(parts)
                if bits is None:
                    incomplete += 1
                    continue
                if len(bits) < 38 or len(bits) > 1008:
                    raise AssertionError(f"{name}:{row}: unexpected AIS message length {len(bits)}")
                typ, mmsi = bits_uint(bits, 0, 6), bits_uint(bits, 8, 30)
                if ex_type and typ != int(ex_type):
                    raise AssertionError(f"{name}:{row}: message type mismatch {typ} != {ex_type}")
                if ex_mmsi and mmsi != int(ex_mmsi):
                    raise AssertionError(f"{name}:{row}: source id mismatch {mmsi} != {ex_mmsi}")
                dac = fi = ""
                if typ in (6, 8):
                    at = 72 if typ == 6 else 40
                    if len(bits) >= at + 16:
                        dac, fi = bits_uint(bits, at, 10), bits_uint(bits, at + 10, 6)
                        if ex_dac and dac != int(ex_dac):
                            raise AssertionError(f"{name}:{row}: DAC mismatch {dac} != {ex_dac}")
                        if ex_fi and fi != int(ex_fi):
                            raise AssertionError(f"{name}:{row}: FI mismatch {fi} != {ex_fi}")
                complete += 1
                dest.write(f"{typ}\t{mmsi}\t{dac}\t{fi}\t{len(bits)}\t"
                           f"{to_hex(bits)}\t{name}\t{row}\n")
                exported += 1
            if records != details["rows"]:
                raise AssertionError(f"{name}: expected {details['rows']} rows, got {records}")
            if complete < details["min_complete"]:
                raise AssertionError(f"{name}: too few complete records: {complete}")
            summary[name] = {"rows": records, "complete": complete,
                             "incomplete": incomplete, "fragments": checked}
    return summary, exported


def fetch(manifest, cache):
    """Explicit opt-in only. Pin both commit and per-file Git blob hash."""
    cache.mkdir(parents=True, exist_ok=True)
    base = manifest["worldwide_2025"]["url_prefix"].rstrip("/")
    for name, details in manifest["worldwide_2025"]["files"].items():
        dest = cache / name
        if dest.exists():
            check_upstream_bytes(dest, details["git_blob_sha1"])
            continue
        if "/" in name or ".." in name:
            raise AssertionError("invalid corpus filename")
        with urllib.request.urlopen(base + "/" + name, timeout=30) as response:
            data = response.read(2 * 1024 * 1024 + 1)
        if len(data) > 2 * 1024 * 1024:
            raise AssertionError(f"{name}: exceeds bounded download cap")
        dest.write_bytes(data)
        check_upstream_bytes(dest, details["git_blob_sha1"])


def export_japan(out):
    """Only checksum-valid excerpts become real parser fixtures."""
    out.parent.mkdir(parents=True, exist_ok=True)
    exported = 0
    with out.open("w", encoding="utf-8", newline="\n") as dest:
        dest.write("type\tmmsi\tdac\tfi\tbits\thex\tsource\trow\n")
        for row, line in enumerate(_JAPAN.read_text(encoding="utf-8").splitlines(), 1):
            if not line or line.startswith("#"):
                continue
            expectation, nmea = line.split("\t", 1)
            if expectation != "accept":
                continue
            bits = combine([nmea])
            typ, mmsi = bits_uint(bits, 0, 6), bits_uint(bits, 8, 30)
            dest.write(f"{typ}\t{mmsi}\t\t\t{len(bits)}\t{to_hex(bits)}\t"
                       f"japan_research_nmea.tsv\t{row}\n")
            exported += 1
    return exported


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--fetch", action="store_true", help="opt-in retrieval of pinned public NMEA source files")
    ap.add_argument("--cache-dir", type=pathlib.Path, default=pathlib.Path("ais-test-runs/corpus"))
    ap.add_argument("--export", type=pathlib.Path, default=pathlib.Path("ais-test-runs/corpus.tsv"))
    ap.add_argument("--japan-only", action="store_true", help="fully offline Japanese excerpt test")
    args = ap.parse_args()
    manifest = json.loads(_MANIFEST.read_text(encoding="utf-8"))
    japan = check_japan()
    print("Japan maritime research NMEA excerpt:", japan)
    if args.japan_only:
        count = export_japan(args.export)
        print(f"exported {count} Japanese research NMEA payloads to {args.export}")
        return
    if args.fetch:
        fetch(manifest, args.cache_dir)
    args.export.parent.mkdir(parents=True, exist_ok=True)
    summary, exported = check_world(manifest, args.cache_dir, args.export)
    print("Worldwide 2025 independent NMEA validation:")
    for name, stats in summary.items():
        print(" ", name, stats)
    print(f"exported {exported} complete NMEA payloads to {args.export}")


if __name__ == "__main__":
    main()
