@echo off
rem Double-clickable wrapper for setup.ps1 (bypasses the PowerShell script policy for this run only).
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" %*
pause
