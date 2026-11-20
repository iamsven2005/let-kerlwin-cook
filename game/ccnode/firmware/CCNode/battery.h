// Battery level for BAT:<percent>. TinyScreen+ reads its LiPo through a 1:2
// divider on A4, against VCC measured with the SAMD21's 1.1 V bandgap
// (TinyCircuits' getVCC()/getBattVoltage() method).
#pragma once
#include <stdint.h>

// Coarse single-cell LiPo voltage -> percent curve (resting voltage, linear
// between points). Pure function, host-tested.
inline uint8_t lipoPercent(float volts) {
  static const float    v[] = { 3.40f, 3.55f, 3.68f, 3.74f, 3.80f, 3.87f, 3.95f, 4.05f, 4.15f };
  static const uint8_t  p[] = { 0,     5,     15,    30,    45,    60,    75,    90,    100   };
  const int n = sizeof(v) / sizeof(v[0]);
  if (volts <= v[0]) return 0;
  if (volts >= v[n - 1]) return 100;
  for (int i = 1; i < n; i++) {
    if (volts < v[i]) {
      float f = (volts - v[i - 1]) / (v[i] - v[i - 1]);
      return (uint8_t)(p[i - 1] + f * (p[i] - p[i - 1]) + 0.5f);
    }
  }
  return 100;
}

namespace Battery {
  float   readVolts();     // ~0.2 ms, call rarely (every 60 s)
  uint8_t readPercent();
}
