# IR proximity diagnostic (standalone)

Open `IRProximityDiagnostic.ino` as a separate sketch in Arduino IDE; select the TinyScreen+ board and install the TinyScreen and Wireling libraries. It does not run in the main SurveyFirmware modes. Open native USB serial at 115200 baud. It powers the Wireling adapter and displays an uncalibrated level and bar, with an ambient-IR warning; serial reports the level, number of detected frequencies and the raw 0–5 hit counts for each frequency.

**Check wiring and edit the pins before powering the emitter.** Defaults A2 emitter and A0 receiver follow the actual constants in `IR_Proximity_Test.ino`, not its contradictory port comments. The other legacy sketch used A0/A1. Confirm your board's Wireling-port mapping, the active-low demodulating receiver, emitter drive requirements and current limiting from the actual hardware documentation. Avoid pin conflicts; the legacy notes say Wi-Fi uses A3 reset. Verify that the board core supports `tone()` at these carrier frequencies. Never look directly into the emitter.

This ports the first legacy diagnostic's 38, 36.5, 35, 33.5, 32, 30.5 and 29 kHz tone bursts: 1 ms transmit, 400 us receiver tail, 5 ms recovery; five trials each, three hits for detection. Unlike the original early exit, all frequencies are sampled, exposing non-monotonic responses; the level still counts consecutive detections from 38 kHz. The second sketch's separate 38–43 kHz bit-banged sweep and arbitrary centimetre formula are **not** implemented. No value is a measured distance; a level of zero does not establish clear space or >40 cm. Ambient sampling detects only LOW signals during its 30 ms window, not all interference; a stuck-low receiver can resemble strong reflection.

**Calibration and safety:** receiver gain control, optics, surface reflectivity, angle, sunlight, ambient IR, nearby emitters and hardware timing affect readings; even the assumed frequency-sensitivity trend may fail. With motion disabled, compare repeated raw readings at known distances against different surfaces and angles and with/without ambient sources, including no target and disconnected/faulty sensors. Recalibrate for each physical setup and environment. No calibration or detection performance is guaranteed; never use this heuristic for collision avoidance, navigation, or human safety.

Host checks (a C++11 compiler is required), run from this directory:

```sh
g++ -std=c++11 tests/diagnostic_test.cpp -o ir_test && ./ir_test
g++ -std=c++11 -I tests/stubs tests/sketch_test.cpp -o ir_sketch_test && ./ir_sketch_test
```

The second check exercises setup and two loops with minimal fake hardware APIs. Neither check is an Arduino board build or hardware test.
