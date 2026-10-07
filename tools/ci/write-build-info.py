#!/usr/bin/env python3
"""Write build-info.json (+ release notes) for a CI channel build of the Tab5 firmware.

  write-build-info.py --channel nightly --build-id "nightly abc1234 2026-10-07" \
      --asset dist/OrcSDR-Tab5-nightly-abc1234.bin [--c6-provenance c6/c6-provenance.json] \
      [--run-url URL] --json dist/build-info.json --notes dist/notes.md
Consumed by theorc.dev (theorc-site/tools/build-fw-site.py). Schema 1; keep fields additive.
"""
import argparse, datetime, hashlib, json, pathlib, re, subprocess

WARNING = ("Nightly build from main. Includes changes headed for the next release. They've passed "
           "testing but haven't had a full release check, so expect the occasional bug.")


def git(*a):
    return subprocess.check_output(["git", *a], text=True).strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--channel", default="nightly")
    ap.add_argument("--build-id", required=True)
    ap.add_argument("--asset", required=True)
    ap.add_argument("--c6-provenance")
    ap.add_argument("--run-url", default="")
    ap.add_argument("--state", default="ok", choices=["ok", "same-as-release"])
    ap.add_argument("--json", required=True)
    ap.add_argument("--notes")
    a = ap.parse_args()

    commit = git("rev-parse", "HEAD")
    base = git("describe", "--tags", "--match", "v[0-9]*", "--abbrev=0", "HEAD")
    changes = []
    for line in git("log", "--first-parent", "--format=%h%x09%s", f"{base}..HEAD").splitlines():
        sha, _, subject = line.partition("\t")
        m = re.match(r"Merge (?:PR|pull request) #(\d+)", subject)
        changes.append({"sha": sha, "subject": subject, "pr": int(m.group(1)) if m else None})
    asset = pathlib.Path(a.asset)
    data = asset.read_bytes()
    info = {
        "schema": 1, "channel": a.channel, "state": a.state, "build_id": a.build_id,
        "commit": commit, "short": commit[:7], "branch": "main" if a.channel == "nightly" else git("rev-parse", "--abbrev-ref", "HEAD"),
        "built_at": datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "commit_date": git("show", "-s", "--format=%cI", "HEAD"),
        "base_tag": base, "base_commit": git("rev-list", "-n1", base),
        "commits_since_tag": int(git("rev-list", "--count", f"{base}..HEAD")),
        "first_parent_since_tag": len(changes), "changes": changes,
        "asset": asset.name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
        "idf": "5.5.4", "target": "esp32p4", "run_url": a.run_url,
    }
    if a.c6_provenance:
        c6 = json.loads(pathlib.Path(a.c6_provenance).read_text())
        info["c6"] = {k: c6.get(k) for k in ("hosted_version", "source_revision", "sha256", "bytes")}
    pathlib.Path(a.json).write_text(json.dumps(info, indent=2) + "\n")

    if a.notes:
        lines = [f"**{WARNING}**", "",
                 f"Build: `{a.build_id}` (commit {commit[:7]}, {len(changes)} merged changes since {base}).",
                 "On the Tab5, Settings > System > BUILD shows this line; please include it in bug reports.", "",
                 "Install from the browser: https://theorc.dev/sdr/nightly/ (keeps settings unless you tick Erase device).",
                 f"Flashing the .bin yourself: it is the complete image for address 0x0 and overwrites saved settings.", "",
                 f"## Changes since {base}", ""]
        lines += [f"- {c['subject']} ({c['sha']})" for c in changes[:50]] or ["- (none)"]
        if len(changes) > 50:
            lines.append(f"- and {len(changes) - 50} more")
        lines += ["", f"Full diff: https://github.com/hardcoreerik/OrcSDR/compare/{base}...{commit}"]
        if a.run_url:
            lines.append(f"Build log: {a.run_url}")
        pathlib.Path(a.notes).write_text("\n".join(lines) + "\n")
    print(json.dumps({k: info[k] for k in ("build_id", "commit", "base_tag", "commits_since_tag", "asset", "sha256")}))


if __name__ == "__main__":
    main()
