// SAMD21 watchdog (brief v1.4 A4): ~2 s timeout, fed from the main loop. If the
// loop ever stalls (e.g. a hung peripheral), the node resets and advertises
// again instead of sitting connected but silent. Also reports why the chip
// last reset, so a watchdog reset is visible on the splash and in the log.
#pragma once
#include <Arduino.h>
#include "config.h"

namespace Watchdog {

// Clock: GCLK2 <- OSCULP32K / 32 = 1024 Hz; WDT period 2048 cycles = 2 s.
inline void begin() {
#if WATCHDOG_ENABLE
  GCLK->GENDIV.reg = GCLK_GENDIV_ID(2) | GCLK_GENDIV_DIV(4);            // divide by 2^(4+1) = 32
  GCLK->GENCTRL.reg = GCLK_GENCTRL_ID(2) | GCLK_GENCTRL_GENEN | GCLK_GENCTRL_SRC_OSCULP32K | GCLK_GENCTRL_DIVSEL;
  while (GCLK->STATUS.bit.SYNCBUSY) {}
  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_WDT | GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK2;
  WDT->CTRL.reg = 0;                                                     // disable while configuring
  while (WDT->STATUS.bit.SYNCBUSY) {}
  WDT->CONFIG.reg = WDT_CONFIG_PER(0x8);                                 // 2048 cycles ~ 2 s
  WDT->INTENCLR.reg = WDT_INTENCLR_EW;
  WDT->CTRL.reg = WDT_CTRL_ENABLE;
  while (WDT->STATUS.bit.SYNCBUSY) {}
#endif
}

// Cheap enough for every loop pass: skip the write while the previous one syncs.
inline void feed() {
#if WATCHDOG_ENABLE
  if (!WDT->STATUS.bit.SYNCBUSY) WDT->CLEAR.reg = WDT_CLEAR_CLEAR_KEY;
#endif
}

// Why the chip last reset: "POWER", "WATCHDOG", "RESET-PIN", "SOFTWARE", "BROWNOUT".
inline const char* resetCause() {
  uint8_t r = PM->RCAUSE.reg;
  if (r & PM_RCAUSE_WDT)   return "WATCHDOG";
  if (r & PM_RCAUSE_SYST)  return "SOFTWARE";
  if (r & PM_RCAUSE_EXT)   return "RESET-PIN";
  if (r & (PM_RCAUSE_BOD12 | PM_RCAUSE_BOD33)) return "BROWNOUT";
  return "POWER";
}

}  // namespace Watchdog
