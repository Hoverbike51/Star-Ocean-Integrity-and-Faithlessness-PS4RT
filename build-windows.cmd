@echo off
setlocal
rem CMD builds a clean player folder by default; PowerShell remains available for developer-only builds.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-windows.ps1" %*
set "PS4RT_BUILD_EXIT_CODE=%ERRORLEVEL%"
endlocal & exit /b %PS4RT_BUILD_EXIT_CODE%
