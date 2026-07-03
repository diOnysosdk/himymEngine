@echo off
setlocal

echo HiMYM toolchain + local build bootstrap
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_himym_toolchain.ps1" -PrepareLocalBuild -BuildRelease
set EXITCODE=%ERRORLEVEL%

if not "%EXITCODE%"=="0" (
    echo.
    echo Bootstrap failed with exit code %EXITCODE%.
    pause
)

exit /b %EXITCODE%
