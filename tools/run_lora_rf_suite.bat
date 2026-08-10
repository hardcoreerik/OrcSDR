@echo off
setlocal

for %%I in ("%~dp0..") do set "ORCSDR_ROOT=%%~fI"
set "RTL_PORT=%~1"
if not defined RTL_PORT set "RTL_PORT=COM17"
set "MESH_PORT=%~2"
if not defined MESH_PORT set "MESH_PORT=COM24"
set "SUITE_PYTHON=%ORCSDR_ROOT%\.local\lora-decoder-venv\Scripts\python.exe"

if not exist "%SUITE_PYTHON%" (
    echo LoRa test environment is missing: %SUITE_PYTHON%
    echo Create it and install tools\requirements-lora.txt first.
    pause
    exit /b 1
)

cd /d "%ORCSDR_ROOT%"
echo Controlled LoRa RF suite: OrcSDR %RTL_PORT%, Hardcore %MESH_PORT%
echo Stop tools\monitor_lora_to_pc.bat before continuing.
echo.

"%SUITE_PYTHON%" tools\lora_rf_test_suite.py "%RTL_PORT%" "%MESH_PORT%" %3 %4 %5 %6
set "SUITE_EXIT=%ERRORLEVEL%"
if not "%SUITE_EXIT%"=="0" pause
exit /b %SUITE_EXIT%
