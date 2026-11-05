using System.Globalization;

namespace RoombaMapper;

/// <summary>
/// Feature "Robot controls" (Features.RobotControls): the blue row of buttons that drive
/// the robot directly, its Wi-Fi mode / speed / sensor settings (kept in step with what
/// the robot reports), and "Send robot here" on the map's right-click menu.
/// </summary>
public sealed partial class MainForm
{
    private readonly FlowLayoutPanel _controlRow = new()
    {
        Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(6, 0, 6, 6), WrapContents = true,
        BackColor = Color.FromArgb(240, 245, 250)
    };
    private readonly Button _runBtn = CtlButton("▶ Start survey");
    private readonly Button _pauseBtn = CtlButton("⏸ Pause");
    private readonly Button _homeBtn = CtlButton("⌂ Return to start");
    private readonly Button _measureBtn = CtlButton("◎ Measure here");
    private readonly Button _fwdBtn = CtlButton("↑ Forward");
    private readonly Button _backBtn = CtlButton("↓ Back");
    private readonly Button _leftBtn = CtlButton("↶ Left");
    private readonly Button _rightBtn = CtlButton("↷ Right");
    private readonly Button _resetPosBtn = CtlButton("Set start here");
    private readonly Button _roamBtn = CtlButton("📶 Find better AP");
    private readonly ComboBox _sensorBox = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 110 };
    private readonly CheckBox _probeBox = new() { Text = "Probe wall", AutoSize = true, Margin = new Padding(3, 6, 3, 3) };
    private readonly ComboBox _modeBox = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 70 };
    private readonly NumericUpDown _speedBox = new() { Minimum = 20, Maximum = 200, Increment = 10, Value = 100, Width = 60 };
    private readonly ContextMenuStrip _mapMenu = new();
    private readonly ToolStripMenuItem _sendHereItem = new("Send robot here");
    private (double X, double Y) _menuPoint;
    private bool _syncingUi;
    private string? _lastState;

    private void BuildControlRow()
    {
        _sensorBox.Items.AddRange(new object[] { "Front only", "Front + left", "Front + right" });
        _sensorBox.SelectedIndex = 0;
        _modeBox.Items.AddRange(new object[] { "FAST", "SLOW" });
        _modeBox.SelectedIndex = 0;
        _controlRow.Controls.AddRange(new Control[]
        {
            Caption("Robot:"), _runBtn, _pauseBtn, _homeBtn, _measureBtn,
            new Label { Width = 12 }, _fwdBtn, _backBtn, _leftBtn, _rightBtn,
            new Label { Width = 12 }, Caption("Wi-Fi mode:"), _modeBox, Caption("Speed %:"), _speedBox,
            Caption("IR sensors:"), _sensorBox, _probeBox, _roamBtn, _resetPosBtn
        });
        _mapMenu.Items.Add(_sendHereItem);
        _map.ContextMenuStrip = _mapMenu;

        _runBtn.Click += (_, _) => SendCommand("RUN", "Start survey");
        _pauseBtn.Click += (_, _) => SendCommand("PAUSE", "Pause");
        _homeBtn.Click += (_, _) => SendCommand("HOME", "Return to start");
        _measureBtn.Click += (_, _) => SendCommand("MEASURE", "Measure here");
        _roamBtn.Click += (_, _) => SendCommand("ROAM", "Find better AP");
        _fwdBtn.Click += (_, _) => SendCommand("STEP,FWD", "Forward");
        _backBtn.Click += (_, _) => SendCommand("STEP,BACK", "Back");
        _leftBtn.Click += (_, _) => SendCommand("STEP,LEFT", "Turn left");
        _rightBtn.Click += (_, _) => SendCommand("STEP,RIGHT", "Turn right");
        _resetPosBtn.Click += (_, _) =>
        {
            if (MessageBox.Show(this, "Make the robot's current position the new start point (0, 0)?\n" +
                                      "The path drawn so far will no longer line up, so the map will be cleared.",
                                "Set start here", MessageBoxButtons.OKCancel) != DialogResult.OK) return;
            SendCommand("RESET", "Set start here");
            ClearAll();
            _world = new RoombaMapper.Core.WorldMap();     // the old map no longer lines up
            MapChangedNow();
            UpdateMissionButtons();
        };
        _modeBox.SelectedIndexChanged += (_, _) =>
        {
            if (!_syncingUi) SendCommand($"MODE,{_modeBox.SelectedItem}", $"{_modeBox.SelectedItem} mode");
        };
        _sensorBox.SelectedIndexChanged += (_, _) =>
        {
            _probeBox.Enabled = _sensorBox.SelectedIndex == 0;   // probing is only for front-only
            if (_syncingUi) return;
            string code = _sensorBox.SelectedIndex switch { 1 => "FL", 2 => "FR", _ => "F" };
            SendCommand($"SENSORS,{code}", $"Sensors: {_sensorBox.SelectedItem}");
        };
        _probeBox.CheckedChanged += (_, _) =>
        {
            if (!_syncingUi) SendCommand(_probeBox.Checked ? "PROBE,ON" : "PROBE,OFF", "Wall probing");
        };
        _speedBox.ValueChanged += (_, _) =>
        {
            if (!_syncingUi && _simRobot == null) SendCommand($"SPEED,{(int)_speedBox.Value}", $"Speed {(int)_speedBox.Value}%");
        };
        _mapMenu.Opening += (_, e) =>
        {
            _menuPoint = _map.ScreenToWorld(_map.PointToClient(Cursor.Position));
            _sendHereItem.Text = $"Send robot here ({_menuPoint.X / 1000:0.00} m, {_menuPoint.Y / 1000:0.00} m)";
            _sendHereItem.Enabled = _active?.CanSend == true;
        };
        _sendHereItem.Click += (_, _) =>
        {
            var (x, y) = _menuPoint;
            SendCommand(string.Create(CultureInfo.InvariantCulture, $"GOTO,{x:0},{y:0}"), "Go to point");
            _map.Target = (x, y);
        };
    }

    /// <summary>Called by the core whenever the connection changes (row usable only with a robot).</summary>
    private void UpdateControlRow()
    {
        bool can = RobotReady;        // real robot, or the simulated one (Speed % sets its animation)
        if (_controlRow.Enabled != can) _controlRow.Enabled = can;
        UpdateMissionButtons();
    }

    private void ControlsOnTelemetry(Telemetry t)
    {
        if (t.State == null) return;

        // Arrived (or stopped) after going somewhere: remove the target marker
        bool wasGoing = _lastState is "GOTO" or "HOME";
        if (wasGoing && t.State is not ("GOTO" or "HOME" or "MEASURE")) _map.Target = null;
        _lastState = t.State;

        // Keep the mode/speed controls matching the robot (without sending commands back)
        _syncingUi = true;
        if (t.Mode is "FAST" or "SLOW" && !_modeBox.Focused) _modeBox.SelectedItem = t.Mode;
        if (t.SpeedPct is int sp && !_speedBox.Focused && _pending.Count == 0)
            _speedBox.Value = Math.Clamp(sp, (int)_speedBox.Minimum, (int)_speedBox.Maximum);
        if (t.Sensors != null && _pending.Count == 0 && !_sensorBox.DroppedDown)
        {
            _sensorBox.SelectedIndex = t.Sensors switch { "FL" => 1, "FR" => 2, _ => 0 };
            _probeBox.Checked = t.Sensors == "F+P";
            _probeBox.Enabled = _sensorBox.SelectedIndex == 0;
        }
        _syncingUi = false;
    }
}
