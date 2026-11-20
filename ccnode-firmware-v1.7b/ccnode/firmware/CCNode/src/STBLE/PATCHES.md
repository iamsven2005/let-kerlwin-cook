# Vendored STBLE library (TinyCircuits), with one patch

Source: TinyCircuits BLE TinyShield ASD2116 repository, `examples/STBLE` (STBLE 0.1.2),
https://github.com/TinyCircuits/TinyCircuits-TinyShield-BLE-ASD2116

It is bundled inside the sketch (`src/STBLE`) so the build doesn't depend on which
STBLE copy happens to be installed, and so that the patch below is always applied.
Licence: see the file headers. `LICENSE-ST.txt` collects the three notices they carry
(ST BSD-3-Clause, ST "guidance only" disclaimer, SICS BSD) verbatim. TinyCircuits ships STBLE without
a separate licence file.

## Patch 1: real IRQ masking on SAMD21 (`arduino_bluenrg_ble.cpp`)

`Enable_SPI_IRQ()` / `Disable_SPI_IRQ()` were empty stubs. ST's HCI code relies on them
to keep the BlueNRG interrupt handler (`HCI_Isr`, attached to pin 2) from running
in the middle of a main-loop SPI write or while the main loop edits the HCI packet lists.
The patch masks/unmasks only that pin's EIC line (`EIC->INTENCLR/INTENSET`). Edges that arrive
while it is masked stay latched and are serviced on unmask, and `HCI_Process()` also
polls the IRQ line explicitly.

## Patch 2: bounded interrupt loop (`STBlueNRG/hci.c`, `HCI_Isr`)

`HCI_Isr()` looped `while (BlueNRG_DataPresent())`. If the IRQ line stayed high while the module
returned nothing (module not answering, shield badly seated, floating line), the MCU stayed inside
the interrupt forever. The display, USB serial and everything else froze at boot. Now it reads at most
8 packets per call and returns as soon as a read comes back empty. `HCI_Process()` (called every main
loop pass) picks up anything left.

## Patch 2b: poll while waiting for a command reply (`STBlueNRG/hci.c`, `hci_send_req`)

The wait loop for a command reply only checked the queue and relied on the IRQ's rising edge to fill
it. It now also calls `HCI_Isr()` itself when the IRQ line is high. Without this, an edge that was
missed (or left pending by patch 2's early return) would turn into a 100 ms command timeout.

## Patch 3: IRQ pin pull-down (`arduino_bluenrg_ble.cpp`, `BNRG_SPI_Init`)

The IRQ pin was `INPUT` (floating). It is now `INPUT_PULLDOWN`, so a missing or unseated shield reads
"no data" instead of random levels. The BlueNRG-MS drives the line actively, so the weak internal
pull-down doesn't affect normal operation.

Caveat: the Arduino SAMD core's shared `EIC_Handler` checks every attached pin's flag, so if
another pin's interrupt fired while this line was masked, the BLE callback could still run. CCNode
attaches no other external interrupts (buttons and the accelerometer are polled), so this can't
happen. Keep it that way, or extend the patch, if you add any.

## Patch 4: receive buffer pool 2 -> 8 (`STBlueNRG/hci.c`, `HCI_READ_PACKET_NUM_MAX`)

TinyCircuits set the HCI receive pool to 2 packets (sized for an AVR's 2 KB of RAM). When the hub sends
a burst of writes, for example the full state it resends after every HELLO, both buffers fill with
those events. A command reply from the radio then has nowhere to go: every notification the node sends
(including `PONG`) waited out the 100 ms command timeout and could be reported as failed. That also
stalled the main loop. 8 buffers of 128 bytes use 1 KB of the SAMD21's 32 KB.

