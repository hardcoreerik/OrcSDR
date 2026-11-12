# Run from any cwd. Saves test logs in the calling directory.
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Build = Join-Path ([System.IO.Path]::GetTempPath()) 'orcsdr-dmr-device-probe'
foreach ($Mode in @('release', 'asan')) {
  $Args = @('-S', $Root, '-B', "$Build-$Mode", '-DDMR_DEVICE_PROBE_WITH_DECODER=OFF')
  if ($Mode -eq 'asan') { $Args += '-DDMR_DEVICE_PROBE_SANITIZERS=ON'; $Args += '-DCMAKE_BUILD_TYPE=Debug' }
  else { $Args += '-DCMAKE_BUILD_TYPE=Release' }
  & cmake @Args; if ($LASTEXITCODE -ne 0) { throw "cmake configure $Mode failed" }
  & cmake --build "$Build-$Mode" --config Debug --parallel
  if ($LASTEXITCODE -ne 0) { throw "cmake build $Mode failed" }
  $Log = Join-Path (Get-Location) "dmr_probe_$Mode.log"
  & ctest --test-dir "$Build-$Mode" --output-on-failure --verbose *>&1 | Out-File -FilePath $Log
  if ($LASTEXITCODE -ne 0) { throw "ctest $Mode failed; see $Log" }
  Write-Host "PASS $Mode; log: $Log"
}
