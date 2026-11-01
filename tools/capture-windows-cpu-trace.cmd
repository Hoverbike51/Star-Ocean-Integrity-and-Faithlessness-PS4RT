@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0capture-windows-cpu-trace.ps1"
if errorlevel 1 (
    echo.
    echo CPU trace capture failed. Review the error above.
    pause
    exit /b 1
)
endlocal
