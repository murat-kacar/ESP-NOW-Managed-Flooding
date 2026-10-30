@echo off
rem Start the build and flashing GUI (tools/flasher) inside the ESP-IDF environment.
rem ESP-IDF is activated with the ESP-IDF Installation Manager (EIM) PowerShell profile.
rem Set IDF_ACTIVATE_SCRIPT to use a different ESP-IDF installation.
setlocal

if not defined IDF_ACTIVATE_SCRIPT set "IDF_ACTIVATE_SCRIPT=C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1"

if not exist "%IDF_ACTIVATE_SCRIPT%" (
    echo ESP-IDF activation script not found: %IDF_ACTIVATE_SCRIPT%
    echo Set IDF_ACTIVATE_SCRIPT to the ESP-IDF PowerShell profile of your installation.
    pause
    exit /b 1
)

cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -Command ". '%IDF_ACTIVATE_SCRIPT%' *> $null; python -m tools.flasher; exit $LASTEXITCODE"
if errorlevel 1 pause
endlocal
