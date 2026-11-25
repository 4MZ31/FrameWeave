@echo off
rem FrameWeave for OBS installer: runs Install-FrameWeave-OBS.ps1 next to this file.
rem Install-FrameWeave-OBS.cmd -Uninstall removes the plugin.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-FrameWeave-OBS.ps1" %*
set "code=%ERRORLEVEL%"
echo.
pause
exit /b %code%
