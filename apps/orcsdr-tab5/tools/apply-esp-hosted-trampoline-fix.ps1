$ErrorActionPreference = 'Stop'
$appRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$repoRoot = (& git -C $appRoot rev-parse --show-toplevel).Trim()
$appRelative = (& git -C $appRoot rev-parse --show-prefix).Trim().TrimEnd('/')

# Applied in order; each is skipped when already present. The component
# manager can re-resolve managed_components/, so this runs after reconfigure.
$patches = @(
  @{ File = 'esp-hosted-trampoline-null-delete.patch'; Name = 'trampoline null-delete' },
  @{ File = 'esp-hosted-detached-task-handle.patch'; Name = 'detached task handle (#103)' },
  @{ File = 'esp-hosted-sdio-unresponsive-failure.patch'; Name = 'SDIO unresponsive failure event (#106)' },
  @{ File = 'esp-hosted-sdio-credit-deadline.patch'; Name = 'SDIO credit elapsed-time deadline' },
  @{ File = 'esp-hosted-sdio-aligned-packet-buffers.patch'; Name = 'SDIO aligned packet buffers' }
)

# The patch set depends on the installed ESP-Hosted version. 3.1.0 fixes the detached-task handle upstream, so
# that patch is dropped, and the two SDIO patches are re-based on the 3.1.0 source.
$hostedManifest = Join-Path $appRoot 'managed_components\espressif__esp_hosted\idf_component.yml'
$hostedVersion = ((Select-String -Path $hostedManifest -Pattern '^version:\s*(\S+)').Matches[0].Groups[1].Value)
if ($hostedVersion -eq '3.1.0') {
  $patches = @(
    @{ File = 'esp-hosted-trampoline-null-delete.patch'; Name = 'trampoline null-delete' },
    @{ File = 'esp-hosted-sdio-unresponsive-failure.patch'; Name = 'SDIO unresponsive failure event (#106)' },
    @{ File = 'esp-hosted-3.1.0-sdio-credit-deadline.patch'; Name = 'SDIO credit elapsed-time deadline (3.1.0)' },
    @{ File = 'esp-hosted-3.1.0-sdio-aligned-packet-buffers.patch'; Name = 'SDIO aligned packet buffers (3.1.0)' }
  )
} elseif ($hostedVersion -ne '3.0.6') {
  throw "No local patch set for ESP-Hosted ${hostedVersion}."
}

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
