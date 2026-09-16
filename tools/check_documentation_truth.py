#!/usr/bin/env python3
"""Deterministic, standard-library checks for high-confidence documentation drift."""

from __future__ import annotations

import argparse
import dataclasses
import json
import os
import re
import sys
from pathlib import Path
from urllib.parse import unquote


CURRENT_DRIVER_DOCS = ("PROJECT_STATUS.md", "architecture.md", "docs/API_ESP_RTL_SDR.md")
CURRENT_DOCS = (
    "README.md",
    "PROJECT_STATUS.md",
    "architecture.md",
    "Roadmap.md",
    "SECURITY.md",
    "apps/orcsdr-tab5/README.md",
    "docs/API_ESP_RTL_SDR.md",
    "docs/TAB5_BUILD_POLICY.md",
    "docs/RADIO_CONFIGURATION.md",
)
HISTORICAL_DOCS = (
    "docs/OrcSDR TV Mission-Control Experience.md",
    "docs/P25_PHASE2_WORKPHASE.md",
    "docs/M5TAB5_VALIDATION_REPORT.md",
    "docs/M5TAB5_INTEGRATION.md",
    "docs/M5TAB5_RTL_RADIO_NEXT_STEPS.md",
    "docs/ESP32_RTL_SDR_DRIVER_PLAN.md",
    "docs/IMPLEMENTATION_FROM_PEER_RESEARCH.md",
    "docs/PEER_USB_PIPELINE_DEEP_DIVE.md",
    "docs/GATE2_IMPLEMENTATION_LOCK.md",
    "docs/recordings/2026-08-08-last-recording-analysis.md",
)
PROMPT_PATTERNS = (
    re.compile(r"\byour job is to\b", re.I),
    re.compile(r"\byou are (?:codex|claude)\b", re.I),
    re.compile(r"\bimplement the following\b", re.I),
    re.compile(r"<instructions?>", re.I),
)


@dataclasses.dataclass(frozen=True)
class Diagnostic:
    level: str
    code: str
    path: str
    line: int
    message: str


@dataclasses.dataclass
class Report:
    errors: list[Diagnostic] = dataclasses.field(default_factory=list)
    warnings: list[Diagnostic] = dataclasses.field(default_factory=list)
    passes: list[str] = dataclasses.field(default_factory=list)

    def add(self, level: str, code: str, path: str, line: int, message: str) -> None:
        item = Diagnostic(level, code, path, line, message)
        (self.errors if level == "ERROR" else self.warnings).append(item)


def _text(root: Path, relative: str) -> str | None:
    path = root / relative
    return path.read_text(encoding="utf-8") if path.is_file() else None


def _line(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def _current_markdown(root: Path) -> list[Path]:
    paths = [root / item for item in CURRENT_DOCS]
    guide = root / "docs/user-guide"
    if guide.is_dir():
        paths.extend(guide.rglob("*.md"))
    return sorted({path for path in paths if path.is_file()})


def _dependency_pin(root: Path, report: Report) -> str | None:
    manifest_path = "apps/orcsdr-tab5/main/idf_component.yml"
    manifest = _text(root, manifest_path)
    if manifest is None:
        report.add("ERROR", "driver-pin", manifest_path, 1, "dependency manifest is missing")
        return None
    block = re.search(r"esp[-_]rtl[-_]sdr\s*:\s*(.*?)(?=\n\s{0,2}\S|\Z)", manifest, re.S)
    match = re.search(r"version\s*:\s*[\"']?([0-9a-f]{40})", block.group(1) if block else "")
    if not match:
        report.add("ERROR", "driver-pin", manifest_path, 1, "immutable esp-rtl-sdr SHA not found")
        return None
    pin = match.group(1)
    lock_path = "apps/orcsdr-tab5/dependencies.lock"
    lock = _text(root, lock_path)
    if lock is not None and not re.search(rf"version\s*:\s*[\"']?{pin}\b", lock):
        report.add("ERROR", "driver-pin", lock_path, 1, f"lock file does not contain manifest pin {pin}")
    return pin


def _check_driver(root: Path, report: Report) -> None:
    pin = _dependency_pin(root, report)
    if pin is None:
        return
    for relative in CURRENT_DRIVER_DOCS:
        text = _text(root, relative)
        if text is None:
            report.add("ERROR", "driver-pin", relative, 1, "authoritative driver document is missing")
        elif pin not in text:
            report.add("ERROR", "driver-pin", relative, 1, f"current immutable pin {pin} is not documented")
        else:
            stale = re.search(r"current[^\n]{0,80}(?:v?0\.7\.9)|(?:v?0\.7\.9)[^\n]{0,80}current", text, re.I)
            if stale:
                report.add("ERROR", "driver-pin", relative, _line(text, stale.start()), "stale current driver version claim")
    if not any(item.code == "driver-pin" for item in report.errors):
        report.passes.append(f"Current esp-rtl-sdr pin documented correctly ({pin})")


def _check_ci_claims(root: Path, report: Report) -> None:
    workflows = list((root / ".github/workflows").glob("*.y*ml"))
    if not workflows:
        return
    pattern = re.compile(r"\b(?:orcsdr\s+has\s+)?no\s+(?:github actions\s+)?ci\b", re.I)
    for path in _current_markdown(root):
        text = path.read_text(encoding="utf-8")
        for match in pattern.finditer(text):
            report.add("ERROR", "no-ci", path.relative_to(root).as_posix(), _line(text, match.start()), "unqualified no-CI claim conflicts with existing workflows")
    if not any(item.code == "no-ci" for item in report.errors):
        report.passes.append(f"CI description matches {len(workflows)} workflow files")


def _check_measurement(root: Path, report: Report) -> None:
    source_path = root / "apps/orcsdr-tab5/ui/main.cpp"
    architecture = _text(root, "architecture.md")
    if not source_path.is_file() or architecture is None:
        report.add("ERROR", "main-measurement", "architecture.md", 1, "main.cpp or architecture measurement is missing")
        return
    data = source_path.read_bytes().replace(b"\r\n", b"\n")
    actual_bytes, actual_lines = len(data), len(data.splitlines())
    match = re.search(r"main\.cpp measurement \(Git-normalized\):\s*([\d,]+) bytes\s*\(~[\d.]+ KiB\),\s*([\d,]+) lines", architecture, re.I)
    if not match:
        report.add("ERROR", "main-measurement", "architecture.md", 1, "standard main.cpp measurement line is missing")
        return
    documented_bytes = int(match.group(1).replace(",", ""))
    documented_lines = int(match.group(2).replace(",", ""))
    byte_drift = abs(actual_bytes - documented_bytes) / max(actual_bytes, 1) * 100
    line_drift = abs(actual_lines - documented_lines) / max(actual_lines, 1) * 100
    detail = (f"actual {actual_bytes:,} bytes ({actual_bytes / 1024:.1f} KiB), {actual_lines:,} lines; "
              f"documented {documented_bytes:,} bytes, {documented_lines:,} lines; "
              f"drift {byte_drift:.1f}% bytes/{line_drift:.1f}% lines")
    if max(byte_drift, line_drift) > 5:
        report.add("ERROR", "main-measurement", "architecture.md", _line(architecture, match.start()), detail)
    elif max(byte_drift, line_drift) > 3:
        report.add("WARNING", "main-measurement", "architecture.md", _line(architecture, match.start()), detail)
    else:
        report.passes.append(f"main.cpp architecture measurement ({detail})")


def _enum_values(text: str) -> list[str] | None:
    match = re.search(r"enum class Id\s*:\s*\w+\s*\{(.*?)\}", text, re.S)
    if not match:
        return None
    return [item.strip().split("=")[0].strip() for item in match.group(1).split(",") if item.strip()]


def _documented_ids(text: str, label: str) -> list[str] | None:
    match = re.search(rf"^{re.escape(label)}:\s*(.+)$", text, re.M)
    return re.findall(r"`([a-z0-9_]+)`", match.group(1)) if match else None


def _check_screens(root: Path, report: Report) -> None:
    architecture = _text(root, "architecture.md")
    if architecture is None:
        return
    contracts = (
        ("apps/orcsdr-tab5/ui/screen_controller.hpp", "ScreenController IDs", set()),
        ("apps/orcsdr-tab5/ui/dashboard_registry.hpp", "Dashboard IDs", {"count"}),
    )
    for source, label, excluded in contracts:
        source_text = _text(root, source)
        actual = _enum_values(source_text or "")
        documented = _documented_ids(architecture, label)
        if actual is None or documented is None:
            report.add("WARNING", "screen-drift", "architecture.md", 1, f"could not compare {label}")
            continue
        actual_set = set(actual) - excluded
        documented_set = set(documented)
        if actual_set != documented_set:
            missing = sorted(actual_set - documented_set)
            removed = sorted(documented_set - actual_set)
            report.add("ERROR", "screen-drift", "architecture.md", 1, f"{label} mismatch; missing={missing}, removed={removed}")
    if not any(item.code == "screen-drift" for item in report.errors + report.warnings):
        report.passes.append("Screen and dashboard IDs match source enums")


def _check_links(root: Path, report: Report) -> None:
    link_re = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
    for path in _current_markdown(root):
        text = path.read_text(encoding="utf-8")
        for match in link_re.finditer(text):
            raw = match.group(1).strip().split()[0].strip("<>")
            if not raw or raw.startswith(("#", "http://", "https://", "mailto:")):
                continue
            target = unquote(raw.split("#", 1)[0].split("?", 1)[0])
            resolved = (root / target.lstrip("/")) if target.startswith("/") else (path.parent / target)
            if not resolved.exists():
                report.add("ERROR", "local-link", path.relative_to(root).as_posix(), _line(text, match.start()), f"local link target does not exist: {raw}")
    if not any(item.code == "local-link" for item in report.errors):
        report.passes.append("Repository-local Markdown links resolve")


def _check_file_references(root: Path, report: Report) -> None:
    pattern = re.compile(r"`((?:apps|docs|tests|tools|\.github)/[^`*{}<>]+\.[A-Za-z0-9]+)`")
    for path in _current_markdown(root):
        text = path.read_text(encoding="utf-8")
        for match in pattern.finditer(text):
            reference = match.group(1)
            if not (root / reference).exists():
                report.add("ERROR", "file-reference", path.relative_to(root).as_posix(), _line(text, match.start()), f"referenced repository file does not exist: {reference}")
    if not any(item.code == "file-reference" for item in report.errors):
        report.passes.append("High-confidence documented file references resolve")


def _check_resolved_claims(root: Path, report: Report) -> None:
    guards = {
        "docs/user-guide/settings.md": (r"does not accept tune or volume commands",),
        "docs/user-guide/downloads.md": (r"3\.0\.6 migration is not yet a release acceptance path",),
        "PROJECT_STATUS.md": (r"no message has yet been confirmed as a real, over-the-air POCSAG decode",),
        "README.md": (r"retain decoded messages in a local archive", r"dedicated workspace for receiving and inspecting satellite signals"),
        "Roadmap.md": (r"no public catalog release or hardware install evidence yet",),
    }
    for relative, patterns in guards.items():
        text = _text(root, relative)
        if text is None:
            continue
        for pattern in patterns:
            match = re.search(pattern, text, re.I)
            if match:
                report.add("ERROR", "resolved-claim", relative, _line(text, match.start()), "known obsolete current-state claim resurfaced")
    if not any(item.code == "resolved-claim" for item in report.errors):
        report.passes.append("Resolved high-risk claims remain absent")


def _check_prompt_residue(root: Path, report: Report) -> None:
    for path in _current_markdown(root):
        text = path.read_text(encoding="utf-8")
        for pattern in PROMPT_PATTERNS:
            match = pattern.search(text)
            if match:
                report.add("WARNING", "prompt-residue", path.relative_to(root).as_posix(), _line(text, match.start()), f"possible prompt residue: {match.group(0)}")


def _check_history_labels(root: Path, report: Report) -> None:
    for relative in HISTORICAL_DOCS:
        text = _text(root, relative)
        if text is not None and not re.search(r"Historical|Superseded", text[:800], re.I):
            report.add("WARNING", "history-label", relative, 1, "historical document lacks a visible historical/superseded notice")
    history = root / "docs/history"
    if history.is_dir():
        for path in history.rglob("*.md"):
            text = path.read_text(encoding="utf-8")
            if not re.search(r"Historical|Superseded", text[:800], re.I):
                report.add("WARNING", "history-label", path.relative_to(root).as_posix(), 1, "docs/history file lacks a visible historical/superseded notice")


def run_checks(root: Path) -> Report:
    root = root.resolve()
    report = Report()
    for check in (_check_driver, _check_ci_claims, _check_measurement, _check_links,
                  _check_file_references, _check_screens, _check_resolved_claims,
                  _check_prompt_residue, _check_history_labels):
        check(root, report)
    return report


def _render(report: Report) -> str:
    lines = ["OrcSDR Documentation Truth Check", "================================"]
    lines.extend(f"PASS  {message}" for message in report.passes)
    for item in report.warnings:
        lines.append(f"WARN  {item.path}:{item.line}: {item.message}")
    for item in report.errors:
        lines.append(f"ERROR {item.path}:{item.line}: {item.message}")
    lines.extend(("", f"{len(report.errors)} errors", f"{len(report.warnings)} warnings"))
    return "\n".join(lines)


def _github_output(report: Report, rendered: str) -> None:
    for item in report.errors + report.warnings:
        level = "error" if item.level == "ERROR" else "warning"
        print(f"::{level} file={item.path},line={item.line}::{item.message}")
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as handle:
            handle.write("```text\n" + rendered + "\n```\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    report = run_checks(args.root)
    rendered = _render(report)
    if args.json:
        print(json.dumps(dataclasses.asdict(report), indent=2))
    else:
        print(rendered)
    if os.environ.get("GITHUB_ACTIONS") == "true":
        _github_output(report, rendered)
    return 1 if report.errors else 0


if __name__ == "__main__":
    sys.exit(main())
