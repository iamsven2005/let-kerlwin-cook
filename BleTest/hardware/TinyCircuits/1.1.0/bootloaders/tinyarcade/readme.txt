1- Prerequisites

Use of the Makefile- should be able to compile inside Arduino install. You may have to move this folder to the Arduino SAMD(Zero) folder for access to tools.

2- Selecting between USB and UART interface

Set the define SAM_BA_INTERFACE to
SAM_BA_UART_ONLY for only UART interface
SAM_BA_USBCDC_ONLY for only USB CDC interface
SAM_BA_BOTH_INTERFACES for enabling both the interfaces

SAM_BA_INTERFACE value should be modified in
Project Options -> C/C++ Compiler -> Preprocessor -> Defined symbols
Project Options -> Assembler -> Preprocessor -> Defined symbols

3- Start application check

Bootloader checks for the state of BOOT_LOAD_PIN (configurable by the user from main.h). If BOOT_LOAD_PIN is pulled low, bootloader execution is resumed.
Else, the first location of application is fetched and checked. If it is empty (0xFFFFFFFF), then bootloader execution is resumed. Else it jumps to application and starts execution from there.

For TinyScreen+, the bootloader modification is to change the USB VID/PID from Arduino defaults, and change BOOT_LOAD_PIN to PB23