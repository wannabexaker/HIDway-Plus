@echo off
rem One click: update HIDway on this PC, on the second PC and on the Raspberry Pi relay.
rem Hosts come from update-all.local.ini beside this file; see update-all.example.ini.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0update-all.ps1" %*
echo.
pause
