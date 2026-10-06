param([string]$IdfPath = 'C:\Espressif\frameworks\esp-idf-v5.5.4')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$gitDir = (git -C $repo rev-parse --path-format=absolute --git-common-dir).Trim()
$source = Join-Path (Split-Path $gitDir -Parent) '.orcsdr-cache\hosted-c6\src'
$lock = Get-Content (Join-Path $repo 'tools\release\hosted-c6-release.json') -Raw | ConvertFrom-Json
if (-not (Test-Path (Join-Path $source '.orcsdr-components\esp_hosted'))) { throw 'Pinned ESP-Hosted source cache is unavailable.' }
if ((git -C $source rev-parse HEAD).Trim() -ne $lock.source_revision) { throw 'ESP-Hosted source revision mismatch.' }
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\python_env\idf5.5_py3.14_env'
$env:PATH = "C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64;$env:IDF_PYTHON_ENV_PATH\Scripts;$env:PATH"
. (Join-Path $IdfPath 'export.ps1')
Push-Location $PSScriptRoot
try {
  $componentRoot = Join-Path $source '.orcsdr-components'
  if (-not (Test-Path -LiteralPath 'sdkconfig')) {
    idf.py -B build -D "EXTRA_COMPONENT_DIRS=$componentRoot" set-target esp32c6
    if ($LASTEXITCODE) { throw 'OrcDial C6 configuration failed.' }
  }
  idf.py -B build -D "EXTRA_COMPONENT_DIRS=$componentRoot" build
  if ($LASTEXITCODE) { throw 'OrcDial C6 build failed.' }
} finally { Pop-Location }
