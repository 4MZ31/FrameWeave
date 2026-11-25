@echo off
rem FrameWeave installer: runs Install-FrameWeave.ps1 next to this file. Extra arguments are passed on
rem (for example: Install-FrameWeave.cmd -Uninstall).
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-FrameWeave.ps1" %*
set "code=%ERRORLEVEL%"
echo.
pause
exit /b %code%
