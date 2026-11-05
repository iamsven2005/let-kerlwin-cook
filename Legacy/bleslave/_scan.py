# One-off: list BLE devices visible from this Windows box.
import asyncio
from bleak import BleakScanner

async def main():
    print("scanning (WinRT adapter)...")
    devices = await BleakScanner.discover(timeout=8)
    for d in devices:
        print(f"  {d.name or '<no name>'}  {d.address}")
    print(f"total: {len(devices)}")

asyncio.run(main())
