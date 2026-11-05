using System.Drawing.Imaging;
using System.Globalization;
using System.IO.Ports;
using System.Text;
using RoombaMapper.Sources;

namespace RoombaMapper;

public sealed partial class MainForm : Form
{
    private const double WheelCircumferenceMm = Math.PI * 40.0; // 40 mm wheel

    private readonly MapView _map = new() { Dock = DockStyle.Fill };

    private readonly ComboBox _source = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 140 };
    private readonly ComboBox _comPort = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 90 };
    private readonly Button _refreshPorts = new() { Text = "Refresh", AutoSize = true };
    private readonly TextBox _bleName = new() { Width = 120, Text = "Roomba" };
    private readonly NumericUpDown _udpPort = new() { Minimum = 1024, Maximum = 65535, Value = 4210, Width = 75 };
    private readonly Button _connect = new() { Text = "Connect", Width = 95 };
    private readonly Button _clear = new() { Text = "Clear map", AutoSize = true };
    private readonly Button _fit = new() { Text = "Fit view", AutoSize = true };
    private readonly Button _exportCsv = new() { Text = "Export CSV", AutoSize = true };
    private readonly Button _savePng = new() { Text = "Save image", AutoSize = true };
    private readonly CheckBox _showHeat = new() { Text = "Heat map", Checked = true, AutoSize = true };
    private readonly CheckBox _showPath = new() { Text = "Path", Checked = true, AutoSize = true };

    private readonly FlowLayoutPanel _serialGroup, _bleGroup, _udpGroup;

    // Robot control row
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

    // Commands waiting for the robot to confirm
    private long _nextCmdId = DateTimeOffset.Now.ToUnixTimeSeconds() % 100000 * 1000;
    private readonly Dictionary<long, (string Line, string Label, int Tries, DateTime Sent)> _pending = new();
    private readonly System.Windows.Forms.Timer _retryTimer = new() { Interval = 300 };
    private bool _syncingUi;
    private bool _helloSent;
    private string? _lastState;

    private readonly Label _distLbl = ValueLabel(), _turnsLbl = ValueLabel(), _posLbl = ValueLabel(),
        _headLbl = ValueLabel(), _rssiLbl = ValueLabel(), _irLbl = ValueLabel(), _obstLbl = ValueLabel(),
        _countLbl = ValueLabel(), _stateLbl = ValueLabel(), _apLbl = ValueLabel();
    private readonly Label _statusLbl = new() { AutoSize = true, MaximumSize = new Size(200, 0), ForeColor = Color.DimGray };
    private readonly TextBox _log = new()
    {
        Dock = DockStyle.Bottom, Height = 110, Multiline = true, ReadOnly = true,
        ScrollBars = ScrollBars.Vertical, Font = new Font("Consolas", 9), BackColor = Color.FromArgb(248, 248, 248)
    };

    private DataSourceBase? _active;
    private int _withSignal;

    public MainForm()
    {
        Text = "Roomba Wi-Fi Survey Mapper";
        Size = new Size(1250, 820);
        StartPosition = FormStartPosition.CenterScreen;
        Font = new Font("Segoe UI", 9);

        _source.Items.AddRange(new object[] { "Simulation", "USB Serial", "Bluetooth LE", "Wi-Fi (UDP)" });
        _source.SelectedIndex = 3; // Wi-Fi (UDP)

        _serialGroup = Group(Caption("Port:"), _comPort, _refreshPorts);
        _bleGroup = Group(Caption("Device name:"), _bleName);
        _udpGroup = Group(Caption("UDP port:"), _udpPort);

        var top = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(6), WrapContents = true };
        top.Controls.AddRange(new Control[]
        {
            Caption("Source:"), _source, _serialGroup, _bleGroup, _udpGroup, _connect,
            new Label { Width = 20 }, _clear, _fit, _showHeat, _showPath, _exportCsv, _savePng
        });

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

        var side = new FlowLayoutPanel
        {
            Dock = DockStyle.Right, Width = 230, FlowDirection = FlowDirection.TopDown,
            WrapContents = false, Padding = new Padding(12), AutoScroll = true, BackColor = Color.FromArgb(250, 250, 250)
        };
        AddStat(side, "Robot state", _stateLbl);
        AddStat(side, "Distance travelled", _distLbl);
        AddStat(side, "Wheel rotations (40 mm wheel)", _turnsLbl);
        AddStat(side, "Position", _posLbl);
        AddStat(side, "Heading", _headLbl);
        AddStat(side, "Wi-Fi signal", _rssiLbl);
        AddStat(side, "Access point (MAC end, channel)", _apLbl);
        AddStat(side, "IR sensors: something there?", _irLbl);
        AddStat(side, "Obstacles", _obstLbl);
        AddStat(side, "Readings", _countLbl);
        side.Controls.Add(new Label { Text = "Status", AutoSize = true, ForeColor = Color.Gray, Margin = new Padding(0, 16, 0, 0) });
        side.Controls.Add(_statusLbl);

        // Order matters for docking: the Fill control is added first.
        Controls.Add(_map);
        Controls.Add(side);
        Controls.Add(_log);
        Controls.Add(_missionRow);
        Controls.Add(_controlRow);
        Controls.Add(top);

        _source.SelectedIndexChanged += (_, _) => UpdateSourceUi();
        _refreshPorts.Click += (_, _) => RefreshPorts();
        _connect.Click += ConnectClicked;
        _clear.Click += (_, _) => ClearAll();
        _fit.Click += (_, _) => _map.FitToData();
        _showHeat.CheckedChanged += (_, _) => _map.ShowHeat = _showHeat.Checked;
        _showPath.CheckedChanged += (_, _) => _map.ShowPath = _showPath.Checked;
        _exportCsv.Click += (_, _) => ExportCsv();
        _savePng.Click += (_, _) => SavePng();
        FormClosing += (_, _) => Disconnect();

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
            if (!_syncingUi) SendCommand($"SPEED,{(int)_speedBox.Value}", $"Speed {(int)_speedBox.Value}%");
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
        _retryTimer.Tick += (_, _) => RetryCommands();
        _retryTimer.Start();

        BuildMissionRow();
        RefreshPorts();
        UpdateSourceUi();
        UpdateControlRow();
        ClearAll();
        SetStatus("Ready. Press Connect to listen for the robot on Wi-Fi.");
    }

    // ---------------------------------------------------------------- UI helpers

    private static Button CtlButton(string text) => new() { Text = text, AutoSize = true };

    private static Label Caption(string text) =>
        new() { Text = text, AutoSize = true, Margin = new Padding(6, 7, 2, 3) };

    private static Label ValueLabel() =>
        new() { AutoSize = true, Font = new Font("Segoe UI", 14, FontStyle.Bold), Margin = new Padding(0, 0, 0, 8) };

    private static FlowLayoutPanel Group(params Control[] controls)
    {
        var p = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = Padding.Empty };
        p.Controls.AddRange(controls);
        return p;
    }

    private static void AddStat(Control parent, string caption, Label value)
    {
        parent.Controls.Add(new Label { Text = caption, AutoSize = true, ForeColor = Color.Gray });
        parent.Controls.Add(value);
    }

    private void UpdateSourceUi()
    {
        _serialGroup.Visible = _source.SelectedIndex == 1;
        _bleGroup.Visible = _source.SelectedIndex == 2;
        _udpGroup.Visible = _source.SelectedIndex == 3;
    }

    private void RefreshPorts()
    {
        _comPort.Items.Clear();
        _comPort.Items.AddRange(SerialPort.GetPortNames().OrderBy(p => p).Cast<object>().ToArray());
        if (_comPort.Items.Count > 0) _comPort.SelectedIndex = _comPort.Items.Count - 1;
    }

    // ---------------------------------------------------------------- connecting

    private async void ConnectClicked(object? sender, EventArgs e)
    {
        if (_active != null) { Disconnect(); return; }

        DataSourceBase? src;
        switch (_source.SelectedIndex)
        {
            case 0: src = new SimulationSource(); break;
            case 1:
                if (_comPort.SelectedItem is not string port) { SetStatus("No COM port selected. Plug in the TinyScreen+ and press Refresh."); return; }
                src = new SerialSource(port); break;
            case 2: src = new BleSource(_bleName.Text.Trim()); break;
            default: src = new UdpSource((int)_udpPort.Value); break;
        }

        src.TelemetryReceived += t => OnUi(src, () => OnTelemetry(t));
        src.ObstacleDetected += o => OnUi(src, () => OnObstacle(o));
        src.ShapeFinished += sh => OnUi(src, () => OnShape(sh));
        src.CommandAcknowledged += ack => OnUi(src, () => OnAck(ack));
        src.ReadingReceived += r =>
        {
            src.Send($"A,{r.Seq}");             // confirm straight away, so the robot can delete it
            OnUi(src, () => OnReading(r));
        };
        src.RobotMessage += msg => OnUi(src, () => SetStatus("Robot: " + msg));
        src.Status += m => OnUi(src, () => SetStatus(m));
        src.RawLine += l => OnUi(src, () => AppendLog(l));

        _active = src;
        _connect.Text = "Disconnect";
        _source.Enabled = false;
        try
        {
            await src.StartAsync();
        }
        catch (Exception ex)
        {
            var msg = string.IsNullOrWhiteSpace(ex.Message) ? "(no message)" : ex.Message.Trim();
            SetStatus($"Could not connect: {msg} [{ex.GetType().Name}, 0x{ex.HResult:X8}]");
            Disconnect();
        }
    }

    private void Disconnect()
    {
        var src = _active;
        _active = null;
        src?.Dispose();
        _connect.Text = "Connect";
        _source.Enabled = true;
        _pending.Clear();
        _helloSent = false;
        _missionCts?.Cancel();
        UpdateControlRow();
        if (src != null) SetStatus($"Disconnected from {src.Name}");
    }

    /// <summary>Run on the UI thread, but only if this source is still the active one.</summary>
    private void OnUi(DataSourceBase src, Action action)
    {
        if (IsDisposed || !IsHandleCreated) return;
        BeginInvoke(new Action(() => { if (ReferenceEquals(src, _active)) action(); }));
    }

    // ---------------------------------------------------------------- data

    private void OnTelemetry(Telemetry t)
    {
        _lastTelemetry = t;
        _telemetrySeq++;
        PushRoomWalls(t.X, t.Y, RobotRadiusMm, null);   // the robot is inside the room
        if (_coverageActive && _markingLane) MarkCovered(t.X, t.Y);   // lane driving = covered
        // Phase 1: record the path once the robot has reached the first wall
        if (_recordingRoomTrace && t.State == "TRACE" && _map.Obstacles.Count > _roomTraceStartPoints)
            _roomTracePath.Add((t.X, t.Y));

        // First data from a robot we can talk to: say hello, so the robot learns this
        // PC's address and sends straight here (more reliable than broadcasting)
        if (!_helloSent && _active?.CanSend == true)
        {
            _helloSent = true;
            SendCommand("HELLO", "Hello");
        }
        _map.AddSample(t);
        if (t.Rssi.HasValue) _withSignal++;

        _distLbl.Text = $"{t.DistanceMm / 1000:0.00} m";
        _turnsLbl.Text = $"{t.DistanceMm / WheelCircumferenceMm:0.0}";
        _posLbl.Text = $"x {t.X / 1000:0.00}  y {t.Y / 1000:0.00} m";
        _headLbl.Text = $"{t.HeadingDeg:0}°";
        if (t.Rssi is double r)   // keep showing the last reading until a new one arrives
        {
            string quality = r >= -50 ? "Excellent" : r >= -70 ? "Good" : "Weak";
            _rssiLbl.Text = $"{r:0} dBm  {quality}";
            _rssiLbl.ForeColor = _map.ColorFor(r);
        }
        if (t.Rssi.HasValue) _countLbl.Text = $"{_withSignal} Wi-Fi readings";

        if (t.AccessPoint != null) _apLbl.Text = t.AccessPoint;

        if (t.State != null)
        {
            _stateLbl.Text = $"{t.State}  {t.Mode}  {t.SpeedPct}%";
            _stateLbl.ForeColor = t.State == "PAUSED" ? Color.DarkGoldenrod : Color.SteelBlue;

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
        UpdateControlRow();

        if (t.Ir is null)
        {
            _irLbl.Text = "no sensor";
            _irLbl.ForeColor = Color.Gray;
        }
        else
        {
            static string F(bool? b) => b == true ? "YES" : "no";
            _irLbl.Text = t.Sensors switch
            {
                "FL" => $"Front {F(t.Ir)} · Left {F(t.IrLeft)}",
                "FR" => $"Front {F(t.Ir)} · Right {F(t.IrRight)}",
                _ => F(t.Ir)
            };
            bool any = t.Ir == true || t.IrLeft == true || t.IrRight == true;
            _irLbl.ForeColor = any ? Color.Firebrick : Color.SeaGreen;
        }
    }

    // Seq numbers seen recently: the robot resends a reading if our confirmation got lost
    private readonly Dictionary<int, DateTime> _recentSeqs = new();

    private void OnReading(WifiReading r)
    {
        var now = DateTime.Now;
        if (_recentSeqs.TryGetValue(r.Seq, out var seen) && (now - seen).TotalSeconds < 60) return;  // duplicate
        _recentSeqs[r.Seq] = now;
        if (_recentSeqs.Count > 2000)
            foreach (var old in _recentSeqs.Where(kv => (now - kv.Value).TotalSeconds > 60).Select(kv => kv.Key).ToList())
                _recentSeqs.Remove(old);

        _map.AddReading(r);
        _withSignal++;
        string quality = r.Rssi >= -50 ? "Excellent" : r.Rssi >= -70 ? "Good" : "Weak";
        _rssiLbl.Text = r.Mode == 'S' ? $"{r.Rssi} dBm  {quality} (avg)" : $"{r.Rssi} dBm  {quality}";
        _rssiLbl.ForeColor = _map.ColorFor(r.Rssi);
        _countLbl.Text = $"{_withSignal} Wi-Fi readings";
    }

    private void OnObstacle(ObstacleEvent o)
    {
        _lastObstacle = o;
        _obstacleSeq++;
        if (o.ShapeId != _roomShapeId && o.FacingDeg is double f)
            PushRoomWalls(o.X, o.Y, 0, f);              // something solid out there
        if (o.ShapeId > 0) _lastTraceShapeId = o.ShapeId;
        _map.AddObstacle(o);
        UpdateShapeLabel();
        if (_showNoGo.Checked && o.ShapeId == 0) RefreshNoGo();
    }

    private void OnShape(ShapeEvent e)
    {
        _map.FinishShape(e);
        _lastShape = e;
        _shapeSeq++;
        UpdateCorners(e.ShapeId);
        UpdateShapeLabel();
        RefreshNoGo();
        string what = e.Kind switch { 1 => "object", 2 => "room wall", _ => "incomplete outline" };
        SetStatus($"Outline #{e.ShapeId} finished: {what}");
    }

    private void UpdateShapeLabel() =>
        _obstLbl.Text = _map.ShapeCount > 0
            ? $"{_map.ShapeCount} outlines ({_map.ObjectCount} objects)"
            : _map.Obstacles.Count.ToString();

    private void ClearAll()
    {
        _map.Clear();
        _roomShapeId = null;
        _roomWalls = null;
        _covered.Clear();
        _gaveUp.Clear();
        _withSignal = 0;
        _distLbl.Text = "0.00 m";
        _turnsLbl.Text = "0.0";
        _posLbl.Text = "x 0.00  y 0.00 m";
        _headLbl.Text = "0°";
        _rssiLbl.Text = "no reading";
        _rssiLbl.ForeColor = Color.Gray;
        _countLbl.Text = "0";
        _irLbl.Text = "no sensor";
        _irLbl.ForeColor = Color.Gray;
        _obstLbl.Text = "0";
        _stateLbl.Text = "unknown";
        _apLbl.Text = "unknown";
        _apLbl.ForeColor = Color.SteelBlue;
        _stateLbl.ForeColor = Color.Gray;
        _map.Target = null;
    }

    private void SetStatus(string message)
    {
        _statusLbl.Text = message;
        AppendLog("[status] " + message);
    }

    private void AppendLog(string line)
    {
        if (_log.TextLength > 30000) _log.Text = _log.Text[^15000..];
        _log.AppendText(line + Environment.NewLine);
    }

    // ---------------------------------------------------------------- robot commands

    private void UpdateControlRow()
    {
        bool can = _active?.CanSend == true;
        if (_controlRow.Enabled != can) _controlRow.Enabled = can;
        UpdateMissionButtons();
    }

    /// <summary>Sends a command and keeps resending until the robot confirms it.</summary>
    /// <summary>Sends a command (resent until confirmed). Returns its id, or -1 if it couldn't be sent.</summary>
    private long SendCommand(string command, string label)
    {
        if (_active is not { CanSend: true } src)
        {
            SetStatus("Can't reach the robot yet. Connect and wait until its data arrives.");
            return -1;
        }
        long id = ++_nextCmdId;
        string line = $"C,{id},{command}";
        _pending[id] = (line, label, 1, DateTime.Now);
        src.Send(line);
        AppendLog("> " + (command.StartsWith("SET,pass,") ? $"C,{id},SET,pass,********" : line));
        return id;
    }

    private void RetryCommands()
    {
        if (_pending.Count == 0 || _active is not { CanSend: true } src) return;
        foreach (var (id, p) in _pending.ToList())
        {
            if ((DateTime.Now - p.Sent).TotalMilliseconds < 600) continue;
            if (p.Tries >= 15)   // ~9 s: allows for a Wi-Fi reconnect
            {
                _pending.Remove(id);
                _failedCommands.Add(id);
                SetStatus($"No reply from the robot for \"{p.Label}\". Check it's powered and on Wi-Fi.");
                continue;
            }
            _pending[id] = (p.Line, p.Label, p.Tries + 1, DateTime.Now);
            src.Send(p.Line);
        }
    }

    private void OnAck(CommandAck ack)
    {
        if (_pending.Remove(ack.Id)) SetStatus("Robot: " + ack.Text);
    }

    // ---------------------------------------------------------------- saving

    private void ExportCsv()
    {
        if (_map.Samples.Count == 0 && _map.Readings.Count == 0) { SetStatus("Nothing to export yet."); return; }
        using var dlg = new SaveFileDialog { Filter = "CSV files|*.csv", FileName = $"wifi_survey_{DateTime.Now:yyyyMMdd_HHmm}.csv" };
        if (dlg.ShowDialog(this) != DialogResult.OK) return;
        File.WriteAllText(dlg.FileName, BuildCsv());
        SetStatus($"Saved {_map.Samples.Count} positions and {_map.Readings.Count} Wi-Fi readings to {Path.GetFileName(dlg.FileName)}");
    }

    private string BuildCsv()
    {
        var inv = CultureInfo.InvariantCulture;
        static string B(bool? b) => b switch { true => "1", false => "0", null => "" };
        var sb = new StringBuilder("type,time_ms,x_mm,y_mm,heading_deg,distance_mm,rssi_dbm,ir_front,ir_left,ir_right,shape_id\n");
        foreach (var t in _map.Samples)
            sb.AppendLine(string.Join(",", "P",
                t.TimeMs.ToString(inv), t.X.ToString("0.#", inv), t.Y.ToString("0.#", inv),
                t.HeadingDeg.ToString("0.#", inv), t.DistanceMm.ToString("0.#", inv),
                t.Rssi?.ToString("0", inv) ?? "",
                B(t.Ir), B(t.IrLeft), B(t.IrRight), ""));
        foreach (var r in _map.Readings)   // Wi-Fi readings (R): rssi at x,y; ir columns unused
            sb.AppendLine(string.Join(",", "R", r.Seq.ToString(inv),
                r.X.ToString("0.#", inv), r.Y.ToString("0.#", inv), "", "",
                r.Rssi.ToString(inv), "", "", "", r.Mode.ToString()));
        foreach (var o in _map.Obstacles)
            sb.AppendLine(string.Join(",", "O", o.TimeMs.ToString(inv),
                o.X.ToString("0.#", inv), o.Y.ToString("0.#", inv), "", "", "", "", "", "",
                o.ShapeId.ToString(inv)));
        return sb.ToString();
    }

    /// <summary>
    /// Saves the heat map as a picture (whole room in view, without the planned route
    /// or target) plus the readings as a CSV with the same name.
    /// </summary>
    private void ExportHeatmap()
    {
        using var dlg = new SaveFileDialog
        {
            Filter = "PNG image|*.png", FileName = $"wifi_heatmap_{DateTime.Now:yyyyMMdd_HHmm}.png",
            Title = "Export heat map (the readings are saved as a CSV next to it)"
        };
        if (dlg.ShowDialog(this) != DialogResult.OK) return;

        var route = _map.PlannedPath;
        var target = _map.Target;
        _map.PlannedPath = null;
        _map.Target = null;
        _map.FitToData();
        try
        {
            using var bmp = new Bitmap(_map.Width, _map.Height);
            _map.DrawToBitmap(bmp, new Rectangle(0, 0, bmp.Width, bmp.Height));
            bmp.Save(dlg.FileName, ImageFormat.Png);
        }
        finally
        {
            _map.PlannedPath = route;
            _map.Target = target;
        }
        string csvPath = Path.ChangeExtension(dlg.FileName, ".csv");
        File.WriteAllText(csvPath, BuildCsv());
        SetStatus($"Heat map saved: {Path.GetFileName(dlg.FileName)} and {Path.GetFileName(csvPath)}");
    }

    private void SavePng()
    {
        using var dlg = new SaveFileDialog { Filter = "PNG image|*.png", FileName = $"wifi_heatmap_{DateTime.Now:yyyyMMdd_HHmm}.png" };
        if (dlg.ShowDialog(this) != DialogResult.OK) return;

        using var bmp = new Bitmap(_map.Width, _map.Height);
        _map.DrawToBitmap(bmp, new Rectangle(0, 0, bmp.Width, bmp.Height));
        bmp.Save(dlg.FileName, ImageFormat.Png);
        SetStatus($"Saved image {Path.GetFileName(dlg.FileName)}");
    }
}
