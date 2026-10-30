using System.Drawing.Imaging;
using System.Globalization;
using System.IO.Ports;
using System.Text;
using RoombaMapper.Sources;

namespace RoombaMapper;

public sealed class MainForm : Form
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

    private readonly Label _distLbl = ValueLabel(), _turnsLbl = ValueLabel(), _posLbl = ValueLabel(),
        _headLbl = ValueLabel(), _rssiLbl = ValueLabel(), _irLbl = ValueLabel(), _obstLbl = ValueLabel(),
        _countLbl = ValueLabel();
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
        _source.SelectedIndex = 0;

        _serialGroup = Group(Caption("Port:"), _comPort, _refreshPorts);
        _bleGroup = Group(Caption("Device name:"), _bleName);
        _udpGroup = Group(Caption("UDP port:"), _udpPort);

        var top = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(6), WrapContents = true };
        top.Controls.AddRange(new Control[]
        {
            Caption("Source:"), _source, _serialGroup, _bleGroup, _udpGroup, _connect,
            new Label { Width = 20 }, _clear, _fit, _showHeat, _showPath, _exportCsv, _savePng
        });

        var side = new FlowLayoutPanel
        {
            Dock = DockStyle.Right, Width = 230, FlowDirection = FlowDirection.TopDown,
            WrapContents = false, Padding = new Padding(12), AutoScroll = true, BackColor = Color.FromArgb(250, 250, 250)
        };
        AddStat(side, "Distance travelled", _distLbl);
        AddStat(side, "Wheel rotations (40 mm wheel)", _turnsLbl);
        AddStat(side, "Position", _posLbl);
        AddStat(side, "Heading", _headLbl);
        AddStat(side, "Wi-Fi signal", _rssiLbl);
        AddStat(side, "IR sensor: obstacle ahead?", _irLbl);
        AddStat(side, "Obstacles marked", _obstLbl);
        AddStat(side, "Readings", _countLbl);
        side.Controls.Add(new Label { Text = "Status", AutoSize = true, ForeColor = Color.Gray, Margin = new Padding(0, 16, 0, 0) });
        side.Controls.Add(_statusLbl);

        // Order matters for docking: the Fill control is added first.
        Controls.Add(_map);
        Controls.Add(side);
        Controls.Add(_log);
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

        RefreshPorts();
        UpdateSourceUi();
        ClearAll();
        SetStatus("Ready. Try Simulation first to see how it works.");
    }

    // ---------------------------------------------------------------- UI helpers

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
        _map.AddSample(t);
        if (t.Rssi.HasValue) _withSignal++;

        _distLbl.Text = $"{t.DistanceMm / 1000:0.00} m";
        _turnsLbl.Text = $"{t.DistanceMm / WheelCircumferenceMm:0.0}";
        _posLbl.Text = $"x {t.X / 1000:0.00}  y {t.Y / 1000:0.00} m";
        _headLbl.Text = $"{t.HeadingDeg:0}°";
        _rssiLbl.Text = t.Rssi is double r ? $"{r:0} dBm" : "no reading";
        _rssiLbl.ForeColor = t.Rssi is double rr ? _map.ColorFor(rr) : Color.Gray;
        _countLbl.Text = $"{_map.Samples.Count} ({_withSignal} with signal)";

        _irLbl.Text = t.Ir switch { true => "YES", false => "no", null => "no sensor" };
        _irLbl.ForeColor = t.Ir switch { true => Color.Firebrick, false => Color.SeaGreen, null => Color.Gray };
    }

    private void OnObstacle(ObstacleEvent o)
    {
        _map.AddObstacle(o);
        _obstLbl.Text = _map.Obstacles.Count.ToString();
    }

    private void ClearAll()
    {
        _map.Clear();
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

    // ---------------------------------------------------------------- saving

    private void ExportCsv()
    {
        if (_map.Samples.Count == 0) { SetStatus("Nothing to export yet."); return; }
        using var dlg = new SaveFileDialog { Filter = "CSV files|*.csv", FileName = $"wifi_survey_{DateTime.Now:yyyyMMdd_HHmm}.csv" };
        if (dlg.ShowDialog(this) != DialogResult.OK) return;

        var inv = CultureInfo.InvariantCulture;
        var sb = new StringBuilder("type,time_ms,x_mm,y_mm,heading_deg,distance_mm,rssi_dbm,ir\n");
        foreach (var t in _map.Samples)
            sb.AppendLine(string.Join(",", "P",
                t.TimeMs.ToString(inv), t.X.ToString("0.#", inv), t.Y.ToString("0.#", inv),
                t.HeadingDeg.ToString("0.#", inv), t.DistanceMm.ToString("0.#", inv),
                t.Rssi?.ToString("0", inv) ?? "",
                t.Ir switch { true => "1", false => "0", null => "" }));
        foreach (var o in _map.Obstacles)
            sb.AppendLine(string.Join(",", "O", o.TimeMs.ToString(inv),
                o.X.ToString("0.#", inv), o.Y.ToString("0.#", inv), "", "", "", ""));
        File.WriteAllText(dlg.FileName, sb.ToString());
        SetStatus($"Saved {_map.Samples.Count} readings to {Path.GetFileName(dlg.FileName)}");
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
