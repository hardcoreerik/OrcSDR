$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
& wsl.exe --cd $repoRoot bash ./tools/test-radio-scan.sh
if ($LASTEXITCODE -ne 0) { throw "Radio scan host tests failed with exit code $LASTEXITCODE." }
Write-Host 'Radio scan host tests passed (optimized + ASan/LSan/UBSan).'
