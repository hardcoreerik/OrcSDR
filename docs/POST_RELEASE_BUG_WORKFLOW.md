# Post-release bug workflow

GitHub Issues is OrcSDR's single bug list. Do not maintain a second list in a document or spreadsheet.

## 1. Record the report

Open one issue for each distinct problem. Include the exact OrcSDR version and release filename, installation method, Tab5 and receiver hardware, dashboard or mode, frequency, antenna when reception is involved, steps, expected result, actual result, and useful logs or screenshots.

If the problem appeared after an update, include the last release known to work. A build completing successfully does not prove that a packaged firmware image works on hardware.

## 2. Triage it

Apply `bug`, then add only labels that are supported by the report:

- `regression`: behavior worked in an earlier release and now fails.
- `release-blocker`: the problem prevents the next release from being published safely.
- `needs-hardware-validation`: code or automated checks cannot establish the result without a physical Tab5, receiver, antenna, or real signal.

Assign the issue to the milestone for the release expected to contain the fix. If that release is not decided, leave the milestone empty instead of guessing.

Record whether the report is reproduced, needs more information, or cannot yet be reproduced. Reception reports must be bounded to the tested receiver, band, antenna, gain, and location; one setup does not establish support or failure everywhere.

## 3. Repair it

Keep the repair narrow and link its pull request to the issue. Do not mix dependency updates, generated data, or unrelated cleanup into the fix. Review comments must be checked against the current code before changes are made.

## 4. Verify the release package

Run the narrow automated regression first. For every firmware-changing repair, install the exact completed `.bin` intended for release and repeat the original steps. Record the packaged image hash, test setup, result, and any remaining limitations.

A source build, successful flash, or development image is not a substitute for testing the packaged release image.

## 5. Close or carry forward

Close the issue only after the repair is verified at the level required by the report. State which release contains the fix and what was tested.

If a serious problem is discovered after publication, add it promptly to that release's Known Issues section and point to the open issue. Remove or mark the warning resolved only when a verified replacement release is available.

Pull requests that are stale, conflicting, or superseded should not be merged merely because their old review passed. Preserve any still-useful fix in a current branch, verify it again, and close the obsolete pull request only with maintainer approval.
