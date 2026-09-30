# grok-review.ps1 -- OrcSDR entry point for the global grok-review skill.
#
# The review logic (quick / diff / full / adversary tiers, strict verdict parsing, repo-agnostic prompts) lives in
# the skill so every repository shares one maintained copy:
#     %USERPROFILE%\.claude\skills\grok-review\grok-review.ps1   (see SKILL.md next to it)
#
# OrcSDR's own rules are in .grok\review-context.md, which the script feeds to the reviewer.
#
# Examples (run from any OrcSDR worktree):
#     tools\grok-review.ps1 -Mode diff
#     tools\grok-review.ps1 -Mode full -Branch claude/tab5-keyboard
#     tools\grok-review.ps1 -Mode adversary -Branch claude/tab5-keyboard -Claims "what the change claims"
#     tools\grok-review.ps1 -Mode full -PR 131 -DryRun          # scope and prompt only, no tokens
param([Parameter(ValueFromRemainingArguments = $true)]$Forward)

$skillScript = Join-Path $env:USERPROFILE '.claude\skills\grok-review\grok-review.ps1'
if (-not (Test-Path $skillScript)) {
    Write-Host "The global grok-review skill is not installed at $skillScript." -ForegroundColor Red
    Write-Host 'Install it (or copy it from a machine that has it), then re-run.' -ForegroundColor Red
    exit 5
}
& $skillScript @Forward
exit $LASTEXITCODE
