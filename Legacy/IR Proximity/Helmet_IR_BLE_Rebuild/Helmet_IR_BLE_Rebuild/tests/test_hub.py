import asyncio
from pathlib import Path
import sys
import unittest
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "laptop"))
from protocol import parse_reading
from hub import Relay, session, writer, UART_RX, UART_TX


class ProtocolTests(unittest.TestCase):
    def test_boundaries(self):
        for wire in (b"L,0,6,0", b"R,6,0,65535"):
            self.assertIsNotNone(parse_reading(wire))

    def test_invalid(self):
        for wire in (b"L,7,0,1", b"R,0,0,65536", b"X,1,2,3", b"L,1,2,-1",
                     b"L,1,2,1\n", b"L,1,2,", b"L,1,2,000000", b"\xff", b"L,1,2"):
            self.assertIsNone(parse_reading(wire), wire)


class RelayTests(unittest.TestCase):
    def setUp(self):
        self.r = Relay()
        self.r.active.add("L")

    def test_forward_only_fresh_new_source(self):
        self.assertTrue(self.r.accept("L", b"L,1,2,9", 10))
        self.assertIsNotNone(self.r.pending("L", -1, 11))
        self.assertIsNone(self.r.pending("L", 1, 11))
        self.assertIsNone(self.r.pending("L", -1, 12))

    def test_duplicate_does_not_refresh_age(self):
        self.r.accept("L", b"L,1,2,9", 10)
        self.assertFalse(self.r.accept("L", b"L,1,2,9", 11))
        self.assertEqual(self.r.latest["L"].at, 10)

    def test_wrong_board_rejected(self):
        self.assertFalse(self.r.accept("L", b"R,1,2,0", 10))
        self.assertEqual(self.r.latest, {})

    def test_reconnect_and_wrap(self):
        self.r.accept("L", b"L,1,2,65535", 10)
        self.assertTrue(self.r.accept("L", b"L,1,2,0", 11))
        previous_generation = self.r.latest["L"].generation
        self.r.clear("L")
        self.r.accept("L", b"L,1,2,0", 12)
        self.assertGreater(self.r.latest["L"].generation, previous_generation)

    def test_disconnect_clears_and_blocks_stale(self):
        self.r.accept("L", b"L,1,2,1", 10)
        self.r.active.clear()
        self.assertIsNone(self.r.pending("L", -1, 10.1))
        self.r.clear("L")
        self.assertNotIn("L", self.r.latest)


class FakeClient:
    def __init__(self, device=None, **kwargs):
        self.is_connected = True
        self.properties = {UART_TX: ["notify"], UART_RX: ["write-without-response"]}
        self.services = self
        self.writes = []
        self.concurrent = 0
        self.max_concurrent = 0
        self.on_disconnect = kwargs.get("disconnected_callback")
        self.exited = False
        self.rx_callback = None

    async def __aenter__(self):
        return self

    async def __aexit__(self, *args):
        self.is_connected = False
        self.exited = True

    def get_characteristic(self, uuid):
        return SimpleNamespace(properties=self.properties[uuid])

    async def start_notify(self, uuid, callback):
        self.rx_callback = callback

    async def write_gatt_char(self, uuid, data, response):
        self.concurrent += 1
        self.max_concurrent = max(self.max_concurrent, self.concurrent)
        try:
            await asyncio.sleep(.005)
            self.writes.append(bytes(data))
        finally:
            self.concurrent -= 1


class AsyncTests(unittest.IsolatedAsyncioTestCase):
    async def test_single_writer_and_newest_slot(self):
        import time
        relay = Relay()
        relay.active.add("L")
        for seq in range(1000):
            relay.accept("L", f"L,1,2,{seq}".encode(), time.monotonic())
        client = FakeClient()
        lost = asyncio.Event()
        task = asyncio.create_task(writer(client, "R", relay, lost))
        await asyncio.sleep(.15)
        lost.set()
        await task
        self.assertEqual(client.max_concurrent, 1)
        self.assertIn(b"H", client.writes)
        self.assertEqual([p for p in client.writes if p != b"H"], [b"L,1,2,999"])
        self.assertEqual(len(relay.latest), 1)

    async def test_session_cancellation_cleans_up(self):
        relay = Relay()
        client = FakeClient()
        task = asyncio.create_task(session(SimpleNamespace(address="test"), "L", relay,
                                          lambda *a, **k: client))
        await asyncio.sleep(.08)
        self.assertIn("L", relay.active)
        client.rx_callback(None, bytearray(b"L,1,2,1"))
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertTrue(client.exited)
        self.assertNotIn("L", relay.active)
        self.assertNotIn("L", relay.latest)

    async def test_write_failure_leaves_no_active_connection(self):
        relay = Relay()
        client = FakeClient()
        async def fail(*a, **kw):
            raise RuntimeError("simulated radio failure")
        client.write_gatt_char = fail
        with self.assertRaisesRegex(RuntimeError, "simulated"):
            await session(SimpleNamespace(address="test"), "L", relay, lambda *a, **k: client)
        self.assertTrue(client.exited)
        self.assertEqual(relay.active, set())


if __name__ == "__main__":
    unittest.main()
