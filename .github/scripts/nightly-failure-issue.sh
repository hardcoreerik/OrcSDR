#!/usr/bin/env bash
# File or close the single open nightly-failure issue for this workflow run.
#
# The workflow passes the four needed job results. A cancelled result leaves any
# existing issue alone. Any other non-success result opens one issue (or comments
# on the open one). A published nightly, or the gate's deliberate "nothing to
# build" skip, closes that issue. This script never builds or uploads firmware.
set -euo pipefail

export GH_PROMPT_DISABLED=1
export GH_NO_UPDATE_NOTIFIER=1
export GH_PAGER=cat

: "${GH_TOKEN:?}"
: "${GITHUB_REPOSITORY:?}"
: "${GITHUB_RUN_ID:?}"
: "${GITHUB_SERVER_URL:?}"
: "${ASSIGNEE:?}"
: "${GATE_RESULT:?}"
: "${TESTS_RESULT:?}"
: "${BUILD_RESULT:?}"
: "${PUBLISH_RESULT:?}"
: "${NIGHTLY_RUN_URL:?}"

LABEL=${LABEL:-nightly-failure}
NIGHTLY_EVENT=${NIGHTLY_EVENT:-schedule}
NIGHTLY_ATTEMPT=${NIGHTLY_ATTEMPT:-1}
NIGHTLY_SHA=${NIGHTLY_SHA:-}
GATE_BUILD=${GATE_BUILD:-}
GITHUB_WORKFLOW=${GITHUB_WORKFLOW:-Firmware nightly}
MARKER="<!-- nightly-failure-run:${GITHUB_RUN_ID}:${NIGHTLY_ATTEMPT} -->"
export LABEL NIGHTLY_EVENT NIGHTLY_ATTEMPT NIGHTLY_SHA GATE_BUILD GITHUB_WORKFLOW MARKER

mode=leave
outcome=""
saw_cancelled=0
saw_failure=0
for result in "$GATE_RESULT" "$TESTS_RESULT" "$BUILD_RESULT" "$PUBLISH_RESULT"; do
  case "$result" in
    success|skipped) ;;
    cancelled|canceled) saw_cancelled=1 ;;
    *) saw_failure=1 ;;
  esac
done

if [ "$saw_cancelled" -eq 1 ]; then
  mode=cancelled
elif [ "$saw_failure" -eq 1 ]; then
  mode=failure
elif [ "$GATE_RESULT" = success ] && [ "$TESTS_RESULT" = success ] \
  && [ "$BUILD_RESULT" = success ] && [ "$PUBLISH_RESULT" = success ]; then
  mode=resolve
  outcome="The nightly published successfully."
elif [ "$GATE_RESULT" = success ] && [ "$GATE_BUILD" = false ] \
  && [ "$TESTS_RESULT" = skipped ] && [ "$BUILD_RESULT" = skipped ] \
  && [ "$PUBLISH_RESULT" = skipped ]; then
  mode=resolve
  outcome="The nightly skipped on purpose because main did not need a new firmware build."
fi

note="nightly-failure-issue: mode=${mode} gate=${GATE_RESULT} tests=${TESTS_RESULT} build=${BUILD_RESULT} publish=${PUBLISH_RESULT} gate_build=${GATE_BUILD:-unset}"
echo "$note"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
  printf '%s\n' "$note" >>"$GITHUB_STEP_SUMMARY"
fi

if [ "$mode" = cancelled ]; then
  echo "A needed job was cancelled; not opening or closing a nightly-failure issue."
  exit 0
fi
if [ "$mode" = leave ]; then
  echo "This run is not a failure, a published nightly, or a deliberate skip. Leaving the nightly-failure issue unchanged."
  exit 0
fi

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

label_encoded=$(python3 -c 'import urllib.parse,sys; print(urllib.parse.quote(sys.argv[1], safe=""))' "$LABEL")
label_err=$tmpdir/label.err
if gh api --silent "repos/${GITHUB_REPOSITORY}/labels/${label_encoded}" >/dev/null 2>"$label_err"; then
  label_exists=1
elif grep -q 'HTTP 404' "$label_err"; then
  label_exists=0
else
  echo "Could not look up the ${LABEL} label." >&2
  cat "$label_err" >&2
  exit 1
fi

if [ "$label_exists" -eq 0 ] && [ "$mode" = resolve ]; then
  echo "No ${LABEL} label yet; nothing to close."
  exit 0
fi
if [ "$label_exists" -eq 0 ]; then
  gh label create "$LABEL" --repo "$GITHUB_REPOSITORY" \
    --color B60205 \
    --description "Firmware nightly failed; closed automatically when a later nightly is green"
fi

list_json=$tmpdir/issues.json
gh issue list --repo "$GITHUB_REPOSITORY" --state open --label "$LABEL" \
  --limit 100 --json number >"$list_json"
numbers_text=$(python3 -c 'import json,sys; print("\n".join(str(i["number"]) for i in json.load(open(sys.argv[1]))))' "$list_json")
open_issues=()
while IFS= read -r number; do
  [ -n "$number" ] || continue
  open_issues+=("$number")
done <<<"$numbers_text"

if [ "$mode" = resolve ] && [ "${#open_issues[@]}" -eq 0 ]; then
  echo "No open ${LABEL} issue."
  exit 0
fi

write_body() {
  local kind=$1
  JOBS_FILE=${JOBS_FILE:-} LOG_FILE=${LOG_FILE:-} BODY_FILE=$tmpdir/body.md BODY_KIND=$kind \
    python3 - <<'PY'
import json, os, re, sys

kind = os.environ["BODY_KIND"]
assignee = os.environ["ASSIGNEE"]
marker = os.environ["MARKER"]
run_url = os.environ["NIGHTLY_RUN_URL"]
event = os.environ["NIGHTLY_EVENT"]
attempt = os.environ["NIGHTLY_ATTEMPT"]
sha = os.environ.get("NIGHTLY_SHA") or ""
server = os.environ["GITHUB_SERVER_URL"].rstrip("/")
repo = os.environ["GITHUB_REPOSITORY"]
workflow = os.environ.get("GITHUB_WORKFLOW") or "Firmware nightly"
results = (
    f"gate={os.environ['GATE_RESULT']}, tests={os.environ['TESTS_RESULT']}, "
    f"build={os.environ['BUILD_RESULT']}, publish={os.environ['PUBLISH_RESULT']}"
)

def redact(text: str) -> str:
    text = re.sub(r"github_pat_[A-Za-z0-9_]{8,}", "[redacted]", text)
    text = re.sub(r"gh[pousr]_[A-Za-z0-9_]{8,}", "[redacted]", text)
    text = re.sub(r"(?i)(authorization:\s*)\S+", r"\1[redacted]", text)
    text = re.sub(r"(?i)(bearer\s+)\S+", r"\1[redacted]", text)
    return text

lines = [f"@{assignee}", ""]
if kind == "resolve":
    lines.append("The firmware nightly is green again. Closing this issue.")
elif kind == "again":
    lines.append("The firmware nightly failed again.")
else:
    lines.append("The firmware nightly failed.")
lines += [
    "",
    f"- Run: {run_url} (attempt {attempt}, {event})",
    f"- Workflow: {workflow}",
]
if sha:
    lines.append(f"- Commit: [`{sha[:7]}`]({server}/{repo}/commit/{sha})")
lines.append(f"- Jobs: {results}")

if kind == "resolve":
    lines.append(f"- Outcome: {os.environ['NIGHTLY_OUTCOME']}")
else:
    jobs = {"jobs": []}
    jobs_path = os.environ.get("JOBS_FILE") or ""
    if jobs_path:
        try:
            with open(jobs_path, encoding="utf-8") as fh:
                jobs = json.load(fh)
        except (OSError, json.JSONDecodeError):
            jobs = {"jobs": []}
    failed = []
    for job in jobs.get("jobs") or []:
        conclusion = job.get("conclusion") or ""
        if conclusion not in ("failure", "timed_out"):
            continue
        name = job.get("name") or "unknown job"
        steps = [
            step for step in (job.get("steps") or [])
            if (step.get("conclusion") or "") in ("failure", "timed_out")
        ]
        if not steps:
            failed.append(f"- `{name}` (job failed before a step was marked failed)")
        for step in steps:
            failed.append(f"- `{name}` / {step.get('name') or 'unknown step'}")
    if not failed:
        for name, result in (
            ("gate", os.environ["GATE_RESULT"]),
            ("tests", os.environ["TESTS_RESULT"]),
            ("build", os.environ["BUILD_RESULT"]),
            ("publish", os.environ["PUBLISH_RESULT"]),
        ):
            if result not in ("success", "skipped", "cancelled", "canceled"):
                failed.append(f"- `{name}` (step list not available yet; job result `{result}`)")
    lines += ["", "### Failing steps", ""]
    lines.extend(failed or ["- (no failed step was reported)"])

    raw = ""
    log_path = os.environ.get("LOG_FILE") or ""
    if log_path:
        try:
            with open(log_path, encoding="utf-8", errors="replace") as fh:
                raw = fh.read()
        except OSError:
            raw = ""
    raw = raw.replace("\x00", "").replace("\r\n", "\n").replace("\r", "\n")
    raw = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", raw)
    raw = redact(raw)
    raw_lines = raw.splitlines()
    while raw_lines and raw_lines[-1] == "":
        raw_lines.pop()
    truncated = len(raw_lines) > 80
    kept = raw_lines[-80:]
    tail = "\n".join(kept)
    if len(tail) > 4000:
        truncated = True
        tail = tail[-4000:]
        if "\n" in tail:
            tail = tail.split("\n", 1)[1]
    lines += ["", "### Log tail", ""]
    if not tail.strip():
        lines.append("Failed-step logs were not available. The job and step names above are from the run summary.")
    else:
        if truncated:
            lines.append("Last lines of the failed step logs:")
            lines.append("")
        fence = "~~~~"
        while fence in tail or ("`" * len(fence)) in tail:
            fence += "~"
        lines += [fence, tail, fence]
    lines += ["", marker]

path = os.environ["BODY_FILE"]
with open(path, "w", encoding="utf-8") as fh:
    fh.write("\n".join(lines) + "\n")
PY
}

assign_best_effort() {
  local number=$1
  if ! gh issue edit "$number" --repo "$GITHUB_REPOSITORY" --add-assignee "$ASSIGNEE"; then
    echo "::warning::Could not assign ${ASSIGNEE} on issue #${number}. The issue still @mentions them."
  fi
}

if [ "$mode" = resolve ]; then
  export NIGHTLY_OUTCOME="$outcome"
  write_body resolve
  for number in "${open_issues[@]}"; do
    [ -n "$number" ] || continue
    gh issue comment "$number" --repo "$GITHUB_REPOSITORY" --body-file "$tmpdir/body.md"
    assign_best_effort "$number"
    gh issue close "$number" --repo "$GITHUB_REPOSITORY" --reason completed
    echo "Closed nightly-failure issue #${number}"
  done
  exit 0
fi

targets=()
if [ "${#open_issues[@]}" -gt 0 ]; then
  for number in "${open_issues[@]}"; do
    view=$tmpdir/view-${number}.json
    gh issue view "$number" --repo "$GITHUB_REPOSITORY" --json body,comments >"$view"
    if python3 - "$view" "$MARKER" <<'PY'
import json, sys
data = json.load(open(sys.argv[1], encoding="utf-8"))
parts = [data.get("body") or ""]
for comment in data.get("comments") or []:
    parts.append((comment or {}).get("body") or "")
sys.exit(0 if sys.argv[2] in "\n".join(parts) else 1)
PY
    then
      echo "Issue #${number} already records run ${GITHUB_RUN_ID} attempt ${NIGHTLY_ATTEMPT}."
      continue
    fi
    targets+=("$number")
  done
  if [ "${#targets[@]}" -eq 0 ]; then
    echo "Open nightly-failure issue already records this run."
    exit 0
  fi
fi

jobs_file=$tmpdir/jobs.json
log_file=$tmpdir/failed.log
if ! gh run view "$GITHUB_RUN_ID" --repo "$GITHUB_REPOSITORY" --json jobs >"$jobs_file" 2>"$tmpdir/jobs.err"; then
  echo "::warning::Could not read job steps; the issue will list the failed jobs without step names."
  printf '%s\n' '{"jobs":[]}' >"$jobs_file"
fi

log_tries=${NIGHTLY_ALERT_LOG_RETRIES:-4}
log_sleep=${NIGHTLY_ALERT_LOG_SLEEP:-5}
log_ok=0
for ((attempt=1; attempt<=log_tries; attempt++)); do
  if timeout 60 gh run view "$GITHUB_RUN_ID" --repo "$GITHUB_REPOSITORY" --log-failed >"$log_file" 2>"$tmpdir/log.err"; then
    log_ok=1
    break
  fi
  echo "Failed-step log fetch attempt ${attempt} failed." >&2
  if [ "$attempt" -lt "$log_tries" ]; then
    sleep "$log_sleep"
  fi
done
if [ "$log_ok" -eq 0 ]; then
  echo "::warning::Failed-step logs were not available."
  : >"$log_file"
fi

export JOBS_FILE=$jobs_file LOG_FILE=$log_file
if [ "${#open_issues[@]}" -eq 0 ]; then
  write_body new
  if ! url=$(gh issue create --repo "$GITHUB_REPOSITORY" \
      --title "Firmware nightly failed" \
      --label "$LABEL" \
      --assignee "$ASSIGNEE" \
      --body-file "$tmpdir/body.md"); then
    echo "::warning::Could not create the issue assigned to ${ASSIGNEE}; opening it with an @mention only."
    url=$(gh issue create --repo "$GITHUB_REPOSITORY" \
      --title "Firmware nightly failed" \
      --label "$LABEL" \
      --body-file "$tmpdir/body.md")
    number=${url##*/}
    assign_best_effort "$number"
  fi
  case "$url" in
    */issues/[0-9]*) ;;
    *) echo "gh issue create did not return an issue URL: ${url}" >&2; exit 1 ;;
  esac
  echo "Opened ${url}"
  exit 0
fi

write_body again
for number in "${targets[@]}"; do
  gh issue comment "$number" --repo "$GITHUB_REPOSITORY" --body-file "$tmpdir/body.md"
  assign_best_effort "$number"
  echo "Commented on nightly-failure issue #${number}"
done
