@echo off
setlocal DisableDelayedExpansion
title SENSY-ONE for Homey
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0installer\install-windows.ps1"
set "INSTALL_RESULT=%ERRORLEVEL%"
if not "%INSTALL_RESULT%"=="0" echo Installation stopped. See the error above. You can run this installer again.
pause
exit /b %INSTALL_RESULT%
