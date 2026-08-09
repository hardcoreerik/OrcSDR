@echo off
setlocal

for %%I in ("%~dp0..") do set "ORCSDR_ROOT=%%~fI"
set "MONITOR_PORT=%~1"
if not defined MONITOR_PORT set "MONITOR_PORT=COM17"
set "MONITOR_PYTHON=%ORCSDR_ROOT%\.local\lora-decoder-venv\Scripts\python.exe"
set "CAPTURE_DIR=%ORCSDR_ROOT%\.local\lora-captures"

if not exist "%MONITOR_PYTHON%" (
    echo LoRa decoder environment is missing:
    echo   %MONITOR_PYTHON%
    echo.
    echo Recreate it and install tools\requirements-lora.txt before monitoring.
    pause
    exit /b 1
)

cd /d "%ORCSDR_ROOT%"
echo OrcSDR LoRa monitor: %MONITOR_PORT%
echo PC captures: %CAPTURE_DIR%
echo Press Ctrl+C to stop.
echo.

"%MONITOR_PYTHON%" tools\decode_orciq.py --watch-port "%MONITOR_PORT%" --capture-dir "%CAPTURE_DIR%"
set "MONITOR_EXIT=%ERRORLEVEL%"
if not "%MONITOR_EXIT%"=="0" pause
exit /b %MONITOR_EXIT%
