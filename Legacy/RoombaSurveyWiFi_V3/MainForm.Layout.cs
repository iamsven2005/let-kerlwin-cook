using RoombaMapper.Sources;
using RoombaMapper.Ui;

namespace RoombaMapper;

/// <summary>
/// The V3 shell: header, connection bar, navigation rail with pages, the map with its
/// toolbar and log, and the inspector. It only ARRANGES the controls declared in the other
/// MainForm files (their event handlers are wired there and are unchanged), and adds
/// read-only displays of state the app already keeps (connection, last-data time,
/// pending commands). Pages are hidden, never rebuilt, so switching pages does not touch
/// the connection, the map or a running phase.
/// </summary>
public sealed partial class MainForm
{
    /// <summary>Display threshold only: data older than this is shown as stale.</summary>
    private const double StaleSeconds = 3;

    private readonly ToolTip _tips = new() { AutoPopDelay = 20000, InitialDelay = 400 };
    private readonly NavRail _nav = new() { Width = 92 };
    private readonly Panel _pageHost = new() { Dock = DockStyle.Left, Width = 284, BackColor = Theme.Canvas };
    private readonly Splitter _pageSplitter = new() { Dock = DockStyle.Left, Width = 4, BackColor = Theme.Border };
    private readonly Panel _inspector = new() { Dock = DockStyle.Right, Width = 272, AutoScroll = true, BackColor = Theme.Canvas, Padding = new Padding(8, 8, 8, 0) };
    private readonly Splitter _inspectorSplitter = new() { Dock = DockStyle.Right, Width = 4, BackColor = Theme.Border };
    private readonly Button _inspectorToggle = new() { Text = "Show inspector", AutoSize = true, Visible = false };
    private readonly Button _inspectorHide = new() { Text = "›", Size = new Size(26, 26), AccessibleName = "Hide inspector" };
    private readonly List<Panel> _pages = new();
    private bool _pageAutoHidden;

    // Header
    private readonly Pill _modeBadge = new() { Text = "NOT CONNECTED" };
    private readonly Label _linkDot = new() { Text = "●", AutoSize = true, ForeColor = Theme.ChromeMuted, Font = Theme.Body };
    private readonly Label _linkLbl = new() { AutoEllipsis = true, ForeColor = Theme.ChromeMuted, TextAlign = ContentAlignment.MiddleLeft };

    // Read-only displays
    private readonly Swatch _rssiSwatch = new() { Margin = new Padding(0, 7, 6, 0) };
    private readonly Label _inspectorNote = new() { Dock = DockStyle.Top, Padding = new Padding(6, 4, 6, 4), Font = Theme.Small };
    private readonly Label _robotHint = new() { Dock = DockStyle.Top, Padding = new Padding(8, 6, 8, 6), Font = Theme.Small };
    private readonly Panel _robotHintBox = new() { Dock = DockStyle.Top, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Padding = new Padding(12, 0, 12, 10) };
    private readonly Label _missionHint = new() { Dock = DockStyle.Top, Padding = new Padding(8, 6, 8, 6), Font = Theme.Small };
    private readonly Label _cursorLbl = new() { AutoSize = true, ForeColor = Theme.TextMuted, Margin = new Padding(10, 8, 0, 0) };
    private readonly Label _diagSource = ValueLabel(), _diagCmdPath = ValueLabel(), _diagTelemetry = ValueLabel(),
        _diagReadings = ValueLabel(), _diagPositions = ValueLabel(), _diagPending = ValueLabel(), _diagLastData = ValueLabel(), _diagFeatures = ValueLabel();
    private readonly System.Windows.Forms.Timer _uiClock = new() { Interval = 500 };
    private DateTime _lastDataAt = DateTime.MinValue;

    /// <summary>Asks before a destructive action. Replaceable so tests needn't click a dialog.</summary>
    private Func<string, string, bool> _confirm = null!;

    private void BuildShell()
    {
        _confirm = (text, title) =>
            MessageBox.Show(this, text, title, MessageBoxButtons.OKCancel, MessageBoxIcon.Warning) == DialogResult.OK;

        var center = new Panel { Dock = DockStyle.Fill, BackColor = Theme.Surface };
        center.Controls.Add(_map);
        center.Controls.Add(BuildLogPanel(center));      // MainForm.Log.cs: splitter + log, docked bottom
        center.Controls.Add(BuildMapToolbar());

        BuildPages();
        BuildInspector();

        // Docking: the Fill control first; the last one added takes the outermost place
        Controls.Add(center);
        Controls.Add(_inspectorSplitter);
        Controls.Add(_inspector);
        Controls.Add(_pageSplitter);
        Controls.Add(_pageHost);
        Controls.Add(_nav);
        Controls.Add(BuildConnectionBar());
        Controls.Add(BuildHeader());

        // Splitter limits are not scaled with the window, so give them in device pixels
        _pageSplitter.MinSize = Theme.Px(this, 240);
        _pageSplitter.MinExtra = Theme.Px(this, 420);
        _inspectorSplitter.MinSize = Theme.Px(this, 220);
        _inspectorSplitter.MinExtra = Theme.Px(this, 420);

        ApplyTheme();
        _nav.Select(0);

        void ShowInspector(bool show)
        {
            _inspector.Visible = _inspectorSplitter.Visible = show;
            _inspectorToggle.Visible = !show;
            ApplyResponsiveLayout();
        }
        _inspectorToggle.Click += (_, _) => ShowInspector(true);
        _inspectorHide.Click += (_, _) => ShowInspector(false);
        _map.MouseMove += (_, e) =>
        {
            var (x, y) = _map.ScreenToWorld(e.Location);
            _cursorLbl.Text = $"Cursor  x {x / 1000:0.00} m   y {y / 1000:0.00} m";
        };
        _map.MouseLeave += (_, _) => _cursorLbl.Text = "";
        Resize += (_, _) => ApplyResponsiveLayout();
        _uiClock.Tick += (_, _) => UpdateLinkUi();
        _uiClock.Start();
        FormClosed += (_, _) => _uiClock.Dispose();
    }

    protected override void OnLoad(EventArgs e)
    {
        base.OnLoad(e);
        var area = Screen.FromControl(this).WorkingArea;
        if (Width > area.Width || Height > area.Height) WindowState = FormWindowState.Maximized;
        ApplyResponsiveLayout();
    }

    protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
    {
        // Ctrl+1..5 switch pages
        var key = keyData & Keys.KeyCode;
        if ((keyData & Keys.Modifiers) == Keys.Control && key >= Keys.D1 && key < Keys.D1 + _pages.Count)
        {
            ShowPage(key - Keys.D1);
            return true;
        }
        return base.ProcessCmdKey(ref msg, keyData);
    }

    // ================================================================ header and connection bar

    private Control BuildHeader()
    {
        var header = new Panel { Dock = DockStyle.Top, Height = 48, BackColor = Theme.Chrome };
        var brand = new Label { Text = "Roomba Survey", AutoSize = true, Font = Theme.Brand, ForeColor = Theme.ChromeText, Margin = new Padding(0, 0, 4, 0) };
        var version = new Label { Text = "V3", AutoSize = true, Font = Theme.SmallSemibold, ForeColor = Color.FromArgb(120, 210, 220), Margin = new Padding(0, 0, 4, 0) };
        _linkDot.Margin = new Padding(4, 0, 0, 0);
        _debugBtn.Margin = new Padding(8, 0, 8, 0);
        _stopMissionBtn.MinimumSize = new Size(96, 32);
        _stopMissionBtn.Margin = new Padding(0);

        // Left group, right group, and the link text taking whatever is between (with an ellipsis)
        var left = new Control[] { brand, version, _modeBadge, _linkDot };
        var right = new Control[] { _debugBtn, _stopMissionBtn };
        header.Controls.AddRange(left);
        header.Controls.AddRange(right);
        header.Controls.Add(_linkLbl);
        header.Layout += (_, _) =>
        {
            int h = header.ClientSize.Height, x = Theme.Px(header, 14);
            foreach (var c in left)
            {
                x += c.Margin.Left;
                c.Location = new Point(x, (h - c.Height) / 2);
                x += c.Width + c.Margin.Right;
            }
            int r = header.ClientSize.Width - Theme.Px(header, 10);
            for (int i = right.Length - 1; i >= 0; i--)
            {
                var c = right[i];
                r -= c.Margin.Right + c.Width;
                c.Location = new Point(r, (h - c.Height) / 2);
                r -= c.Margin.Left;
            }
            _linkLbl.SetBounds(x + Theme.Px(header, 4), 0, Math.Max(0, r - x - Theme.Px(header, 12)), h);
        };

        _tips.SetToolTip(_modeBadge, "REAL HARDWARE: a USB, Bluetooth or Wi-Fi source.\nSIMULATION: data generated inside this app, not measured.");
        _tips.SetToolTip(_debugBtn, "Show or hide the glove's debug pages window");
        _tips.SetToolTip(_stopMissionBtn,
            "Cancels the running mapping phase and sends PAUSE to the robot (if it can receive commands).\n" +
            "This is a software stop over the radio/USB link, not a hardware emergency stop.");
        return header;
    }

    private Control BuildConnectionBar()
    {
        var bar = new FlowLayoutPanel
        {
            Dock = DockStyle.Top, AutoSize = true, WrapContents = true, BackColor = Theme.Surface,
            Padding = new Padding(10, 5, 10, 5)
        };
        bar.Paint += (_, e) => { using var p = new Pen(Theme.Border); e.Graphics.DrawLine(p, 0, bar.Height - 1, bar.Width, bar.Height - 1); };
        _connect.Margin = new Padding(8, 3, 3, 3);
        bar.Controls.AddRange(new Control[]
        {
            Caption("Source:"), _source, _serialGroup, _bleGroup, _udpGroup, _connect, Separator(), _frontierSim,
        });
        _tips.SetToolTip(_source, "Where the data comes from. Simulation sources generate data inside the app; they are not live measurements.");
        _tips.SetToolTip(_frontierSim, "Starts the four-IR frontier explorer simulation (no hardware, synthetic Wi-Fi values)");
        _tips.SetToolTip(_deviceIp, "Optional: the robot's IP address, if broadcast discovery doesn't work on this network");
        return bar;
    }

    private static Control Separator() =>
        new Panel { Width = 1, Height = 22, BackColor = Theme.Border, Margin = new Padding(6, 6, 6, 3) };

    // ================================================================ map column

    private Control BuildMapToolbar()
    {
        var bar = new FlowLayoutPanel
        {
            Dock = DockStyle.Top, AutoSize = true, WrapContents = true, BackColor = Theme.Surface,
            Padding = new Padding(8, 4, 8, 4)
        };
        bar.Paint += (_, e) => { using var p = new Pen(Theme.Border); e.Graphics.DrawLine(p, 0, bar.Height - 1, bar.Width, bar.Height - 1); };
        bar.Controls.AddRange(new Control[]
        {
            _fit, _zoomOut, _zoomIn, Separator(), _exportCsv, _savePng, _exportHeat, Separator(), _clear,
            _inspectorToggle,
        });
        _tips.SetToolTip(_fit, "Fit everything recorded into view (scroll to zoom, drag to pan)");
        _tips.SetToolTip(_zoomIn, "Zoom in");
        _tips.SetToolTip(_zoomOut, "Zoom out");
        _tips.SetToolTip(_exportCsv, "Save positions, Wi-Fi readings and obstacle hits as CSV");
        _tips.SetToolTip(_savePng, "Save the map exactly as shown");
        _tips.SetToolTip(_exportHeat, "Save the whole heat map as PNG (no planned route or target) plus the readings as a CSV next to it");
        _tips.SetToolTip(_clear, "Clear what the map shows (path, heat map, readings). The mapped room and obstacles are kept. Asks first.");
        _tips.SetToolTip(_inspectorToggle, "Show the inspector panel again");
        _tips.SetToolTip(_map, "Scroll to zoom, drag to pan, right-click to send the robot to a point");
        return bar;
    }

    private void ClearMapClicked()
    {
        bool hasData = _map.Samples.Count > 0 || _map.Readings.Count > 0 || _map.Obstacles.Count > 0;
        if (hasData && !_confirm(
                "Clear the map display?\n\nThis removes the path, heat map, Wi-Fi readings and raw obstacle hits " +
                "shown on the map. The room and obstacles from the mapping phases are kept.\n\n" +
                "Export first if you still need the readings.", "Clear map"))
            return;
        ClearAll();
    }

    // ================================================================ pages

    private void BuildPages()
    {
        // ---- Missions
        var phases = new UiSection("Mapping phases",
            "Run in order. Phases 2 and 3 need a room map, from phase 1 or Load map. " +
            "STOP (top right) cancels a running phase.");
        phases.Add(Hint(_missionHint));
        phases.Add(ButtonStack(_phase1Btn, _phase2Btn, _phase3Btn));
        var survey = new UiSection("Wi-Fi survey (phase 3)",
            "Phase 3 also uses the Wi-Fi mode (FAST / SLOW) set on the Robot page.");
        var surveyProps = new PropertyTable();
        surveyProps.AddRow("Lane spacing (cm)", _laneSpacing);
        survey.Add(surveyProps);
        survey.Add(_guardBox);
        var files = new UiSection("Room map files",
            "Save writes name.json (room + obstacles) and name_room_only.json next to it.");
        files.Add(ButtonRow(_saveMapBtn, _loadMapBtn));
        AddPage("Missions", "Mapping and survey", "Map the room, find obstacles and run the Wi-Fi survey",
                phases, survey, files);

        // ---- Robot (everything in _controlRow is enabled only when the robot can be driven)
        var firmware = new UiSection("Robot survey (firmware)",
            "The robot's own survey commands. Each is resent until the robot confirms it.");
        firmware.Add(ButtonGrid(_runBtn, _pauseBtn, _homeBtn, _measureBtn, _roamBtn));
        var drive = new UiSection("Manual drive", "One step per press. Right-click the map to send the robot to a point.");
        drive.Add(DrivePad());
        var sensing = new UiSection("Drive and sensing");
        var sensingProps = new PropertyTable();
        sensingProps.AddRow("Wi-Fi mode", _modeBox);
        sensingProps.AddRow("Speed %", _speedBox);
        sensingProps.AddRow("IR sensors", _sensorBox);
        sensing.Add(sensingProps);
        sensing.Add(_probeBox);
        _tips.SetToolTip(_probeBox, "Front-only sensing: probe along walls. Not available with a side sensor.");
        var origin = new UiSection("Origin",
            "Makes the robot's current position the new start (0, 0). Asks first; the map is cleared because it no longer lines up.");
        origin.Add(ButtonRow(_resetPosBtn));
        foreach (var s in new Control[] { origin, sensing, drive, firmware }) _controlRow.Controls.Add(s);
        _robotHintBox.Controls.Add(Hint(_robotHint));
        AddPage("Robot", "Robot control", "Commands for a robot connected over USB Serial or Wi-Fi (UDP)",
                _robotHintBox, _controlRow);

        // ---- Wearable
        var glove = new UiSection("Survey glove",
            "For the GloveSurvey firmware over Wi-Fi (UDP). The person walks and the app records. " +
            "Buttons for features the glove didn't advertise are disabled.");
        glove.Add(_gloveRow);
        AddPage("Wearable", "Wearable survey", "Calibrate the glove, walk the room and obstacles, then follow the survey guide", glove);

        // ---- Diagnostics
        var console = new UiSection("Robot command",
            "Sends C,<id>,<text> and resends until the robot acknowledges. A typed \"C,\" prefix is removed; SET,pass values are masked in the log.");
        _cmdBox.Margin = new Padding(0, 0, 0, 6);
        console.Add(_cmdBox);
        console.Add(ButtonRow(_cmdSendBtn));
        var counters = new UiSection("Link", "From the app's own state; refreshed twice a second.");
        var cp = new PropertyTable(112);
        cp.AddRow("Source", _diagSource);
        cp.AddRow("Command path", _diagCmdPath);
        cp.AddRow("Last data", _diagLastData);
        cp.AddRow("Telemetry lines", _diagTelemetry);
        cp.AddRow("Positions (P)", _diagPositions, "Position lines plotted as the path", _tips);
        cp.AddRow("Readings (R)", _diagReadings, "Cached Wi-Fi readings received as R lines (the inspector's count also includes signal values in P lines)", _tips);
        cp.AddRow("Awaiting reply", _diagPending, "Commands sent but not yet confirmed by the robot (each is resent for about 9 s)", _tips);
        cp.AddRow("Glove features", _diagFeatures, "From the glove's F line", _tips);
        counters.Add(cp);
        foreach (var l in new[] { _diagSource, _diagCmdPath, _diagTelemetry, _diagReadings, _diagPositions, _diagPending, _diagLastData, _diagFeatures })
            l.Font = Theme.Body;
        var pages = new UiSection("Glove debug pages",
            "\"Debug pages\" in the top bar opens the window. While it is open the app asks the glove to send its pages " +
            "(MIRROR,ON); hiding it stops them. Recording to CSV is in that window.");
        AddPage("Diagnostics", "Diagnostics", "Send raw commands and check the link", console, counters, pages);

        // ---- Settings
        var geometry = new UiSection("Robot geometry",
            "Used by the mapping phases and the no-go overlay. Locked while a phase runs.");
        var gp = new PropertyTable(120);
        gp.AddRow("Length L (mm)", _lenBox);
        gp.AddRow("Width W (mm)", _widBox);
        gp.AddRow("IR range R (mm)", _rangeBox);
        geometry.Add(gp);
        var about = new UiSection("About this build",
            "Roomba Survey V3: a user-interface revision of App/. Robot commands, telemetry parsing, mapping " +
            "logic and file formats are the same as App/. Simulation sources are always labelled SIMULATION.");
        AddPage("Settings", "Settings", "Robot dimensions used for planning", geometry, about);

        foreach (var p in _pages) _pageHost.Controls.Add(p);
        _nav.SelectedChanged += ShowPage;
        _nav.ActiveItemClicked += () => SetPageVisible(!_pageHost.Visible, user: true);
    }

    private void AddPage(string navText, string title, string subtitle, params Control[] sections)
    {
        var page = new Panel { Dock = DockStyle.Fill, AutoScroll = true, BackColor = Theme.Canvas, Visible = false, Padding = new Padding(8, 0, 8, 4) };
        for (int i = sections.Length - 1; i >= 0; i--) page.Controls.Add(sections[i]);   // Dock Top
        page.Controls.Add(PageHeader(title, subtitle));
        _pages.Add(page);
        _nav.AddItem(navText, $"{title} (Ctrl+{_pages.Count}). Click again to hide the panel.", _tips);
    }

    private Control PageHeader(string title, string subtitle)
    {
        var head = new Panel { Dock = DockStyle.Top, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Padding = new Padding(4, 10, 4, 10) };
        var sub = Theme.Wrapping(new Label { Text = subtitle, Dock = DockStyle.Top, ForeColor = Theme.TextSecondary, Font = Theme.Small });
        var row = new Panel { Dock = DockStyle.Top, Height = 30 };
        var t = new Label { Text = title, Dock = DockStyle.Fill, Font = Theme.PageTitle, ForeColor = Theme.Text, TextAlign = ContentAlignment.MiddleLeft, AutoEllipsis = true };
        var hide = new Button { Text = "‹", Width = 28, Dock = DockStyle.Right, AccessibleName = "Hide panel" };
        row.Controls.Add(t);              // Fill first, for docking
        row.Controls.Add(hide);
        head.Controls.Add(sub);
        head.Controls.Add(row);
        hide.Click += (_, _) => SetPageVisible(false, user: true);
        _tips.SetToolTip(hide, "Hide this panel to give the map more room (click the page name to bring it back)");
        return head;
    }

    private void ShowPage(int index)
    {
        if (_nav.SelectedIndex != index) { _nav.Select(index); return; }   // comes back here via SelectedChanged
        for (int i = 0; i < _pages.Count; i++) _pages[i].Visible = i == index;
        if (!_pageHost.Visible) SetPageVisible(true, user: true);
    }

    private void SetPageVisible(bool visible, bool user)
    {
        if (user) _pageAutoHidden = false;
        _pageHost.Visible = _pageSplitter.Visible = visible;
    }

    /// <summary>Keeps the map usable on narrow windows: hides the page panel first, then brings it back.</summary>
    private void ApplyResponsiveLayout()
    {
        if (WindowState == FormWindowState.Minimized || _pages.Count == 0) return;
        int minMap = Theme.Px(this, 460);
        int others = _nav.Width + (_inspector.Visible ? _inspector.Width + _inspectorSplitter.Width : 0);
        int mapWithPage = ClientSize.Width - others - _pageHost.Width - _pageSplitter.Width;
        if (_pageHost.Visible && mapWithPage < minMap)
        {
            SetPageVisible(false, user: false);
            _pageAutoHidden = true;
        }
        else if (!_pageHost.Visible && _pageAutoHidden && mapWithPage >= minMap + Theme.Px(this, 40))
        {
            SetPageVisible(true, user: false);
            _pageAutoHidden = false;
        }
    }

    // ================================================================ inspector

    private void BuildInspector()
    {
        var status = new UiSection("Status");
        status.Add(Theme.Wrapping(_inspectorNote));
        _statusLbl.Padding = new Padding(0, 6, 0, 0);
        status.Add(Theme.Wrapping(_statusLbl));

        var mission = new UiSection("Mission");
        mission.Add(Theme.Wrapping(_missionLbl));
        var bar = new Panel { Height = 16, Padding = new Padding(0, 6, 0, 0) };
        _progress.Dock = DockStyle.Fill;
        bar.Controls.Add(_progress);
        mission.Add(bar);
        _progressLbl.Padding = new Padding(0, 2, 0, 0);
        mission.Add(Theme.Wrapping(_progressLbl));

        var robot = new UiSection("Robot");
        var rp = new PropertyTable(96);
        rp.AddRow("State", _stateLbl, "State, Wi-Fi mode and speed reported by the robot", _tips);
        rp.AddRow("Position", _posLbl, "From the robot's odometry; (0, 0) is the start point", _tips);
        rp.AddRow("Heading", _headLbl);
        rp.AddRow("Distance", _distLbl, "Distance travelled", _tips);
        rp.AddRow("Wheel turns", _turnsLbl, "Wheel rotations of the 40 mm wheel", _tips);
        robot.Add(rp);

        var wifi = new UiSection("Wi-Fi");
        var wp = new PropertyTable(96);
        wp.AddRow("Signal", _rssiLbl, "Received signal strength (RSSI, dBm) and the app's quality word. The square is its heat-map colour.",
                  _tips, leading: _rssiSwatch);
        wp.AddRow("Access point", _apLbl, "MAC end and channel, as reported by the robot", _tips);
        wp.AddRow("Readings", _countLbl);
        wifi.Add(wp);

        var sensors = new UiSection("Sensors");
        var sp = new PropertyTable(96);
        sp.AddRow("IR (blocked?)", _irLbl, "IR sensors: is something there?", _tips);
        sp.AddRow("Obstacles", _obstLbl, "Obstacles mapped, and seeds left to investigate", _tips);
        sensors.Add(sp);

        var layers = new UiSection("Map layers");
        _showNoGo.Text = "No-go overlay";
        var checks = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.TopDown, WrapContents = false };
        foreach (var c in new[] { _showHeat, _showPath, _showNoGo }) { c.Margin = new Padding(0, 2, 0, 2); checks.Controls.Add(c); }
        layers.Add(checks);
        _tips.SetToolTip(_showHeat, "Heat map built from Wi-Fi readings");
        _tips.SetToolTip(_showPath, "The path the robot (or glove) has travelled");
        layers.Add(new Label { Text = "Signal colour scale (dBm)", AutoSize = false, Height = 24, ForeColor = Theme.TextSecondary, TextAlign = ContentAlignment.BottomLeft });
        layers.Add(new RssiLegend(_map, (ExcellentDbm, "Excellent"), (GoodDbm, "Good")));

        foreach (var s in new Control[] { layers, sensors, wifi, robot, mission, status }) _inspector.Controls.Add(s);
        var head = new Panel { Dock = DockStyle.Top, Height = 30, Padding = new Padding(4, 0, 0, 4) };
        _inspectorHide.Dock = DockStyle.Right;
        head.Controls.Add(new Label { Text = "INSPECTOR", Dock = DockStyle.Fill, Font = Theme.SmallSemibold, ForeColor = Theme.TextMuted, TextAlign = ContentAlignment.MiddleLeft });
        head.Controls.Add(_inspectorHide);
        _inspector.Controls.Add(head);
        _tips.SetToolTip(_inspectorHide, "Hide the inspector to give the map more room");
    }

    // ================================================================ live state

    private void ShowRssiColor(Color c)
    {
        _rssiSwatch.Value = c;
        _rssiLbl.ForeColor = Theme.Text;    // the heat colour itself is too light to read as text
    }

    /// <summary>Header badge, last-data age, inspector note, hints and diagnostics. Read-only.</summary>
    private void UpdateLinkUi()
    {
        if (IsDisposed || Disposing) return;      // a phase can still report in while the window closes
        var src = _active;
        bool simRobot = _simRobot != null;
        bool sim = simRobot || src is SimulationSource or FrontierSimulationSource;
        double age = _lastDataAt == DateTime.MinValue ? double.NaN : (DateTime.Now - _lastDataAt).TotalSeconds;
        bool stale = !double.IsNaN(age) && age >= StaleSeconds;

        string badge, link, note, cmdPath;
        Color fill, dot, noteBack, noteFore;
        if (src == null && !simRobot)
        {
            badge = "NOT CONNECTED"; fill = Theme.Neutral; dot = Theme.ChromeMuted;
            link = "Choose a source and press Connect";
            cmdPath = "none";
            note = _telemetrySeq > 0 ? "Not connected. The values below are the last ones received, not live."
                                     : "Not connected. No data received yet.";
            (noteBack, noteFore) = (Theme.Hover, Theme.TextSecondary);
        }
        else
        {
            badge = sim ? "SIMULATION" : "REAL HARDWARE";
            fill = sim ? Theme.CautionStrong : Theme.Accent;
            string name = simRobot ? "Simulated robot (no hardware)" : src!.Name;
            string data = double.IsNaN(age) ? (simRobot ? "idle until a mapping phase runs" : "waiting for data")
                        : stale ? $"STALE: no data for {age:0} s"
                        : $"receiving, last data {age:0.0} s ago";
            cmdPath = simRobot ? "simulated robot (mapping phases only)"
                    : sim ? "none (synthetic data source)"
                    : src!.CanSend ? "open"
                    : src is BleSource ? "none (Bluetooth LE is receive-only)"
                    : "not yet (waiting for the robot's first data)";
            link = $"{name}   ·   {data}   ·   commands: {cmdPath}";
            dot = double.IsNaN(age) ? Theme.ChromeMuted : stale ? Theme.CautionStrong : Theme.OkBright;
            if (sim)
            {
                note = "SIMULATION: positions and Wi-Fi values are generated by the app, not measured.";
                (noteBack, noteFore) = (Theme.CautionSoft, Theme.Caution);
            }
            else if (double.IsNaN(age) || stale)
            {
                note = double.IsNaN(age) ? "Connected, waiting for the robot's first data."
                                         : $"No data for {age:0} s. The values below may be out of date.";
                (noteBack, noteFore) = (Theme.CautionSoft, Theme.Caution);
            }
            else
            {
                note = "Live data from the robot.";
                (noteBack, noteFore) = (Theme.AccentSoft, Theme.AccentHover);
            }
        }

        if (_modeBadge.Text != badge)
        {
            _modeBadge.Text = badge;
            _modeBadge.Fill = fill;
            _modeBadge.ForeColor = sim ? Theme.Text : Color.White;
        }
        if (_linkLbl.Text != link)
        {
            _linkLbl.Text = link;
            _tips.SetToolTip(_linkLbl, link);       // the full text when the header is too narrow
        }
        if (_linkDot.ForeColor != dot) _linkDot.ForeColor = dot;
        Set(_inspectorNote, note);
        if (_inspectorNote.BackColor != noteBack) { _inspectorNote.BackColor = noteBack; _inspectorNote.ForeColor = noteFore; }
        UpdateRobotHint(src, simRobot);

        if (_pages.Count > 3 && _pages[3].Visible)   // Diagnostics page values: only while it's shown
        {
            Set(_diagSource, src?.Name ?? (simRobot ? "Simulated robot (no hardware)" : "not connected"));
            Set(_diagCmdPath, cmdPath);
            Set(_diagLastData, double.IsNaN(age) ? "none yet" : $"{age:0.0} s ago");
            Set(_diagTelemetry, _telemetrySeq.ToString("N0"));
            Set(_diagPositions, _map.Samples.Count.ToString("N0"));
            Set(_diagReadings, _map.Readings.Count.ToString("N0"));
            Set(_diagPending, _pending.Count.ToString());
            Set(_diagFeatures, _debugFeatures is { } f ? f[2..].Replace(",", ", ") : "none advertised");
        }

        static void Set(Label l, string text) { if (l.Text != text) l.Text = text; }
    }

    private void UpdateRobotHint(DataSourceBase? src, bool simRobot)
    {
        string text =
            simRobot ? "Simulated robot: it moves only during mapping phases (Missions page). These buttons send real " +
                       "robot commands, so they report that no robot is reachable."
            : src == null ? "Not connected. Robot commands need USB Serial or Wi-Fi (UDP). With Wi-Fi the controls " +
                            "unlock once the robot's first data arrives."
            : src.CanSend ? ""
            : src is BleSource ? "Bluetooth LE is receive-only in this app: commands can't be sent. Use USB Serial or Wi-Fi (UDP) to drive the robot."
            : src is SimulationSource or FrontierSimulationSource
                ? "This simulation only produces data and takes no commands. Choose \"Simulated robot (no hardware)\" to try the mapping phases."
            : "Waiting for the robot's first data. The controls unlock once the robot's address is known.";
        if (_robotHint.Text != text) _robotHint.Text = text;
        _robotHintBox.Visible = text.Length > 0;
    }

    private void UpdateMissionHint(bool connected, bool running, bool hasRoom)
    {
        string text = running ? "A phase is running. STOP (top right) cancels it."
                    : !connected ? "Connect to the robot (USB Serial or Wi-Fi UDP), or choose \"Simulated robot (no hardware)\", to run a phase."
                    : !hasRoom ? "Phases 2 and 3 need a room map: run 1 Map room, or Load map."
                    : "";
        if (_missionHint.Text != text) _missionHint.Text = text;
        _missionHint.Visible = text.Length > 0;
    }

    // ================================================================ layout helpers

    private static Label Hint(Label l)
    {
        Theme.Wrapping(l);
        l.BackColor = Theme.CautionSoft;
        l.ForeColor = Theme.Caution;
        return l;
    }

    private static FlowLayoutPanel ButtonRow(params Control[] controls)
    {
        var p = new FlowLayoutPanel { AutoSize = true, WrapContents = true, Margin = Padding.Empty };
        foreach (var c in controls) c.Margin = new Padding(0, 0, 6, 0);
        p.Controls.AddRange(controls);
        return p;
    }

    /// <summary>Full-width buttons, one per line.</summary>
    private static TableLayoutPanel ButtonStack(params Button[] buttons)
    {
        var t = new TableLayoutPanel { ColumnCount = 1, AutoSize = true, Margin = Padding.Empty };
        t.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        foreach (var b in buttons)
        {
            b.AutoSize = false;
            b.Dock = DockStyle.Fill;
            b.TextAlign = ContentAlignment.MiddleLeft;
            b.Margin = new Padding(0, 0, 0, 6);
            t.RowStyles.Add(new RowStyle(SizeType.Absolute, 40));
            t.Controls.Add(b);
        }
        return t;
    }

    /// <summary>Equal-width buttons, two per line.</summary>
    private static TableLayoutPanel ButtonGrid(params Button[] buttons)
    {
        var t = new TableLayoutPanel { ColumnCount = 2, AutoSize = true, Margin = Padding.Empty };
        t.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        t.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        for (int i = 0; i < buttons.Length; i++)
        {
            if (i % 2 == 0) t.RowStyles.Add(new RowStyle(SizeType.Absolute, 38));
            var b = buttons[i];
            b.AutoSize = false;
            b.Dock = DockStyle.Fill;
            b.Margin = new Padding(i % 2 == 0 ? 0 : 3, 0, i % 2 == 0 ? 3 : 0, 6);
            t.Controls.Add(b, i % 2, i / 2);
        }
        return t;
    }

    private TableLayoutPanel DrivePad()
    {
        var t = new TableLayoutPanel { ColumnCount = 3, RowCount = 3, AutoSize = true, Margin = Padding.Empty };
        for (int i = 0; i < 3; i++)
        {
            t.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 33.3f));
            t.RowStyles.Add(new RowStyle(SizeType.Absolute, 38));
        }
        void Put(Button b, int col, int row)
        {
            b.AutoSize = false;
            b.Dock = DockStyle.Fill;
            b.Margin = new Padding(2, 0, 2, 6);
            t.Controls.Add(b, col, row);
        }
        Put(_fwdBtn, 1, 0);
        Put(_leftBtn, 0, 1);
        Put(_rightBtn, 2, 1);
        Put(_backBtn, 1, 2);
        return t;
    }

    private void ApplyTheme()
    {
        var special = new HashSet<Control> { _connect, _stopMissionBtn, _debugBtn };
        void Walk(Control root)
        {
            foreach (Control c in root.Controls)
            {
                if (c is Button b && !special.Contains(b)) Theme.Style(b);
                if (c.HasChildren) Walk(c);
            }
        }
        Walk(this);
        Theme.StyleInputs(this);
        Theme.FitAutoSize(this);
        foreach (var b in new[] { _fwdBtn, _backBtn, _leftBtn, _rightBtn }) b.Padding = Padding.Empty;
        Theme.Style(_stopMissionBtn, Theme.ButtonKind.Danger);
        _stopMissionBtn.Font = new Font("Segoe UI", 9.75f, FontStyle.Bold);
        Theme.Style(_debugBtn, Theme.ButtonKind.Chrome);
        void ConnectLook() => Theme.Style(_connect, _connect.Text == "Connect" ? Theme.ButtonKind.Primary : Theme.ButtonKind.Secondary);
        ConnectLook();
        _connect.TextChanged += (_, _) => ConnectLook();
        foreach (var b in new[] { _phase1Btn, _phase2Btn, _phase3Btn }) b.Font = Theme.BodySemibold;
    }
}
