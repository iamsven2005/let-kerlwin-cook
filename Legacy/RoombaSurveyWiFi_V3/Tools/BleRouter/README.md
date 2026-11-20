# Offline routing and optional diagnostic BLE tools

`router.py` preserves role/group routing, and `brain_core.py` models legacy IR/motor decisions. Both are transport-independent. The brain remains a simulation: **never connect its outbox to a real motor**. Its decisions lack safety interlocks, including stale-sensor handling. These tools are not integrated with the C# app, board-to-board architecture, or firmware.

Two independent laptop diagnostics port legacy behavior:

- `hub.py`: legacy broadcast hub; when writes are enabled, forwards each non-ID board message to all other boards and console sends to all boards.
- `master.py`: legacy selective master; uses `router.py` to forward only `R <targets> <payload>`, with role/group expansion, sender exclusion, and unknown/offline errors. Other messages remain master-only. There is no offline queue.

**Safety:** Running either tool without arguments does not scan or connect. `--connect` explicitly enables scanning/connection and sends only the `ID?` identification query by default. All other writes, including automatic forwarding, console messages and error replies, are blocked unless `--allow-writes` is also supplied. Enabling writes can send arbitrary motor commands and make hardware move, including via broadcast or a board's routed message. Device-supplied IDs are not authenticated; this is not a motor safety system. Do not enable writes without deliberate authorization, a safe test setup and an independent emergency stop. `--allow-writes` is not an assertion of safety. No hardware testing has been performed.

For diagnostics install the optional `bleak` package in your Python environment, then see `python App/Tools/BleRouter/master.py --help` or `hub.py --help`. `--connect` scans periodically for named devices matching `--name-prefix` (default `BlueNRG`), so unnamed devices will not be found. The console accepts `list`, `groups`, and `send <targets> <payload>`; the hub ignores the targets argument. Payloads over 20 UTF-8 bytes are rejected, not truncated. A successful BLE write does not establish delivery or motor safety.

Run the offline tests without bleak or hardware:

```sh
python -m unittest discover -s App/Tools/BleRouter -p 'test_*.py' -v
python App/Tools/BleRouter/sim_test.py
```

Legacy firmware, probes, and the generic slave remain separate in `Legacy/`. The TailGate thermal detector is unrelated to this BLE tool.
