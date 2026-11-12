using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using RoombaMapper.Core;

namespace RoombaMapper;

/// <summary>
/// The three mapping phases in the app. All navigation and mapping logic is in Core
/// (Phases.cs, EdgeFollower.cs, CoverageEngine.cs, ...); this file only hosts it:
/// buttons, settings, progress, saving/loading maps and the prompts after each phase.
///   1 Map room       - room outline with labelled sections, seeds for phase 2
///   2 Find obstacles - investigates every seed, sweeps the room, saves the maps
///   3 Wi-Fi survey   - serpentine survey on the finished map, heat map export
/// </summary>
public sealed partial class MainForm
{
    // ---- state
    private Telemetry? _lastTelemetry;
    private long _telemetrySeq;
    private readonly HashSet<long> _failedCommands = new();
    private CancellationTokenSource? _missionCts;
    private WorldMap _world = new();
    private readonly RobotConfig _cfg = new();
    private RobotLink _link = null!;
    private SimRobot? _simRobot;              // "Simulated robot (no hardware)" source
    private bool RobotReady => _simRobot != null || _active?.CanSend == true;
    private MissionHost _host = null!;
    private DateTime _lastNoGo = DateTime.MinValue;
    private readonly System.Windows.Forms.Timer _pingTimer = new() { Interval = 2000 };

    // ---- UI (placed on the Missions page, the inspector and the header by MainForm.Layout.cs)
    private readonly Button _phase1Btn = CtlButton("1   Map room");
    private readonly Button _phase2Btn = CtlButton("2   Find obstacles");
    private readonly Button _phase3Btn = CtlButton("3   Wi-Fi survey");
    private readonly Button _stopMissionBtn = new()
    {
        Text = "■  STOP", AutoSize = true, BackColor = Color.Firebrick, ForeColor = Color.White,
        Font = new Font("Segoe UI", 9, FontStyle.Bold)
    };
    private readonly Button _saveMapBtn = CtlButton("Save map");
    private readonly Button _loadMapBtn = CtlButton("Load map");
    private readonly NumericUpDown _laneSpacing = new() { Minimum = 20, Maximum = 200, Increment = 5, Value = 40, Width = 50 };
    private readonly NumericUpDown _lenBox = new() { Minimum = 60, Maximum = 600, Increment = 10, Value = 160, Width = 55 };
    private readonly NumericUpDown _widBox = new() { Minimum = 60, Maximum = 600, Increment = 10, Value = 160, Width = 55 };
    private readonly NumericUpDown _rangeBox = new() { Minimum = 20, Maximum = 800, Increment = 10, Value = 100, Width = 55 };
    private readonly CheckBox _guardBox = new() { Text = "Guard survey", Checked = true, AutoSize = true, Margin = new Padding(6, 6, 3, 3) };
    private readonly CheckBox _showNoGo = new() { Text = "Show no-go", AutoSize = true, Margin = new Padding(6, 6, 3, 3) };
    private readonly TextBox _cmdBox = new() { Width = 150, PlaceholderText = "e.g. SET,ssid,MyNetwork" };
    private readonly Button _cmdSendBtn = CtlButton("Send");
    private readonly ProgressBar _progress = new() { Height = 10, Minimum = 0, Maximum = 100, Style = ProgressBarStyle.Continuous };
    private readonly Label _progressLbl = new() { ForeColor = Ui.Theme.TextSecondary };
    private readonly Label _missionLbl = new() { ForeColor = Ui.Theme.Text };

    private void BuildMissionRow()
    {
        _link = new RobotLink(CommandAsync, () => SendCommand("PAUSE", "Stop"));
        _host = new MissionHost(this);
        _map.World = _world;

        var tip = new ToolTip();
        tip.SetToolTip(_lenBox, "Robot length L (mm)");
        tip.SetToolTip(_widBox, "Robot width W (mm)");
        tip.SetToolTip(_rangeBox, "IR trigger range R (mm): how far beyond the front edge the sensor says YES");
        tip.SetToolTip(_guardBox, "Phase 3: wiggle-scan every few cm so the body can't brush something new (slower)");
        tip.SetToolTip(_showNoGo, "Shade where the robot's centre may not go (dark = something there, light = too close)");

        _phase1Btn.Click += async (_, _) => await StartPhase1Async();
        _phase2Btn.Click += async (_, _) => await StartPhase2Async();
        _phase3Btn.Click += async (_, _) => await StartPhase3Async();
        _stopMissionBtn.Click += (_, _) =>
        {
            // Emergency stop: cancel the phase AND tell the robot to stop right now
            _missionCts?.Cancel();
            if (_active?.CanSend == true) SendCommand("PAUSE", "STOP");
            MissionStatus("STOP pressed.");
        };
        _saveMapBtn.Click += (_, _) => SaveMapFiles();
        _loadMapBtn.Click += (_, _) => LoadMapFile();
        _showNoGo.CheckedChanged += (_, _) => RefreshNoGo(force: true);
        foreach (var box in new[] { _lenBox, _widBox, _rangeBox }) box.ValueChanged += (_, _) => { ApplyConfig(); RefreshNoGo(force: true); };

        void sendTyped()
        {
            var text = _cmdBox.Text.Trim();
            if (text.Length == 0) return;
            if (text.StartsWith("C,")) text = text[2..];
            SendCommand(text, text.StartsWith("SET,pass,") ? "SET,pass" : text);
            _cmdBox.Clear();
        }
        _cmdSendBtn.Click += (_, _) => sendTyped();
        _cmdBox.KeyDown += (_, e) =>
        {
            if (e.KeyCode != Keys.Enter) return;
            e.SuppressKeyPress = true;
            sendTyped();
        };

        // Keep-alive: the robot stops a commanded movement if the app goes quiet for 10 s
        _pingTimer.Tick += (_, _) => { if (_active?.CanSend == true) _active.Send("PING"); };
        _pingTimer.Start();

        _missionLbl.Text = "Place the robot at its start spot, facing the same way each time.";
    }

    private void UpdateMissionButtons()
    {
        bool connected = RobotReady;
        bool running = _missionCts != null;
        bool hasRoom = _world.Room != null;
        _phase1Btn.Enabled = connected && !running;
        _phase2Btn.Enabled = connected && !running && hasRoom;
        _phase3Btn.Enabled = connected && !running && hasRoom;
        _loadMapBtn.Enabled = !running;
        _saveMapBtn.Enabled = !running && hasRoom;
        foreach (var b in new Control[] { _lenBox, _widBox, _rangeBox, _laneSpacing, _guardBox }) b.Enabled = !running;
        UpdateMissionHint(connected, running, hasRoom);   // says why a phase button is disabled
    }

    private void ApplyConfig()
    {
        _cfg.LengthMm = (double)_lenBox.Value;
        _cfg.WidthMm = (double)_widBox.Value;
        _cfg.RangeMm = (double)_rangeBox.Value;
        _cfg.SurveyLaneMm = (double)_laneSpacing.Value * 10;
        _cfg.SurveyGuard = _guardBox.Checked;
        _map.Cfg = _cfg;
    }

    // ================================================================ phases

    private async Task StartPhase1Async()
    {
        if (_world.Room != null &&
            MessageBox.Show(this, "Map the room again? The current room and obstacle map will be replaced.",
                            "Map room", MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK) return;
        ClearAll();
        await RunMission("Phase 1: Map room", async (nav, ct) =>
        {
            var p = new Phase1MapRoom(nav);
            await p.RunAsync(ct);
            if (_world.Room is { Closed: false })
                MessageBox.Show(this, "The room outline is INCOMPLETE: the trace didn't close.\n\n" +
                                      "Check the outline. You can still run Phase 2, or map the room again.",
                                "Phase 1", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        });
    }

    private async Task StartPhase2Async()
    {
        bool ok = await RunMission("Phase 2: Find obstacles", async (nav, ct) =>
        {
            var p = new Phase2FindObstacles(nav);
            await p.RunAsync(ct);
        });
        if (ok && MessageBox.Show(this, "Obstacles mapped. Save the maps now?\n\n" +
                                        "(Saves two files: room + obstacles, and room only.)",
                                  "Phase 2 complete", MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes)
            SaveMapFiles();
    }

    private async Task StartPhase3Async()
    {
        _map.ClearSurveyData();
        _withSignal = 0;
        bool slow = (string?)_modeBox.SelectedItem == "SLOW";
        bool ok = await RunMission("Phase 3: Wi-Fi survey", async (nav, ct) =>
        {
            var p = new Phase3WifiSurvey(nav) { Slow = slow };
            await p.RunAsync(ct);
        });
        if (!ok) return;

        if (MessageBox.Show(this, $"The Wi-Fi survey is complete, with {_map.Readings.Count} readings.\n\n" +
                                  "Export the heat map now? (Saves a PNG picture and a CSV of the readings.)",
                            "Survey complete", MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes)
            ExportHeatmap();

        int temps = _world.Obstacles.Count(o => o.Kind == ObstacleKind.Temporary);
        if (temps > 0)
        {
            var keep = MessageBox.Show(this, $"{temps} unexpected obstacle{(temps == 1 ? " was" : "s were")} found during the survey.\n\n" +
                                             "Keep them in the map (Yes), or discard them (No, e.g. it was a person)?",
                                       "Temporary obstacles", MessageBoxButtons.YesNo, MessageBoxIcon.Question);
            if (keep == DialogResult.Yes)
                foreach (var o in _world.Obstacles.Where(o => o.Kind == ObstacleKind.Temporary))
                {
                    o.Kind = ObstacleKind.Pillar;
                    o.Unconfirmed = true;
                    o.Note = "kept from the Wi-Fi survey (not traced)";
                }
            else _world.ClearTemporary();
            MapChangedNow();
        }
    }

    /// <summary>
    /// Runs a phase. Any problem stops the robot: the STOP button (cancel), a mission
    /// problem (e.g. stuck sensor), lost contact, or an unexpected error.
    /// Returns true if the phase finished normally.
    /// </summary>
    private async Task<bool> RunMission(string name, Func<Nav, CancellationToken, Task> body)
    {
        if (_missionCts != null) return false;
        if (!RobotReady) { SetStatus("Connect to the robot (or the simulated robot) first."); return false; }
        ApplyConfig();
        _missionCts = new CancellationTokenSource();
        var ct = _missionCts.Token;
        UpdateMissionButtons();
        _map.World = _world;
        IRobot robot = _simRobot != null ? new AnimatedSim(_simRobot, this) : _link;
        var nav = new Nav(robot, _cfg, _world, _host);
        bool ok = false;
        try
        {
            MissionStatus(name + "...");
            await body(nav, ct);
            ok = true;
        }
        catch (OperationCanceledException)
        {
            MissionStatus($"{name}: STOPPED. The robot has been told to stop.");
            await robot.StopAsync();
        }
        catch (MissionException ex)
        {
            MissionStatus($"{name} stopped: {ex.Message}");
            await robot.StopAsync();
        }
        catch (RobotException ex)
        {
            MissionStatus($"{name} stopped: {ex.Message}");
            await robot.StopAsync();
        }
        catch (Exception ex)
        {
            MissionStatus($"{name} stopped by an unexpected error: {ex.Message}");
            AppendLog(ex.ToString());
            await robot.StopAsync();
        }
        finally
        {
            _missionCts.Dispose();
            _missionCts = null;
            _map.Target = null;
            UpdateMissionButtons();
            MapChangedNow();
        }
        return ok;
    }

    /// <summary>Sends a command, waits until the robot confirms it. Used by RobotLink.</summary>
    private async Task<long> CommandAsync(string command, string label, CancellationToken ct)
    {
        long id = SendCommand(command, label);
        if (id < 0) throw new RobotException("the robot isn't connected.");
        while (_pending.ContainsKey(id)) await Task.Delay(20, ct);
        if (_failedCommands.Remove(id)) throw new RobotException($"the robot didn't answer \"{label}\".");
        return id;
    }

    private void MissionStatus(string text)
    {
        _missionLbl.Text = text;
        SetStatus(text);
    }

    // ================================================================ map display

    private void MapChangedNow()
    {
        _map.World = _world;
        _map.GrowToWorld();
        _map.Invalidate();
        RefreshNoGo(force: false);
        UpdateShapeLabel();
    }

    private void RefreshNoGo(bool force)
    {
        if (!_showNoGo.Checked || (_world.Room == null && _world.Obstacles.Count == 0)) { _map.SetNoGo(null); return; }
        if (!force && (DateTime.Now - _lastNoGo).TotalMilliseconds < 1500) return;
        _lastNoGo = DateTime.Now;
        ApplyConfig();
        var pl = _world.BuildPlanner(_cfg);
        int w = pl.GridWidth, h = pl.GridHeight;
        var px = new int[w * h];
        int solid = Color.FromArgb(120, 70, 70, 90).ToArgb(), near = Color.FromArgb(70, 120, 120, 140).ToArgb();
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++)
            {
                int iy = h - 1 - j;
                px[j * w + i] = pl.IsOccupiedCell(i, iy) ? solid : pl.IsBlockedCell(i, iy) ? near : 0;
            }
        var bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb);
        var data = bmp.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        Marshal.Copy(px, 0, data.Scan0, px.Length);
        bmp.UnlockBits(data);
        _map.SetNoGo(bmp, pl.GridX0, pl.GridY0 + h * Planner.CellMm, Planner.CellMm);
    }

    /// <summary>What the Core reports while a phase runs (always on the UI thread).</summary>
    private sealed class MissionHost : IMissionHost
    {
        private readonly MainForm _f;
        private DateTime _lastDraw = DateTime.MinValue;
        public MissionHost(MainForm f) { _f = f; }
        public void Status(string text) => _f.MissionStatus(text);
        public void Progress(double fraction, string detail)
        {
            _f._progress.Value = (int)Math.Round(Math.Clamp(fraction, 0, 1) * 100);
            _f._progressLbl.Text = detail;
        }
        public void Log(string text) => _f.AppendLog("[map] " + text);
        public void MapChanged()
        {
            if ((DateTime.Now - _lastDraw).TotalMilliseconds < 250) { _f._map.Invalidate(); return; }
            _lastDraw = DateTime.Now;
            _f.MapChangedNow();
        }
        public void PlanChanged(IReadOnlyList<(double X, double Y)>? route) => _f._map.PlannedPath = route?.ToList();
    }

    // ================================================================ simulated robot

    /// <summary>
    /// The simulated robot, slowed down so it can be watched: after every primitive it
    /// waits a moment, and its position and any Wi-Fi readings go onto the map just like
    /// the real robot's telemetry.
    /// </summary>
    private sealed class AnimatedSim : IRobot
    {
        private readonly SimRobot _r;
        private readonly MainForm _f;
        private int _readingsSent;
        private long _t;
        public AnimatedSim(SimRobot r, MainForm f) { _r = r; _f = f; }

        private async Task ShowAsync(CancellationToken ct)
        {
            int delay = (int)Math.Clamp(6000 / Math.Max(20, (double)_f._speedBox.Value), 5, 300);
            await Task.Delay(delay, ct);
            var p = _r.Pose;
            _t += delay;
            _f.OnTelemetry(new Telemetry(_t, p.X, p.Y, p.HeadingDeg, _r.Travelled, null, false,
                                         State: "SIM", Mode: (string?)_f._modeBox.SelectedItem, SpeedPct: (int)_f._speedBox.Value));
            for (; _readingsSent < _r.Readings.Count; _readingsSent++)
            {
                var (x, y, rssi) = _r.Readings[_readingsSent];
                int v = (int)Math.Round(rssi);
                _f.OnReading(new WifiReading(_readingsSent, 'F', v, v, v, 0, x, y));
            }
        }

        public Pose Pose => _r.Pose;
        public async Task ResetPoseAsync(CancellationToken ct) { await _r.ResetPoseAsync(ct); await ShowAsync(ct); }
        public async Task<MoveResult> MoveAsync(double mm, bool i, CancellationToken ct) { var m = await _r.MoveAsync(mm, i, ct); await ShowAsync(ct); return m; }
        public async Task TurnAsync(double d, CancellationToken ct) { await _r.TurnAsync(d, ct); await ShowAsync(ct); }
        public async Task<MoveResult> GotoAsync((double X, double Y) p, CancellationToken ct) { var m = await _r.GotoAsync(p, ct); await ShowAsync(ct); return m; }
        public async Task<ScanResult> ScanAsync(double h, CancellationToken ct) { var s = await _r.ScanAsync(h, ct); await ShowAsync(ct); return s; }
        public async Task<LookResult> LookAsync(int side, CancellationToken ct) { var l = await _r.LookAsync(side, ct); await ShowAsync(ct); return l; }
        public async Task MeasureAsync(CancellationToken ct) { await _r.MeasureAsync(ct); await ShowAsync(ct); }
        public async Task HomeAsync(CancellationToken ct) { await _r.HomeAsync(ct); await ShowAsync(ct); }
        public Task SetAutoRoamAsync(bool on, CancellationToken ct) => _r.SetAutoRoamAsync(on, ct);
        public Task StopAsync() => _r.StopAsync();
    }

    // ================================================================ save / load

    /// <summary>Saves name.json (room + obstacles) and name_room_only.json (room only).</summary>
    private void SaveMapFiles()
    {
        if (_world.Room == null) { SetStatus("Nothing to save yet: map the room first."); return; }
        using var dlg = new SaveFileDialog
        {
            Filter = "Room map|*.json", FileName = "room_map.json",
            Title = "Save map (a room-only copy is saved next to it)"
        };
        if (dlg.ShowDialog(this) != DialogResult.OK) return;
        string roomOnly = Path.Combine(Path.GetDirectoryName(dlg.FileName) ?? "",
                                       Path.GetFileNameWithoutExtension(dlg.FileName) + "_room_only.json");
        File.WriteAllText(dlg.FileName, _world.ToJson(roomOnly: false));
        File.WriteAllText(roomOnly, _world.ToJson(roomOnly: true));
        SetStatus($"Saved {Path.GetFileName(dlg.FileName)} (room + {_world.Obstacles.Count(o => o.Blocks)} obstacles) " +
                  $"and {Path.GetFileName(roomOnly)} (room only).");
    }

    private void LoadMapFile()
    {
        using var dlg = new OpenFileDialog { Filter = "Room map|*.json" };
        if (dlg.ShowDialog(this) != DialogResult.OK) return;
        try
        {
            var loaded = WorldMap.FromJson(File.ReadAllText(dlg.FileName));
            if (loaded.Room == null) throw new InvalidDataException("this file has no room in it (old map format?)");
            ClearAll();
            _world = loaded;
            MapChangedNow();
            _map.FitToData();
            UpdateMissionButtons();
            SetStatus($"Map loaded: {_world.Obstacles.Count(o => o.Blocks)} obstacles, " +
                      $"{_world.Seeds.Count(s => s.State == SeedState.Pending)} seeds to investigate. " +
                      "Put the robot on its start spot, facing the same way as when it was mapped.");
        }
        catch (Exception ex)
        {
            SetStatus("Couldn't load map: " + ex.Message);
        }
    }
}
