$ErrorActionPreference = 'Stop'
$appRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$repoRoot = (& git -C $appRoot rev-parse --show-toplevel).Trim()
$appRelative = (& git -C $appRoot rev-parse --show-prefix).Trim().TrimEnd('/')

# Applied in order; each is skipped when already present. The component
# manager can re-resolve managed_components/, so this runs after reconfigure.
$patches = @(
  @{ File = 'esp-hosted-trampoline-null-delete.patch'; Name = 'trampoline null-delete' },
  @{ File = 'esp-hosted-detached-task-handle.patch'; Name = 'detached task handle (#103)' },
  @{ File = 'esp-hosted-sdio-unresponsive-failure.patch'; Name = 'SDIO unresponsive failure event (#106)' }
)

foreach ($entry in $patches) {
  $patch = Join-Path $PSScriptRoot (Join-Path 'patches' $entry.File)

  $priorErrorActionPreference = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  & git -C $repoRoot apply --check --ignore-space-change --directory=$appRelative -- $patch 2>$null
  $applyCheckExitCode = $LASTEXITCODE
  $ErrorActionPreference = $priorErrorActionPreference
  if ($applyCheckExitCode -eq 0) {
    & git -C $repoRoot apply --ignore-space-change --directory=$appRelative -- $patch
    if ($LASTEXITCODE -ne 0) { throw "Unable to apply the ESP-Hosted $($entry.Name) patch." }
    continue
  }

  $ErrorActionPreference = 'Continue'
  & git -C $repoRoot apply --reverse --check --ignore-space-change --directory=$appRelative -- $patch 2>$null
  $reverseCheckExitCode = $LASTEXITCODE
  $ErrorActionPreference = $priorErrorActionPreference
  if ($reverseCheckExitCode -ne 0) {
    throw "The installed ESP-Hosted component does not match the $($entry.Name) patch."
  }
}
