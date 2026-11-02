@echo off
rem Double-click to remove FTL Speed from the FTL game folder.
if not exist "%~dp0install.ps1" (
    echo Extract the whole zip first, then run Uninstall.bat from the extracted folder.
    pause
    exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -Uninstall %*
echo.
pause
