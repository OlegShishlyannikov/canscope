@echo off
setlocal

REM canscope Windows installer.
REM Runs the bundled Linux install.sh inside WSL. canscope is a Linux-only TUI
REM (uses SocketCAN); under Windows it lives in a WSL2 distro and is invoked
REM as `wsl canscope ...` from PowerShell / cmd.

where wsl >nul 2>&1
if errorlevel 1 (
  echo.
  echo ERROR: WSL is not installed.
  echo.
  echo Install WSL2 first:
  echo   https://learn.microsoft.com/en-us/windows/wsl/install
  echo.
  pause
  exit /b 1
)

cd /d "%~dp0"

echo.
echo Installing canscope into your default WSL distribution.
echo You may be prompted for the WSL user's sudo password.
echo.

wsl bash -c "sudo PREFIX=/usr/local bash './install.sh'"

if errorlevel 1 (
  echo.
  echo Installation failed. See output above.
  pause
  exit /b 1
)

echo.
echo Done. To run canscope from Windows:
echo   wsl canscope -h
echo.
echo Note: SocketCAN inside WSL2 requires a custom kernel build with CAN
echo support, or USB-CAN passthrough via usbipd-win. See README.txt.
echo.
pause
endlocal
