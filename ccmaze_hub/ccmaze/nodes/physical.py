"""PhysicalNode over BLE (bleak), run in an asyncio loop on a daemon thread.

The render loop never blocks on BLE: commands are queued into the BLE thread, and incoming lines
are timestamped on arrival and put on the hub's thread-safe event queue.

Connection life cycle (section 14 A2), per known node, forever until it's removed:
  scan for its own name (CCNODE-<id>) -> connect to the freshly discovered device -> subscribe
  -> wait for HELLO -> session (PING every ble_ping_interval_s, writes, notifications)
  -> link lost -> tear down completely -> scan again.
Failed attempts back off 1, 2, 4, 8, 10, 10 ... s. The lobby's Reconnect button skips the wait.
A device object from an earlier scan is never reused: a power-cycled TinyShield may come back
with a new random address (HUB-Q33), and BlueZ may have dropped the old object.

One shared scanner (Discovery) serves the lobby scan and every reconnect loop at once, and is
paused while a connect is in progress (BlueZ connects more reliably when not discovering).

A node is lost on a BLE disconnect, or after lost_after_missed_pongs missed PONGs (the hub then
drops the link and reconnects: HUB-Q23). Every write is awaited; a failed write is logged (debug
view + log file) and ends the session so recovery goes through a fresh connect.
"""
from __future__ import annotations

import asyncio
import contextlib
import itertools
import logging
import threading
import time
from concurrent.futures import Future
from dataclasses import dataclass
from typing import Callable, Optional

from ..config import Config
from ..sim import events as E
from . import ble_platform
from .base import Node, NodeHub
from .protocol import LineBuffer, encode, parse_line

log = logging.getLogger(__name__)

try:  # bleak is optional at import time so the game runs without it
    from bleak import BleakClient, BleakScanner
    BLEAK_AVAILABLE = True
except Exception:  # pragma: no cover - depends on platform
    BleakClient = BleakScanner = None
    BLEAK_AVAILABLE = False

HELLO_TIMEOUT_S = 5.0
_order = itertools.count()          # FIFO tie-break inside the write priority queue
PRIO_PING, PRIO_DISPLAY = 0, 1      # PING jumps the queue so pacing can't make PONGs look missed


@dataclass
class ScanResult:
    address: str
    name: str
    node_id: str
    rssi: Optional[int]
    seen_at: float


class PhysicalNode(Node):
    kind = "physical"

    def __init__(self, hub: NodeHub, node_id: str, address: str, worker: "BleWorker", name: str = ""):
        super().__init__(node_id, hub)
        self.address = address
        self.adv_name = name or f"{worker.cfg.ble_name_prefix}{node_id}"
        self.worker = worker
        self.fw_version = ""
        self.rssi: Optional[int] = None
        self.wanted = True              # keep reconnecting while True
        self.status = "waiting"
        self.next_attempt_at: Optional[float] = None
        self.attempts = 0
        self.fast_failed = False        # remembered-address attempt failed: scan by name until a good session
        self.sync_pending: set[str] = set()   # after HELLO: the next F: and B: are a silent state sync
        self.awaiting_pong = False
        self.ping_queued = False
        self.missed_pongs = 0
        self.buf = LineBuffer()
        self.last_rx = 0.0
        # set by the worker, inside the BLE loop
        self._hello: Optional[asyncio.Event] = None
        self._lost: Optional[asyncio.Event] = None
        self._kick: Optional[asyncio.Event] = None

    def _transmit(self, field: str, payload: str = "") -> None:
        if not self.connected:
            return
        self.worker.write(self, encode(field, payload))

    # Called in the BLE loop with raw notification bytes (tests call it directly).
    def on_data(self, data: bytes, ts: Optional[float] = None) -> None:
        ts = time.monotonic() if ts is None else ts   # timestamp on arrival
        self.last_rx = ts
        for line in self.buf.feed(data):
            self.log_debug("<", line, ts)
            parsed = parse_line(line)
            if parsed is None:
                self.log_debug("!", f"unrecognised message {line!r}", ts)
                continue
            kind, value = parsed
            if kind == E.PONG:
                self.awaiting_pong = False
                self.missed_pongs = 0
                self.last_pong = ts
                if self.last_ping_sent is not None:
                    self.rtt_ms = (ts - self.last_ping_sent) * 1000.0
                continue
            if kind == E.BATTERY:
                self.battery = int(value)
                continue
            if kind == E.HELLO:
                parts = value.split(":", 1)
                if parts[0] and parts[0] != self.node_id:
                    self.log_debug("!", f"HELLO says id {parts[0]!r}, expected {self.node_id!r}", ts)
                self.fw_version = parts[1] if len(parts) > 1 else ""
                self.sync_pending = {E.FLIP, E.BUTTONS}
                self.invalidate()           # the node reset to blank defaults: resend everything
                if not self.connected:
                    self.connected = True   # only now may the hub write display state
                    self.status = "connected"
                    self.emit(E.CONNECTED, "", ts)
                if self._hello is not None:
                    self._hello.set()
            elif kind == E.FLIP and E.FLIP in self.sync_pending:
                kind = E.FSYNC
                self.sync_pending.discard(E.FLIP)
            elif kind == E.BUTTONS and E.BUTTONS in self.sync_pending:
                kind = E.BSYNC
                self.sync_pending.discard(E.BUTTONS)
            self.emit(kind, value, ts)

    def reconnect_now(self) -> None:
        self.worker.reconnect_now(self)

    def close(self) -> None:
        self.wanted = False
        self.worker.disconnect(self)

    @property
    def label(self) -> str:
        return f"{self.node_id} (BLE)"


class Discovery:
    """One BleakScanner shared by everyone in the BLE loop. Runs while anybody listens and
    nobody is connecting."""

    def __init__(self, cfg: Config):
        self.cfg = cfg
        self.listeners: set[Callable] = set()
        self.scanner = None
        self.paused = 0
        self.error: Optional[BaseException] = None
        self._lock = asyncio.Lock()

    def _on_detect(self, device, adv) -> None:
        for cb in list(self.listeners):
            try:
                cb(device, adv)
            except Exception:  # pragma: no cover - a listener bug must not kill the scanner
                log.exception("scan listener failed")

    async def _sync(self) -> None:
        async with self._lock:
            want = bool(self.listeners) and self.paused == 0
            if want and self.scanner is None:
                try:
                    sc = BleakScanner(detection_callback=self._on_detect, **ble_platform.scanner_kwargs())
                    await sc.start()
                    self.scanner = sc
                    self.error = None
                except Exception as e:
                    self.error = e
                    raise
            elif not want and self.scanner is not None:
                sc, self.scanner = self.scanner, None
                with contextlib.suppress(Exception):
                    await sc.stop()

    async def listen(self, cb: Callable, seconds: float, until: Optional[asyncio.Event] = None) -> None:
        self.listeners.add(cb)
        try:
            await self._sync()
            if until is None:
                await asyncio.sleep(seconds)
            else:
                with contextlib.suppress(asyncio.TimeoutError):
                    await asyncio.wait_for(until.wait(), seconds)
        finally:
            self.listeners.discard(cb)
            with contextlib.suppress(Exception):
                await self._sync()

    async def find(self, name: str, seconds: float):
        """Wait until an advertisement with this exact name is seen. Returns (device, adv) or None."""
        found: dict = {}
        got = asyncio.Event()

        def cb(device, adv):
            n = adv.local_name or device.name or ""
            if n == name and not got.is_set():
                found["d"] = (device, adv)
                got.set()

        await self.listen(cb, seconds, got)
        return found.get("d")

    @contextlib.asynccontextmanager
    async def pause(self):
        self.paused += 1
        try:
            await self._sync()
            yield
        finally:
            self.paused -= 1
            with contextlib.suppress(Exception):
                await self._sync()


class BleWorker:
    # spec HUB-Q9: bleak cannot request a connection interval; the firmware asks for it.
    def __init__(self, cfg: Config, hub: NodeHub):
        self.cfg = cfg
        self.hub = hub
        self.loop: Optional[asyncio.AbstractEventLoop] = None
        self.thread: Optional[threading.Thread] = None
        self.scan_results: dict[str, ScanResult] = {}    # by node id (addresses can change)
        self.scanning = False
        self.status = "BLE ready" if BLEAK_AVAILABLE else "BLE unavailable (install bleak)"
        self.discovery: Optional[Discovery] = None
        self._clients: dict[str, object] = {}
        self._outq: dict[str, asyncio.Queue] = {}
        self._tasks: dict[str, asyncio.Task] = {}

    @property
    def available(self) -> bool:
        return BLEAK_AVAILABLE

    def _cfg_s(self, key: str, default: float) -> float:
        return float(self.cfg.get(key, default))

    def start(self) -> None:
        if self.thread or not BLEAK_AVAILABLE:
            return
        self.loop = asyncio.new_event_loop()
        self.thread = threading.Thread(target=self._run, name="ble", daemon=True)
        self.thread.start()

    def _run(self) -> None:
        ble_platform.prepare_ble_thread()
        asyncio.set_event_loop(self.loop)
        self.discovery = Discovery(self.cfg)
        self.loop.run_forever()

    def _submit(self, coro) -> Optional[Future]:
        if not self.loop:
            self.start()
        if not self.loop:
            coro.close()
            return None
        fut = asyncio.run_coroutine_threadsafe(coro, self.loop)
        fut.add_done_callback(self._report)
        return fut

    @staticmethod
    def _report(fut: Future) -> None:
        if not fut.cancelled() and fut.exception() is not None:
            log.error("BLE task failed", exc_info=fut.exception())

    async def _discovery(self) -> Discovery:
        while self.discovery is None:   # _run sets it before the loop starts; just in case
            await asyncio.sleep(0.01)
        return self.discovery

    # --- lobby scan -----------------------------------------------------------
    def scan(self) -> None:
        if not self.scanning:
            self._submit(self._scan())

    async def _scan(self) -> None:
        self.scanning = True
        self.status = "scanning..."
        prefix = self.cfg.ble_name_prefix
        seen: dict[str, ScanResult] = {}

        def cb(device, adv):
            name = adv.local_name or device.name or ""
            if name.startswith(prefix):
                nid = name[len(prefix):] or device.address
                seen[nid] = ScanResult(device.address, name, nid, adv.rssi, time.monotonic())

        try:
            disc = await self._discovery()
            await disc.listen(cb, self.cfg.ble_scan_timeout_s)
            self.scan_results.update(seen)
            self.status = f"scan done: {len(seen)} node(s)"
        except Exception as e:  # no adapter, powered off, permissions, ...
            problem, fix = ble_platform.describe_ble_error(e)
            self.status = f"scan failed: {problem}. {fix}"
            log.error("scan failed: %s", e)
        finally:
            self.scanning = False

    # --- connections ----------------------------------------------------------
    def connect(self, result: ScanResult) -> PhysicalNode:
        node = self.hub.nodes.get(result.node_id)
        if not isinstance(node, PhysicalNode):
            node = PhysicalNode(self.hub, result.node_id, result.address, self, result.name)
            node.rssi = result.rssi
            self.hub.add(node)
        elif result.address:
            node.address = result.address   # a fresh scan result beats the remembered address
            node.fast_failed = False
        node.wanted = True
        self._submit(self._start_maintain(node))
        return node

    async def _start_maintain(self, node: PhysicalNode) -> None:
        node._kick = node._kick or asyncio.Event()
        t = self._tasks.get(node.node_id)
        if t and not t.done():
            node._kick.set()
            return
        self._tasks[node.node_id] = asyncio.ensure_future(self._maintain(node))

    def reconnect_now(self, node: PhysicalNode) -> None:
        """Lobby Reconnect button: same scan-then-connect path, immediately."""
        node.wanted = True
        node.log_debug("*", "reconnect requested")

        def kick():
            if node._lost is not None and node.connected:
                node._lost.set()           # drop the current link; the loop reconnects at once
            if node._kick is not None:
                node._kick.set()
        if self.loop:
            self.loop.call_soon_threadsafe(kick)
        self._submit(self._start_maintain(node))

    def disconnect(self, node: PhysicalNode) -> None:
        node.wanted = False

        def stop():
            for ev in (node._lost, node._kick):
                if ev is not None:
                    ev.set()
        if self.loop:
            self.loop.call_soon_threadsafe(stop)

    def write(self, node: PhysicalNode, data: bytes) -> None:
        q = self._outq.get(node.node_id)
        if q is not None and self.loop:
            self.loop.call_soon_threadsafe(q.put_nowait, (PRIO_DISPLAY, next(_order), data))

    async def _wait_kick(self, node: PhysicalNode, seconds: float) -> None:
        node.next_attempt_at = time.monotonic() + seconds
        node._kick.clear()
        with contextlib.suppress(asyncio.TimeoutError):
            await asyncio.wait_for(node._kick.wait(), seconds)
        node.next_attempt_at = None

    async def _maintain(self, node: PhysicalNode) -> None:
        lo = self._cfg_s("ble_reconnect_min_s", 1.0)
        hi = self._cfg_s("ble_reconnect_max_s", 10.0)
        backoff = lo
        forgot = False
        disc = await self._discovery()
        while node.wanted:
            node.attempts += 1
            ok_session = False
            # v1.5 X2 / HUB-Q33: firmware 1.4+ keeps a fixed address, so try the remembered one
            # first (no scan); if that fails, fall back to scanning by name as before.
            if node.address and not node.fast_failed:
                try:
                    node.log_debug("*", f"fast reconnect to remembered address {node.address}")
                    ok_session = await self._session(node, node.address)
                except Exception as e:
                    node.fast_failed = True
                    node.log_debug("!", f"fast attempt failed: {type(e).__name__}: {e}; scanning by name")
                if ok_session:
                    backoff, forgot = lo, False
                    node.fast_failed = False
                    continue
                if not node.wanted:
                    break
            try:
                # spec HUB-Q33: always scan by name first; the address may have changed since
                node.status = "scanning"
                node.log_debug("*", f"scanning for {node.adv_name}")
                found = await disc.find(node.adv_name, self._cfg_s("ble_scan_timeout_s", 5.0))
                if not node.wanted:
                    break
                if found is None:
                    raise LookupError(f"{node.adv_name} not advertising")
                device, adv = found
                if device.address != node.address:
                    node.log_debug("*", f"address {node.address} -> {device.address}")
                node.address = device.address
                node.rssi = adv.rssi
                ok_session = await self._session(node, device)
            except Exception as e:
                problem = ble_platform.describe_ble_error(e)[0]
                node.status = f"retrying: {problem}"
                node.log_debug("!", f"attempt failed: {type(e).__name__}: {e}")
                log.info("%s: attempt failed: %s", node.node_id, e)
                if ble_platform.is_stale_device_error(e) and not forgot:
                    forgot = True
                    removed = await ble_platform.forget_device(node.address)
                    node.log_debug("*", f"stale BlueZ device: removed={removed}; retrying once now")
                    continue
            if not node.wanted:
                break
            if ok_session:
                backoff, forgot = lo, False
                node.fast_failed = False    # next time, try the remembered address first again
                continue                    # link lost after a good session: reconnect at once
            node.status = f"offline, retry in {backoff:.0f} s"
            node.log_debug("*", f"retry in {backoff:.1f} s")
            await self._wait_kick(node, backoff)
            if node._kick.is_set():
                backoff = lo
            else:
                backoff = min(backoff * 2, hi)
        node.status = "removed"
        node.log_debug("*", "stopped (removed from lobby)")

    async def _session(self, node: PhysicalNode, device) -> bool:
        """Connect, subscribe, wait for HELLO, run until the link is lost. Returns True if the
        node said HELLO (a usable session happened)."""
        cfg = self.cfg
        lost = asyncio.Event()
        node._lost = lost
        node._hello = asyncio.Event()
        loop = asyncio.get_running_loop()

        def on_disconnect(_c):
            loop.call_soon_threadsafe(lost.set)

        client = BleakClient(device, timeout=10.0, disconnected_callback=on_disconnect,
                             **ble_platform.client_kwargs())
        tasks: list[asyncio.Task] = []
        subscribed = False
        tx_uuid = cfg.ble_tx_uuid.lower()
        try:
            node.status = "connecting"
            node.log_debug("*", f"connecting to {getattr(device, 'address', device)}")
            disc = await self._discovery()
            async with disc.pause():
                await client.connect()
            self._clients[node.node_id] = client
            rx_uuid = cfg.ble_rx_uuid.lower()
            rx = client.services.get_characteristic(rx_uuid)
            if rx is None:
                raise RuntimeError("RX characteristic not found (check ble_rx_uuid)")
            # spec HUB-Q34: optional acknowledged writes, to diagnose dropped write commands
            with_response = bool(cfg.get("ble_write_with_response", False)) or \
                "write-without-response" not in rx.properties
            q: asyncio.PriorityQueue = asyncio.PriorityQueue()
            self._outq[node.node_id] = q
            node.missed_pongs = 0
            node.awaiting_pong = False
            node.ping_queued = False
            node.status = "subscribing"
            await client.start_notify(tx_uuid, lambda _s, data: node.on_data(bytes(data)))
            subscribed = True
            node.log_debug("*", f"subscribed; waiting for HELLO (writes {'with' if with_response else 'without'}"
                                f" response)")
            try:
                await asyncio.wait_for(node._hello.wait(), HELLO_TIMEOUT_S)
            except asyncio.TimeoutError:
                raise RuntimeError(f"no HELLO within {HELLO_TIMEOUT_S:.0f} s of subscribing") from None

            gap = max(0.0, float(cfg.get("ble_write_gap_ms", 5))) / 1000.0
            burst_max = max(1, int(cfg.get("ble_burst_max", 10)))
            burst_win = max(0.0, float(cfg.get("ble_burst_window_ms", 100))) / 1000.0
            recent: list[float] = []

            async def write_loop():
                # v1.5 X5: writes at least ble_write_gap_ms apart, and at most ble_burst_max
                # messages in any ble_burst_window_ms, so a full state resend can't overflow the
                # node's receive buffers.
                last = 0.0
                while True:
                    _prio, _n, data = await q.get()
                    text = data.decode("ascii", "replace").rstrip("\n")
                    wait = last + gap - time.monotonic()
                    if wait > 0:
                        await asyncio.sleep(wait)
                    now_ = time.monotonic()
                    recent[:] = [t for t in recent if now_ - t < burst_win]
                    if len(recent) >= burst_max:            # burst full: wait for the oldest to age out
                        await asyncio.sleep(max(0.0, recent[0] + burst_win - now_))
                        now_ = time.monotonic()
                        recent[:] = [t for t in recent if now_ - t < burst_win]
                    t0 = time.monotonic()
                    try:
                        await client.write_gatt_char(rx_uuid, data, response=with_response)
                    except Exception as e:
                        node.write_errors += 1
                        node.log_debug("!", f"write failed: {text!r}: {type(e).__name__}: {e}")
                        log.error("%s: write %r failed: %s", node.node_id, text, e)
                        raise
                    last = time.monotonic()
                    recent.append(t0)
                    node.writes_ok += 1
                    if text == "PING":
                        node.last_ping_sent = t0
                        node.ping_queued = False
                        node.awaiting_pong = True       # the PONG clock starts once it's really sent
                    took = (time.monotonic() - t0) * 1000
                    node.log_debug(">", text if took < 50 else f"{text}   (write took {took:.0f} ms)", t0)

            async def ping_loop():
                while True:
                    await asyncio.sleep(cfg.ble_ping_interval_s)
                    if node.ping_queued:
                        continue                         # last PING not even sent yet: not a miss
                    if node.awaiting_pong:
                        node.missed_pongs += 1
                        node.log_debug("!", f"missed PONG ({node.missed_pongs}/{cfg.lost_after_missed_pongs})")
                        if node.missed_pongs >= cfg.lost_after_missed_pongs:
                            node.status = "ping timeout"
                            node.log_debug("!", "lost: too many missed PONGs, dropping the link")
                            lost.set()
                            return
                    node.ping_queued = True
                    q.put_nowait((PRIO_PING, next(_order), encode("PING")))

            tasks = [asyncio.ensure_future(write_loop()), asyncio.ensure_future(ping_loop()),
                     asyncio.ensure_future(lost.wait())]
            done, _ = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
            for t in done:
                exc = None if t.cancelled() else t.exception()
                if exc:
                    node.log_debug("!", f"session ended: {type(exc).__name__}: {exc}")
            node.log_debug("*", "link lost")
            return True
        finally:
            # full teardown before any new attempt (14 A2)
            for t in tasks:
                t.cancel()
            self._outq.pop(node.node_id, None)
            if subscribed:
                with contextlib.suppress(Exception):
                    await asyncio.wait_for(client.stop_notify(tx_uuid), 2.0)
            with contextlib.suppress(Exception):
                await asyncio.wait_for(client.disconnect(), 5.0)
            self._clients.pop(node.node_id, None)
            node._lost = None
            node._hello = None
            if node.connected:
                node.connected = False
                node.emit(E.DISCONNECTED)
            del client
