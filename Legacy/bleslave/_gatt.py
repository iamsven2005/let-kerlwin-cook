# One-off: connect to the non-BlueNRG device (with retries) and dump its
# full GATT so we can identify what it actually is.
import asyncio
from bleak import BleakScanner, BleakClient

async def dump(d):
    for attempt in range(4):
        try:
            c = BleakClient(d)
            await c.connect(timeout=20)
            print(f"=== {d.address} ({d.name or '<no name>'}) GATT ===")
            for s in c.services:
                print(f"service {s.address}")
                for h in s.handles():
                    for cc in h.characteristics:
                        print(f"  char uuid={cc.uuid} name={cc.get_readable_name()} write={cc.write}")
            await c.disconnect()
            return True
        except Exception as e:
            print(f"  {d.address} try {attempt+1}: {type(e).__name__}")
            await asyncio.sleep(2)
    return False

async def main():
    for attempt in range(8):
        devices = await BleakScanner.discover(timeout=8)
        print(f"[scan {attempt+1}] " + ", ".join(f"{d.name or '<no name>'} {d.address}" for d in devices))
        others = [d for d in devices if not (d.name or "").startswith("BlueNRG")]
        if others:
            for d in others:
                await dump(d)
            return
        await asyncio.sleep(3)
    print("never saw the unpaired device")

asyncio.run(main())
