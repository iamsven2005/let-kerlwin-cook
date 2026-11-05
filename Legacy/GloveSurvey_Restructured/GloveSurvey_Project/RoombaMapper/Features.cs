namespace RoombaMapper;

/// <summary>
/// Switch each app feature on (true) or off (false). This is the ONLY place to do it.
/// What each feature hooks into is listed in MainForm.Features.cs (the feature table).
/// An off feature adds nothing to the window and receives nothing from the main loop.
/// (The compiler may warn "unreachable code" for an off feature: that is expected.)
/// The glove firmware has its own switches: GloveSurvey/src/core/features.h.
/// </summary>
public static class Features
{
    // ---------------------------------------------------------------- data sources
    public const bool SourceSimulation = true;       // fake robot driving lanes (test the heat map)
    public const bool SourceUsbSerial = true;        // glove or robot on the USB cable
    public const bool SourceBluetoothLe = true;      // robot over Bluetooth LE
    public const bool SourceWifiUdp = true;          // glove or robot over Wi-Fi (UDP)
    public const bool SourceSimulatedRobot = true;   // robot in a virtual room (test the phases)

    // ---------------------------------------------------------------- robot
    public const bool RobotControls = true;          // blue row: drive buttons, mode, speed, sensors, "send robot here"
    public const bool RobotMission = true;           // purple row: phases 1-3, robot size, no-go, save / load map
    public const bool RobotPhase15 = false;          // Phase 1.5 revisit button (placeholder: not written yet)

    // ---------------------------------------------------------------- glove
    public const bool GloveMode = true;              // green row: turns, "I'm here", calibration, walks, survey guide

    // ---------------------------------------------------------------- shared
    public const bool CommandBox = true;             // type any command (e.g. SET,ssid,MyNetwork)
    public const bool StatsPanel = true;             // right-hand panel: distance, position, signal, IR...
    public const bool LogPanel = true;               // the log at the bottom
    public const bool HeatMap = true;                // heat-map layer and its checkbox
    public const bool PathLayer = true;              // path layer and its checkbox
    public const bool ExportCsv = true;              // "Export CSV" button
    public const bool SaveImage = true;              // "Save image" button
    public const bool DebugPanel = true;             // "Debug" button: every glove feature's debug page
}
