@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-windows.ps1" %*
set "PS4RT_BUILD_EXIT_CODE=%ERRORLEVEL%"
endlocal & exit /b %PS4RT_BUILD_EXIT_CODE%
