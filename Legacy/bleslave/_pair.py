# One-off: pair-then-connect the unpaired board. WinRT shows never-paired
# devices with a randomized address and no name; pairing first makes it
# connectable.
import asyncio
from bleak import BleakScanner, BleakClient

UART_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
UART_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

async def main():
    for attempt in range(8):
        devices = await BleakScanner.discover(timeout=8)
        print(f"[scan {attempt+1}] " + ", ".join(f"{d.name or '<no name>'} {d.address}" for d in devices))
        others = [d for d in devices if not (d.name or "").startswith("BlueNRG")]
        if others:
            d = others[0]
            try:
                c = BleakClient(d, pair_before_connect=True)
                await c.connect(timeout=30)
                got = []
                def handler(_h, data):
                    got.append(data.decode(errors="ignore").strip())
                try:
                    await c.start_notify(UART_TX, handler)
                    await c.write_gatt_char(UART_RX, b"ID?", response=False)
                    await asyncio.sleep(3)
                except Exception as e:
                    print(f"  {d.address} no UART: {type(e).__name__}")
                print(f"  {d.address} replies: {got or '<no reply>'}")
                await c.disconnect()
                return
            except Exception as e:
                print(f"  pair+connect try {attempt+1} failed: {type(e).__name__}")
        await asyncio.sleep(3)
    print("never saw the unpaired device")

asyncio.run(main())
