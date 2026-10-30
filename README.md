# Chef-Eye — TinyScreen+ boards ↔ laptop hub

Three files, two roles:

| File | Runs on | What it does |
|---|---|---|
| `BleTest/BleTest.ino` | the watch board (Arduino tab 1) | shows status on the OLED, replies `ID <n>` when the hub asks, relays received messages, sends button presses |
| `BleTest/UART.ino` | the watch board (Arduino tab 2) | BlueNRG-MS BLE stack + Nordic UART Service (NUS) — no `setup()`/`loop()` of its own |
| `hub.py` | the laptop (Python) | finds every `BlueNRG*` board over Bluetooth, connects to all, asks each for its ID, relays messages between boards |

Hardware: TinyCircuits **TinyScreen Smart Watch Kit** (TinyDuino + USB TinyShield + BLE TinyShield (ST), programmed as a **TinyScreen+**). One board per USB port; the BLE TinyShield must be physically attached.

## 1. Flash the board (Arduino IDE or arduino-cli)

### Arduino IDE route
1. Add to *Additional Boards Manager URLs*: `http://files.tinycircuits.com/ArduinoBoards/package_tinycircuits_index.json`
2. Install **Arduino SAMD Boards** and **TinyCircuits SAMD Boards** in Boards Manager; install the **TinyScreen** library in Library Manager.
3. Open the `BleTest/` folder as a sketch (folder name = main tab name). Select board **TinyScreen+**, port `COMxx (TinyScreen+)`.
4. Upload.

### arduino-cli route (already set up in this repo)
`BleTest/arduino-cli.yaml` vendors the TinyCircuits board package and both libraries (`BleTest/hardware/`, `BleTest/libraries/`), so:

```sh
arduino-cli --config-file BleTest/arduino-cli.yaml compile \
  --fqbn TinyCircuits:samd:tinyscreen --upload --verify -p COM3 BleTest
```

Verified working: 25,140 bytes (9%) of flash, upload + checksum verify on COM3.

Before uploading, power the board ON and plug in the micro-USB cable. If it won't connect, use the force-bootloader trick from the TinyScreen+ tutorial (power off, hold the button nearest the USB port while switching ON).

## 2. Run the hub

```sh
pip install bleak          # hub.py needs Python 3.10+ and bleak
python hub.py              # in a real terminal — it reads your typing
```

What a healthy run looks like:

```
[hub] searching for boards named 'BlueNRG*' ...
[hub] found BlueNRG (DD:26:17:24:09:2F)
[hub] connected to DD:26:17:24:09:2F, asking for ID...
[hub] DD:26:17:24:09:2F is Board 2
```

Type any text to broadcast it to all boards; `list` shows connected boards.

## Renaming the Bluetooth (discoverable) name

The advertised name is set in **three places** — all must match:

1. **Scan name** — `BleTest/UART.ino`, in `setConnectable()` (~line 169). Characters are spelled out one by one after the tag byte:
   ```cpp
   const char local_name[] = {AD_TYPE_COMPLETE_LOCAL_NAME, 'C','h','e','f','E','y','e','-','2'};
   ```
   `sizeof(local_name)` is computed automatically, so nothing else to change there. Keep it ≤ 63 chars (GATT limit).
2. **GATT device-name characteristic** — `BleTest/UART.ino`, in `BLEsetup()` (~line 51), what apps show after connecting:
   ```cpp
   const char *name = "ChefEye-2";
   ```
3. **Hub filter** — `hub.py` line 16:
   ```python
   NAME_PREFIX = "ChefEye"   # must match the new advertised name
   ```

Then re-flash (`arduino-cli ... --upload --verify -p COM3 BleTest`) and the hub finds it under the new name.

Caveat: Windows caches BLE names per adapter — if it still shows the old name, forget the device in Windows Bluetooth settings, or match on the address instead (addresses never change).

## Gotchas
- The board only advertises as `BlueNRG` **after** the sketch is flashed and running — a powered-on board without the sketch is invisible to the hub.
- Windows needs the board's power switch ON; the hub talks Bluetooth, the flash talks USB — both at once is fine.
- `BOARD_ID` in `BleTest.ino` (`'1'`–`'6'`) must differ per board; the hub groups boards by that ID.
- BLE payloads are capped at 20 bytes per message (both sides truncate).
- If a board was flashed with `LOW_POWER`-style serial-disabled builds it stops responding over USB; re-flash to talk again.

## Kit notes
- Kit is *discontinued* at TinyCircuits; the ST BLE TinyShield (BlueNRG-MS) is the one in this kit — use `STBLE.h`, not the Nordic `NRF8001` API.
- Micro USB cable is not included in the kit.
