# RobotReference — legacy Wi-Fi navigation simulation

This is a separate Arduino sketch copied from `Legacy/RoombaSurveyWiFi_asherhouse/TinyScreenRoomba`, not part of the new SurveyFirmware main sketch. The legacy files remain unchanged. It preserves the old survey, GOTO/HOME, step, obstacle avoidance, edge tracing, Wi-Fi telemetry/cache and roaming algorithms for reference. It is not connected to the separate motor controller and implements no direct BLE link.

**Wheel motion and odometry are simulated only.** The sketch starts paused, but RUN, GOTO and other commands can change the simulated position and produce ordinary-looking telemetry. They do not drive motors or report real movement. The original motor-driver branch is a TODO; the reference sketch explicitly requires `SIMULATE_MOTORS` to be 1. Do not use its commands, coordinates, IR decisions, or telemetry as proof that hardware moved or is safe. Physical motion needs a separately designed and verified motor link, feedback, emergency stop and communications-loss handling.

The default IR setting is physical (`SIMULATE_IR = 0`), unlike the virtual wheels. Set it to 1 only for the built-in virtual room. Real IR with virtual movement is not a physical robot model and must not be used for safety decisions. This old variant uses a TinyScreen+, ATWINC1500 Wi-Fi TinyShield, and, when physical IR is enabled, Wireling hardware; it is not a drop-in hat firmware replacement.

Both Wi-Fi credential defaults in `wifi_logic.cpp` are empty placeholders. Configure them locally for a test network or via the legacy SET commands; do not commit credentials. Connection retries block until a network is available. The legacy UDP/serial protocol has no authentication; use only on an isolated trusted network and do not attach a physical motor implementation to it.

Build this folder as its own sketch (`RobotReference.ino` must match the folder name), targeting TinyScreen+ with the TinyCircuits SAMD board package and the TinyScreen, Wireling, and WiFi101 libraries installed. The original low-level WiFi101 driver include is required. Compilation does not establish hardware compatibility or safe operation.
