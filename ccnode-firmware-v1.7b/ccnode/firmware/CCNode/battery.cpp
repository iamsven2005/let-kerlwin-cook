#include <Arduino.h>
#include "battery.h"
#include "config.h"

namespace {
  // VCC from the internal 1.1 V bandgap, as in TinyCircuits' TinyScreen+
  // battery example. ADC settings touched here are saved and restored so
  // analogRead() keeps working normally.
  float readVcc() {
    uint16_t sampctrl = ADC->SAMPCTRL.reg;
    uint32_t inputctrl = ADC->INPUTCTRL.reg;

    SYSCTRL->VREF.reg |= SYSCTRL_VREF_BGOUTEN;
    while (ADC->STATUS.bit.SYNCBUSY);
    ADC->SAMPCTRL.bit.SAMPLEN = 0x1;
    while (ADC->STATUS.bit.SYNCBUSY);
    ADC->INPUTCTRL.bit.MUXPOS = 0x19;               // internal bandgap
    while (ADC->STATUS.bit.SYNCBUSY);
    ADC->CTRLA.bit.ENABLE = 1;
    while (ADC->STATUS.bit.SYNCBUSY);
    ADC->SWTRIG.bit.START = 1;                      // first conversion is discarded
    ADC->INTFLAG.bit.RESRDY = 1;
    while (ADC->STATUS.bit.SYNCBUSY);
    ADC->SWTRIG.bit.START = 1;
    while (ADC->INTFLAG.bit.RESRDY == 0);
    uint32_t raw = ADC->RESULT.reg;
    while (ADC->STATUS.bit.SYNCBUSY);
    ADC->CTRLA.bit.ENABLE = 0;
    while (ADC->STATUS.bit.SYNCBUSY);
    SYSCTRL->VREF.reg &= ~SYSCTRL_VREF_BGOUTEN;

    ADC->SAMPCTRL.reg = sampctrl;
    while (ADC->STATUS.bit.SYNCBUSY);
    ADC->INPUTCTRL.reg = inputctrl;
    while (ADC->STATUS.bit.SYNCBUSY);

    if (raw == 0) return 3.3f;
    return (1.1f * 1023.0f) / raw;                  // default 10-bit resolution, VCC reference
  }
}

namespace Battery {

float readVolts() {
  float vcc = readVcc();
  analogRead(BAT_PIN);                              // throw away the first reading
  uint32_t sum = 0;
  for (int i = 0; i < 4; i++) sum += analogRead(BAT_PIN);
  return vcc * (sum / 4.0f) / 1023.0f / BAT_DIVIDER;
}

uint8_t readPercent() { return lipoPercent(readVolts()); }

}  // namespace Battery
