# One-command OrcSDR support report

**Firmware support:** `RTL_SUPPORT REPORT` is available only in OrcSDR firmware
that includes the support-report feature. Older release images do not recognize it.
`RTL_HELP` lists the command on supported builds.

## One line in a serial console (Windows/macOS/Linux)

Connect a computer to the **Tab5's PC-facing USB Serial/JTAG** port (not the
RTL-SDR's USB host port). Open that serial port in a terminal at 115200 baud,
8N1, then paste:

```text
RTL_SUPPORT REPORT
```

Copy everything from `ORC_SUPPORT_BEGIN` through `ORC_SUPPORT_END` and
paste it into a GitHub issue, Discord message, or email. The device does not
need Wi-Fi or authentication for the report and does not restart the receiver.
The report is read-only: it does not start streaming, reset a driver, change a
frequency, alter settings, or format storage.

The report includes firmware/build and SDR driver versions, USB VID/PID,
detected profile, driver state/last error, radio state, stream metrics, heap/DMA
headroom and reset reason. It **does not intentionally include** USB serial
numbers, MAC addresses, location data, SSIDs, credentials, IP addresses or
pairing keys. Review any report before posting it publicly.

If the report shows `state="FAULT"` or `last_error="ESP_RTL_SDR_ERR_FAULT"`,
the *earliest serial error from startup* is often needed to determine the
underlying cause. Keep a full serial log too when possible.

## Windows: PowerShell only, no Python or downloaded script

For a Windows user who does not already have a serial console, they can
paste this directly into PowerShell. It detects the Tab5's native USB
Serial/JTAG interface, requests the report, prints it and copies it to the
clipboard. **No external downloads or Python packages.** Disconnect any other
serial monitor first.

```powershell
$ports = @(Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -match 'VID_303A&PID_1001' -and $_.Name -match '\(COM\d+\)'
})
if ($ports.Count -ne 1) { throw "Expected one Tab5 serial port; found $($ports.Count). Close other ESP32 devices and retry." }
$com = [regex]::Match($ports[0].Name, 'COM\d+').Value
$s = [System.IO.Ports.SerialPort]::new($com, 115200)
$s.DtrEnable = $false
$s.RtsEnable = $false
$s.ReadTimeout = 500
$lines = [System.Collections.Generic.List[string]]::new()
$started = $false
$done = $false
try {
    $s.Open()
    $s.WriteLine('RTL_SUPPORT REPORT')
    $deadline = (Get-Date).AddSeconds(15)
    while ((Get-Date) -lt $deadline -and -not $done) {
        try { $line = $s.ReadLine().Trim() }
        catch [System.TimeoutException] { continue }
        if ($line -like 'ORC_SUPPORT_BEGIN*') { $started = $true }
        if ($started) { $lines.Add($line) }
        if ($line -eq 'ORC_SUPPORT_END') { $done = $true }
    }
} finally {
    if ($s.IsOpen) { $s.Close() }
}
if (-not $done) { throw 'Incomplete report. Check the firmware version, USB cable, and serial port; RTL_HELP should list RTL_SUPPORT REPORT.' }
$report = $lines -join "`n"
$report | Set-Clipboard
$report
```

If the PC-facing cable causes power or USB symptoms, record that condition
separately: historical Tab5 testing found cases where PC VBUS affected power
stability. Never erase or reflash just to collect a support report.

## Maintenance

- Implementation: `apps/orcsdr-tab5/ui/support_report.hpp` (pure formatter),
  `apps/orcsdr-tab5/ui/serial_support.{hpp,cpp}` (serial command module), and
  the Tab5 snapshot hook in `apps/orcsdr-tab5/ui/main.cpp`.
- Sanity tests: `tests/serial_support_tests.cpp` and `python tools/test_support_report.py`
  (requires a C++17 host compiler; no ESP-IDF required).
- Format: schema 1, line-oriented, bounded output and explicit BEGIN/END.
- Extension rule: add new **known-safe** fields only. Do not print generic
  Settings, NVS, Wi-Fi, RTC/location, device serial, or arbitrary log dumps.
- The report is a point-in-time snapshot; it cannot replace a boot log for
  root cause analysis of transient failures.
