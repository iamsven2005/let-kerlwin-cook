# One-off: connect using the Device object from the same scan (WinRT
# may keep a stable handle even when the address string goes stale),
# and retry a few times.
import asyncio
from bleak import BleakScanner, BleakClient

UART_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
UART_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

async def try_device(d):
    for attempt in range(4):
        try:
            c = BleakClient(d)
            await c.connect(timeout=20)
            got = []
            def handler(_h, data):
                got.append(data.decode(errors="ignore").strip())
            try:
                await c.start_notify(UART_TX, handler)
                await c.write_gatt_char(UART_RX, b"ID?", response=False)
                await asyncio.sleep(3)
            except Exception as e:
                print(f"  {d.address} no UART: {type(e).__name__}")
                # dump GATT to identify it
                try:
                    for s in c.services:
                        print(f"  service {s.address}")
                        for h in s.handles():
                            for cc in h.characteristics:
                                print(f"    char uuid={cc.uuid} rw={cc.write}")
                except Exception:
                    pass
            print(f"  {d.address} ({d.name or '<no name>'}) replies: {got or '<no reply>'}")
            await c.disconnect()
            return bool(got)
        except Exception as e:
            print(f"  {d.address} connect try {attempt+1}: {type(e).__name__}")
            await asyncio.sleep(2)
    return False

async def main():
    for attempt in range(8):
        devices = await BleakScanner.discover(timeout=8)
        print(f"[scan {attempt+1}] " + ", ".join(f"{d.name or '<no name>'} {d.address}" for d in devices))
        if len(devices) >= 2:
            ok = 0
            for d in devices:
                if (d.name or "").startswith("BlueNRG"):
                    ok += 1  # already proven working
                else:
                    ok += await try_device(d)
            print(f"boards answering over UART: {ok} of {len(devices)}")
            return
        await asyncio.sleep(3)
    print("never saw 2 devices in one scan")

asyncio.run(main())
