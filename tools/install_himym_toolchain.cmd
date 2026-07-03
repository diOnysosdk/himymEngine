@echo off
setlocal

echo HiMYM toolchain installer
echo Requesting elevation if needed...
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_himym_toolchain.ps1"
set EXITCODE=%ERRORLEVEL%

if not "%EXITCODE%"=="0" (
    echo.
    echo Installer failed with exit code %EXITCODE%.
    pause
)

exit /b %EXITCODE%
