$ErrorActionPreference = 'Stop'
$appRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$patch = Join-Path $PSScriptRoot 'patches\m5gfx-tab5-pageflip.patch'
$repoRoot = (& git -C $appRoot rev-parse --show-toplevel).Trim()
$appRelative = (& git -C $appRoot rev-parse --show-prefix).Trim().TrimEnd('/')

$priorErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& git -C $repoRoot apply --check --ignore-space-change --directory=$appRelative -- $patch 2>$null
$applyCheckExitCode = $LASTEXITCODE
$ErrorActionPreference = $priorErrorActionPreference
if ($applyCheckExitCode -eq 0) {
  & git -C $repoRoot apply --ignore-space-change --directory=$appRelative -- $patch
  if ($LASTEXITCODE -ne 0) { throw 'Unable to apply the M5GFX Tab5 page-flip patch.' }
  exit 0
}

$ErrorActionPreference = 'Continue'
& git -C $repoRoot apply --reverse --check --ignore-space-change --directory=$appRelative -- $patch 2>$null
$reverseCheckExitCode = $LASTEXITCODE
$ErrorActionPreference = $priorErrorActionPreference
if ($reverseCheckExitCode -ne 0) {
  throw 'The installed M5GFX component does not match the Tab5 page-flip patch.'
}
