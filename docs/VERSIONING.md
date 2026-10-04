# OrcSDR versioning and release policy

OrcSDR uses [Semantic Versioning 2.0.0](https://semver.org). This document is the rule set for
choosing a version number, cutting a release, and pinning the `esp_rtl_sdr` driver. The driver has
its own numbers and its own policy ([`esp-rtl-sdr` `docs/VERSIONING.md`](https://github.com/hardcoreerik/esp-rtl-sdr/blob/master/docs/VERSIONING.md));
the two are kept in step by the pinning rule below.

## Format

    vMAJOR.MINOR.PATCH[-STAGE.N]        STAGE = alpha | beta | rc        N = 1, 2, 3, ...

Examples: `v0.3.0-beta.1`, `v0.3.0-beta.2`, `v0.3.0-rc.1`, `v0.3.0`, `v0.3.1`.

- Stages sort `alpha < beta < rc < (no suffix)`. `N` is a separate dotted number, so `beta.10` sorts
  after `beta.9`. Do not glue the number on (`beta7`, `rc3`): a glued number sorts as text and breaks
  at 10.
- The firmware version is the git tag. `apps/orcsdr-tab5/CMakeLists.txt` takes it from `git describe`
  and requires it to start with `vMAJOR.MINOR.PATCH`. A build that is not exactly on a tag reports
  `git describe` output such as `v0.3.0-beta.1-12-gabc1234`.
- The M5Burner bundle, the GitHub release, the release notes file and the tag all carry the same
  string.

## What changes the number

Pick the highest row that applies. When in doubt between two rows, take the higher one.

| Change | Bump |
|---|---|
| Incompatible change to something users or scripts rely on: the serial CLI command set or its output, saved settings / NVS layout, the web console or Android TV protocol, removed features | **MAJOR** (pre-1.0: **MINOR**, listed under *Breaking* in the release notes) |
| New user-visible capability: a new receiver or dashboard, a new band or mode, a new setting or control, a new serial command, a new supported dongle, a driver **minor** bump that exposes new capability | **MINOR** |
| A fix, with no new capability: wrong behaviour, crash, audio or display glitch, performance work, a driver **patch** bump, dependency refresh, documentation of behaviour | **PATCH** |
| Documentation, tests, CI or tooling only | none: goes out with the next release |
| New hardware evidence only, no code change | none: update `PROJECT_STATUS.md` in the next release |

While the major version is 0, the project is pre-1.0: a MINOR release can still change how things work,
and the release notes must say so plainly. The criteria for `1.0.0` have not been defined.

## Stages

| Stage | Meaning | Allowed changes |
|---|---|---|
| `alpha.N` | Incomplete; anything may change | anything |
| `beta.N` | The intended product is usable; behaviour and UX still evolve through validation | features, fixes |
| `rc.N` | The exact stable release is frozen | release-blocking fixes, evidence and release documentation only; any code fix increments `N` |
| none | Stable | new work starts on the next version |

Each published build of the same core version increments `N`. A private or experimental test build is
**not** given a stage or a tag. Identify it by its full source commits, artifact SHA-256 and build
number.

## How the driver is pinned

The dependency is `esp_rtl_sdr` in `apps/orcsdr-tab5/main/idf_component.yml`, locked in
`apps/orcsdr-tab5/dependencies.lock`.

| Build | Pin | Example |
|---|---|---|
| **Development** (feature branches, PRs, `main` between releases) | a driver **branch and commit**: the immutable 40-character SHA, with the branch named in a comment | `# codex/some-driver-branch` |
| **Release** (any tagged OrcSDR build) | a **published driver release**: the SHA of that release's tag, with the release named in a comment | `# esp_rtl_sdr v0.9.1` |

A SHA is used in both cases, never a tag name, because a tag can be moved and a SHA cannot. The
documentation-truth check (`tools/check_documentation_truth.py`) fails if the manifest and lock
disagree or if `PROJECT_STATUS.md`, `architecture.md` and `docs/API_ESP_RTL_SDR.md` do not state the
current pin. Before tagging, verify the pinned SHA is exactly what the driver tag points at:

    git ls-remote https://github.com/hardcoreerik/esp-rtl-sdr.git refs/tags/v0.9.1^{}

The driver's own release comes first: a release cannot pin a driver release that does not exist yet.

### Driver compatibility

| OrcSDR | Pinned driver release | Driver reports |
|---|---|---|
| `v0.3.0-beta.2` | `esp_rtl_sdr v0.9.3` | `0.9.3` |
| `v0.3.0-beta.1` | `esp_rtl_sdr v0.9.1` | `0.9.1` |
| `v0.2.0-beta7` and earlier | untagged driver commits / branches | see that release's notes |

Add a row for every release.

## Where the version lives

Change these in the release-preparation PR:

1. `docs/releases/<tag>.md` — the release notes (see *Release notes* below)
2. `PROJECT_STATUS.md` — the current release line and the evidence tables
3. `docs/VERSIONING.md` — the driver compatibility table above
4. `main/idf_component.yml` and `dependencies.lock` — the driver pin for a release
5. `README.md` and the user guide, if they quote a version

The tag itself is created after the pull request merges, on the merge commit.

## Release notes

Every release has `docs/releases/<tag>.md`, and the GitHub release page carries the same text. They are
written for people, not for tools:

- **Plain language.** Say what changed for someone using the radio, then what changed underneath.
- **Say what went wrong.** List the regressions found and fixed during the cycle, including ones we
  caused ourselves, and the wrong turns that were tried and rejected.
- **Say what is still wrong.** List known issues with links, and what was **not** measured or tested.
- **State the evidence level** with every claim, using the vocabulary in `PROJECT_STATUS.md`. Never write
  "verified" without the device, the setup and a link to the evidence.
- **Thank the people.** Everyone who filed an issue, sent a pull request, tested on their own hardware or
  reviewed code gets named (by GitHub handle) with what they did.
- **Say how it was made:** keep the AI-assistance statement in the README visible, and never present a
  result as measured unless it was.

Release notes and validation reports are immutable dated evidence; `PROJECT_STATUS.md` is the current
truth.

## Cutting a release

1. **Publish the driver release first** if the release needs new driver work (driver `docs/VERSIONING.md`).
2. **Prepare on a branch** `codex/release-<tag>`: release notes, `PROJECT_STATUS.md`, this compatibility
   table, and the driver pin (a release pin). Run `python tools/check_documentation_truth.py`.
3. **Open the PR to `main`.** CI must pass: Documentation Truth, P25 core tests, radio-scan tests. Address
   or triage every review finding; file an issue for anything deferred.
4. **Merge with a merge commit.** Do not squash or rebase: the driver and evidence reference commit ids.
5. **Tag the release commit locally** (annotated: `git tag -a <tag> -m "OrcSDR <tag>"`) but do not push it
   yet. Build from a clean checkout of that exact tag:

       .\tools\release\build-m5burner.ps1
       .\tools\release\test-m5burner-bundle.ps1 -BundlePath .\dist\OrcSDR-Tab5-<tag> -Version <tag>

   Then build and test the settings-safe installer zips from that bundle (they are attached to the release
   in step 7):

       .\tools\release\build-installer.ps1 -Version <tag>
       .\tools\release\test-installer-package.ps1 -Version <tag>

6. **Pass the hardware gate** in [`M5BURNER_HARDWARE_GATE.md`](M5BURNER_HARDWARE_GATE.md) on the exact
   package: private M5Burner listing installed through its share code, then Home, FM audio, RTL-SDR,
   Wi-Fi scan, a saved-profile connection, RF24 and the UI regression. Keep serial logs and photos with
   the tag record.
7. **Push the tag and create the GitHub release** with the release notes as its body and the verified
   artifacts attached (the M5Burner `.bin`, `SHA256SUMS.txt` and the installer zips). How the release is flagged on GitHub
   follows the stage: `alpha` and `rc` are published as **prereleases**; `beta` and stable releases are
   published as normal **releases** (and are **Latest** while they are the newest public one).
   v0.3.0-beta.1 was published this way.
8. **Verify what you published.** Download every attached asset back from the release page and compare
   its SHA-256 with the bundle you tested, and confirm a full-flash image really is a complete image for
   its address. (The original `v0.2.0-beta7` download was an application-only image labelled as a complete
   one; it had to be replaced in place after a user found it. This step exists so that does not repeat.)
9. **Make the M5Burner listing public** (a manual step in the M5Burner account) only after step 8.
10. **Record it:** update `PROJECT_STATUS.md` to say the release is published, and follow
    [`POST_RELEASE_BUG_WORKFLOW.md`](POST_RELEASE_BUG_WORKFLOW.md) for reports.

Never move, delete or re-create a published tag. If a release is wrong, fix forward with the next
number and say so in its notes; replacing an asset in place is a last resort and must be stated in the
release notes.

## Hotfixes

Branch from the release tag (`hotfix/<next-patch-tag>`), open a PR to `main`, and follow the release
steps with the next PATCH number. A hotfix uses a release driver pin if the driver changed.

## Legacy tags

Earlier tags stay as history and are never renamed or moved:

- `v0.2.0-beta.1` to `v0.2.0-beta.5` and `v0.2.0-beta.6` (dotted).
- `v0.2.0-beta.6-multidongle-rc1` to `-rc4`: experimental multi-dongle builds. They used "rc" as a label
  for an experimental package, which conflicts with the meaning above; that naming is retired.
- `v0.2.0-beta7` (glued, no dot).

Under strict SemVer ordering these do not sort perfectly against each other (`beta.N` sorts before
`beta7`). It does not matter going forward: the new scheme starts at `v0.3.0-beta.1`, and `0.3.x` sorts
above all of `0.2.x`.