# Windows setup for the Command Card Maze Race hub.
#   powershell -ExecutionPolicy Bypass -File setup.ps1            (or double-click setup.bat)
#   powershell -ExecutionPolicy Bypass -File setup.ps1 -Recreate  always rebuild .venv
# Creates .venv (fresh if it came from another OS), installs the pinned requirements and runs
# doctor.py. Never copy .venv between machines or OSes: run this script on each one instead.
# Works with Windows PowerShell 5.1 and PowerShell 7.
param([switch]$Recreate)
# "Continue": native tools write warnings to stderr; we check $LASTEXITCODE ourselves.
$ErrorActionPreference = "Continue"
Set-Location -LiteralPath $PSScriptRoot
$Venv = Join-Path $PSScriptRoot ".venv"
$VenvPy = Join-Path $Venv "Scripts\python.exe"

function Invoke-Quiet([string]$Exe, [string[]]$ArgList) {
    # Run a native command, discard its output, return $true if it exited with 0.
    try {
        & $Exe @ArgList *> $null
        return ($LASTEXITCODE -eq 0)
    } catch {
        return $false
    }
}

# 1. a Python between 3.10 and 3.13 (prefer the py launcher)
$Check = "import sys; sys.exit(0 if (3,10) <= sys.version_info[:2] <= (3,13) else 1)"
$PyExe = $null
$PyArgs = @()
foreach ($cand in @("py -3.13", "py -3.12", "py -3.11", "py -3.10", "python")) {
    $parts = $cand.Split(" ")
    $exe = $parts[0]
    $rest = @()
    if ($parts.Length -gt 1) { $rest = @($parts[1]) }
    if (-not (Get-Command $exe -ErrorAction SilentlyContinue)) { continue }
    if (Invoke-Quiet $exe ($rest + @("-c", $Check))) {
        $PyExe = $exe
        $PyArgs = $rest
        break
    }
}
if (-not $PyExe) {
    Write-Host "No Python 3.10-3.13 found. Install one from https://www.python.org/downloads/windows/"
    Write-Host "(tick 'Add python.exe to PATH' or use the 'py' launcher), then run this script again."
    exit 1
}
Write-Host ("Using " + (& $PyExe @PyArgs --version 2>&1))

# 2. a fresh, working virtual environment for THIS OS
if (Test-Path -LiteralPath $Venv) {
    $usable = (-not $Recreate) -and (Test-Path -LiteralPath $VenvPy) -and (Invoke-Quiet $VenvPy @("-c", "import sys"))
    if (-not $usable) {
        Write-Host "Removing .venv (unusable here: missing, broken, or created on another OS)."
        Remove-Item -LiteralPath $Venv -Recurse -Force
    }
}
if (-not (Test-Path -LiteralPath $Venv)) {
    & $PyExe @PyArgs -m venv $Venv
    if ($LASTEXITCODE -ne 0) { Write-Host "Could not create the virtual environment."; exit 1 }
}
& $VenvPy -m pip install --upgrade pip | Out-Null
& $VenvPy -m pip install -r requirements.txt
if ($LASTEXITCODE -ne 0) { Write-Host "Installing requirements failed (see above)."; exit 1 }

# 3. environment check (a failure here is reported, not fatal)
Write-Host ""
& $VenvPy doctor.py

Write-Host ""
Write-Host "Note: on Windows the OS chooses the BLE connection interval; it cannot be configured."
Write-Host "Setup done. Start the hub with run.bat   (no Bluetooth: run.bat --no-ble --virtual 4 --players 2)"
