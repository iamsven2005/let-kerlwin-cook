"""All OS-specific BLE handling lives here (spec 1: Linux/BlueZ primary, Windows/WinRT supported).

The rest of the hub calls these helpers and never branches on the OS itself.
Pure functions (main.conf editing, error descriptions) are unit-tested on any OS.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import Any

if sys.platform.startswith("linux"):
    SYSTEM = "linux"
elif sys.platform == "win32":
    SYSTEM = "windows"
elif sys.platform == "darwin":
    SYSTEM = "macos"
else:  # pragma: no cover
    SYSTEM = sys.platform

# BlueZ connection-interval tuning (units of 1.25 ms: 6 -> 7.5 ms, 12 -> 15 ms).
BLUEZ_MAIN_CONF = Path("/etc/bluetooth/main.conf")
LE_MIN_INTERVAL = 6
LE_MAX_INTERVAL = 12
MARK = "# set by ccmaze setup.sh (undo: tools/bluez_conn_interval.py undo)"


# ------------------------------------------------------------------ bleak arguments
def scanner_kwargs() -> dict[str, Any]:
    """Extra BleakScanner arguments. Both BlueZ and WinRT scan actively by default, which is
    what we need: the name is in the advertising data, the service UUID in the scan response."""
    return {}


def client_kwargs() -> dict[str, Any]:
    """Extra BleakClient arguments."""
    if SYSTEM == "windows":
        # DIY peripheral whose GATT table may change between firmware builds: don't trust
        # the Windows attribute cache.
        return {"winrt": {"use_cached_services": False}}
    return {}


def prepare_ble_thread() -> None:
    """Called first thing on the BLE thread. Nothing is needed today: bleak's WinRT backend
    runs fine on a fresh (MTA) worker thread, and BlueZ talks D-Bus from any thread."""
    return None


# ------------------------------------------------------------------ messages
def connection_interval_note() -> str:
    if SYSTEM == "linux":
        return ("Linux/BlueZ: the node requests 7.5-15 ms; allow it by setting MinConnectionInterval=6 and "
                "MaxConnectionInterval=12 under [LE] in /etc/bluetooth/main.conf (setup.sh offers to do this).")
    if SYSTEM == "windows":
        return ("Windows: the OS chooses the connection interval and it cannot be configured. "
                "Use the node's debug mode to print the negotiated interval.")
    return "This OS is not a supported hub platform; the connection interval is chosen by the OS."


def describe_ble_error(exc: BaseException) -> tuple[str, str]:
    """Turn a bleak/OS error into (problem, fix) text for the lobby and doctor.py."""
    name = type(exc).__name__
    reason = getattr(exc, "reason", None)
    rname = getattr(reason, "name", str(reason) if reason is not None else "")
    text = str(exc)
    if rname == "POWERED_OFF" or "powered off" in text.lower() or "not powered" in text.lower():
        fix = ("Linux: run `bluetoothctl power on` (and `rfkill unblock bluetooth` if it's blocked)."
               if SYSTEM == "linux" else "Windows: Settings > Bluetooth & devices > turn Bluetooth on.")
        return "Bluetooth is turned off", fix
    if rname == "NO_BLUETOOTH" or "no bluetooth adapters" in text.lower() or "No powered Bluetooth" in text:
        return ("No Bluetooth adapter found",
                "Check that the laptop has Bluetooth LE or plug in a USB BLE dongle"
                + ("; then `sudo systemctl restart bluetooth`." if SYSTEM == "linux" else "."))
    if rname.startswith("DENIED") or "AccessDenied" in text or "NotPermitted" in text:
        fix = ("Linux: make sure your user may use BlueZ over D-Bus (on most distros: "
               "`sudo usermod -aG bluetooth $USER`, then log out and in)."
               if SYSTEM == "linux" else "Windows: Settings > Privacy & security > Bluetooth: allow apps access.")
        return "Bluetooth access denied", fix
    if SYSTEM == "linux" and ("org.freedesktop.DBus.Error.ServiceUnknown" in text
                              or ("org.bluez" in text and "not" in text.lower())):
        return "The BlueZ service isn't running", "Run `sudo systemctl enable --now bluetooth`."
    if SYSTEM == "linux" and isinstance(exc, (FileNotFoundError, ConnectionRefusedError)):
        return ("Can't reach BlueZ over the system D-Bus",
                "Run `sudo systemctl enable --now bluetooth` (and make sure dbus is running).")
    return f"{name}: {text}", GENERIC_FIX


GENERIC_FIX = "Check that Bluetooth is on and working; `python doctor.py` gives a full diagnosis."


# ------------------------------------------------------------------ BlueZ main.conf (pure functions)
_SECTION = re.compile(r"^\s*\[(?P<name>[^\]]+)\]\s*$")
_KEY = re.compile(r"^\s*(?P<comment>#\s*)?(?P<key>MinConnectionInterval|MaxConnectionInterval)\s*=\s*(?P<val>\S*)\s*$")


def read_le_intervals(conf_text: str) -> dict[str, int | None]:
    """Active (uncommented) Min/MaxConnectionInterval values in the [LE] section."""
    out: dict[str, int | None] = {"MinConnectionInterval": None, "MaxConnectionInterval": None}
    section = None
    for line in conf_text.splitlines():
        m = _SECTION.match(line)
        if m:
            section = m.group("name").strip()
            continue
        if section != "LE":
            continue
        k = _KEY.match(line)
        if k and not k.group("comment") and k.group("val").isdigit():
            out[k.group("key")] = int(k.group("val"))
    return out


def le_intervals_ok(conf_text: str) -> bool:
    v = read_le_intervals(conf_text)
    return v["MinConnectionInterval"] == LE_MIN_INTERVAL and v["MaxConnectionInterval"] == LE_MAX_INTERVAL


def apply_le_intervals(conf_text: str, lo: int = LE_MIN_INTERVAL, hi: int = LE_MAX_INTERVAL) -> str:
    """Return main.conf text with Min/MaxConnectionInterval set under [LE].

    Existing lines for the keys in [LE] (commented or not) are replaced in place; missing keys
    are added right after the [LE] header; a missing [LE] section is appended. Everything else
    is left byte-for-byte unchanged.
    """
    want = {"MinConnectionInterval": lo, "MaxConnectionInterval": hi}
    lines = conf_text.splitlines()
    out: list[str] = []
    section = None
    le_header_idx = None
    done: set[str] = set()
    for line in lines:
        m = _SECTION.match(line)
        if m:
            section = m.group("name").strip()
            out.append(line)
            if section == "LE":
                le_header_idx = len(out) - 1
            continue
        if section == "LE":
            k = _KEY.match(line)
            if k:
                key = k.group("key")
                if key in done:
                    continue        # drop duplicate definitions
                out.append(f"{key}={want[key]}")
                done.add(key)
                continue
        out.append(line)
    missing = [k for k in want if k not in done]
    if missing:
        add = [MARK] + [f"{k}={want[k]}" for k in missing]
        if le_header_idx is None:
            if out and out[-1].strip():
                out.append("")
            out += ["[LE]"] + add
        else:
            out[le_header_idx + 1:le_header_idx + 1] = add
    return "\n".join(out) + "\n"


# ------------------------------------------------------------------ stale BlueZ devices (14 A2)
_STALE_MARKERS = (
    "BleakDeviceNotFoundError", "was not found", "org.freedesktop.DBus.Error.UnknownObject",
    "org.bluez.Error.Failed", "le-connection-abort-by-local", "br-connection-canceled",
    "org.bluez.Error.NotAvailable", "Software caused connection abort",
)


def is_stale_device_error(exc: BaseException) -> bool:
    """True if a connect failure looks like BlueZ holding a stale device object."""
    if SYSTEM != "linux":
        return False
    text = f"{type(exc).__name__}: {exc}"
    return any(m in text for m in _STALE_MARKERS)


async def forget_device(address: str) -> bool:
    """Linux only: remove BlueZ's cached device object (org.bluez.Adapter1.RemoveDevice) so the
    next scan creates a fresh one. Returns True if a device was removed. No-op elsewhere."""
    if SYSTEM != "linux" or not address:
        return False
    try:
        from dbus_fast import BusType, Message, MessageType
        from dbus_fast.aio import MessageBus
    except Exception:
        return False
    bus = None
    try:
        bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
        reply = await bus.call(Message(destination="org.bluez", path="/",
                                       interface="org.freedesktop.DBus.ObjectManager",
                                       member="GetManagedObjects"))
        if reply.message_type == MessageType.ERROR:
            return False
        for path, ifaces in reply.body[0].items():
            dev = ifaces.get("org.bluez.Device1")
            if not dev or str(dev["Address"].value).upper() != address.upper():
                continue
            adapter = dev["Adapter"].value
            r = await bus.call(Message(destination="org.bluez", path=adapter, interface="org.bluez.Adapter1",
                                       member="RemoveDevice", signature="o", body=[path]))
            return r.message_type != MessageType.ERROR
        return False
    except Exception:
        return False
    finally:
        if bus is not None:
            bus.disconnect()
