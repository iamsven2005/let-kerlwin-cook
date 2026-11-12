# Four-IR helmet — fresh Bluetooth + IR implementation

This package is for the two TinyScreen+ boards in your handover: L measures
LEFT / MIDDLE LEFT; R measures RIGHT / MIDDLE RIGHT. Both screens show all four
directions. A Windows laptop relays readings over Bluetooth Low Energy (BLE).

The application, IR scheduler, BLE peripheral implementation and Python relay
were written afresh. The IR burst logic comes from your proven
`Legacy/IR Proximity/Kerlwin_IR_Distance_Checker.ino`. The low-level ST/TinyCircuits
BlueNRG driver comes from your supplied project and is bundled with targeted
corrections; it is not a new radio stack.

**Build-checked and software-tested; not yet tested on your physical boards.**
These changes address specific code hazards, but they do not establish the cause
of the old L freeze or guarantee that a board/power/wiring issue is resolved.

## 1. Connections — same ports on each board

| Board | Direction | Emitter | Receiver |
|---|---|---|---|
| L | LEFT | Wireling port 0 / A0 | Wireling port 1 / A1 |
| L | MIDDLE LEFT | Wireling port 2 / A2 | Wireling port 3 / A3 |
| R | RIGHT | Wireling port 0 / A0 | Wireling port 1 / A1 |
| R | MIDDLE RIGHT | Wireling port 2 / A2 | Wireling port 3 / A3 |

Each stack needs TinyScreen+, ASD2022 Wireling Adapter, ASD2116-R ST BLE shield,
two IR emitters and two IR receivers. These are four IR pairs across both boards.
Use USB power for the first tests. Keep an opaque divider between each emitter
and its receiver, as in your successful original test.

The ports provide separate GPIO signals; the IR scanner does not repeatedly
switch the I²C multiplexer to select them. `Wireling.begin()` powers the adapter.
Do not move the Bluetooth shield jumpers for this implementation.

## 2. Upload the two Arduino sketches

1. Extract the entire ZIP. Do not open a sketch directly inside the ZIP.
2. In Arduino IDE Preferences, add this Additional Boards Manager URL if needed:
   `https://files.tinycircuits.com/ArduinoBoards/package_tinycircuits_index.json`
3. In Boards Manager, install **TinyCircuits SAMD Boards**. Version **1.1.0** was
   used for the compile check. Select **TinyScreen+**, Build Option **Default**.
4. In Library Manager, install **TinyScreen** and **Wireling** by TinyCircuits.
   The checked versions are TinyScreen **1.1.0**, Wireling **0.1.1**.
5. Connect board L. Open `firmware/Helmet_Left/Helmet_Left.ino` and upload to its
   USB port.
6. Connect board R. Open `firmware/Helmet_Right/Helmet_Right.ino` and upload to its
   USB port.

**Keep the complete `src` directory beside each `.ino`.** The corrected Bluetooth
driver lives there and Arduino compiles it automatically. You do not need to
install STBLE. Do not add an old `UART.ino` tab or `#include <STBLE.h>`; these
sketches use their own bundled driver through relative includes. Existing STBLE
installations can remain because this project does not include them.

L advertises as `HelmetL`; R advertises as `HelmetR`. Their development Bluetooth
addresses differ. Use only one L and one R with these addresses in the same area;
additional sets require unique addresses in `src/HelmetBle.cpp`.

## 3. First test: local IR with no laptop program

Power each uploaded board. Move an object in front of each pair separately.
L should update L and ML, R should update MR and R. Opposite-board rows show `--`
until the relay is running. `BLE WAIT` is normal at this point.

The little marker at the top right alternates every half-second. It shows that
the main loop is still reaching the display update. This is independent of the
Bluetooth connection status.

## 4. Run the laptop relay

Use Python **3.10 or newer**. Turn on Windows Bluetooth. Close the previous
`hub_stable.py`, other BLE terminals and phone apps connected to these boards.
Windows Settings pairing is not required for this BLE application.

Open a terminal in the extracted `laptop` folder and run:

```powershell
py -3 -m pip install -r requirements.txt
py -3 hub.py
```

After installing requirements, you can also double-click `run_hub.bat`.

Expected logs:

```text
CONNECTED L (02:80:E1:00:48:4C)
CONNECTED R (02:80:E1:00:48:52)
L: connected packets=... IR=3,5 seq=... age=0.1s
R: connected packets=... IR=2,4 seq=... age=0.2s
```

Timestamps are added automatically. Both screens should show four live bars and
`BLE LINK`. Test one direction at a time and check both screens.

Useful commands:

```powershell
py -3 hub.py --only L
py -3 hub.py --only R
py -3 hub.py --verbose
py -3 hub.py --l-address 02:80:E1:00:48:4C --r-address 02:80:E1:00:48:52
```

Press Ctrl+C to stop. The relay cancels pending writes and disconnects cleanly.
It retries disconnected boards independently with a delay between attempts.

## 5. What the screen means

| Text | Meaning |
|---|---|
| `BLE WAIT` | No active radio connection yet |
| `BLE LINK` | Connected and a laptop heartbeat or relayed message arrived recently |
| `BLE HUB?` | Radio reports connected, but no recent application traffic from laptop |
| `BLE ERR n` | Initialisation/recovery error; `n` is the last BlueNRG status code |
| `BLE OFF` | Bluetooth deliberately disabled in the sketch |
| `--` on a row | No valid local sample or no recent peer measurement |
| `NONE` on a row | Fresh strength measurement of zero |
| `~6cm` to `~36cm` | Kerlwin's original rough conversion of strength 6 to 1 |

The bars encode **reflection strength 0–6**. They are not calibrated centimetre
measurements. Object material, angle, ambient light, optical leakage and other
emitters can affect them. Zero strength does not prove the area is clear or that
an object is more than a particular distance away.

## 6. What was preserved from Kerlwin's working checker

For each pair:

- Sweep the six requested frequencies, 38 through 43 kHz.
- Use the original integer `halfPeriodUs = 500 / frequency` calculation.
- Emit exactly 25 HIGH/LOW pulses per test and sample the receiver after each.
- Count a frequency as detected only when **more than 5** samples are LOW.
- Allow at least 2 ms recovery after each burst.
- Sum detected frequencies to obtain strength 0–6.
- Use `(7 - strength) * 6` only as the original approximate display conversion.

Integer microsecond timing and GPIO overhead mean the actual carrier is not an
exact 38/39/40/41/42/43 kHz. That behaviour was deliberately preserved from your
successful checker, rather than replacing it with a different pulse generator.

One board emits from only one pair at a time. Each full two-pair scan begins
approximately every 150 ms. The scheduler returns to Bluetooth processing during
the 2 ms recovery gaps. It performs no BLE processing, drawing or printing inside
a 25-pulse burst. Normal USB/system interrupts remain enabled.

The two boards are not optically synchronised with each other. Separate and aim
the sensors appropriately. If one board changes readings only when the other is
powered, test for cross-board optical interference; it is distinct from a whole
processor freeze.

## 7. Bluetooth and freeze-related changes

- **No HCI/SPI work in a hardware ISR.** The supplied driver attached `HCI_Isr`
  directly to the BLE interrupt. Its interrupt-disable helpers were empty,
  allowing HCI queue/SPI activity to interrupt other HCI operations. This build
  polls from the foreground instead and also polls while waiting for command
  replies.
- **Bounded reads.** The supplied `while (BlueNRG_DataPresent())` could retry
  endlessly when IRQ remained HIGH and reads returned no data. This build returns
  after an empty/invalid read and limits each poll to eight read attempts.
- **Correct packed packet layouts.** The checked compiler ignored packing in
  the original typedef placement. Packing now applies to the structure type;
  compile-time checks verify the connection and attribute-event wire layouts.
- **Bounded buffers and checked lengths.** Callback code only copies data into
  a fixed 20-byte slot or changes connection/subscription flags. It never sends
  another command recursively.
- **Separate connection and notification states.** Measurements are sent only
  after the laptop subscribes to notifications.
- **Limited traffic.** Firmware sends the latest measurement at most four times
  per second. The relay has one writer per board and retains only the newest
  peer sample, avoiding task/packet queues that grow over time.
- **Freshness checks.** Sequence numbers advance when scans finish, not when a
  BLE send succeeds. Repeated transmissions of a stuck scan do not refresh the
  laptop's source timestamp. Peer rows expire after 2.5 s. The relay refuses to
  forward samples older than 2 s and reconnects a source with no new sequence for
  8 s. A one-second laptop heartbeat is separate from the peer sensor readings.
- **Recovery.** Repeated command failures trigger BLE-controller reinitialisation.
  The IR/display application continues between retries. No automatic processor
  watchdog reset is enabled, so it will not conceal a remaining whole-board hang.
- **USB logging is OFF by default.** There is no wait for Serial Monitor at
  startup. Logging is optional because the checked older TinyScreen+ core's
  `availableForWrite()` returns a fixed capacity, so it cannot guarantee a
  subsequent USB write will be nonblocking.

These are concrete implementation changes, not proof that any single one caused
your earlier L freeze. The OLED driver and I²C library still contain hardware
waits; a low-level bus or power fault can still require hardware investigation.

## 8. If L still freezes: isolate it with these switches

The top of each `.ino` has `ENABLE_IR`, `ENABLE_BLE`, `ENABLE_SCREEN`,
`ENABLE_USB_LOG` and `TRACE_PHASES`. Re-upload after changing them. Test L alone
under USB power first.

| Test | IR | BLE | Screen | What to observe |
|---|---|---|---|---|
| Original sensing path in the new app | true | false | true | Local bars and moving marker continue |
| BLE without IR activity | false | true | true | Run `hub.py --only L`; sequence and packet count continue |
| Integrated, no display refresh | true | true | false | Serial and laptop sequence continue |
| Full build | true | true | true | Bars, marker, laptop sequence all continue |

**BLE-only mode sends diagnostic zero values with an advancing sequence.** They
are not physical readings. Run that test with the other board off so its screen
cannot mistake test values for measurements. The local rows display `--` because
IR is disabled.

To enable diagnostics, set `ENABLE_USB_LOG = true` and re-upload. In Serial
Monitor select the board's USB port, **115200 baud**. A normal once-per-second
line looks like:

```text
L s=82 ir=3,5 tx=48/0 rec=0 e=0
```

`s`: completed scan/test sequence; `ir`: outer,inner strengths; `tx`: successful
characteristic updates / failed updates; `rec`: BLE reinitialisation attempts;
`e`: last BLE status. A successful characteristic update is not proof of delivery
to the laptop.

Set `TRACE_PHASES = true` for optional `BLE> / BLE<`, `IR> / IR<`, `TX> / TX<`,
`OLED> / OLED<` markers, with `ENABLE_USB_LOG = true` too. Tracing changes timing
and can drop lines or stall inside the older core's USB implementation;
an unmatched marker alone is not definitive proof of a hang location. Save the
last USB lines, the laptop log and whether the top-right marker stopped.

For initial acceptance, run L alone, R alone, then both together for at least
30 minutes each. Move objects periodically. Turn one board off, check its peer
bars expire, then turn it back on and check automatic reconnection. Stop/restart
the laptop relay too. These physical checks are still pending.

## 9. Files and validation

| File/folder | Purpose |
|---|---|
| `firmware/Helmet_Left/` | Complete preconfigured L sketch and bundled driver |
| `firmware/Helmet_Right/` | Same implementation, preconfigured as R |
| `laptop/hub.py` | Discovery, two connections, freshness checks, relay, reconnect |
| `laptop/protocol.py` | Strict measurement-message parser |
| `laptop/run_hub.bat` | Windows launcher after Python requirements are installed |
| `tests/` | Host-side IR, protocol, relay and HCI regression checks |
| `VALIDATION.md` | Exact build/test results and limitations |
| `THIRD_PARTY.md` | Source attribution and bundled-driver change notes |

Wire format: `L,outer,inner,sequence` / `R,outer,inner,sequence`, for example
`L,3,5,123`. Every GATT notification/write is one whole ASCII message, at most
11 bytes. `H` is the hub heartbeat. Sequence is a wrapping unsigned 16-bit number.
This protocol requires the new hub and both new sketches; the old hub/firmware
are not interchangeable with it.

BLE service UUID is `6e400001-b5a3-f393-e0a9-e50e24dcca9e`. The laptop writes
`...0002...` without response and subscribes to notifications on `...0003...`.
These settings preserve the service convention from the supplied project.

Developer checks on Linux/macOS with Python, GCC and G++:

```bash
bash tests/run_checks.sh
```

For an Arduino CLI build with the libraries/core installed:

```bash
arduino-cli compile --fqbn TinyCircuits:samd:tinyscreen:BuildOption=normal firmware/Helmet_Left
arduino-cli compile --fqbn TinyCircuits:samd:tinyscreen:BuildOption=normal firmware/Helmet_Right
```
