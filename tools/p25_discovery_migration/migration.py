#!/usr/bin/env python3
"""Strict, non-mutating preview of OrcSDR P25 profile.cfg v2 into Radio Discovery.

No firmware imports, no library journal writes, no network or external dependencies.
See README.md for the deliberate user-profile / directory-candidate boundary.
"""
from __future__ import annotations

import argparse
from decimal import Decimal, InvalidOperation
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys
from typing import Any

SCHEMA = "orcsdr.p25-discovery-migration-preview"
VERSION = 1
MIN_HZ, MAX_HZ = 24_000_000, 1_766_000_000
MAX_FILE = 65536
MAX_INPUT_PROFILES = 256  # safety ceiling, not the firmware capacity
SOURCE_PROFILE_LIMIT = 16
SOURCE_CC_LIMIT = 8
SOURCE_TG_LIMIT = 8
NAME_LIMIT = 47     # P25 v2 name[48], and discovery label[48]
ALIAS_LIMIT = 31    # P25 v2 talkgroup alias[32]
ID_PATTERN = re.compile(r"[A-Za-z0-9_-]{1,31}\Z")
NUM_PATTERN = re.compile(r"[0-9]+\Z")

SCALARS = {
    "version", "system_name", "nac", "wacn", "system_id", "rfss", "site",
    "last_control_channel_hz", "auto_follow", "encryption_skip", "modulation",
    "cqpsk_timing_gain", "cqpsk_carrier_gain", "hold_talkgroup",
}
REPEATED = {"control_channel_hz", "talkgroup"}
INTEGER_BOUNDS = {
    "nac": (0, 0xFFF), "wacn": (0, 0xFFFFF), "system_id": (0, 0xFFF),
    "rfss": (0, 255), "site": (0, 255), "hold_talkgroup": (0, 65535),
    "last_control_channel_hz": (0, 0xFFFFFFFF),
}
DEFAULTS: dict[str, Any] = {
    "system_name": "My P25 System", "nac": 0, "wacn": 0, "system_id": 0,
    "rfss": 0, "site": 0, "last_control_channel_hz": 0,
    "auto_follow": True, "encryption_skip": True, "modulation": "auto",
    "cqpsk_timing_gain": 0.005, "cqpsk_carrier_gain": 0.008,
    "hold_talkgroup": 0,
}


class MigrationError(Exception):
    pass


def issue(code: str, message: str, *, line: int | None = None) -> dict[str, Any]:
    item: dict[str, Any] = {"code": code, "message": message}
    if line is not None:
        item["line"] = line
    return item


def number(text: str, key: str, low: int, high: int) -> int:
    if not NUM_PATTERN.fullmatch(text):
        raise ValueError(f"{key} must be an unsigned decimal integer")
    value = int(text)
    if not low <= value <= high:
        raise ValueError(f"{key} outside {low}..{high}")
    return value


def valid_text(value: str, limit: int, key: str) -> str:
    if not value or len(value.encode("utf-8")) > limit or "\x00" in value:
        raise ValueError(f"{key} must have 1..{limit} UTF-8 bytes and no NUL")
    return value


def gain(text: str, key: str) -> float:
    try:
        d = Decimal(text)
    except InvalidOperation as e:
        raise ValueError(f"{key} is not a finite decimal") from e
    high = Decimal("0.05") if key == "cqpsk_timing_gain" else Decimal("0.1")
    if not d.is_finite() or not Decimal("0.0001") <= d <= high:
        raise ValueError(f"{key} outside 0.0001..{high}")
    # Exact spelling retained separately in entries. float is the schema's number type.
    return float(d)


def read_bounded(path: Path) -> bytes | None:
    """Read a single regular file without following a final symlink; None means oversize.

    The additional byte detects growth even after the size check. No silent truncation.
    """
    flags = os.O_RDONLY
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    if hasattr(os, "O_NONBLOCK"):
        flags |= os.O_NONBLOCK
    fd = os.open(path, flags)
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            raise MigrationError("profile input is not a regular file")
        data = b""
        while len(data) <= MAX_FILE:
            block = os.read(fd, MAX_FILE + 1 - len(data))
            if not block:
                return data
            data += block
        return None
    finally:
        os.close(fd)


def reject_duplicate_json_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    obj: dict[str, Any] = {}
    for key, value in pairs:
        if key in obj:
            raise MigrationError(f"existing snapshot has duplicate JSON key {key!r}")
        obj[key] = value
    return obj


def parse_profile(data: bytes) -> dict[str, Any]:
    errors: list[dict[str, Any]] = []
    unsupported: list[dict[str, Any]] = []
    entries: list[dict[str, Any]] = []
    cfg: dict[str, Any] = dict(DEFAULTS)
    cfg.update({"version": None, "control_channels_hz": [], "talkgroups": []})
    if len(data) > MAX_FILE:
        return {"config": cfg, "entries": [], "unsupported_fields": [],
                "errors": [issue("oversized_file", f"source exceeds {MAX_FILE} bytes")],
                "explicit_fields": []}
    try:
        text = data.decode("utf-8-sig")
    except UnicodeDecodeError:
        return {"config": cfg, "entries": [], "unsupported_fields": [],
                "errors": [issue("invalid_utf8", "source is not UTF-8")],
                "explicit_fields": []}
    seen: set[str] = set()
    control_seen: set[int] = set()
    talkgroup_seen: set[int] = set()
    for line_no, raw in enumerate(text.splitlines(), start=1):
        if len(raw.encode("utf-8")) > 127:
            errors.append(issue("line_too_long", "P25 parser supports at most 127 bytes per line", line=line_no))
            continue
        value = raw.strip()
        if not value or value.startswith(("#", ";")):
            continue
        if "=" not in value:
            errors.append(issue("missing_equals", "non-comment line has no =", line=line_no))
            continue
        key, raw_value = (field.strip() for field in value.split("=", 1))
        entries.append({"key": key, "value": raw_value, "line": line_no})
        if key not in SCALARS and key not in REPEATED:
            unsupported.append({"key": key, "value": raw_value, "line": line_no})
            continue
        if key in SCALARS and key in seen:
            errors.append(issue("duplicate_key", f"repeated scalar {key!r} is ambiguous", line=line_no))
            continue
        if key in SCALARS:
            seen.add(key)
        try:
            if key == "version":
                cfg[key] = number(raw_value, key, 2, 2)
            elif key == "system_name":
                cfg[key] = valid_text(raw_value, NAME_LIMIT, key)
            elif key in INTEGER_BOUNDS:
                cfg[key] = number(raw_value, key, *INTEGER_BOUNDS[key])
            elif key in ("auto_follow", "encryption_skip"):
                if raw_value not in ("true", "false", "0", "1"):
                    raise ValueError(f"{key} expected true/false/0/1")
                cfg[key] = raw_value in ("true", "1")
            elif key == "modulation":
                if raw_value not in ("auto", "c4fm", "cqpsk"):
                    raise ValueError("modulation must be auto/c4fm/cqpsk")
                cfg[key] = raw_value
            elif key in ("cqpsk_timing_gain", "cqpsk_carrier_gain"):
                cfg[key] = gain(raw_value, key)
            elif key == "control_channel_hz":
                hz = number(raw_value, key, MIN_HZ, MAX_HZ)
                if hz in control_seen:
                    raise ValueError("duplicate control channel in one profile")
                control_seen.add(hz)
                cfg["control_channels_hz"].append(hz)
                if len(cfg["control_channels_hz"]) > SOURCE_CC_LIMIT:
                    errors.append(issue("source_channel_capacity", "P25 v2 accepts at most 8 control channels", line=line_no))
            elif key == "talkgroup":
                if "," not in raw_value:
                    raise ValueError("talkgroup requires ID,alias")
                raw_id, alias = (part.strip() for part in raw_value.split(",", 1))
                tg = number(raw_id, "talkgroup ID", 1, 65535)
                valid_text(alias, ALIAS_LIMIT, "talkgroup alias")
                if tg in talkgroup_seen:
                    raise ValueError("duplicate talkgroup ID in one profile")
                talkgroup_seen.add(tg)
                cfg["talkgroups"].append({"id": tg, "alias": alias})
                if len(cfg["talkgroups"]) > SOURCE_TG_LIMIT:
                    errors.append(issue("source_talkgroup_capacity", "P25 v2 accepts at most 8 aliases", line=line_no))
        except ValueError as e:
            errors.append(issue("invalid_field", str(e), line=line_no))
    if "version" not in seen:
        errors.append(issue("missing_version", "explicit version=2 is mandatory"))
    if not cfg["control_channels_hz"]:
        errors.append(issue("missing_control_channels", "profile needs at least one control_channel_hz"))
    last = cfg["last_control_channel_hz"]
    if last and last not in cfg["control_channels_hz"]:
        errors.append(issue("orphan_last_channel", "last_control_channel_hz must be in control channel list"))
    return {"config": cfg, "entries": entries, "unsupported_fields": unsupported,
            "errors": errors, "explicit_fields": sorted(seen)}


def source_files(source: Path) -> tuple[list[tuple[str, Path, str]], str | None, list[dict[str, Any]]]:
    """Return (profile id, file, relative POSIX path), active ID, discovery errors."""
    if source.is_symlink():
        raise MigrationError("input root may not be a symlink")
    if source.is_file():
        if source.name != "profile.cfg":
            raise MigrationError("single-file mode requires a file named profile.cfg")
        pid = source.parent.name
        if not ID_PATTERN.fullmatch(pid):
            raise MigrationError("single-file parent is not a valid P25 profile ID")
        return [(pid, source, "profile.cfg")], None, []
    if not source.is_dir():
        raise MigrationError("input must be an existing SD root directory or profile.cfg")
    orcsdr_root = source / "orcsdr"
    if orcsdr_root.is_symlink():
        raise MigrationError("orcsdr may not be a symlink")
    profiles = orcsdr_root / "p25"
    if profiles.is_symlink():
        raise MigrationError("orcsdr/p25 may not be a symlink")
    if not profiles.is_dir():
        legacy_only = orcsdr_root / "P25.cfg"
        if legacy_only.is_file() and not legacy_only.is_symlink():
            return [("legacy-P25", legacy_only, "orcsdr/P25.cfg")], None, []
        raise MigrationError("missing orcsdr/p25 and no readable legacy P25.cfg")
    # Never follow links when walking data. Only direct profile.cfg files are considered.
    directories = sorted(profiles.iterdir(), key=lambda p: p.name)
    if len(directories) > MAX_INPUT_PROFILES:
        raise MigrationError(f"more than {MAX_INPUT_PROFILES} directory entries; refuse partial scan")
    found: list[tuple[str, Path, str]] = []
    problems: list[dict[str, Any]] = []
    for item in directories:
        if item.name == "active.txt":
            continue
        if not ID_PATTERN.fullmatch(item.name):
            problems.append(issue("unsupported_entry", f"unsupported profile directory entry {item.name!r}"))
            continue
        if item.is_symlink():
            problems.append(issue("symlink_blocked", f"profile entry {item.name!r} is a symlink"))
            continue
        if not item.is_dir():
            problems.append(issue("unexpected_entry", f"{item.name!r} is not a profile directory"))
            continue
        cfg = item / "profile.cfg"
        if cfg.is_symlink():
            problems.append(issue("symlink_blocked", f"{item.name!r}/profile.cfg is a symlink"))
            continue
        if not cfg.is_file():
            if (item / "profile.cfg.bak").exists():
                problems.append(issue("backup_only", f"{item.name!r} has a backup but no primary; manual recovery required"))
            else:
                problems.append(issue("missing_profile", f"{item.name!r} has no profile.cfg"))
            continue
        if (item / "profile.cfg.bak").exists():
            problems.append(issue("backup_ignored", f"{item.name!r} has an ignored backup; verify the canonical primary"))
        found.append((item.name, cfg, f"orcsdr/p25/{item.name}/profile.cfg"))
    cases: dict[str, str] = {}
    for pid, _, _ in found:
        folded = pid.casefold()
        if folded in cases:
            problems.append(issue("profile_id_case_collision", f"{pid!r} differs from {cases[folded]!r} only by case; FAT compatibility ambiguous"))
        else:
            cases[folded] = pid
    active: str | None = None
    active_path = profiles / "active.txt"
    if active_path.is_symlink():
        problems.append(issue("symlink_blocked", "active.txt is a symlink"))
    elif active_path.exists():
        if not active_path.is_file() or active_path.stat().st_size > 4096:
            problems.append(issue("invalid_active", "active.txt is not a bounded regular file"))
        else:
            try:
                content = active_path.read_text(encoding="utf-8").strip()
                if not ID_PATTERN.fullmatch(content):
                    raise ValueError("bad active profile ID")
                active = content
            except (UnicodeError, ValueError):
                problems.append(issue("invalid_active", "active.txt has invalid contents"))
    if active is not None and active not in {item[0] for item in found}:
        problems.append(issue("dangling_active", "active profile not found among readable profiles"))
    legacy = orcsdr_root / "P25.cfg"
    if legacy.is_symlink():
        problems.append(issue("symlink_blocked", "legacy P25.cfg is a symlink"))
    elif legacy.is_file():
        if found:
            problems.append(issue("legacy_coexists", "legacy P25.cfg coexists with profile store; it may not be active"))
        found.append(("legacy-P25", legacy, "orcsdr/P25.cfg"))
    return found, active, problems


def identity(config: dict[str, Any], explicit: list[str]) -> dict[str, Any]:
    """Pack the exact P25 identity into the public ProtocolIdentity bitmask.

    1=access_code_known; 4=system_known; 8=site_known in evidence.hpp.
    WACN(20) + SYSID(12) fit exactly into system_id(32);
    RFSS(8) + site(8) fit exactly into site_id(32).
    """
    e = set(explicit)
    mask = 0
    data: dict[str, Any] = {"known_fields": 0}
    if "nac" in e:
        mask |= 1
        data["access_code"] = config["nac"]
    if {"wacn", "system_id"} <= e:
        mask |= 4
        data["system_id"] = (config["wacn"] << 12) | config["system_id"]
    if {"rfss", "site"} <= e:
        mask |= 8
        data["site_id"] = (config["rfss"] << 8) | config["site"]
    data["known_fields"] = mask
    return data


def compatible(a: dict[str, Any], b: dict[str, Any]) -> bool:
    if a["frequency_hz"] != b["frequency_hz"] or a["protocol"] != b["protocol"]:
        return False
    ai, bi = a["identity"], b["identity"]
    for flag, key in (("access_code_known", "access_code"),
                      ("system_known", "system_id"), ("site_known", "site_id")):
        mask = {"access_code_known": 1, "system_known": 4, "site_known": 8}[flag]
        if (ai["known_fields"] & mask) and (bi["known_fields"] & mask) and ai[key] != bi[key]:
            return False
    return True


def same_identity(a: dict[str, Any], b: dict[str, Any]) -> bool:
    return a == b


def digest_json(obj: Any) -> str:
    return hashlib.sha256(json.dumps(obj, sort_keys=True, ensure_ascii=False,
                                     separators=(",", ":")).encode("utf-8")).hexdigest()


def load_existing(path: Path | None) -> list[dict[str, Any]]:
    if path is None:
        return []
    if path.is_symlink() or not path.is_file() or path.stat().st_size > 4 * 1024 * 1024:
        raise MigrationError("existing snapshot must be a bounded regular JSON file")
    try:
        d = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_json_keys)
    except (UnicodeError, ValueError) as e:
        raise MigrationError("existing snapshot is not valid UTF-8 JSON") from e
    if not isinstance(d, dict) or set(d) != {"schema", "version", "records"} or \
            d["schema"] != "orcsdr.radio-discovery-candidates" or d["version"] != 1 or \
            type(d["records"]) is not list or len(d["records"]) > 65535:
        raise MigrationError("existing candidate snapshot schema/version/length is invalid")
    result: list[dict[str, Any]] = []
    allowed_mask = 1 | 4 | 8
    for i, record in enumerate(d["records"], start=1):
        if not isinstance(record, dict) or set(record) != {"protocol", "frequency_hz", "identity", "label", "source_sha256"}:
            raise MigrationError(f"existing record {i}: unsupported fields")
        if record["protocol"] != "p25_phase1" or type(record["frequency_hz"]) is not int or not MIN_HZ <= record["frequency_hz"] <= MAX_HZ:
            raise MigrationError(f"existing record {i}: wrong protocol/frequency")
        iv = record["identity"]
        if not isinstance(iv, dict) or type(iv.get("known_fields")) is not int or \
                iv["known_fields"] & ~allowed_mask:
            raise MigrationError(f"existing record {i}: invalid identity flags")
        expected = {"known_fields"}
        for flag, key, high in ((1, "access_code", 4095),
                                 (4, "system_id", 0xFFFFFFFF),
                                 (8, "site_id", 65535)):
            if iv["known_fields"] & flag:
                expected.add(key)
                if type(iv.get(key)) is not int or not 0 <= iv[key] <= high:
                    raise MigrationError(f"existing record {i}: {key} outside bounds")
        if set(iv) != expected or type(record["label"]) is not str or \
                len(record["label"].encode("utf-8")) > 47 or \
                type(record["source_sha256"]) is not str or not re.fullmatch(r"[0-9a-f]{64}", record["source_sha256"]):
            raise MigrationError(f"existing record {i}: malformed identity/label/source hash")
        for prior in result:
            if (prior["frequency_hz"], prior["protocol"], prior["identity"]) == \
                    (record["frequency_hz"], record["protocol"], record["identity"]):
                raise MigrationError(f"existing record {i}: repeated baseline identity")
        result.append(record)
    return result


def preview(source: Path, directory_capacity: int | None = None,
            existing_count: int | None = None,
            existing_snapshot: Path | None = None) -> dict[str, Any]:
    if (directory_capacity is None) != (existing_count is None):
        raise MigrationError("--directory-capacity and --existing-directory-count must be supplied together")
    if directory_capacity is not None and (not 1 <= directory_capacity <= 65535 or
                                           not 0 <= existing_count <= directory_capacity):
        raise MigrationError("invalid directory capacity/occupied count")
    files, active, discovery_issues = source_files(source)
    if existing_snapshot is not None:
        if existing_snapshot.resolve() == source.resolve():
            raise MigrationError("existing snapshot cannot be the input")
    existing = load_existing(existing_snapshot)
    if existing_count is not None and existing_count < len(existing):
        raise MigrationError("existing-directory-count cannot be less than baseline rows")
    output: dict[str, Any] = {
        "schema": SCHEMA, "version": VERSION,
        "authority": "host_only_read_only_preview_never_apply",
        "source_contract": {
            "repo_path": "apps/orcsdr-tab5/ui/p25_config.hpp",
            "schema_version": 2, "max_profiles": SOURCE_PROFILE_LIMIT,
            "max_control_channels_per_profile": SOURCE_CC_LIMIT,
            "max_talkgroups_per_profile": SOURCE_TG_LIMIT,
        },
        "destination_contract": {
            "model": "components/orcsdr_radio_discovery/include/orcsdr_radio_discovery/library.hpp",
            "collection": "DirectoryRecord candidate", "origin": "directory_import",
            "proof": "candidate", "not_rf_observation": True,
            "directory_capacity": directory_capacity,
            "existing_directory_count_asserted": existing_count,
            "existing_snapshot_rows": len(existing),
            "snapshot_completeness_unverified": True,
        },
        "input": {"mode": "single" if source.is_file() else "sd_root",
                  "active_profile_id": active, "profile_count": len(files),
                  "source_root": "<provided input; absolute host path excluded>"},
        "discovery_issues": discovery_issues, "profiles": [],
        "plan": {"creates": [], "updates": [], "deletes": [],
                 "sidecars": [], "requires_manual_adapter": True, "can_apply": False},
        "summary": {},
    }
    if len(files) > SOURCE_PROFILE_LIMIT:
        discovery_issues.append(issue("source_profile_capacity", f"found {len(files)} profiles; P25 v2 supports at most 16"))
    id_counts: dict[str, int] = {}
    for pid, _, _ in files:
        id_counts[pid.casefold()] = id_counts.get(pid.casefold(), 0) + 1
    flat: list[dict[str, Any]] = []
    for index, (pid, path, relpath) in enumerate(files):
        try:
            data_or_none = read_bounded(path)
            if data_or_none is None:
                data = b""
                parsed = parse_profile(b"x" * (MAX_FILE + 1))
                sha = None
            else:
                data = data_or_none
                parsed = parse_profile(data)
                sha = hashlib.sha256(data).hexdigest()
        except (OSError, MigrationError) as e:
            data = b""
            parsed = {"config": dict(DEFAULTS, version=None, control_channels_hz=[], talkgroups=[]),
                      "entries": [], "explicit_fields": [], "unsupported_fields": [],
                      "errors": [issue("read_error", f"could not read source ({type(e).__name__})")]}
            sha = None
        cfg = parsed["config"]
        errors = list(parsed["errors"])
        if id_counts[pid.casefold()] > 1:
            errors.append(issue("profile_id_collision", "same or case-folded profile ID occurs multiple times"))
        if index >= SOURCE_PROFILE_LIMIT:
            errors.append(issue("source_profile_capacity", "profile is beyond P25 v2 16-profile limit"))
        if parsed["unsupported_fields"]:
            errors.append(issue("unsupported_fields", "unknown P25 v2 keys require review; no migration offered"))
        ident = identity(cfg, parsed["explicit_fields"])
        partial = []
        for group in (("wacn", "system_id"), ("rfss", "site")):
            present = [k in parsed["explicit_fields"] for k in group]
            if any(present) and not all(present):
                partial.append({"fields": list(group), "present": [k for k in group if k in parsed["explicit_fields"]],
                                "reason": "cannot assert a complete discovery identity"})
        if "nac" not in parsed["explicit_fields"]:
            partial.append({"fields": ["nac"], "present": [], "reason": "NAC not explicitly known"})
        semantic = {"name": cfg["system_name"], "config": cfg}
        profile = {
            "id": pid, "source_path": relpath, "source_sha256": sha,
            "source_bytes": len(data) if sha else None, "active": pid == active,
            "parsed": cfg, "explicit_fields": parsed["explicit_fields"],
            "raw_entries": parsed["entries"], "unsupported_fields": parsed["unsupported_fields"],
            "errors": errors, "partial_identity": partial,
            "sidecar": {
                "status": "retained_in_plan_not_representable_in_discovery_v1",
                "source_profile_id": pid, "system_name": cfg["system_name"],
                "raw_p25_identity": {k: cfg[k] for k in ("nac", "wacn", "system_id", "rfss", "site")},
                "talkgroups": cfg["talkgroups"], "control_channels_hz": cfg["control_channels_hz"],
                "last_control_channel_hz": cfg["last_control_channel_hz"],
                "auto_follow": cfg["auto_follow"], "encryption_skip": cfg["encryption_skip"],
                "modulation": cfg["modulation"], "cqpsk_timing_gain": cfg["cqpsk_timing_gain"],
                "cqpsk_carrier_gain": cfg["cqpsk_carrier_gain"],
                "hold_talkgroup": cfg["hold_talkgroup"],
                "active_profile": pid == active,
                "unsupported_fields": parsed["unsupported_fields"],
                "source_path": relpath, "source_sha256": sha,
            }, "records": [],
        }
        output["profiles"].append(profile)
        for cc_index, frequency in enumerate(cfg["control_channels_hz"]):
            model = {
                "candidate": {"protocol": "p25_phase1", "origin": "directory_import",
                              "proof": "candidate", "frequency_hz": frequency, "identity": ident},
                "source": {"name": "Local P25 v2 profile", "url": "", "license": "",
                           "sha256": sha, "sha_known": sha is not None,
                           "retrieved_known": False},
                "label": cfg["system_name"],
            }
            record = {"control_index": cc_index, "status": "new", "issues": [],
                      "semantic_sha256": digest_json(semantic), "model": model}
            if errors:
                record["status"] = "blocked_source"
                record["issues"].append("source_parse_or_unsupported_fields")
            profile["records"].append(record)
            flat.append({"profile": profile, "record": record})
    # Compare every eligible pair. Preserve mutually ambiguous candidates for review,
    # rather than choosing whichever filename appeared first.
    precedence = {"new": 0, "duplicate": 1, "ambiguous": 2, "conflict": 3,
                  "blocked_capacity": 4, "capacity_unverified": 4, "blocked_source": 5}

    def set_status(r: dict[str, Any], status: str, reason: str) -> None:
        if r["status"] == "blocked_source":
            return
        if precedence[status] > precedence[r["status"]]:
            r["status"] = status
        if reason not in r["issues"]:
            r["issues"].append(reason)

    for i, obj in enumerate(flat):
        a = obj["record"]
        if a["status"] == "blocked_source":
            continue
        ai = a["model"]["candidate"]
        for later in flat[i + 1:]:
            b = later["record"]
            if b["status"] == "blocked_source":
                continue
            bi = b["model"]["candidate"]
            if not compatible(ai, bi):
                continue
            if same_identity(ai, bi):
                if a["semantic_sha256"] == b["semantic_sha256"]:
                    set_status(b, "duplicate", f"exact_equivalent_to:{obj['profile']['id']}:{a['control_index']}")
                else:
                    set_status(a, "conflict", f"same_discovery_identity_as:{later['profile']['id']}:{b['control_index']}")
                    set_status(b, "conflict", f"same_discovery_identity_as:{obj['profile']['id']}:{a['control_index']}")
            else:
                set_status(a, "ambiguous", f"overlapping_incomplete_identity:{later['profile']['id']}:{b['control_index']}")
                set_status(b, "ambiguous", f"overlapping_incomplete_identity:{obj['profile']['id']}:{a['control_index']}")

    # Optional existing candidates are read-only metadata; they may be incomplete.
    for obj in flat:
        rec = obj["record"]
        if rec["status"] == "blocked_source":
            continue
        candidate = rec["model"]["candidate"]
        for old_index, old in enumerate(existing, start=1):
            old_candidate = {"protocol": old["protocol"], "frequency_hz": old["frequency_hz"], "identity": old["identity"]}
            if not compatible(candidate, old_candidate):
                continue
            # Existing records aren't assumed identical merely because labels match;
            # source hash is separately required, and settings aren't represented.
            if candidate["identity"] == old_candidate["identity"] and \
                    old["label"] == rec["model"]["label"] and \
                    old["source_sha256"] == obj["profile"]["source_sha256"]:
                set_status(rec, "duplicate", f"existing_candidate_equivalent:{old_index}")
            elif candidate["identity"] == old_candidate["identity"]:
                set_status(rec, "conflict", f"existing_candidate_same_identity:{old_index}")
            else:
                set_status(rec, "ambiguous", f"existing_candidate_overlap:{old_index}")

    available = None if directory_capacity is None else directory_capacity - existing_count
    for obj in flat:
        rec = obj["record"]
        if rec["status"] == "new":
            if available is None:
                set_status(rec, "capacity_unverified", "destination_directory_capacity_not_supplied")
            elif available == 0:
                set_status(rec, "blocked_capacity", "destination_directory_full")
            else:
                available -= 1
                output["plan"]["creates"].append({
                    "profile_id": obj["profile"]["id"],
                    "control_index": rec["control_index"],
                    "model": rec["model"],
                    "source_path": obj["profile"]["source_path"],
                })
    for profile in output["profiles"]:
        output["plan"]["sidecars"].append(profile["sidecar"])
    counts: dict[str, int] = {}
    for obj in flat:
        k = obj["record"]["status"]
        counts[k] = counts.get(k, 0) + 1
    output["summary"] = {
        "source_profiles": len(files), "candidate_channels": len(flat),
        "status_counts": dict(sorted(counts.items())),
        "proposed_directory_creates": len(output["plan"]["creates"]),
        "source_errors": sum(len(p["errors"]) for p in output["profiles"]),
        "directory_issues": len(discovery_issues),
        "lossless_p25_settings_in_plan_only": True,
        "not_ready_to_apply": True,
    }
    # A stable content-addressed plan identifier excludes its own value.
    output["plan_sha256"] = digest_json(output)
    return output


def export_json(plan: dict[str, Any], path: Path | None, input_path: Path,
                existing: Path | None) -> None:
    encoded = json.dumps(plan, indent=2, sort_keys=True, ensure_ascii=False, allow_nan=False) + "\n"
    if path is None:
        sys.stdout.write(encoded)
        return
    source_root = input_path if input_path.is_dir() else input_path.parent
    dest = path.absolute()
    resolved_dest = dest.resolve(strict=False)
    resolved_source = source_root.resolve()
    if (resolved_dest == input_path.resolve() or
            (existing and resolved_dest == existing.resolve()) or
            resolved_dest == resolved_source or resolved_source in resolved_dest.parents):
        raise MigrationError("output must be outside input tree and cannot replace an input or existing snapshot")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        fd = os.open(dest, flags, 0o600)
    except OSError as e:
        raise MigrationError(f"cannot create new output ({type(e).__name__}); existing files are never overwritten") from e
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
    except Exception:
        try:
            dest.unlink()
        except OSError:
            pass
        raise


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Read-only P25 v2 -> Radio Discovery migration preview")
    parser.add_argument("input", type=Path, help="SD root containing orcsdr/p25 or a single profile.cfg")
    parser.add_argument("--directory-capacity", type=int, help="target Library<RF,DIR,NOTE> DIR capacity (explicit)")
    parser.add_argument("--existing-directory-count", type=int, help="known occupied destination directory count")
    parser.add_argument("--existing-candidates", type=Path, help="optional read-only versioned candidate snapshot")
    parser.add_argument("--output", type=Path, help="new JSON plan path OUTSIDE input tree (never overwrite)")
    args = parser.parse_args(argv)
    try:
        result = preview(args.input, args.directory_capacity, args.existing_directory_count,
                         args.existing_candidates)
        export_json(result, args.output, args.input, args.existing_candidates)
    except (MigrationError, OSError) as e:
        print(f"P25 migration preview refused: {e}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
