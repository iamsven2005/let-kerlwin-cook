#!/usr/bin/env python3
"""Environment check for the Command Card Maze Race hub (Linux and Windows).

    python doctor.py              # full check, including a BLE scan for CCNODE- nodes
    python doctor.py --no-scan    # skip the scan (e.g. no nodes powered on)

Checks the Python version, the pinned packages, the Bluetooth adapter and whether it's
powered, and scans for nodes. On Linux it also checks the BlueZ version, the bluetooth
service, your permissions and the connection-interval setting; on Windows the Windows build
and whether Bluetooth is on. Every failure prints a fix. Exit code: 0 = no failures, 1 = failures.
Run by setup.sh / setup.ps1; safe to run any time.
"""
from __future__ import annotations

import argparse
import asyncio
import importlib.metadata as md
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

SUPPORTED_PY = ((3, 10), (3, 13))
MIN_BLUEZ = (5, 55)          # bleak's minimum
MIN_WINDOWS_BUILD = 16299    # Windows 10 1709, bleak's minimum

_results: list[str] = []
_COLOR = sys.stdout.isatty() and os.environ.get("NO_COLOR") is None


def _tag(level: str) -> str:
    tags = {"ok": "[ OK ]", "warn": "[WARN]", "fail": "[FAIL]", "info": "[INFO]"}
    colours = {"ok": "32", "warn": "33", "fail": "31", "info": "36"}
    t = tags[level]
    return f"\033[{colours[level]}m{t}\033[0m" if _COLOR else t


def report(level: str, msg: str, fix: str = "") -> None:
    _results.append(level)
    print(f"{_tag(level)} {msg}")
    if fix:
        for line in fix.splitlines():
            print(f"       fix: {line}")


def run(cmd: list[str], timeout: float = 5.0) -> tuple[int, str]:
    if shutil.which(cmd[0]) is None:
        return 127, ""
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return r.returncode, (r.stdout or "") + (r.stderr or "")
    except subprocess.TimeoutExpired:
        return 124, ""
    except OSError as e:
        return 126, str(e)


# ------------------------------------------------------------------ generic checks
def check_python() -> None:
    v = sys.version_info[:2]
    lo, hi = SUPPORTED_PY
    s = f"Python {platform.python_version()} ({sys.executable})"
    if v < lo:
        report("fail", s, f"Install Python {lo[0]}.{lo[1]}-{hi[0]}.{hi[1]} and re-run setup.")
    elif v > hi:
        report("warn", s + f": newer than tested ({hi[0]}.{hi[1]}); pygame may have no wheels for it",
               f"If installing fails, use Python {hi[0]}.{hi[1]}.")
    else:
        report("ok", s)
    if sys.prefix == getattr(sys, "base_prefix", sys.prefix):
        report("warn", "not running inside a virtual environment",
               "Use ./setup.sh (Linux) or setup.ps1 (Windows), then ./run.sh or run.bat.")


def _pins() -> dict[str, str]:
    pins = {}
    req = ROOT / "requirements.txt"
    if req.exists():
        for line in req.read_text(encoding="utf-8").splitlines():
            line = line.split("#", 1)[0].strip()
            m = re.match(r"^([A-Za-z0-9_.\-]+)\s*==\s*([^\s;]+)", line)
            if m:
                pins[m.group(1).lower()] = m.group(2)
    return pins


def check_packages() -> bool:
    ok_bleak = False
    for name, want in _pins().items():
        try:
            have = md.version(name)
        except md.PackageNotFoundError:
            level = "warn" if name == "pytest" else "fail"
            report(level, f"{name} not installed (want {want})", "Run the setup script, or: pip install -r requirements.txt")
            continue
        if have != want:
            report("warn", f"{name} {have} installed, requirements.txt pins {want}",
                   "pip install -r requirements.txt")
        else:
            report("ok", f"{name} {have}")
        if name == "bleak":
            ok_bleak = True
    return ok_bleak


# ------------------------------------------------------------------ Linux
def _version_tuple(s: str) -> tuple[int, ...]:
    return tuple(int(x) for x in re.findall(r"\d+", s)[:2])


def check_linux() -> None:
    from ccmaze.nodes.ble_platform import BLUEZ_MAIN_CONF, le_intervals_ok, read_le_intervals

    adapters = sorted(p.name for p in Path("/sys/class/bluetooth").glob("hci*")) \
        if Path("/sys/class/bluetooth").exists() else []
    if adapters:
        report("ok", f"Bluetooth adapter(s): {', '.join(adapters)}")
    else:
        report("fail", "no Bluetooth adapter found (/sys/class/bluetooth is empty)",
               "Enable Bluetooth in the BIOS / airplane-mode switch, or plug in a USB BLE dongle.")

    for rf in Path("/sys/class/rfkill").glob("rfkill*"):
        try:
            if (rf / "type").read_text().strip() == "bluetooth":
                soft = (rf / "soft").read_text().strip() == "1"
                hard = (rf / "hard").read_text().strip() == "1"
                if hard:
                    report("fail", f"{rf.name}: Bluetooth is hard-blocked", "Flip the hardware wireless switch / airplane-mode key.")
                elif soft:
                    report("fail", f"{rf.name}: Bluetooth is soft-blocked", "rfkill unblock bluetooth")
        except OSError:
            pass

    rc, out = run(["bluetoothctl", "--version"])
    if rc == 127:
        report("fail", "bluetoothctl not found (BlueZ not installed?)",
               "Debian/Ubuntu: sudo apt install bluez   Fedora: sudo dnf install bluez   Arch: sudo pacman -S bluez bluez-utils")
    else:
        ver = _version_tuple(out)
        if ver and ver < MIN_BLUEZ:
            report("fail", f"BlueZ {'.'.join(map(str, ver))} is too old (need >= {MIN_BLUEZ[0]}.{MIN_BLUEZ[1]})",
                   "Upgrade your distribution's bluez package.")
        elif ver:
            report("ok", f"BlueZ {'.'.join(map(str, ver))}")
        else:
            report("warn", f"could not read the BlueZ version ({out.strip()[:60]!r})")

    rc, out = run(["systemctl", "is-active", "bluetooth"])
    first = (out.strip().splitlines() or [""])[0]
    if rc == 127:
        report("warn", "systemctl not found; can't check the bluetooth service")
    elif first == "active":
        report("ok", "bluetooth service is running")
    elif "systemd" in out and "booted" in out:
        report("warn", "this system doesn't run systemd; can't check the bluetooth service",
               "Make sure bluetoothd is running.")
    else:
        report("fail", f"bluetooth service is {first or 'not running'}", "sudo systemctl enable --now bluetooth")

    rc, out = run(["bluetoothctl", "show"], timeout=5)
    if rc == 0 and "Powered:" in out:
        if re.search(r"Powered:\s*yes", out):
            report("ok", "adapter is powered on")
        else:
            report("fail", "adapter is powered off", "bluetoothctl power on")
    elif rc not in (127,):
        report("warn", "could not query the adapter with `bluetoothctl show`")

    try:
        import grp
        import pwd
        user = pwd.getpwuid(os.getuid()).pw_name
        groups = {g.gr_name for g in grp.getgrall() if user in g.gr_mem}
        groups.add(grp.getgrgid(pwd.getpwuid(os.getuid()).pw_gid).gr_name)
        if os.getuid() == 0:
            report("info", "running as root (the game itself should run as your normal user)")
        elif "bluetooth" in groups:
            report("ok", f"user {user} is in the 'bluetooth' group")
        else:
            report("info", f"user {user} is not in the 'bluetooth' group; most distros don't need it "
                           "(the scan below is the real permission test)")
    except Exception:
        pass

    if BLUEZ_MAIN_CONF.exists():
        try:
            text = BLUEZ_MAIN_CONF.read_text(encoding="utf-8", errors="replace")
            if le_intervals_ok(text):
                report("ok", "BlueZ [LE] connection interval set to 7.5-15 ms (6/12)")
            else:
                v = read_le_intervals(text)
                report("warn", f"BlueZ [LE] connection interval not tuned "
                               f"(Min={v['MinConnectionInterval']}, Max={v['MaxConnectionInterval']})",
                       "Re-run ./setup.sh and accept the offer, or: "
                       "sudo .venv/bin/python tools/bluez_conn_interval.py apply")
        except OSError as e:
            report("warn", f"could not read {BLUEZ_MAIN_CONF}: {e}")
    else:
        report("warn", f"{BLUEZ_MAIN_CONF} not found; can't check the connection-interval setting")


# ------------------------------------------------------------------ Windows
def check_windows() -> None:
    try:
        build = sys.getwindowsversion().build  # type: ignore[attr-defined]
    except AttributeError:
        build = 0
    name = f"Windows {platform.release()} (build {build})"
    if build >= MIN_WINDOWS_BUILD:
        report("ok", name)
    else:
        report("fail", f"{name} is too old for Bluetooth LE via WinRT (need build {MIN_WINDOWS_BUILD}+, "
                       "Windows 10 1709 or later)", "Update Windows (Windows 10 22H2 or Windows 11 recommended).")
    try:
        from winrt.windows.devices.radios import Radio, RadioKind, RadioState  # type: ignore

        async def radios():
            return [r for r in await Radio.get_radios_async() if r.kind == RadioKind.BLUETOOTH]

        rs = asyncio.run(radios())
        if not rs:
            report("fail", "no Bluetooth radio found", "Enable Bluetooth in Device Manager / plug in a BLE dongle.")
        elif any(r.state == RadioState.ON for r in rs):
            report("ok", "Bluetooth is turned on")
        else:
            report("fail", "Bluetooth is turned off", "Settings > Bluetooth & devices > Bluetooth: On")
    except Exception:
        report("info", "Bluetooth on/off state will be checked by the scan below")
    report("info", "Windows chooses the BLE connection interval itself; it can't be configured "
                   "(use the node's debug mode to print the negotiated interval)")


# ------------------------------------------------------------------ scan
def check_scan(seconds: float, prefix: str) -> None:
    try:
        from bleak import BleakScanner

        from ccmaze.nodes import ble_platform
    except Exception as e:
        report("fail", f"can't import bleak ({e})", "pip install -r requirements.txt")
        return

    async def scan():
        return await BleakScanner.discover(timeout=seconds, return_adv=True, **ble_platform.scanner_kwargs())

    print(f"       scanning {seconds:.0f} s for {prefix}* nodes...")
    try:
        found = asyncio.run(scan())
    except Exception as e:
        problem, fix = ble_platform.describe_ble_error(e)
        if fix == ble_platform.GENERIC_FIX:
            fix = "Fix the [FAIL] items above first; if they all pass, re-run with a node powered on nearby."
        report("fail", f"BLE scan failed: {problem}", fix)
        return
    nodes = []
    for addr, (dev, adv) in found.items():
        name = adv.local_name or dev.name or ""
        if name.startswith(prefix):
            nodes.append((name, addr, adv.rssi))
    report("ok", f"BLE scan works ({len(found)} device(s) seen)")
    if nodes:
        for name, addr, rssi in sorted(nodes):
            report("ok", f"found node {name}  {addr}  RSSI {rssi} dBm")
    else:
        report("warn", f"no {prefix}* nodes found",
               "Power the node on (not connected to another hub/phone), keep it within a few metres, and retry.")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Check the hub's environment.")
    ap.add_argument("--no-scan", action="store_true", help="skip the BLE scan")
    ap.add_argument("--scan-seconds", type=float, default=5.0)
    a = ap.parse_args(argv)

    prefix = "CCNODE-"
    try:
        from ccmaze.config import Config
        prefix = Config.load(ROOT / "config.json").ble_name_prefix
    except Exception:
        pass

    print(f"Command Card Maze Race hub - environment check ({platform.system()} {platform.release()})")
    check_python()
    have_bleak = check_packages()
    if sys.platform.startswith("linux"):
        check_linux()
    elif sys.platform == "win32":
        check_windows()
    else:
        report("warn", f"{platform.system()} is not a supported hub OS (Linux and Windows are)")
    if not a.no_scan and have_bleak:
        check_scan(a.scan_seconds, prefix)
    fails = _results.count("fail")
    warns = _results.count("warn")
    print()
    if fails:
        print(f"{fails} problem(s) and {warns} warning(s). Fix the [FAIL] items above, then run doctor.py again.")
        print("(Virtual nodes work without Bluetooth: ./run.sh --no-ble / run.bat --no-ble)")
    else:
        print(f"All good{f' ({warns} warning(s))' if warns else ''}.")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
