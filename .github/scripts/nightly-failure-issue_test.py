#!/usr/bin/env python3
"""Dry-run the nightly failure issue script against a fake gh CLI.

The fake records every invocation and stores issues in a JSON file. Nothing
here talks to GitHub or builds firmware.
"""

from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SCRIPT = ROOT / "nightly-failure-issue.sh"
WORKFLOW = ROOT.parent / "workflows" / "firmware-nightly.yml"

MOCK_GH = r"""#!/usr/bin/env python3
import json, os, sys
from pathlib import Path

args = sys.argv[1:]
state_path = Path(os.environ["GH_MOCK_STATE"])
log_path = Path(os.environ["GH_MOCK_LOG"])
state = json.loads(state_path.read_text())
with log_path.open("a", encoding="utf-8") as fh:
    fh.write(json.dumps(args) + "\n")

def save():
    state_path.write_text(json.dumps(state))

def die(msg, code=1):
    print(msg, file=sys.stderr)
    raise SystemExit(code)

def option(name):
    if name not in args:
        return None
    i = args.index(name)
    if i + 1 >= len(args):
        die(f"missing value for {name}", 2)
    return args[i + 1]

FLAGS = {
    "--repo", "-R", "--json", "--jq", "--label", "--title", "--body-file",
    "--assignee", "--add-assignee", "--reason", "--color", "--description",
    "--limit", "--state", "-L", "-F", "-l", "-t", "-a", "-b",
}

def positionals(start):
    out = []
    i = start
    while i < len(args):
        if args[i] in FLAGS:
            i += 2
            continue
        if args[i].startswith("-"):
            i += 1
            continue
        out.append(args[i])
        i += 1
    return out

def find_issue(number):
    for issue in state["issues"]:
        if issue["number"] == int(number):
            return issue
    die(f"no issue {number}", 2)

cmd = args[0]
if cmd == "api":
    endpoint = positionals(1)[0]
    name = endpoint.rstrip("/").rsplit("/", 1)[-1]
    if name in state["labels"]:
        print(json.dumps({"name": name}))
        raise SystemExit(0)
    error = state.get("label_http_error") or "gh: Not Found (HTTP 404)"
    die(error, 1)

if cmd == "label" and args[1] == "create":
    name = positionals(1)[1]
    if name not in state["labels"]:
        state["labels"].append(name)
    save()
    print(name)
    raise SystemExit(0)

if cmd == "issue" and args[1] == "list":
    label = option("--label")
    wanted = option("--state") or "open"
    found = []
    for issue in state["issues"]:
        if wanted != "all" and issue["state"] != wanted:
            continue
        if label and label not in issue["labels"]:
            continue
        found.append({"number": issue["number"]})
    print(json.dumps(found))
    raise SystemExit(0)

if cmd == "issue" and args[1] == "view":
    issue = find_issue(positionals(2)[0])
    print(json.dumps({
        "body": issue.get("body") or "",
        "comments": [{"body": comment} for comment in issue.get("comments") or []],
    }))
    raise SystemExit(0)

if cmd == "issue" and args[1] == "create":
    if option("--assignee") and state.get("fail_assignee_on_create"):
        die("assignee rejected", 1)
    body = Path(option("--body-file")).read_text(encoding="utf-8")
    number = state["next"]
    state["next"] = number + 1
    assignees = [option("--assignee")] if option("--assignee") else []
    labels = [option("--label")] if option("--label") else []
    state["issues"].append({
        "number": number,
        "state": "open",
        "title": option("--title"),
        "body": body,
        "labels": labels,
        "assignees": assignees,
        "comments": [],
        "reason": "",
    })
    save()
    repo = os.environ["GITHUB_REPOSITORY"]
    print(f"https://github.com/{repo}/issues/{number}")
    raise SystemExit(0)

if cmd == "issue" and args[1] == "comment":
    issue = find_issue(positionals(2)[0])
    issue["comments"].append(Path(option("--body-file")).read_text(encoding="utf-8"))
    save()
    print(f"https://github.com/{os.environ['GITHUB_REPOSITORY']}/issues/{issue['number']}#issuecomment-{len(issue['comments'])}")
    raise SystemExit(0)

if cmd == "issue" and args[1] == "edit":
    issue = find_issue(positionals(2)[0])
    assignee = option("--add-assignee")
    if assignee:
        if state.get("fail_assignee_on_edit"):
            die("edit assignee rejected", 1)
        if assignee not in issue["assignees"]:
            issue["assignees"].append(assignee)
        save()
    raise SystemExit(0)

if cmd == "issue" and args[1] == "close":
    issue = find_issue(positionals(2)[0])
    issue["state"] = "closed"
    issue["reason"] = option("--reason") or ""
    save()
    raise SystemExit(0)

if cmd == "run" and args[1] == "view":
    if option("--json") == "jobs":
        if state.get("fail_jobs"):
            die("jobs api down", 1)
        print(json.dumps(state.get("run") or {"jobs": []}))
        raise SystemExit(0)
    if "--log-failed" in args:
        left = int(state.get("log_failures_remaining") or 0)
        if left:
            state["log_failures_remaining"] = left - 1
            save()
            die("logs not ready", 1)
        sys.stdout.write(state.get("log") or "")
        raise SystemExit(0)

die("unexpected gh invocation: " + " ".join(args), 2)
"""


def blank_state(**overrides) -> dict:
    state = {
        "labels": [],
        "issues": [],
        "next": 1,
        "run": {"jobs": []},
        "log": "",
        "log_failures_remaining": 0,
        "fail_jobs": False,
        "fail_assignee_on_create": False,
        "fail_assignee_on_edit": False,
        "label_http_error": "",
    }
    state.update(overrides)
    return state


def open_issue(number: int, body: str = "old failure", **overrides) -> dict:
    issue = {
        "number": number,
        "state": "open",
        "title": "Firmware nightly failed",
        "body": body,
        "labels": ["nightly-failure"],
        "assignees": ["hardcoreerik"],
        "comments": [],
        "reason": "",
    }
    issue.update(overrides)
    return issue


FAILED_TESTS_JOBS = {
    "jobs": [
        {
            "name": "tests",
            "conclusion": "failure",
            "steps": [
                {"name": "Radio scan core tests", "conclusion": "success"},
                {"name": "P25 core tests", "conclusion": "failure"},
            ],
        },
        {"name": "build", "conclusion": "skipped", "steps": []},
        {"name": "publish", "conclusion": "skipped", "steps": []},
    ]
}


class NightlyFailureIssueTest(unittest.TestCase):
    def run_alert(self, results: dict, state: dict, extra_env: dict | None = None):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            mock = work / "gh"
            mock.write_text(MOCK_GH, encoding="utf-8")
            mock.chmod(0o755)
            state_path = work / "state.json"
            log_path = work / "gh.log"
            state_path.write_text(json.dumps(state), encoding="utf-8")
            log_path.write_text("", encoding="utf-8")
            env = {
                "PATH": f"{work}:/usr/bin:/bin",
                "GH_MOCK_STATE": str(state_path),
                "GH_MOCK_LOG": str(log_path),
                "GH_TOKEN": "dummy",
                "GITHUB_REPOSITORY": "hardcoreerik/OrcSDR",
                "GITHUB_SERVER_URL": "https://github.com",
                "GITHUB_RUN_ID": "4242",
                "GITHUB_WORKFLOW": "Firmware nightly",
                "ASSIGNEE": "hardcoreerik",
                "LABEL": "nightly-failure",
                "NIGHTLY_SHA": "0123456789abcdef0123456789abcdef01234567",
                "NIGHTLY_RUN_URL": "https://github.com/hardcoreerik/OrcSDR/actions/runs/4242",
                "NIGHTLY_EVENT": "schedule",
                "NIGHTLY_ATTEMPT": "1",
                "NIGHTLY_ALERT_LOG_RETRIES": "1",
                "NIGHTLY_ALERT_LOG_SLEEP": "0",
                "GATE_RESULT": results["gate"],
                "TESTS_RESULT": results["tests"],
                "BUILD_RESULT": results["build"],
                "PUBLISH_RESULT": results["publish"],
                "GATE_BUILD": results.get("gate_build", ""),
            }
            if extra_env:
                env.update(extra_env)
            completed = subprocess.run(
                ["bash", str(SCRIPT)],
                env=env,
                text=True,
                capture_output=True,
                check=False,
            )
            saved = json.loads(state_path.read_text(encoding="utf-8"))
            calls = [json.loads(line) for line in log_path.read_text(encoding="utf-8").splitlines() if line.strip()]
            return completed, saved, calls

    def test_failure_opens_one_assigned_issue_with_run_step_and_log_tail(self):
        log = "\n".join(f"line-{i:03d}" for i in range(200))
        log += "\nerror: boom\nAuthorization: Bearer ghp_ABCDEFGHIJKLMNOPQRSTUVWXYZ1234\n"
        completed, state, calls = self.run_alert(
            {"gate": "success", "tests": "failure", "build": "skipped", "publish": "skipped", "gate_build": "true"},
            blank_state(run=FAILED_TESTS_JOBS, log=log),
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(state["labels"], ["nightly-failure"])
        self.assertEqual(len(state["issues"]), 1)
        issue = state["issues"][0]
        self.assertEqual(issue["state"], "open")
        self.assertEqual(issue["title"], "Firmware nightly failed")
        self.assertEqual(issue["assignees"], ["hardcoreerik"])
        self.assertEqual(issue["comments"], [])
        body = issue["body"]
        self.assertIn("@hardcoreerik", body)
        self.assertIn("https://github.com/hardcoreerik/OrcSDR/actions/runs/4242", body)
        self.assertIn("`tests` / P25 core tests", body)
        self.assertNotIn("Radio scan core tests", body)
        self.assertIn("error: boom", body)
        self.assertIn("line-199", body)
        self.assertNotIn("line-000", body)
        self.assertIn("Last lines of the failed step logs:", body)
        self.assertNotIn("ghp_ABCDEFGHIJKLMNOPQRSTUVWXYZ1234", body)
        self.assertIn("[redacted]", body)
        self.assertIn("<!-- nightly-failure-run:4242:1 -->", body)
        self.assertIn("[`0123456`](https://github.com/hardcoreerik/OrcSDR/commit/0123456789abcdef0123456789abcdef01234567)", body)
        creates = [call for call in calls if call[:2] == ["issue", "create"]]
        self.assertEqual(len(creates), 1)
        self.assertIn("--assignee", creates[0])
        self.assertNotIn("mode=resolve", completed.stdout)
        self.assertIn("mode=failure", completed.stdout)

    def test_log_fence_grows_past_tildes_in_the_log(self):
        completed, state, _calls = self.run_alert(
            {"gate": "failure", "tests": "skipped", "build": "skipped", "publish": "skipped", "gate_build": ""},
            blank_state(
                log="~~~~\nsecret-fence-line\n",
                run={"jobs": [{"name": "gate", "conclusion": "failure", "steps": []}]},
            ),
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        body = state["issues"][0]["body"]
        self.assertIn("secret-fence-line", body)
        self.assertIn("~~~~~", body)
        self.assertIn("`gate` (job failed before a step was marked failed)", body)

    def test_second_failure_comments_instead_of_opening_another_issue(self):
        state = blank_state(
            labels=["nightly-failure"],
            issues=[open_issue(7, body="first report")],
            next=8,
            run=FAILED_TESTS_JOBS,
            log="second boom\n",
        )
        completed, saved, calls = self.run_alert(
            {"gate": "success", "tests": "failure", "build": "skipped", "publish": "skipped", "gate_build": "true"},
            state,
            {"GITHUB_RUN_ID": "5252", "NIGHTLY_RUN_URL": "https://github.com/hardcoreerik/OrcSDR/actions/runs/5252", "NIGHTLY_ATTEMPT": "1"},
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(len(saved["issues"]), 1)
        issue = saved["issues"][0]
        self.assertEqual(issue["state"], "open")
        self.assertEqual(len(issue["comments"]), 1)
        self.assertIn("failed again", issue["comments"][0])
        self.assertIn("https://github.com/hardcoreerik/OrcSDR/actions/runs/5252", issue["comments"][0])
        self.assertIn("@hardcoreerik", issue["comments"][0])
        self.assertIn("<!-- nightly-failure-run:5252:1 -->", issue["comments"][0])
        self.assertFalse(any(call[:2] == ["issue", "create"] for call in calls))

    def test_same_run_does_not_comment_twice(self):
        marker = "<!-- nightly-failure-run:4242:1 -->"
        state = blank_state(
            labels=["nightly-failure"],
            issues=[open_issue(7, body=f"already filed\n{marker}")],
            next=8,
            log="should not be fetched\n",
        )
        completed, saved, calls = self.run_alert(
            {"gate": "success", "tests": "failure", "build": "skipped", "publish": "skipped", "gate_build": "true"},
            state,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(saved["issues"][0]["comments"], [])
        self.assertFalse(any(call[:2] == ["run", "view"] for call in calls))
        self.assertIn("already records", completed.stdout)

    def test_comments_on_every_open_issue_and_ignores_closed_ones(self):
        state = blank_state(
            labels=["nightly-failure"],
            issues=[
                open_issue(4),
                open_issue(9, state="closed"),
                open_issue(5, assignees=[]),
            ],
            next=10,
            log="again\n",
        )
        completed, saved, calls = self.run_alert(
            {"gate": "success", "tests": "success", "build": "failure", "publish": "skipped", "gate_build": "true"},
            state,
            {"GITHUB_RUN_ID": "6000", "NIGHTLY_RUN_URL": "https://github.com/hardcoreerik/OrcSDR/actions/runs/6000"},
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        by_number = {issue["number"]: issue for issue in saved["issues"]}
        self.assertEqual(len(by_number[4]["comments"]), 1)
        self.assertEqual(len(by_number[5]["comments"]), 1)
        self.assertEqual(by_number[9]["comments"], [])
        self.assertEqual(by_number[9]["state"], "closed")
        self.assertIn("hardcoreerik", by_number[5]["assignees"])
        self.assertEqual(len(saved["issues"]), 3)
        self.assertFalse(any(call[:2] == ["issue", "create"] for call in calls))

    def test_publish_success_closes_the_open_issue(self):
        state = blank_state(
            labels=["nightly-failure"],
            issues=[open_issue(7), open_issue(8)],
            next=9,
        )
        completed, saved, calls = self.run_alert(
            {"gate": "success", "tests": "success", "build": "success", "publish": "success", "gate_build": "true"},
            state,
            {"NIGHTLY_EVENT": "workflow_dispatch"},
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertTrue(all(issue["state"] == "closed" and issue["reason"] == "completed" for issue in saved["issues"]))
        for issue in saved["issues"]:
            self.assertEqual(len(issue["comments"]), 1)
            comment = issue["comments"][0]
            self.assertIn("@hardcoreerik", comment)
            self.assertIn("green again", comment)
            self.assertIn("https://github.com/hardcoreerik/OrcSDR/actions/runs/4242", comment)
            self.assertIn("attempt 1, workflow_dispatch", comment)
            self.assertIn("The nightly published successfully.", comment)
        self.assertFalse(any(call[0] == "run" for call in calls))
        self.assertIn("mode=resolve", completed.stdout)

    def test_deliberate_skip_closes_with_skip_outcome(self):
        state = blank_state(labels=["nightly-failure"], issues=[open_issue(3)], next=4)
        completed, saved, _calls = self.run_alert(
            {"gate": "success", "tests": "skipped", "build": "skipped", "publish": "skipped", "gate_build": "false"},
            state,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        issue = saved["issues"][0]
        self.assertEqual(issue["state"], "closed")
        self.assertIn("skipped on purpose because main did not need a new firmware build", issue["comments"][0])

    def test_green_run_with_no_issue_is_a_no_op(self):
        completed, saved, calls = self.run_alert(
            {"gate": "success", "tests": "success", "build": "success", "publish": "success", "gate_build": "true"},
            blank_state(labels=["nightly-failure"]),
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(saved["issues"], [])
        self.assertFalse(any(call[:2] == ["issue", "create"] for call in calls))
        self.assertIn("No open nightly-failure issue", completed.stdout)

    def test_resolve_does_not_create_the_label(self):
        completed, saved, calls = self.run_alert(
            {"gate": "success", "tests": "skipped", "build": "skipped", "publish": "skipped", "gate_build": "false"},
            blank_state(),
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(saved["labels"], [])
        self.assertFalse(any(call[:2] == ["label", "create"] for call in calls))

    def test_skipped_build_after_a_failed_test_stays_a_failure(self):
        completed, saved, _calls = self.run_alert(
            {"gate": "success", "tests": "failure", "build": "skipped", "publish": "skipped", "gate_build": "true"},
            blank_state(labels=["nightly-failure"], issues=[open_issue(1)], next=2, log="nope\n"),
            {"GITHUB_RUN_ID": "77", "NIGHTLY_RUN_URL": "https://github.com/hardcoreerik/OrcSDR/actions/runs/77"},
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(saved["issues"][0]["state"], "open")
        self.assertIn("mode=failure", completed.stdout)
        self.assertNotIn("Closing this issue", saved["issues"][0]["comments"][0])

    def test_timeout_counts_as_failure(self):
        completed, saved, _calls = self.run_alert(
            {"gate": "success", "tests": "success", "build": "timed_out", "publish": "skipped", "gate_build": "true"},
            blank_state(log=""),
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        body = saved["issues"][0]["body"]
        self.assertIn("job result `timed_out`", body)
        self.assertIn("Failed-step logs were not available", body)

    def test_cancelled_run_does_not_open_or_close(self):
        state = blank_state(labels=["nightly-failure"], issues=[open_issue(7)], next=8)
        completed, saved, calls = self.run_alert(
            {"gate": "success", "tests": "success", "build": "cancelled", "publish": "skipped", "gate_build": "true"},
            state,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(saved["issues"][0]["state"], "open")
        self.assertEqual(saved["issues"][0]["comments"], [])
        self.assertEqual(calls, [])
        self.assertIn("mode=cancelled", completed.stdout)

    def test_weird_skip_leaves_the_issue_open(self):
        state = blank_state(labels=["nightly-failure"], issues=[open_issue(7)], next=8)
        completed, saved, calls = self.run_alert(
            {"gate": "success", "tests": "skipped", "build": "skipped", "publish": "skipped", "gate_build": "true"},
            state,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(saved["issues"][0]["state"], "open")
        self.assertEqual(saved["issues"][0]["comments"], [])
        self.assertEqual(calls, [])
        self.assertIn("mode=leave", completed.stdout)

    def test_missing_logs_still_open_the_issue(self):
        completed, saved, calls = self.run_alert(
            {"gate": "success", "tests": "success", "build": "success", "publish": "failure", "gate_build": "true"},
            blank_state(log_failures_remaining=3, fail_jobs=True),
            {"NIGHTLY_ALERT_LOG_RETRIES": "2"},
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        body = saved["issues"][0]["body"]
        self.assertIn("`publish` (step list not available yet; job result `failure`)", body)
        self.assertIn("Failed-step logs were not available", body)
        self.assertGreaterEqual(sum(1 for call in calls if call[:3] == ["run", "view", "4242"] and "--log-failed" in call), 2)

    def test_assignee_failure_still_opens_an_issue_that_mentions_the_owner(self):
        completed, saved, calls = self.run_alert(
            {"gate": "failure", "tests": "skipped", "build": "skipped", "publish": "skipped"},
            blank_state(fail_assignee_on_create=True, log="gate exploded\n"),
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        issue = saved["issues"][0]
        self.assertEqual(issue["assignees"], ["hardcoreerik"])
        self.assertIn("@hardcoreerik", issue["body"])
        creates = [call for call in calls if call[:2] == ["issue", "create"]]
        self.assertEqual(len(creates), 2)
        self.assertIn("--assignee", creates[0])
        self.assertNotIn("--assignee", creates[1])
        self.assertTrue(any(call[:2] == ["issue", "edit"] for call in calls))

    def test_assignment_failure_does_not_drop_the_mention(self):
        completed, saved, _calls = self.run_alert(
            {"gate": "failure", "tests": "skipped", "build": "skipped", "publish": "skipped"},
            blank_state(fail_assignee_on_create=True, fail_assignee_on_edit=True, log="gate exploded\n"),
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        issue = saved["issues"][0]
        self.assertEqual(issue["assignees"], [])
        self.assertIn("@hardcoreerik", issue["body"])
        self.assertIn("Could not assign hardcoreerik", completed.stderr + completed.stdout)

    def test_label_lookup_error_fails_the_alert(self):
        completed, saved, calls = self.run_alert(
            {"gate": "failure", "tests": "skipped", "build": "skipped", "publish": "skipped"},
            blank_state(label_http_error="gh: Internal Server Error (HTTP 500)"),
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertEqual(saved["issues"], [])
        self.assertFalse(any(call[:2] == ["issue", "create"] for call in calls))

    def test_workflow_wires_the_alert_without_changing_the_build(self):
        text = WORKFLOW.read_text(encoding="utf-8")
        self.assertEqual(text.count("bash tools/ci/build-tab5.sh"), 1)
        self.assertEqual(text.count("bash tools/ci/build-hosted-c6.sh"), 1)
        self.assertEqual(text.count("gh release upload"), 2)
        self.assertIn("espressif/idf:v5.5.4@", text)
        self.assertEqual(text.count("\n      contents: write\n"), 1)
        self.assertEqual(text.count("\n      issues: write\n"), 1)
        self.assertIn('if: "${{ always() && !cancelled() && (github.event_name == \'schedule\' || github.event_name == \'workflow_dispatch\') }}"', text)
        self.assertEqual(text.count("needs: [gate, tests, build, publish]"), 1)
        self.assertIn("ASSIGNEE: hardcoreerik", text)
        self.assertIn("LABEL: nightly-failure", text)
        self.assertIn("bash .github/scripts/nightly-failure-issue.sh", text)
        self.assertIn("actions: read", text)
        self.assertLess(text.index("name: Notify theorc.dev"), text.index("name: Update nightly-failure issue"))
        self.assertLess(text.index("permissions:\n      contents: write"), text.index("permissions:\n      actions: read"))
        publish_job = text.split("  publish:", 1)[1].split("  alert:", 1)[0]
        self.assertNotIn("nightly-failure", publish_job)
        self.assertIn('name: nightly\n          path: dist/', text)
        self.assertIn("repository_dispatch", text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
