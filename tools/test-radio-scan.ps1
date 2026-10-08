$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
& wsl.exe --cd $repoRoot bash ./tools/test-radio-scan.sh
if ($LASTEXITCODE -ne 0) { throw "Radio scan host tests failed with exit code $LASTEXITCODE." }
Write-Host 'Radio scan, CB scanner, and Airband scanner host tests passed (optimized + ASan/LSan/UBSan).'

# Weather pure-model regression coverage (optimized host builds).
& $Cxx @Common -O2 tests/weather_model_tests.cpp apps/orcsdr-tab5/ui/weather_model.cpp -o "$BuildDir/weather_model_tests.exe"
& "$BuildDir/weather_model_tests.exe"
& $Cxx @Common -O2 tests/weather_noaa_tests.cpp apps/orcsdr-tab5/ui/weather_model.cpp apps/orcsdr-tab5/ui/weather_noaa.cpp -o "$BuildDir/weather_noaa_tests.exe"
& "$BuildDir/weather_noaa_tests.exe"
& $Cxx @Common -O2 tests/weather_report_tests.cpp apps/orcsdr-tab5/ui/weather_model.cpp apps/orcsdr-tab5/ui/weather_report.cpp -o "$BuildDir/weather_report_tests.exe"
& "$BuildDir/weather_report_tests.exe"
& $Cxx @Common -O2 tests/weather_dashboard_state_tests.cpp apps/orcsdr-tab5/ui/weather_model.cpp -o "$BuildDir/weather_dashboard_state_tests.exe"
& "$BuildDir/weather_dashboard_state_tests.exe"
