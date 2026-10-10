$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$runner = Join-Path $repo 'apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($runner, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw "Runner parse failed: $errors" }
foreach ($name in @('ConvertFrom-DriverStatus', 'Assert-DriverRegressionStatus', 'Get-DriverRegressionFrequencies', 'Assert-DriverRegressionContinuity')) {
  $function = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $true)
  if (!$function) { throw "Missing function: $name" }
  Invoke-Expression $function.Extent.Text
}
$nooelec = @(Get-DriverRegressionFrequencies 3)
foreach ($frequency in @(24000, 1766000000)) {
  if (($nooelec | Where-Object Frequency -eq $frequency).Accepted -ne $false) { throw "Nooelec range incorrectly accepts $frequency" }
}
foreach ($frequency in @(100000, 1750000000)) {
  if (($nooelec | Where-Object Frequency -eq $frequency).Accepted -ne $true) { throw "Nooelec range incorrectly rejects $frequency" }
}
if (@(Get-DriverRegressionFrequencies 1 | Where-Object { !$_.Accepted }).Count) { throw 'Blog V4 range changed.' }
foreach ($profile in 1..4) {
  $line = "RTL_DRIVER_STATUS installed=1 version=9.9.9-test state=STREAMING profile=$profile profile_name=`"test`" provisional=0 device_caps=0x001fff7f library_caps=0x001fff7f delivery=callback gain_auto_cap=1 rtl_agc_cap=1 gain_cap=1 bias_cap=1 mode=MANUAL gain_tenth_db=297 rtl_agc=0 bias=0 bytes=123456 blocks=42 effective_sps=2400000 overruns=0 drops=0 shadow_ok=1 metrics_ok=1 frequency_hz=96100000 frequency_ok=1 route=TUNER"
  $status = ConvertFrom-DriverStatus $line
  $selfCheck = "RTL_DRIVER_SELF_CHECK pass=1 version=9.9.9-test profile=$profile device_caps=0x001fff7f required=0x00000001"
  Assert-DriverRegressionStatus $selfCheck $status
  foreach ($bad in @($selfCheck.Replace('pass=1', 'pass=0'), $selfCheck.Replace('required=0x00000001', 'required=0x80000000'), $selfCheck.Replace("profile=$profile", 'profile=99'), 'malformed')) {
    $rejected = $false
    try { Assert-DriverRegressionStatus $bad $status } catch { $rejected = $true }
    if (!$rejected) { throw "Accepted invalid self-check: $bad" }
  }
  foreach ($field in @('ShadowOk', 'MetricsOk', 'FrequencyOk', 'Bytes', 'EffectiveSps', 'GainCap', 'GainAutoCap', 'RtlAgcCap', 'BiasCap')) {
    $old = $status.$field; $status.$field = 0
    $rejected = $false
    try { Assert-DriverRegressionStatus $selfCheck $status } catch { $rejected = $true }
    $status.$field = $old
    if (!$rejected) { throw "Accepted unhealthy status: $field" }
  }
  $previous = [pscustomobject]@{ Bytes = 123455 }
  Assert-DriverRegressionContinuity $status $previous 'test'
  foreach ($field in @('EffectiveSps', 'ShadowOk', 'MetricsOk', 'FrequencyOk')) {
    $old = $status.$field; $status.$field = 0
    $rejected = $false
    try { Assert-DriverRegressionContinuity $status $previous 'test' } catch { $rejected = $true }
    $status.$field = $old
    if (!$rejected) { throw "Accepted unhealthy transition: $field" }
  }
  $status.State = 'IDLE'
  $rejected = $false
  try { Assert-DriverRegressionContinuity $status $previous 'test' } catch { $rejected = $true }
  if (!$rejected) { throw 'Accepted stopped stream with accumulated bytes.' }
  $status.State = 'STARTING'
  $rejected = $false
  try { Assert-DriverRegressionStatus $selfCheck $status } catch { $rejected = $true }
  if (!$rejected) { throw 'Accepted a receiver that is not streaming.' }
}
$release = Get-Content (Join-Path $repo 'apps/orcsdr-tab5/tools/run-release-readiness.ps1') -Raw
if ($release -notmatch "'-DriverRegression'") { throw 'Release runner does not call the generic driver gate.' }
$gate = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Invoke-DriverRegressionTest' }, $true).Extent.Text
if ($gate.IndexOf("Send-And-Wait 'RTL_DRIVER SELF_CHECK'") -lt $gate.IndexOf('Driver test requires active IQ streaming')) {
  throw 'Driver self-check runs before USB streaming is ready.'
}
# Exercise the actual gate with deterministic API responses, including restoration rejection.
Invoke-Expression $gate
function Wait-DeviceReady {}
function Connect-Authenticated {}
function Start-Sleep {}
function Write-SoakLine([string]$Line) { $script:gateLines.Add($Line) }
function Get-DriverStatus {
  $script:mockStatus.Bytes += 1000
  return $script:mockStatus.PSObject.Copy()
}
function Send-And-Wait([string]$Command, [string]$Pattern) {
  if ($Command -eq 'RTL_DRIVER SELF_CHECK') {
    return 'RTL_DRIVER_SELF_CHECK pass=1 version=9.9.9-test profile=1 device_caps=0x001fff7f required=0x00000001'
  }
  if ($Command -match '^RTL_DRIVER TUNE (\d+)$') {
    $frequency = [uint32]$Matches[1]
    if ($script:rejectRestore -and $frequency -eq 94497000) {
      return 'RTL_DRIVER_RESULT accepted=0 result=ESP_RTL_SDR_ERR_NOT_STREAMING'
    }
    $script:mockStatus.Frequency = $frequency
    $script:mockStatus.Route = if ($frequency -lt 24000000) { 'DIRECT_Q' } elseif ($frequency -lt 28800000) { 'HF_UPCONVERTER' } else { 'TUNER' }
  } elseif ($Command -match '^RTL_DRIVER GAINMODE (\S+)$') {
    $script:mockStatus.Mode = $Matches[1]
  } elseif ($Command -match '^RTL_DRIVER GAIN (\d+)$') {
    $script:mockStatus.Gain = [int]$Matches[1]
  } elseif ($Command -match '^RTL_DRIVER RTLAGC (ON|OFF)$') {
    $script:mockStatus.RtlAgc = [int]($Matches[1] -eq 'ON')
  } else { throw "Unexpected test command: $Command" }
  return 'RTL_DRIVER_RESULT accepted=1 result=ESP_OK'
}
$TestBiasTee = $false
foreach ($reject in @($false, $true)) {
  $script:rejectRestore = $reject
  $script:gateLines = [Collections.Generic.List[string]]::new()
  $script:mockStatus = ConvertFrom-DriverStatus 'RTL_DRIVER_STATUS installed=1 version=9.9.9-test state=STREAMING profile=1 profile_name="test" provisional=0 device_caps=0x001fff7f library_caps=0x001fff7f delivery=callback gain_auto_cap=1 rtl_agc_cap=1 gain_cap=1 bias_cap=1 mode=MANUAL gain_tenth_db=14 rtl_agc=0 bias=0 bytes=1000 blocks=1 effective_sps=2400000 overruns=0 drops=0 shadow_ok=1 metrics_ok=1 frequency_hz=94497000 frequency_ok=1 route=TUNER'
  $failed = $false
  try { Invoke-DriverRegressionTest } catch {
    if (!$reject -or $_.Exception.Message -notmatch 'restoration rejected') { throw }
    $failed = $true
  }
  $passed = @($script:gateLines | Where-Object { $_ -match '^RTL_DRIVER_REGRESSION_RESULT pass=1' }).Count -eq 1
  if ($reject -and (!$failed -or $passed)) { throw 'Rejected restoration still passes the gate.' }
  if (!$reject -and (!$passed -or $script:mockStatus.Frequency -ne 94497000 -or $script:mockStatus.Gain -ne 14 -or $script:mockStatus.Mode -ne 'MANUAL')) { throw 'Successful gate did not restore settings.' }
}
Write-Host 'test-driver-regression-runner: PASS'
