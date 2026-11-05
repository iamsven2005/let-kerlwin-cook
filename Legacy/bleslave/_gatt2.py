# One-off: GATT dump of the now-paired device 60:12:58:58:D3:27.
import asyncio
from bleak import BleakClient

async def main():
    c = BleakClient("60:12:58:58:D3:27")
    await c.connect(timeout=30)
    print(f"name: {c.name or '<none>'}")
    for s in c.services:
        print(f"service {s.address}")
        for h in s.handles():
            for cc in h.characteristics:
                print(f"  char uuid={cc.uuid} name={cc.get_readable_name()} write={cc.write}")
    await c.disconnect()

asyncio.run(main())
