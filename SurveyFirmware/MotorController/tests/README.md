# Host simulation test

From this sketch directory on a Linux/WSL host with g++:

```
g++ -std=c++11 -Wall -Wextra -Itests tests/simulation.cpp -o /tmp/motor-simulation-test
/tmp/motor-simulation-test
```

The harness substitutes Arduino, display and BLE APIs and exercises the motor sketch's simulated logic. It does **not** compile `UART.ino` or test the board package, BLE stack, radio, motor driver or hardware.
