using System.Globalization;
using System.IO.Ports;
using RoombaMapper.Sources;

namespace RoombaMapper;

/// <summary>
/// The main window's core: choosing a source and connecting, receiving its lines,
/// sending commands (resent until confirmed), the status line and the log.
/// Everything else is a feature: Features.cs switches them on or off, and the feature
/// table (MainForm.Features.cs) says what each one hooks into. One file per feature:
///   MainForm.Controls.cs   robot control row         MainForm.Mission.cs  mapping phases
///   MainForm.Glove.cs      survey glove              MainForm.Stats.cs    right-hand panel
///   MainForm.MapLayers.cs  heat map / path toggles   MainForm.Export.cs   CSV, image, heat map
///   MainForm.Debug.cs      glove debug panel
/// </summary>
public sealed partial class MainForm : Form
{
    private readonly MapView _map = new() { Dock = DockStyle.Fill };

    // Source names, in the order they appear in the list (only the switched-on ones are added)
    private const string SrcSimulation = "Simulation";
    private const string SrcSerial = "USB Serial";
    private const string SrcBle = "Bluetooth LE";
    private const string SrcUdp = "Wi-Fi (UDP)";
    private const string SrcSimRobot = "Simulated robot (no hardware)";

    private readonly ComboBox _source = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 140 };
    private readonly ComboBox _comPort = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 90 };
    private readonly Button _refreshPorts = new() { Text = "Refresh", AutoSize = true };
    private readonly TextBox _bleName = new() { Width = 120, Text = "Roomba" };
    private readonly NumericUpDown _udpPort = new() { Minimum = 1024, Maximum = 65535, Value = 4210, Width = 75 };
    private readonly TextBox _deviceIp = new() { Width = 110, PlaceholderText = "device IP (optional)" };
    private readonly Button _connect = new() { Text = "Connect", Width = 95 };
    private readonly Button _clear = new() { Text = "Clear map", AutoSize = true };
    private readonly Button _fit = new() { Text = "Fit view", AutoSize = true };

    private readonly FlowLayoutPanel _serialGroup, _bleGroup, _udpGroup;

    /// <summary>Top bar: features add their buttons here.</summary>
    private readonly FlowLayoutPanel _top = new() { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(6), WrapContents = true };

    /// <summary>Right-hand panel: the stats feature adds its values here, the status line goes last.</summary>
    private readonly FlowLayoutPanel _side = new()
    {
        Dock = DockStyle.Right, Width = 230, FlowDirection = FlowDirection.TopDown,
        WrapContents = false, Padding = new Padding(12), AutoScroll = true, BackColor = Color.FromArgb(250, 250, 250)
    };

    // Commands waiting for the robot to confirm
    private long _nextCmdId = DateTimeOffset.Now.ToUnixTimeSeconds() % 100000 * 1000;
    private readonly Dictionary<long, (string Line, string Label, int Tries, DateTime Sent)> _pending = new();
    private readonly System.Windows.Forms.Timer _retryTimer = new() { Interval = 300 };
    private bool _helloSent;

    private readonly Label _statusLbl = new() { AutoSize = true, MaximumSize = new Size(200, 0), ForeColor = Color.DimGray };
    private readonly TextBox _log = new()
    {
        Dock = DockStyle.Bottom, Height = 110, Multiline = true, ReadOnly = true,
        ScrollBars = ScrollBars.Vertical, Font = new Font("Consolas", 9), BackColor = Color.FromArgb(248, 248, 248)
    };

    private DataSourceBase? _active;
    private readonly FeatureRunner _features;

    public MainForm()
    {
        Text = "Roomba Wi-Fi Survey Mapper";
        Size = new Size(1250, 820);
        StartPosition = FormStartPosition.CenterScreen;
        Font = new Font("Segoe UI", 9);

        if (Features.SourceSimulation) _source.Items.Add(SrcSimulation);
        if (Features.SourceUsbSerial) _source.Items.Add(SrcSerial);
        if (Features.SourceBluetoothLe) _source.Items.Add(SrcBle);
        if (Features.SourceWifiUdp) _source.Items.Add(SrcUdp);
        if (Features.SourceSimulatedRobot) _source.Items.Add(SrcSimRobot);
        if (_source.Items.Count > 0) _source.SelectedIndex = Math.Max(0, _source.Items.IndexOf(SrcUdp));

        _serialGroup = Group(Caption("Port:"), _comPort, _refreshPorts);
        _bleGroup = Group(Caption("Device name:"), _bleName);
        _udpGroup = Group(Caption("UDP port:"), _udpPort, Caption("Device IP:"), _deviceIp);

        _top.Controls.AddRange(new Control[]
        {
            Caption("Source:"), _source, _serialGroup, _bleGroup, _udpGroup, _connect,
            new Label { Width = 20 }, _clear, _fit
        });

        // The robot link and the map's world are used by several features: always made
        InitMissionCore();

        // Every switched-on feature adds its rows, buttons and labels
        _features = new FeatureRunner(FeatureTable());
        _features.DispatchBuild();

        _side.Controls.Add(new Label { Text = "Status", AutoSize = true, ForeColor = Color.Gray, Margin = new Padding(0, 16, 0, 0) });
        _side.Controls.Add(_statusLbl);

        // Order matters for docking: the Fill control is added first.
        Controls.Add(_map);
        Controls.Add(_side);
        if (Features.LogPanel) Controls.Add(_log);
        if (Features.GloveMode) Controls.Add(_gloveRow);
        if (_missionRow.Controls.Count > 0) Controls.Add(_missionRow);
        if (Features.RobotControls) Controls.Add(_controlRow);
        Controls.Add(_top);

        _source.SelectedIndexChanged += (_, _) => UpdateSourceUi();
        _refreshPorts.Click += (_, _) => RefreshPorts();
        _connect.Click += ConnectClicked;
        _clear.Click += (_, _) => ClearAll();
        _fit.Click += (_, _) => _map.FitToData();
        FormClosing += (_, _) => Disconnect();

        _retryTimer.Tick += (_, _) => RetryCommands();
        _retryTimer.Start();

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

    private string SelectedSource => _source.SelectedItem as string ?? "";

    private void UpdateSourceUi()
    {
        _serialGroup.Visible = SelectedSource == SrcSerial;
        _bleGroup.Visible = SelectedSource == SrcBle;
        _udpGroup.Visible = SelectedSource == SrcUdp;
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
        if (_active != null || _simRobot != null) { Disconnect(); return; }

        if (SelectedSource == SrcSimRobot)
        {
            // A robot inside the app, in a virtual room: test the mapping phases with no hardware
            ApplyConfig();
            _simRobot = new SimRobot(SimRooms.Demo(), _cfg) { FastReadings = true };
            _connect.Text = "Disconnect";
            _source.Enabled = false;
            ClearAll();
            UpdateControlRow();
            SetStatus("Simulated robot ready in a virtual 2.1 x 2.4 m room (alcove, a box, a small table). " +
                      "Press 1 Map room. Speed % sets how fast it animates.");
            return;
        }

        DataSourceBase? src;
        switch (SelectedSource)
        {
            case SrcSimulation: src = new SimulationSource(); break;
            case SrcSerial:
                if (_comPort.SelectedItem is not string port) { SetStatus("No COM port selected. Plug in the TinyScreen+ and press Refresh."); return; }
                src = new SerialSource(port); break;
            case SrcBle: src = new BleSource(_bleName.Text.Trim()); break;
            case SrcUdp: src = new UdpSource((int)_udpPort.Value, _deviceIp.Text); break;
            default: SetStatus("No source switched on (see Features.cs)."); return;
        }

        src.TelemetryReceived += t => OnUi(src, () => OnTelemetry(t));
        src.ObstacleDetected += o => OnUi(src, () => OnObstacle(o));
        src.ShapeFinished += sh => OnUi(src, () => OnShape(sh));
        src.CommandAcknowledged += ack => OnUi(src, () => OnAck(ack));
        src.ResultLine += line => OnUi(src, () => _link.OnResultLine(line));
        src.ReadingReceived += r =>
        {
            src.Send($"A,{r.Seq}");             // confirm straight away, so the robot can delete it
            OnUi(src, () => OnReading(r));
        };
        src.FeatureLine += line => OnUi(src, () => _features.DispatchFeatureLine(line));
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
        _simRobot = null;
        _connect.Text = "Connect";
        _source.Enabled = true;
        _pending.Clear();
        _helloSent = false;
        _missionCts?.Cancel();
        _features?.DispatchDisconnected();
        UpdateControlRow();
        if (src != null) SetStatus($"Disconnected from {src.Name}");
    }

    /// <summary>Run on the UI thread, but only if this source is still the active one.</summary>
    private void OnUi(DataSourceBase src, Action action)
    {
        if (IsDisposed || !IsHandleCreated) return;
        BeginInvoke(new Action(() => { if (ReferenceEquals(src, _active)) action(); }));
    }

    // ---------------------------------------------------------------- data: core, then every feature

    private void OnTelemetry(Telemetry t)
    {
        _lastTelemetry = t;
        _telemetrySeq++;
        _link.OnTelemetry(t);                        // pose + "command finished" for the mapping phases
        _features.DispatchTelemetry(t);

        // First data from a robot we can talk to: say hello, so the robot learns this
        // PC's address and sends straight here (more reliable than broadcasting)
        if (!_helloSent && _active?.CanSend == true)
        {
            _helloSent = true;
            SendCommand("HELLO", "Hello");
        }
        _map.AddSample(t);
        UpdateControlRow();
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
        _features.DispatchReading(r);
    }

    private void OnObstacle(ObstacleEvent o)
    {
        _map.AddObstacle(o);                         // raw robot hits (the mapped ones are in _world)
        _features.DispatchObstacle(o);
        UpdateShapeLabel();
    }

    private void OnShape(ShapeEvent e)
    {
        _map.FinishShape(e);
        UpdateShapeLabel();
        string what = e.Kind switch { 1 => "object", 2 => "room wall", _ => "incomplete outline" };
        SetStatus($"Outline #{e.ShapeId} finished: {what}");
    }

    private void ClearAll()
    {
        _map.Clear();                                // the display only; the room map is _world
        _map.Target = null;
        _features?.DispatchClear();
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

    // ---------------------------------------------------------------- commands

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
        // During a mapping phase there are hundreds of commands: keep the status line for the phase
        if (_pending.Remove(ack.Id) && _missionCts == null) SetStatus("Robot: " + ack.Text);
    }
}
