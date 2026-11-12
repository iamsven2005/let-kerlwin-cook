# Source and modifications

New helmet application and relay: generated for this project on 11 November 2026.

IR algorithm reference: user's attached
`let-kerlwin-cook/Legacy/IR Proximity/Kerlwin_IR_Distance_Checker.ino`.
Its pulse-count, threshold, frequency calculation and approximate conversion
were preserved; the two-pair scheduling and surrounding application are new.

`firmware/*/src/stble/` contains the STBLE source supplied in
`let-kerlwin-cook/Legacy/BleTest/libraries/STBLE/src/`.
Original STMicroelectronics, TinyCircuits, Contiki/Swedish Institute of Computer
Science and other copyright/license notices remain in the source files.

Bundled STBLE modifications:

- `arduino_bluenrg_ble.cpp`: no hardware interrupt attachment; deassert chip select
  before SPI initialisation; explicit 1 MHz, mode 0 SPI transactions around each
  controller read/write.
- `STBlueNRG/hci.c`: eight read buffers; validate minimum packet/response lengths;
  bound receive polling and return on empty/invalid frames; poll for synchronous
  command replies; avoid refilling while freeing event buffers.
- `STBlueNRG/*.h`: place GCC packing attributes on the structure definitions so
  packed wire layouts work in both C and C++.

The name `HCI_Isr` remains for compatibility with the vendor C code. In this build
it is only invoked as a foreground polling function, never attached to an ISR.

TinyScreen, Wireling, Arduino core and Bleak are external dependencies, installed
separately under their respective licenses. No original project Git history,
unrelated application files, or old integrated helmet firmware is included.
