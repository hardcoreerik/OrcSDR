#!/usr/bin/env python3
"""Download locally and hash-verify NXDN IQ; third-party recordings stay out of Git."""
import argparse
import hashlib
import json
import lzma
from pathlib import Path
import shutil
import sys
import tempfile
from urllib.parse import urlsplit
from urllib.request import Request, urlopen

HERE = Path(__file__).resolve().parent
ASSETS = json.loads((HERE / "manifest.json").read_text())["assets"]
ALLOWED = {"nostr.download", "raw.githubusercontent.com"}

def digests(path):
    size = path.stat().st_size
    sha256 = hashlib.sha256()
    blob = hashlib.sha1(b"blob " + str(size).encode() + b"\0")
    with path.open("rb") as handle:
        for piece in iter(lambda: handle.read(1048576), b""):
            sha256.update(piece)
            blob.update(piece)
    return size, sha256.hexdigest(), blob.hexdigest()

def verify(path, record):
    size, sha, blob = digests(path)
    if size != record["bytes"]:
        raise ValueError("wrong length")
    if record.get("sha256") and record["sha256"] != sha:
        raise ValueError("SHA-256 mismatch")
    if record.get("git_blob_sha1") and record["git_blob_sha1"] != blob:
        raise ValueError("Git blob SHA-1 mismatch")
    return {"id": record["id"], "bytes": size, "sha256": sha, "git_blob_sha1": blob}

def download(record, dest):
    if urlsplit(record["url"]).hostname not in ALLOWED:
        raise ValueError("unapproved host")
    dest.mkdir(parents=True, exist_ok=True)
    output = dest / record["name"]
    if output.exists():
        return verify(output, record)
    with tempfile.TemporaryDirectory(dir=dest) as tmp:
        packed = Path(tmp) / "download"
        req = Request(record["url"], headers={"User-Agent": "OrcSDR-NXDN-fixtures/1"})
        total = 0
        with urlopen(req, timeout=90) as response, packed.open("wb") as fp:
            if urlsplit(response.geturl()).hostname not in ALLOWED:
                raise ValueError("unexpected redirect")
            for part in iter(lambda: response.read(1048576), b""):
                total += len(part)
                if total > 32 * 1048576:
                    raise ValueError("download exceeds size limit")
                fp.write(part)
        unpacked = Path(tmp) / "unpacked"
        if record["compression"] == "xz":
            with lzma.open(packed, "rb") as src, unpacked.open("wb") as dst:
                for part in iter(lambda: src.read(1048576), b""):
                    if dst.tell() + len(part) > record["bytes"]:
                        raise ValueError("decompression exceeds expected length")
                    dst.write(part)
        else:
            unpacked = packed
        result = verify(unpacked, record)
        shutil.move(unpacked, output)
        return result

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--list", action="store_true")
    p.add_argument("--fetch", nargs="*", default=None, metavar="ID")
    p.add_argument("--verify", action="store_true")
    p.add_argument("--dest", type=Path, default=HERE / "data")
    opt = p.parse_args()
    if opt.list:
        for a in ASSETS:
            print(a["id"], a["bytes"], a["format"], a["kind"])
        return 0
    by_id = {a["id"]: a for a in ASSETS}
    selected = list(by_id) if opt.verify or opt.fetch == ["all"] else (opt.fetch or [])
    if not selected:
        p.error("specify --list, --fetch ID|all or --verify")
    report = []
    bad = []
    for key in selected:
        try:
            if key not in by_id:
                raise ValueError("unknown asset")
            a = by_id[key]
            value = (verify(opt.dest / a["name"], a) if opt.verify
                     else download(a, opt.dest))
            print("PASS", key, value["sha256"])
            report.append(value)
        except Exception as exc:
            print("FAIL", key, str(exc), file=sys.stderr)
            bad.append({"id": key, "error": str(exc)})
    (Path.cwd() / "nxdn-assets-report.json").write_text(
        json.dumps({"verified": report, "failures": bad}, indent=2) + "\n")
    with (Path.cwd() / "nxdn-assets.log").open("a") as f:
        f.write(json.dumps({"checked": len(selected), "failures": bad}) + "\n")
    return bool(bad)

if __name__ == "__main__":
    sys.exit(main())
