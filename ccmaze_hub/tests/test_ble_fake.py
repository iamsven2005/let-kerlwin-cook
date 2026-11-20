"""BLE worker against a fake bleak (scanner + client): connect, HELLO sync, ping/RTT, debug log,
write errors, missed PONGs, and the scan-first reconnect loop including power cycles that change
the node's address (14 A1, A2)."""
import asyncio
import itertools
import time

import pytest

from ccmaze.config import Config
from ccmaze.nodes import ble_platform, physical
from ccmaze.nodes.base import NodeHub
from ccmaze.sim import events as E

_addr = itertools.count(1)


def new_address():
    return f"AA:00:00:00:00:{next(_addr):02X}"


class FakeDevice:
    def __init__(self, name, address):
        self.name = name
        self.address = address


class FakeAdv:
    def __init__(self, name, rssi=-50):
        self.local_name = name
        self.rssi = rssi


class World:
    """The fake radio environment: named nodes that can be powered on/off."""

    def __init__(self):
        self.devices: dict[str, FakeDevice] = {}
        self.powered: dict[str, bool] = {}
        self.clients: list = []
        self.answer_pings = True
        self.send_hello = True
        self.fail_writes = False
        self.stale_once = False
        self.scans_started = 0
        self.by_address = 0

    def add(self, name):
        self.devices[name] = FakeDevice(name, new_address())
        self.powered[name] = True

    def power_off(self, name):
        self.powered[name] = False
        for c in list(self.clients):
            if c.connected and c.device.name == name:
                c.drop()

    def power_on(self, name, new_addr=True):
        if new_addr:
            self.devices[name] = FakeDevice(name, new_address())   # random address per boot
        self.powered[name] = True


WORLD = World()


class FakeScanner:
    def __init__(self, detection_callback=None, **kw):
        self.cb = detection_callback
        self.task = None

    async def start(self):
        WORLD.scans_started += 1

        async def adv():
            while True:
                for name, dev in list(WORLD.devices.items()):
                    if WORLD.powered.get(name):
                        self.cb(FakeDevice(name, dev.address), FakeAdv(name))
                await asyncio.sleep(0.02)
        self.task = asyncio.ensure_future(adv())

    async def stop(self):
        if self.task:
            self.task.cancel()


class FakeChar:
    properties = ["write-without-response", "write"]


class FakeServices:
    def get_characteristic(self, uuid):
        return FakeChar()


class FakeClient:
    def __init__(self, device, disconnected_callback=None, **kw):
        if isinstance(device, str):            # connecting by remembered address (v1.5 X2)
            match = [d for d in WORLD.devices.values() if d.address == device]
            device = FakeDevice(match[0].name, device) if match else FakeDevice("?-?", device)
            WORLD.by_address += 1
        self.device = device
        self.write_times = []
        self.cb = disconnected_callback
        self.services = FakeServices()
        self.written = []
        self.notify = None
        self.connected = False
        WORLD.clients.append(self)

    async def connect(self, **kw):
        name = self.device.name
        if name not in WORLD.devices:
            raise Exception(f"Device with address {self.device.address} was not found")
        if WORLD.stale_once:
            WORLD.stale_once = False
            raise Exception("org.bluez.Error.Failed: le-connection-abort-by-local")
        if not WORLD.powered.get(name) or WORLD.devices[name].address != self.device.address:
            raise Exception(f"Device with address {self.device.address} was not found")
        self.connected = True

    async def start_notify(self, uuid, cb):
        self.notify = cb
        if WORLD.send_hello:
            nid = self.device.name.split("-", 1)[1]
            for m in (f"HELLO:{nid}:0.9\n", "F:0\n", "B:0\n", "BAT:64\n"):
                cb(None, bytearray(m.encode()))

    async def stop_notify(self, uuid):
        self.notify = None

    async def write_gatt_char(self, uuid, data, response=False):
        if not self.connected:
            raise Exception("Not connected")
        if WORLD.fail_writes:
            raise Exception("org.bluez.Error.NotPermitted")
        self.written.append(bytes(data))
        self.write_times.append(time.monotonic())
        if data == b"PING\n" and WORLD.answer_pings and self.notify:
            self.notify(None, bytearray(b"PONG\n"))

    def drop(self):
        if self.connected:
            self.connected = False
            if self.cb:
                self.cb(self)

    async def disconnect(self):
        self.drop()


@pytest.fixture
def worker(monkeypatch):
    global WORLD
    WORLD = World()
    WORLD.add("CCNODE-07")
    monkeypatch.setattr(physical, "BleakClient", FakeClient)
    monkeypatch.setattr(physical, "BleakScanner", FakeScanner)
    monkeypatch.setattr(physical, "BLEAK_AVAILABLE", True)
    monkeypatch.setattr(physical, "HELLO_TIMEOUT_S", 0.3)
    cfg = Config({"ble_ping_interval_s": 0.05, "lost_after_missed_pongs": 2, "ble_scan_timeout_s": 0.5,
                  "ble_reconnect_min_s": 0.05, "ble_reconnect_max_s": 0.2})
    hub = NodeHub()
    w = physical.BleWorker(cfg, hub)
    w.start()
    yield w, hub
    for n in list(hub.nodes.values()):
        n.close()
    time.sleep(0.1)
    w.loop.call_soon_threadsafe(w.loop.stop)


def wait_for(pred, timeout=3.0):
    t = time.time()
    while time.time() - t < timeout:
        if pred():
            return True
        time.sleep(0.01)
    return False


def kinds(evs):
    return [e.kind for e in evs]


def connect(w, node_id="07"):
    return w.connect(physical.ScanResult("stale-address", f"CCNODE-{node_id}", node_id, -50, 0.0))


def test_connect_hello_sync_ping_rtt_and_debug_log(worker):
    w, hub = worker
    node = connect(w)
    assert wait_for(lambda: node.connected)
    assert node.address == WORLD.devices["CCNODE-07"].address      # the scanned one, not the stale one
    assert wait_for(lambda: node.rtt_ms is not None)
    evs = hub.drain()
    assert kinds(evs)[:4] == [E.CONNECTED, E.HELLO, E.FSYNC, E.BSYNC]
    assert node.battery == 64
    node.send("CARD", "UUR")
    assert wait_for(lambda: any(t == "CARD:UUR" and d == ">" for _, d, t in node.debug_lines()))
    lines = node.debug_lines()
    assert any(d == "<" and t.startswith("HELLO") for _, d, t in lines)
    assert any(d == ">" and t == "PING" for _, d, t in lines)
    assert node.writes_ok > 0 and node.write_errors == 0


def test_no_writes_before_hello(worker):
    w, hub = worker
    WORLD.send_hello = False
    node = connect(w)
    time.sleep(0.6)
    assert not node.connected
    assert any("no HELLO" in t for _, _, t in node.debug_lines())
    assert all(c.written == [] for c in WORLD.clients)
    WORLD.send_hello = True
    assert wait_for(lambda: node.connected, timeout=3)


def test_missed_pongs_drop_the_link_and_reconnect(worker):
    w, hub = worker
    WORLD.answer_pings = False
    node = connect(w)
    assert wait_for(lambda: node.connected)
    assert wait_for(lambda: E.DISCONNECTED in kinds(hub.drain()), timeout=3)
    assert any("missed PONG" in t for _, _, t in node.debug_lines())
    WORLD.answer_pings = True
    assert wait_for(lambda: node.connected, timeout=3)


def test_power_cycle_with_new_address_rejoins_on_its_own(worker):
    w, hub = worker
    node = connect(w)
    assert wait_for(lambda: node.connected)
    old = node.address
    hub.drain()
    WORLD.power_off("CCNODE-07")
    assert wait_for(lambda: not node.connected)
    time.sleep(0.3)                          # node rebooting, not advertising
    WORLD.power_on("CCNODE-07", new_addr=True)
    t0 = time.time()
    assert wait_for(lambda: node.connected, timeout=5)
    assert time.time() - t0 < 2.0
    assert node.address != old
    evs = kinds(hub.drain())
    assert evs.index(E.DISCONNECTED) < evs.index(E.CONNECTED)


def test_node_that_is_off_at_first_is_found_later(worker):
    w, hub = worker
    WORLD.powered["CCNODE-07"] = False
    node = connect(w)
    time.sleep(1.0)
    assert not node.connected and node.attempts >= 2
    WORLD.power_on("CCNODE-07")
    assert wait_for(lambda: node.connected, timeout=3)


def test_reconnect_button_skips_the_backoff(worker, monkeypatch):
    w, hub = worker
    w.cfg.set("ble_reconnect_min_s", 30.0)
    w.cfg.set("ble_reconnect_max_s", 30.0)
    w.cfg.set("ble_scan_timeout_s", 0.2)
    WORLD.powered["CCNODE-07"] = False
    node = connect(w)
    assert wait_for(lambda: node.next_attempt_at is not None, timeout=2)   # now waiting ~30 s
    WORLD.power_on("CCNODE-07")
    node.reconnect_now()
    assert wait_for(lambda: node.connected, timeout=2)


def test_write_failure_is_logged_and_recovers(worker):
    w, hub = worker
    node = connect(w)
    assert wait_for(lambda: node.connected)
    WORLD.fail_writes = True
    node.send("CARD", "RR")
    assert wait_for(lambda: node.write_errors > 0)
    assert any(d == "!" and "write failed" in t for _, d, t in node.debug_lines())
    WORLD.fail_writes = False
    assert wait_for(lambda: node.connected and node.writes_ok > 0, timeout=3)


def test_stale_device_is_forgotten_and_retried(worker, monkeypatch):
    w, hub = worker
    calls = []

    async def fake_forget(addr):
        calls.append(addr)
        return True
    monkeypatch.setattr(ble_platform, "is_stale_device_error", lambda e: "le-connection-abort" in str(e))
    monkeypatch.setattr(ble_platform, "forget_device", fake_forget)
    WORLD.stale_once = True
    node = connect(w)
    assert wait_for(lambda: node.connected, timeout=3)
    assert len(calls) == 1


def test_removing_the_node_stops_the_loop(worker):
    w, hub = worker
    node = connect(w)
    assert wait_for(lambda: node.connected)
    hub.remove("07")
    assert wait_for(lambda: node.status == "removed", timeout=3)
    assert not node.connected


def test_stable_address_reconnects_fast_without_scanning(worker):
    w, hub = worker
    node = connect(w)
    assert wait_for(lambda: node.connected)
    addr = node.address
    WORLD.power_off("CCNODE-07")
    assert wait_for(lambda: not node.connected)
    scans = WORLD.scans_started
    WORLD.power_on("CCNODE-07", new_addr=False)        # firmware 1.4+: same address after a reboot
    assert wait_for(lambda: node.connected, timeout=5)
    assert node.address == addr and WORLD.by_address >= 1
    assert any("fast reconnect" in t for _, _, t in node.debug_lines())


def test_changed_address_falls_back_to_scan_by_name(worker):
    w, hub = worker
    node = connect(w)
    assert wait_for(lambda: node.connected)
    WORLD.power_off("CCNODE-07")
    assert wait_for(lambda: not node.connected)
    time.sleep(0.2)
    WORLD.power_on("CCNODE-07", new_addr=True)         # e.g. the one-time change on the 1.3 -> 1.4 update
    assert wait_for(lambda: node.connected, timeout=5)
    assert node.address == WORLD.devices["CCNODE-07"].address
    assert any("fast attempt failed" in t for _, _, t in node.debug_lines())


def test_writes_are_paced_and_bursts_limited(worker):
    w, hub = worker
    node = connect(w)
    assert wait_for(lambda: node.connected)
    c = [x for x in WORLD.clients if x.connected][-1]
    n0 = len(c.write_times)
    for i in range(25):
        node.send("CARD", "UUR" if i % 2 else "RR")
    assert wait_for(lambda: len(c.write_times) - n0 >= 25, timeout=3)
    ts = c.write_times[n0:]
    gaps = [b - a for a, b in zip(ts, ts[1:])]
    assert min(gaps) >= 0.0045                          # >= 5 ms apart (timer slack)
    for i in range(len(ts)):
        assert sum(1 for t in ts if ts[i] <= t < ts[i] + 0.099) <= 10
