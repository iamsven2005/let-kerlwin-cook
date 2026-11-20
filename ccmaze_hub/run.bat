@echo off
rem Start the hub from the project's virtual environment. Extra arguments go to main.py
rem (e.g. run.bat --virtual 4 --players 2 --start, or run.bat --help).
setlocal
cd /d "%~dp0"
if not exist ".venv\Scripts\python.exe" (
  echo No virtual environment here yet. Run setup.bat (or setup.ps1^) first.
  exit /b 1
)
".venv\Scripts\python.exe" main.py %*
