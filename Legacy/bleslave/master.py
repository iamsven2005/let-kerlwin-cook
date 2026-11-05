# =====================================================
# Master / slave MASTER (general, not the robot brain)
# - Evolved from Bluetooth/hub.py, but it does NOT broadcast.
#   Old hub: every message -> every other board.
#   This master:
#     * master console:  send <targets> <payload>
#         targets = one role, comma list, group, or ALL
#         e.g. send MOTOR STOP | send IR-F,IR-L ALERT
#              send IR ALERT   | send ALL HELLO
#     * slave message "R <targets> <payload>" -> master strips the
#       envelope and writes the bare payload to only those slaves.
#     * anything else from a slave (e.g. "1", "DONE", "S0") is
#       master-only: logged, never forwarded.
# - Slaves answer "ID <role>" to "ID?", same as BleTest / IR_Node /
#   Motor_Node. A generic slave sketch is in Slave_Node/.
# - Unknown/offline targets: master sends "ERR <role> unknown|offline"
#   back to the sender and still delivers to the known targets.
#   It does not queue for offline boards (see PROTOCOL.md §4.5).
#
# Run with:  python master.py
# Requires:  pip install bleak   (same as the old hub.py)
# Console:   list | send <targets> <payload> | groups
# =====================================================

import asyncio
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])

try:
    from bleak import BleakScanner, BleakClient
except ImportError:  # pragma: no cover - hardware path only
    BleakScanner = BleakClient = None

from router import Router, GROUPS

NAME_PREFIX = "BlueNRG"
RESCAN_EVERY = 10
UART_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # master writes -> slave
UART_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   # slave notifies -> master

router = Router()
# address -> {"client", "role"}
boards = {}
# role -> address (one board per role; a second board claiming the
# same role replaces the old entry and the old board is dropped)
role_to_addr = {}
tasks = set()


def addr_for_role(role):
    return role_to_addr.get(role)


async def send_to_role(role, text):
    addr = addr_for_role(role)
    if addr is None or addr not in boards:
        return False
    try:
        data = text.encode()[:20]
        if len(text.encode()) > 20:
            print(f"  ! '{text}' truncated to 20 bytes for {role} (BLE limit)")
        await boards[addr]["client"].write_gatt_char(UART_RX, data, response=False)
        return True
    except Exception as e:
        print(f"  ! send to {role} failed: {e}")
        return False


async def send_to_addr(addr, text):
    if addr not in boards:
        return False
    try:
        await boards[addr]["client"].write_gatt_char(
            UART_RX, text.encode()[:20], response=False)
        return True
    except Exception as e:
        print(f"  ! send failed: {e}")
        return False


async def on_message(addr, text):
    info = boards.get(addr)
    if not info:
        return
    if text.startswith("ID "):
        role = text[3:].strip()
        old_addr = role_to_addr.get(role)
        if old_addr is not None and old_addr != addr and old_addr in boards:
            print(f"[master] role {role} moved to a new board, dropping old one")
            boards.pop(old_addr, None)
        info["role"] = role
        role_to_addr[role] = addr
        router.register(role)
        print(f"[master] {addr} is {role}")
        return

    role = info["role"]
    result = router.route_from_slave(role, text)
    if result["kind"] == "master_only":
        print(f"[master-only] {role}: {text}")
        return
    # Routed: deliver bare payload to only the selected slaves.
    for target, payload in result["deliveries"]:
        await send_to_role(target, payload)
        print(f"[master] {role} -> {target}: {payload}")
    for bad_role, reason in result["problems"]:
        await send_to_addr(addr, f"ERR {bad_role} {reason}")
        print(f"[master] {role} -> {bad_role}: ERR {reason} (not delivered)")


def make_rx_handler(addr):
    def handler(_sender, data):
        text = data.decode(errors="ignore").strip()
        t = asyncio.create_task(on_message(addr, text))
        tasks.add(t)
        t.add_done_callback(tasks.discard)
    return handler


def make_disconnect_handler(addr):
    def handler(_client):
        info = boards.pop(addr, None)
        if info and info["role"] != "?":
            print(f"[master] {info['role']} left")
            router.unregister(info["role"])
            if role_to_addr.get(info["role"]) == addr:
                del role_to_addr[info["role"]]
    return handler


async def connect(device):
    client = BleakClient(device, disconnected_callback=make_disconnect_handler(device.address))
    try:
        await client.connect(timeout=15)
        boards[device.address] = {"client": client, "role": "?"}
        await client.start_notify(UART_TX, make_rx_handler(device.address))
        print(f"[master] connected to {device.address}, asking for ID...")
        await client.write_gatt_char(UART_RX, b"ID?", response=False)
    except Exception as e:
        print(f"[master] could not connect to {device.address}: {e}")
        boards.pop(device.address, None)


async def discovery_loop():
    while True:
        devices = await BleakScanner.discover(timeout=5)
        for d in devices:
            if d.name and d.name.startswith(NAME_PREFIX) and d.address not in boards:
                print(f"[master] found {d.name} ({d.address})")
                await connect(d)
        await asyncio.sleep(RESCAN_EVERY)


async def console_loop():
    print("[master] commands: list | groups | send <targets> <payload>")
    print("[master]   e.g. send MOTOR STOP | send IR-F,IR-L ALERT | send IR ALERT")
    while True:
        text = (await asyncio.to_thread(input)).strip()
        if not text:
            continue
        if text == "list":
            if not boards:
                print("[master] no boards connected")
            for addr, info in boards.items():
                print(f"  {info['role']}  ({addr})")
            continue
        if text == "groups":
            for name, members in GROUPS.items():
                print(f"  {name} = {', '.join(members)}")
            continue
        if text.startswith("send "):
            rest = text[5:].strip()
            if " " not in rest:
                print("[master] usage: send <targets> <payload>")
                continue
            targets, payload = rest.split(" ", 1)
            deliverable, problems = router.route_from_master(targets)
            for target in deliverable:
                await send_to_role(target, payload)
            print(f"[master] sent '{payload}' to {deliverable or 'nobody'}")
            for bad_role, reason in problems:
                print(f"  ! {bad_role}: {reason}")
            continue
        print("[master] unknown command (list | groups | send <targets> <payload>)")


async def main():
    if BleakScanner is None:
        print("[master] bleak is not installed: pip install bleak")
        print("[master] routing logic is still testable offline: python sim_test.py")
        return
    print(f"[master] searching for boards named '{NAME_PREFIX}*' ...")
    await asyncio.gather(discovery_loop(), console_loop())


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n[master] stopped")
