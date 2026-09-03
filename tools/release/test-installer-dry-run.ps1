#requires -Version 5.1
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$script = Join-Path $root 'install-orcsdr.ps1'
& $script -DryRun -MockHostedLine 'RTL_WIFI_HOSTED host=3.0.6 coprocessor=3.0.6 match=1' | Out-Host
if ($LASTEXITCODE) { throw 'Matched-pair dry run failed.' }
& $script -DryRun -MockHostedLine 'RTL_WIFI_HOSTED host=3.0.6 coprocessor=2.12.6 match=0' | Out-Host
if ($LASTEXITCODE) { throw 'Mismatch-without-update dry run should still flash final P4.' }
& $script -DryRun -UpdateC6 -MockHostedLine 'RTL_WIFI_HOSTED host=3.0.6 coprocessor=3.0.6 match=1' | Out-Host
if ($LASTEXITCODE) { throw 'Guarded C6 update dry run failed.' }
Write-Host 'INSTALLER_DRY_RUN_OK'
