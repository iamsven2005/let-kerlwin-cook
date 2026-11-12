"""Two BLE peripherals, one laptop central. Python 3.10+, Bleak 1.x/2.x.

Notification callbacks only update a bounded latest-reading slot. One writer per
connection serialises all writes. Duplicate samples never refresh source age.
"""
from __future__ import annotations

import argparse
import asyncio
from contextlib import suppress
from dataclasses import dataclass
import logging
import time
from typing import Any

from protocol import Reading, parse_reading

UART_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # laptop -> board
UART_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # board -> laptop
SOURCE_FRESH = 2.0
STALL_TIMEOUT = 8.0
WRITE_TIMEOUT = 3.0
LOG = logging.getLogger("helmet")


@dataclass
class Sample:
    reading: Reading
    data: bytes
    at: float
    generation: int


class Relay:
    def __init__(self) -> None:
        self.latest: dict[str, Sample] = {}
        self.generations = {"L": 0, "R": 0}
        self.packets = {"L": 0, "R": 0}
        self.active: set[str] = set()

    def clear(self, board: str) -> None:
        self.latest.pop(board, None)

    def accept(self, expected: str, data: bytes, now: float) -> bool:
        reading = parse_reading(data)
        if reading is None or reading.board != expected:
            return False
        old = self.latest.get(expected)
        self.packets[expected] += 1
        if old is not None and old.reading.sequence == reading.sequence:
            return False
        self.generations[expected] += 1
        self.latest[expected] = Sample(reading, data, now, self.generations[expected])
        LOG.debug("[%s] outer=%s inner=%s seq=%s", expected,
                  reading.outer, reading.inner, reading.sequence)
        return True

    def pending(self, source: str, sent: int, now: float) -> Sample | None:
        sample = self.latest.get(source)
        if (sample is None or source not in self.active or
                sample.generation == sent or now - sample.at >= SOURCE_FRESH):
            return None
        return sample


async def write(client: Any, payload: bytes) -> None:
    await asyncio.wait_for(
        client.write_gatt_char(UART_RX, payload, response=False), WRITE_TIMEOUT
    )


async def writer(client: Any, board: str, relay: Relay, lost: asyncio.Event) -> None:
    other = "R" if board == "L" else "L"
    sent = -1
    next_heartbeat = 0.0
    while client.is_connected and not lost.is_set():
        now = time.monotonic()
        if now >= next_heartbeat:
            await write(client, b"H")
            next_heartbeat = time.monotonic() + 1.0
            # Separate HCI events so a heartbeat cannot displace a reading.
            await asyncio.sleep(0.03)
        sample = relay.pending(other, sent, time.monotonic())
        if sample is not None:
            await write(client, sample.data)
            sent = sample.generation
        await asyncio.sleep(0.05)


async def monitor(board: str, relay: Relay, lost: asyncio.Event) -> None:
    started = time.monotonic()
    while not lost.is_set():
        sample = relay.latest.get(board)
        last_sample = sample.at if sample else started
        if time.monotonic() - last_sample > STALL_TIMEOUT:
            raise TimeoutError(f"{board}: no new IR sequence for {STALL_TIMEOUT:.0f}s")
        await asyncio.sleep(0.5)


async def session(device: Any, board: str, relay: Relay, client_factory: Any = None) -> None:
    if client_factory is None:
        from bleak import BleakClient
        client_factory = BleakClient
    loop = asyncio.get_running_loop()
    lost = asyncio.Event()

    def disconnected(_client: Any) -> None:
        loop.call_soon_threadsafe(lost.set)

    def receive(_sender: Any, data: bytearray) -> None:
        # No unbounded task creation / queued relay writes in callbacks.
        if not lost.is_set():
            relay.accept(board, bytes(data), time.monotonic())

    relay.clear(board)
    tasks: list[asyncio.Task] = []
    client = client_factory(device, disconnected_callback=disconnected, timeout=15.0)
    try:
        async with client:
            # Check both UUIDs/properties instead of relying on characteristic order.
            tx = client.services.get_characteristic(UART_TX)
            rx = client.services.get_characteristic(UART_RX)
            if tx is None or rx is None or "notify" not in tx.properties or \
                    "write-without-response" not in rx.properties:
                raise RuntimeError("UART service missing; upload the new firmware")
            await asyncio.wait_for(client.start_notify(UART_TX, receive), 5.0)
            relay.active.add(board)
            LOG.info("CONNECTED %s (%s)", board, device.address)
            tasks = [asyncio.create_task(writer(client, board, relay, lost)),
                     asyncio.create_task(monitor(board, relay, lost)),
                     asyncio.create_task(lost.wait())]
            try:
                done, _ = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
                for task in done:
                    task.result()  # propagate failed write or stalled sensor sequence
            finally:
                # Stop GATT operations before the context disconnects the radio.
                lost.set()
                for task in tasks:
                    task.cancel()
                await asyncio.gather(*tasks, return_exceptions=True)
    finally:
        lost.set()
        relay.active.discard(board)
        relay.clear(board)
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        LOG.info("DISCONNECTED %s", board)


async def maintain(board: str, relay: Relay, devices: dict[str, Any]) -> None:
    delay = 1.0
    while True:
        device = devices.get(board)
        if device is None:
            await asyncio.sleep(0.5)
            continue
        try:
            await session(device, board, relay)
            delay = 1.0
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            LOG.warning("%s: %s", board, exc)
        await asyncio.sleep(delay)
        delay = min(delay * 2, 10.0)


async def discover(devices: dict[str, Any], boards: set[str], addresses: dict[str, str]) -> None:
    from bleak import BleakScanner
    while True:
        try:
            found = await BleakScanner.discover(timeout=4.0, return_adv=True)
            candidates: dict[str, list[Any]] = {board: [] for board in boards}
            for device, advert in found.values():
                name = advert.local_name or device.name
                for board in boards:
                    explicit = addresses.get(board)
                    match = (device.address.casefold() == explicit.casefold()) if explicit else name == f"Helmet{board}"
                    if match:
                        candidates[board].append(device)
            for board, matches in candidates.items():
                if len(matches) == 1:
                    devices[board] = matches[0]
                elif len(matches) > 1:
                    devices.pop(board, None)
                    LOG.warning("Multiple Helmet%s devices; specify --%s-address", board, board.lower())
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            LOG.warning("Scan: %s", exc)
        await asyncio.sleep(2.0)


async def status(relay: Relay, boards: set[str]) -> None:
    while True:
        await asyncio.sleep(4.0)
        now = time.monotonic()
        for board in sorted(boards):
            sample = relay.latest.get(board)
            detail = (f"IR={sample.reading.outer},{sample.reading.inner} "
                      f"seq={sample.reading.sequence} age={now-sample.at:.1f}s") if sample else "no fresh readings"
            LOG.info("%s: %s packets=%s %s", board,
                     "connected" if board in relay.active else "waiting",
                     relay.packets[board], detail)


async def main(args: argparse.Namespace) -> None:
    relay = Relay()
    boards = set(args.only) if args.only else {"L", "R"}
    addresses = {b: a for b, a in (("L", args.l_address), ("R", args.r_address)) if a}
    devices: dict[str, Any] = {}
    tasks = [asyncio.create_task(discover(devices, boards, addresses)),
             asyncio.create_task(status(relay, boards))]
    tasks += [asyncio.create_task(maintain(b, relay, devices)) for b in sorted(boards)]
    try:
        await asyncio.gather(*tasks)
    finally:
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", choices=["L", "R"], help="Test one board alone")
    parser.add_argument("--l-address", help="Optional explicit Bluetooth address for L")
    parser.add_argument("--r-address", help="Optional explicit Bluetooth address for R")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(message)s", datefmt="%H:%M:%S")
    try:
        asyncio.run(main(args))
    except KeyboardInterrupt:
        print("\nHelmet relay stopped")
