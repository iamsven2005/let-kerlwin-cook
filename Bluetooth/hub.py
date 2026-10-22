# =====================================================
# Chef-Eye  STEP 2: laptop hub
# - Finds TinyScreen+ boards and connects to all of them
# - Asks each board for its ID
# - Forwards every message from one board to all others
# - Detects boards joining and leaving
# - Anything you type here is sent to all boards
#   (type "list" to see connected boards)
#
# Run with:  python hub.py
# =====================================================

import asyncio
from bleak import BleakScanner, BleakClient

NAME_PREFIX = "BlueNRG"   # the boards' Bluetooth name (change if you rename them)
RESCAN_EVERY = 10         # seconds between searches for new boards

# Nordic UART Service (same one the Bluefruit app used)
UART_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # hub writes here -> board
UART_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   # board notifies here -> hub

boards = {}       # address -> {"client": BleakClient, "id": "?"}
tasks = set()     # keeps background tasks alive


def label(addr):
    if addr in boards:
        return f"B{boards[addr]['id']}"
    return addr


async def send(addr, text):
    if addr not in boards:
        return
    try:
        data = text.encode()[:20]   # BLE max 20 bytes
        await boards[addr]["client"].write_gatt_char(UART_RX, data, response=False)
    except Exception as e:
        print(f"  ! send to {label(addr)} failed: {e}")


async def broadcast(text, exclude=None):
    for addr in list(boards):
        if addr != exclude:
            await send(addr, text)


async def on_message(addr, text):
    if addr not in boards:
        return
    if text.startswith("ID "):
        boards[addr]["id"] = text[3:].strip()
        print(f"[hub] {addr} is Board {boards[addr]['id']}")
        return
    print(f"[{label(addr)}] {text}")
    await broadcast(text, exclude=addr)   # relay to every other board


def make_rx_handler(addr):
    def handler(_sender, data):
        text = data.decode(errors="ignore").strip()
        t = asyncio.create_task(on_message(addr, text))
        tasks.add(t)
        t.add_done_callback(tasks.discard)
    return handler


def make_disconnect_handler(addr):
    def handler(_client):
        if addr in boards:
            print(f"[hub] {label(addr)} left")
            del boards[addr]
    return handler


async def connect(device):
    client = BleakClient(device, disconnected_callback=make_disconnect_handler(device.address))
    try:
        await client.connect(timeout=15)
        boards[device.address] = {"client": client, "id": "?"}
        await client.start_notify(UART_TX, make_rx_handler(device.address))
        print(f"[hub] connected to {device.address}, asking for ID...")
        await send(device.address, "ID?")
    except Exception as e:
        print(f"[hub] could not connect to {device.address}: {e}")
        boards.pop(device.address, None)


async def discovery_loop():
    while True:
        devices = await BleakScanner.discover(timeout=5)
        for d in devices:
            if d.name and d.name.startswith(NAME_PREFIX) and d.address not in boards:
                print(f"[hub] found {d.name} ({d.address})")
                await connect(d)
        await asyncio.sleep(RESCAN_EVERY)


async def console_loop():
    while True:
        text = (await asyncio.to_thread(input)).strip()
        if not text:
            continue
        if text == "list":
            if not boards:
                print("[hub] no boards connected")
            for addr, info in boards.items():
                print(f"  Board {info['id']}  ({addr})")
            continue
        await broadcast(text)
        print(f"[hub] sent '{text[:20]}' to {len(boards)} board(s)")


async def main():
    print(f"[hub] searching for boards named '{NAME_PREFIX}*' ...")
    print("[hub] type a message to send to all boards, or 'list'")
    await asyncio.gather(discovery_loop(), console_loop())


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n[hub] stopped")
