$ErrorActionPreference = 'Stop'
$appRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$patch = Join-Path $PSScriptRoot 'patches\esp-hosted-trampoline-null-delete.patch'
$repoRoot = (& git -C $appRoot rev-parse --show-toplevel).Trim()
$appRelative = (& git -C $appRoot rev-parse --show-prefix).Trim().TrimEnd('/')

$priorErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& git -C $repoRoot apply --check --ignore-space-change --directory=$appRelative -- $patch 2>$null
$applyCheckExitCode = $LASTEXITCODE
$ErrorActionPreference = $priorErrorActionPreference
if ($applyCheckExitCode -eq 0) {
  & git -C $repoRoot apply --ignore-space-change --directory=$appRelative -- $patch
  if ($LASTEXITCODE -ne 0) { throw 'Unable to apply the ESP-Hosted trampoline null-delete patch.' }
  exit 0
}

$ErrorActionPreference = 'Continue'
& git -C $repoRoot apply --reverse --check --ignore-space-change --directory=$appRelative -- $patch 2>$null
$reverseCheckExitCode = $LASTEXITCODE
$ErrorActionPreference = $priorErrorActionPreference
if ($reverseCheckExitCode -ne 0) {
  throw 'The installed ESP-Hosted component does not match the trampoline null-delete patch.'
}
