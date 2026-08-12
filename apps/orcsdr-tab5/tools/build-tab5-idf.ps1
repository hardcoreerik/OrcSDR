param(
  [string]$IdfId = 'esp-idf-5.5.3'
)

$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
. 'C:\Espressif\Initialize-Idf.ps1' -IdfId $IdfId
Set-Location (Join-Path $PSScriptRoot '..')
idf.py build
exit $LASTEXITCODE
