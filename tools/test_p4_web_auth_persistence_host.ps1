$ErrorActionPreference = 'Stop'
$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Exe = Join-Path $env:TEMP 'p4_web_auth_persistence_test.exe'
$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$VsRoot = (& $VsWhere -latest -products '*' -property installationPath).Trim()
$VcVars = Join-Path $VsRoot 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $VcVars)) { throw 'Visual Studio x64 compiler environment not found' }
$Includes = '/I "' + (Join-Path $Repo 'tests/web_auth_stubs') + '" /I "' + (Join-Path $Repo 'apps/orcsdr-p4-hosyond-console/main') + '"'
$Sources = '"' + (Join-Path $Repo 'tests/web_auth_persistence_test.cpp') + '" "' + (Join-Path $Repo 'tests/web_auth_persistence_stubs.cpp') + '" "' + (Join-Path $Repo 'apps/orcsdr-p4-hosyond-console/main/web_auth.cpp') + '"'
$Command = 'call "' + $VcVars + '" >nul && cl.exe /nologo /std:c++17 /EHsc /W4 /WX /DORCSDR_WEB_AUTH_HOST_TEST ' + $Includes + ' ' + $Sources + ' /Fe:"' + $Exe + '"'
Push-Location $env:TEMP
& $env:ComSpec /d /s /c $Command
if ($LASTEXITCODE -ne 0) { Pop-Location; exit $LASTEXITCODE }
Pop-Location
& $Exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Exe --legacy
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Exe --corrupt
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Exe --write-before-reboot
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Exe --write-after-reboot
exit $LASTEXITCODE
