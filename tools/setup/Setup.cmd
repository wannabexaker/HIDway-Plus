@echo off
rem HIDway setup: choose what to install on this PC (HIDway, Tailscale,
rem Moonlight, Sunshine), each with its own official installer.
rem   Setup.cmd            interactive
rem   Setup.cmd -Update    update what is installed
rem   Setup.cmd -WhatIf    show what would happen
if not "%~1"=="" goto cli
if exist "%~dp0HIDway-Setup.exe" (
  start "" "%~dp0HIDway-Setup.exe"
  exit /b 0
)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0hidway-setup-gui.ps1"
exit /b %errorlevel%
:cli
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0hidway-setup.ps1" %*
set rc=%errorlevel%
exit /b %rc%
