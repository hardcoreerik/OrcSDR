$ErrorActionPreference = 'Stop'
$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Exe = Join-Path $env:TEMP 'p4_web_control_host_test.exe'
$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $VsWhere)) { throw 'Visual Studio vswhere.exe not found' }
$VsRoot = (& $VsWhere -latest -products '*' -property installationPath).Trim()
if (-not $VsRoot) { throw 'Visual Studio Build Tools not found' }
$VcVars = Join-Path $VsRoot 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $VcVars)) { throw 'Visual Studio x64 compiler environment not found' }

$Includes = '/I "' + (Join-Path $Repo 'tests/web_control_stubs') + '" /I "' + (Join-Path $Repo 'apps/orcsdr-p4-hosyond-console/main') + '"'
$Sources = '"' + (Join-Path $Repo 'tests/p4_web_control_host_test.cpp') + '" "' + (Join-Path $Repo 'apps/orcsdr-p4-hosyond-console/main/web_control.cpp') + '"'
$Command = 'call "' + $VcVars + '" >nul && cl.exe /nologo /std:c++17 /EHsc /W4 /WX /DORCSDR_WEB_CONTROL_HOST_TEST ' + $Includes + ' ' + $Sources + ' /Fe:"' + $Exe + '"'
Push-Location $env:TEMP
& $env:ComSpec /d /s /c $Command
if ($LASTEXITCODE -ne 0) { Pop-Location; exit $LASTEXITCODE }
Pop-Location
& $Exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Output 'P4 web control host seam tests PASS'
