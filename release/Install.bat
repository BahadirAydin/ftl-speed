@echo off
rem Double-click to copy FTL Speed into the FTL game folder.
if not exist "%~dp0install.ps1" (
    echo Extract the whole zip first, then run Install.bat from the extracted folder.
    pause
    exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
echo.
pause
