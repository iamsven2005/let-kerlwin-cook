# Validation — 11 November 2026

## Board-target builds: PASS

Both final sketches were compiled and linked with Arduino CLI 1.6.0 against:

- FQBN: `TinyCircuits:samd:tinyscreen:BuildOption=normal`
- TinyCircuits SAMD core 1.1.0 / ARM GCC package 7-2017q4
- TinyScreen 1.1.0
- Wireling 0.1.1
- The bundled, corrected STBLE source; no installed STBLE library

| Target | Reported program size | Static data + BSS |
|---|---:|---:|
| Helmet_Left | 26,128 bytes / 262,144 bytes | 5,468 bytes |
| Helmet_Right | 26,128 bytes / 262,144 bytes | 5,468 bytes |

Static RAM excludes stack and runtime allocations. Builds passed with warnings
enabled. The older core reports its existing `digitalPinToInterrupt` macro
redefinition. A full dependency build also reports existing warnings in unused
vendor APIs, including array-parameter `sizeof` and signedness warnings.
The ignored packing warnings were corrected; packet-layout static assertions
passed for the actual ARM target.

L and R `src` contents were checked for equality. Only board identity / introductory
comment in the `.ino` differ. Both default to IR + BLE + screen enabled, USB
logging and phase tracing disabled.

## Host-side checks: PASS

`bash tests/run_checks.sh` completed successfully:

- 10 Python unit/async tests: strict packet parsing, malformed values, identity
  mismatch, fresh-only forwarding, duplicate sequence handling, sequence wrap,
  reconnection, source removal, newest-only buffering, serialised writes,
  cancellation and write-failure cleanup.
- C++ IR simulation against the actual scanner: 25 pulses × 6 frequencies per
  pair, threshold of more than 5 LOW samples, independent readings and emitters,
  swapped input response on a subsequent scan, firmware protocol parser.
- C regression harness linked to the actual bundled HCI source: stuck HIGH IRQ
  with zero SPI bytes returns, truncated packet returns, eight-packet read budget,
  draining a 30-event burst, synchronous command response received by polling,
  and command timeout.
- Python source byte-compilation and `hub.py --help` completed successfully.

The async tests use fake Bluetooth clients. They do not exercise Windows radio
drivers, BLE hardware, actual over-the-air notifications, or real shield SPI.

## Physical validation: PENDING

No physical TinyScreen+, IR pair, BLE shield or Windows Bluetooth adapter was
available in the execution environment. Upload success, OLED appearance on the
96×64 display, optical performance, simultaneous connections and sustained
runtime on board L must still be checked on your hardware. See the README's
staged test procedure and 30-minute-per-stage initial acceptance run.

The original freeze's cause is unconfirmed. No claim of a guaranteed fix is made.
