$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
& wsl.exe --cd $repoRoot bash ./tools/test-setup-wizard.sh
if ($LASTEXITCODE -ne 0) { throw "Setup wizard host tests failed with exit code $LASTEXITCODE." }
Write-Host 'Setup wizard host tests passed (optimized + ASan/LSan/UBSan).'
