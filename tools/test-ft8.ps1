$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
& wsl.exe --cd $repoRoot bash ./tools/test-ft8.sh
if ($LASTEXITCODE -ne 0) { throw "FT8 host tests failed with exit code $LASTEXITCODE." }
Write-Host 'FT8 model and Hunter host tests passed (optimized + ASan/LSan/UBSan).'
