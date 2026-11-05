using System.Globalization;
using System.Text.Json;

namespace RoombaMapper;

/// <summary>
/// The three mapping phases. The app does the thinking (maps, routes); the robot
/// follows simple commands: TRACE, GOTO (stop if blocked), MEASURE, HOME.
///
///   Phase 1  Map room       : trace the walls (wall on the robot's right) back to the
///                             starting point, turn the trace into corners, go home.
///   Phase 2  Find obstacles : drive back-and-forth lanes; anything new in the way is
///                             traced, saved, and routed around; then go home.
///   Phase 3  Wi-Fi survey   : plan lanes through the free space, clear the old path and
///                             heat map, show the planned route, follow it measuring Wi-Fi.
/// </summary>
public sealed partial class MainForm
{
    // ---- settings
    // Robot centre stays this far from walls and obstacles when planning routes.
    // Robot radius (8 cm) + a safety gap. Set in the Mapping row.
    private double KeepAwayMm => (double)_clearanceBox.Value * 10;
    private const double ExploreLaneSpacingMm = 350; // phase 2 lane spacing
    private const double SlowMeasureSpacingMm = 300; // phase 3 SLOW mode: measure this often
    private const double CornerToleranceMm = 80;     // outline simplification

    // ---- mission state
    private Telemetry? _lastTelemetry;
    private long _telemetrySeq;
    private ObstacleEvent? _lastObstacle;
    private long _obstacleSeq;
    private ShapeEvent? _lastShape;
    private long _shapeSeq;
    private int _lastTraceShapeId;
    private int? _roomShapeId;
    private List<(double Nx, double Ny, double C)>? _roomWalls;   // the room's walls as lines
    private const double RobotRadiusMm = 80;
    private const double WallGapMm = 180;   // robot centre to wall when it stops for it (radius + sensor reach)

    // Phase 1: where the robot drove while following the walls
    private readonly List<(double X, double Y)> _roomTracePath = new();
    private bool _recordingRoomTrace;
    private int _roomTraceStartPoints;
    private readonly HashSet<long> _failedCommands = new();
    private CancellationTokenSource? _missionCts;

    // ---- UI
    private readonly FlowLayoutPanel _missionRow = new()
    {
        Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(6, 0, 6, 6), WrapContents = true,
        BackColor = Color.FromArgb(243, 240, 250)
    };
    private readonly Button _phase1Btn = CtlButton("1  Map room");
    private readonly Button _phase2Btn = CtlButton("2  Find obstacles");
    private readonly Button _phase3Btn = CtlButton("3  Wi-Fi survey");
    private readonly Button _stopMissionBtn = CtlButton("■ Stop");
    private readonly Button _saveMapBtn = CtlButton("Save map");
    private readonly Button _loadMapBtn = CtlButton("Load map");
    private readonly NumericUpDown _clearanceBox = new() { Minimum = 9, Maximum = 40, Value = 13, Width = 50 };
    private readonly CheckBox _showNoGo = new() { Text = "Show no-go areas", AutoSize = true, Margin = new Padding(6, 6, 3, 3) };
    private readonly NumericUpDown _laneSpacing = new() { Minimum = 20, Maximum = 200, Increment = 5, Value = 40, Width = 55 };
    private readonly TextBox _cmdBox = new() { Width = 170, PlaceholderText = "e.g. SET,ssid,MyNetwork" };
    private readonly Button _cmdSendBtn = CtlButton("Send");
    private readonly Label _missionLbl = new() { AutoSize = true, Margin = new Padding(10, 7, 3, 3), ForeColor = Color.Indigo };

    private void BuildMissionRow()
    {
        _missionRow.Controls.AddRange(new Control[]
        {
            Caption("Mapping:"), _phase1Btn, _phase2Btn, _phase3Btn, _stopMissionBtn,
            Caption("Survey lane spacing (cm):"), _laneSpacing,
            Caption("Clearance (cm):"), _clearanceBox, _showNoGo, _saveMapBtn, _loadMapBtn,
            Caption("Robot command:"), _cmdBox, _cmdSendBtn, _missionLbl
        });
        // Type any robot command, e.g. SET,ssid,MyNetwork  SET,pass,MyPassword  SET,roam_dbm,-60
        void sendTyped()
        {
            var text = _cmdBox.Text.Trim();
            if (text.Length == 0) return;
            if (text.StartsWith("C,")) text = text[2..];       // in case the full form was typed
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
        _phase1Btn.Click += async (_, _) => await RunMission("Phase 1: map room", Phase1Async);
        _phase2Btn.Click += async (_, _) => await RunMission("Phase 2: find obstacles", Phase2Async);
        _phase3Btn.Click += async (_, _) => await RunMission("Phase 3: Wi-Fi survey", Phase3Async);
        _stopMissionBtn.Click += (_, _) => _missionCts?.Cancel();
        _showNoGo.CheckedChanged += (_, _) => RefreshNoGo();
        _clearanceBox.ValueChanged += (_, _) => RefreshNoGo();
        _saveMapBtn.Click += (_, _) => SaveMap();
        _loadMapBtn.Click += (_, _) => LoadMap();
        _missionLbl.Text = "Place the robot at its start spot, facing the same way each time.";
    }

    private void UpdateMissionButtons()
    {
        bool connected = _active?.CanSend == true;
        bool running = _missionCts != null;
        _phase1Btn.Enabled = connected && !running;
        _phase2Btn.Enabled = connected && !running && _roomShapeId != null;
        _phase3Btn.Enabled = connected && !running && _roomShapeId != null;
        _stopMissionBtn.Enabled = running;
        _loadMapBtn.Enabled = !running;
        _saveMapBtn.Enabled = !running && _roomShapeId != null;
    }

    private async Task RunMission(string name, Func<CancellationToken, Task> mission)
    {
        if (_missionCts != null) return;
        _missionCts = new CancellationTokenSource();
        UpdateMissionButtons();
        var ct = _missionCts.Token;
        MissionStatus(name + "...");
        try
        {
            await CommandOrThrow("AUTOROAM,OFF", "Auto roaming off", ct);
            await mission(ct);
            MissionStatus(name + " finished.");
        }
        catch (OperationCanceledException)
        {
            MissionStatus(name + " stopped.");
            SendCommand("PAUSE", "Pause");
        }
        catch (MissionException ex)
        {
            MissionStatus(name + " failed: " + ex.Message);
            SendCommand("PAUSE", "Pause");
        }
        finally
        {
            _missionCts?.Dispose();
            _missionCts = null;
            _map.Target = null;
            if (_active?.CanSend == true) SendCommand("AUTOROAM,ON", "Auto roaming on");
            UpdateMissionButtons();
        }
    }

    private void MissionStatus(string text)
    {
        _missionLbl.Text = text;
        SetStatus(text);
    }

    private sealed class MissionException : Exception
    {
        public MissionException(string message) : base(message) { }
    }

    // ================================================================ phases

    private async Task Phase1Async(CancellationToken ct)
    {
        ClearAll();   // a new room: start a fresh map
        MissionStatus("Phase 1: driving to the nearest wall, then tracing the room...");

        _roomTracePath.Clear();
        _roomTraceStartPoints = _map.Obstacles.Count;
        _recordingRoomTrace = true;
        ShapeEvent? shape;
        try { shape = await TraceAsync(room: true, ct); }
        finally { _recordingRoomTrace = false; }

        int id = shape?.ShapeId ?? _lastTraceShapeId;
        _map.ShapePoints.TryGetValue(id, out var pts);
        if (shape is not { Kind: 2 })
            MissionStatus("Phase 1: the outline didn't close cleanly; using what was traced.");

        _roomShapeId = id;
        _map.RoomShapeId = id;

        // Walls from the robot's own path (reliable), then make sure every wall hit is
        // outside them. Fall back to the wall hits alone if the path doesn't work out.
        var pathWalls = Geometry.WallsFromPath(_roomTracePath, WallGapMm);
        if (pathWalls != null && Geometry.CornersFromWalls(pathWalls, requireAll: false) is { } pathCorners)
        {
            _roomWalls = pathWalls;
            _map.SetCorners(id, pathCorners);
            if (pts != null)
                foreach (var p in pts.Where(p => p.FacingDeg.HasValue))
                    PushRoomWalls(p.X, p.Y, 0, p.FacingDeg);
        }
        else
        {
            if (pts == null || pts.Count < 3)
                throw new MissionException("the robot didn't drive a full loop round the room.");
            UpdateCorners(id);
        }
        var corners = _map.Corners.TryGetValue(id, out var c) ? c : new();
        double w = corners.Count > 0 ? corners.Max(p => p.X) - corners.Min(p => p.X) : 0;
        double h = corners.Count > 0 ? corners.Max(p => p.Y) - corners.Min(p => p.Y) : 0;
        MissionStatus($"Phase 1: room has {corners.Count} corners, about {w / 1000:0.0} x {h / 1000:0.0} m. Going home...");

        await GoHomeAsync(ct);
    }

    // Phases 2 and 3 (serpentine coverage with obstacle mapping) are in MainForm.Coverage.cs

    // ================================================================ building blocks

    /// <summary>
    /// Has this spot already been traced (a room wall, or part of an outline the robot
    /// went round)? Single hits from bumping into something DON'T count: those are
    /// exactly the obstacles that still need probing round.
    /// </summary>
    private bool IsNearTracedOutline(double x, double y, double radiusMm)
    {
        // A room wall?
        if (_roomShapeId is int rid && _map.Corners.TryGetValue(rid, out var room) && room.Count >= 3)
        {
            if (!Geometry.PointInPolygon(room, x, y)) return true;   // beyond the walls
            for (int i = 0; i < room.Count; i++)
            {
                var a = room[i];
                var b = room[(i + 1) % room.Count];
                double dx = b.X - a.X, dy = b.Y - a.Y, len2 = dx * dx + dy * dy;
                if (len2 < 1) continue;
                double t = Math.Clamp(((x - a.X) * dx + (y - a.Y) * dy) / len2, 0, 1);
                if (Geometry.Dist((x, y), (a.X + t * dx, a.Y + t * dy)) < radiusMm) return true;
            }
        }
        // An outline already traced (finished or not)?
        foreach (var (id, pts) in _map.ShapePoints)
        {
            if (id == _roomShapeId) continue;
            if (pts.Any(p => Geometry.Dist((p.X, p.Y), (x, y)) < radiusMm)) return true;
        }
        return false;
    }

    /// <summary>Drives straight to a point. True = arrived, false = stopped by an obstacle.</summary>
    private async Task<bool> DriveToAsync((double X, double Y) p, CancellationToken ct)
    {
        _map.Target = p;
        await CommandOrThrow(string.Create(CultureInfo.InvariantCulture, $"GOTO,{p.X:0},{p.Y:0},S"), "Go to point", ct);
        var t = await WaitForStateAsync(s => s is not ("GOTO" or "HOME"), ct);
        return t.State != "BLOCKED";
    }

    private async Task MeasureAsync(CancellationToken ct)
    {
        await CommandOrThrow("MEASURE", "Measure here", ct);
        await WaitForStateAsync(s => s != "MEASURE", ct);
    }

    private async Task GoHomeAsync(CancellationToken ct)
    {
        var path = BuildPlanner().Plan(CurrentPose(), (0, 0));
        if (path != null)
            foreach (var wp in path.Skip(1).SkipLast(1))
                if (!await DriveToAsync(wp, ct)) break;
        // Last leg with HOME: it also turns to face the starting direction
        _map.Target = (0, 0);
        await CommandOrThrow("HOME", "Return to start", ct);
        await WaitForStateAsync(s => s != "HOME", ct);
    }

    /// <summary>
    /// Traces an outline. While tracing an object, stops early if the robot starts
    /// following a known room wall (the object is against the wall).
    /// </summary>
    private async Task<ShapeEvent?> TraceAsync(bool room, CancellationToken ct)
    {
        long shapeSeq = _shapeSeq;
        long obstacleSeq = _obstacleSeq;
        int startId = _lastTraceShapeId;
        await CommandOrThrow(room ? "TRACE,ROOM" : "TRACE,OBJ", "Trace", ct);
        long seq = _telemetrySeq;
        (double X, double Y)? firstPoint = null;
        long lastPointSeq = _obstacleSeq;
        var lastPointTime = DateTime.Now;
        var traceStarted = DateTime.Now;

        while (true)
        {
            await Task.Delay(150, ct);
            if (_shapeSeq > shapeSeq) return _lastShape;
            if (_telemetrySeq > seq + 2 && _lastTelemetry?.State != "TRACE") return null;  // stopped

            // Object lost: no new edge point for a long time (probing gives one every few s)
            if (_obstacleSeq != lastPointSeq) { lastPointSeq = _obstacleSeq; lastPointTime = DateTime.Now; }
            if (!room && ((DateTime.Now - lastPointTime).TotalSeconds > 30 ||
                          (DateTime.Now - traceStarted).TotalMinutes > 4))   // never loop forever
            {
                await CommandOrThrow("PAUSE", "Pause", ct);
                return null;
            }

            if (!room && _roomShapeId is int roomId && _obstacleSeq > obstacleSeq &&
                _lastObstacle is { ShapeId: > 0 } o && o.ShapeId != startId)
            {
                firstPoint ??= (o.X, o.Y);
                if (Geometry.Dist(firstPoint.Value, (o.X, o.Y)) > 400 &&
                    _map.ShapePoints.TryGetValue(roomId, out var walls) &&
                    walls.Any(w => Geometry.Dist((w.X, w.Y), (o.X, o.Y)) < 120))
                {
                    await CommandOrThrow("PAUSE", "Pause", ct);
                    return null;
                }
            }
        }
    }

    private async Task<Telemetry> WaitForStateAsync(Func<string, bool> done, CancellationToken ct)
    {
        long seq = _telemetrySeq;
        var lastSeen = DateTime.Now;
        long lastSeq = seq;
        while (true)
        {
            await Task.Delay(100, ct);
            if (_telemetrySeq != lastSeq) { lastSeq = _telemetrySeq; lastSeen = DateTime.Now; }
            if ((DateTime.Now - lastSeen).TotalSeconds > 15)
                throw new MissionException("lost contact with the robot.");
            // Skip the first couple of readings: they may be from before the command
            if (_telemetrySeq > seq + 2 && _lastTelemetry is { State: { } st } t && done(st)) return t;
        }
    }

    private async Task CommandOrThrow(string command, string label, CancellationToken ct)
    {
        long id = SendCommand(command, label);
        if (id < 0) throw new MissionException("the robot isn't connected.");
        while (_pending.ContainsKey(id)) await Task.Delay(50, ct);
        if (_failedCommands.Remove(id)) throw new MissionException($"the robot didn't answer \"{label}\".");
    }

    private (double X, double Y) CurrentPose() =>
        _lastTelemetry is { } t ? (t.X, t.Y) : (0, 0);

    // ================================================================ map data

    private Planner BuildPlanner()
    {
        IReadOnlyList<(double X, double Y)>? room = null;
        var objects = new List<IReadOnlyList<(double X, double Y)>>();
        var loose = new List<(double X, double Y)>();

        foreach (var (id, pts) in _map.ShapePoints)
        {
            var list = pts.Select(p => (p.X, p.Y)).ToList();
            _map.ShapeKinds.TryGetValue(id, out int kind);
            // Use the cleaned-up corners where there are some (straight walls), else the raw points
            var outline = _map.Corners.TryGetValue(id, out var c) && c.Count >= 3 ? c : list;
            if (id == _roomShapeId) room = outline;
            else if (kind == 1) objects.Add(outline);
            else if (UncertainArea(list) is { } hull) objects.Add(hull);   // couldn't finish: block it all
            else loose.AddRange(list);
        }
        loose.AddRange(_map.Obstacles.Where(o => o.ShapeId == 0).Select(o => (o.X, o.Y)));
        var planner = new Planner(room, objects, loose, KeepAwayMm);
        if (_showNoGo.Checked)
        {
            var img = planner.NoGoImage(out var x0, out var yTop, out var cell);
            _map.SetNoGo(img, x0, yTop, cell);
        }
        return planner;
    }

    /// <summary>
    /// An obstacle outline that couldn't be finished: treat everything inside the points
    /// seen so far (their convex hull), plus 10 cm, as blocked. That's conservative: the
    /// robot won't keep poking at an obstacle it only partly knows. Huge or flat outlines
    /// (e.g. a run along a wall) are left as single points instead.
    /// </summary>
    private static List<(double X, double Y)>? UncertainArea(List<(double X, double Y)> pts)
    {
        if (pts.Count < 3) return null;
        var hull = Geometry.ConvexHull(pts);
        if (hull.Count < 3) return null;
        double area = Math.Abs(Geometry.SignedArea(hull));
        if (area < 100 * 100 || area > 1.5e6) return null;   // a line of points, or bigger than 1.5 m^2
        return Geometry.Grow(hull, 100);
    }

    /// <summary>Redraws (or hides) the no-go areas after the map or clearance changes.</summary>
    private void RefreshNoGo()
    {
        if (_showNoGo.Checked && (_roomShapeId != null || _map.Obstacles.Count > 0)) BuildPlanner();
        else _map.SetNoGo(null);
    }

    private void UpdateCorners(int shapeId)
    {
        if (!_map.ShapePoints.TryGetValue(shapeId, out var pts) || pts.Count < 4) return;
        _map.ShapeKinds.TryGetValue(shapeId, out int kind);
        // Only the room and finished objects get corners. A closed outline that isn't the
        // room but went round the inside of something (kind 2) isn't an object either.
        if (shapeId != _roomShapeId && kind != 1) return;
        // Best: walls from the direction the sensor faced. Fallback: simplify the outline.
        List<(double X, double Y)>? corners = null;
        if (pts.All(p => p.FacingDeg.HasValue))
        {
            var facing = pts.Select(p => (p.X, p.Y, Facing: p.FacingDeg!.Value)).ToList();
            if (shapeId == _roomShapeId)
            {
                _roomWalls = Geometry.WallsFromFacing(facing);
                if (_roomWalls != null) corners = Geometry.CornersFromWalls(_roomWalls, requireAll: false);
            }
            else corners = Geometry.CornersFromFacing(facing);
        }
        corners ??= Geometry.SimplifyClosed(pts.Select(p => (p.X, p.Y)).ToList(), CornerToleranceMm);
        _map.SetCorners(shapeId, corners);
    }

    // ---------------------------------------------------------------- refining the walls

    /// <summary>
    /// The robot can't go through a real wall, so if it's ever beyond one of the room's
    /// walls (or sees an obstacle beyond it, straight ahead of that wall), the wall must
    /// really be further out: move it out to there, keeping its direction.
    ///   robot position: margin = robot radius, any wall
    ///   obstacle hit:   margin 0, only walls the sensor was facing (within 35 deg)
    /// Walls are only ever moved outwards.
    /// </summary>
    private void PushRoomWalls(double x, double y, double margin, double? facingDeg)
    {
        if (_roomShapeId is not int id || _roomWalls is not { Count: >= 3 } walls) return;
        var corners = Geometry.CornersFromWalls(walls, requireAll: true);
        if (corners == null) return;

        int n = walls.Count;
        double moved = 0;
        for (int k = 0; k < n; k++)
        {
            var (nx, ny, c) = walls[k];
            if (facingDeg is double f)
            {
                double wallDeg = Math.Atan2(ny, nx) * 180 / Math.PI;
                double diff = Math.Abs(f - wallDeg) % 360;
                if (diff > 180) diff = 360 - diff;
                if (diff > 35) continue;
            }
            // Only the wall the point is actually beside (between its two corners, give or take)
            var a = corners[(k - 1 + n) % n];
            var b = corners[k];
            double dx = b.X - a.X, dy = b.Y - a.Y, len = Math.Sqrt(dx * dx + dy * dy);
            if (len < 1) continue;
            double t = ((x - a.X) * dx + (y - a.Y) * dy) / len;
            if (t < -200 || t > len + 200) continue;

            double need = nx * x + ny * y + margin;
            if (need > c + 1)
            {
                walls[k] = (nx, ny, need);
                moved = Math.Max(moved, need - c);
            }
        }
        if (moved <= 0) return;

        if (Geometry.CornersFromWalls(walls, requireAll: false) is { } newCorners)
            _map.SetCorners(id, newCorners);
        if (moved > 30 && (DateTime.Now - _lastWallMsg).TotalSeconds > 3)
        {
            _lastWallMsg = DateTime.Now;
            SetStatus($"Room wall moved out {moved / 1000:0.00} m: the robot found space beyond it.");
        }
    }
    private DateTime _lastWallMsg = DateTime.MinValue;

    // ---------------------------------------------------------------- save / load

    internal sealed record SavedShape(int Id, int Kind, double[][] Points);
    internal sealed record SavedMap(int? RoomShapeId, List<SavedShape> Shapes, double[][] Loose,
                                    double[][]? RoomWalls = null);

    /// <summary>
    /// Saves two files from one name:
    ///   name.json            the room AND the obstacles
    ///   name_room_only.json  just the room walls (e.g. to map obstacles again after
    ///                        furniture has moved, without re-mapping the room)
    /// </summary>
    private void SaveMap()
    {
        using var dlg = new SaveFileDialog
        {
            Filter = "Room map|*.json", FileName = "room_map.json",
            Title = "Save map (a room-only copy is saved next to it)"
        };
        if (dlg.ShowDialog(this) != DialogResult.OK) return;

        SavedShape ToSaved(int id, List<ObstacleEvent> pts) => new(
            id, _map.ShapeKinds.TryGetValue(id, out var k) ? k : 0,
            pts.Select(p => p.FacingDeg is double f ? new[] { p.X, p.Y, f } : new[] { p.X, p.Y }).ToArray());

        var walls = _roomWalls?.Select(w => new[] { w.Nx, w.Ny, w.C }).ToArray();
        var options = new JsonSerializerOptions { WriteIndented = true };

        // 1. Room + obstacles
        var allShapes = _map.ShapePoints.Select(kv => ToSaved(kv.Key, kv.Value)).ToList();
        var loose = _map.Obstacles.Where(o => o.ShapeId == 0).Select(o => new[] { o.X, o.Y }).ToArray();
        File.WriteAllText(dlg.FileName,
            JsonSerializer.Serialize(new SavedMap(_roomShapeId, allShapes, loose, walls), options));

        // 2. Room only
        var roomShapes = _roomShapeId is int rid && _map.ShapePoints.TryGetValue(rid, out var roomPts)
            ? new List<SavedShape> { ToSaved(rid, roomPts) }
            : new List<SavedShape>();
        string roomOnlyPath = Path.Combine(Path.GetDirectoryName(dlg.FileName) ?? "",
            Path.GetFileNameWithoutExtension(dlg.FileName) + "_room_only.json");
        File.WriteAllText(roomOnlyPath,
            JsonSerializer.Serialize(new SavedMap(_roomShapeId, roomShapes, Array.Empty<double[]>(), walls), options));

        int objects = allShapes.Count(sh => sh.Id != _roomShapeId);
        SetStatus($"Saved {Path.GetFileName(dlg.FileName)} (room + {objects} obstacle outlines, " +
                  $"{loose.Length} single hits) and {Path.GetFileName(roomOnlyPath)} (room only).");
    }

    private void LoadMap()
    {
        using var dlg = new OpenFileDialog { Filter = "Room map|*.json" };
        if (dlg.ShowDialog(this) != DialogResult.OK) return;
        try
        {
            var map = JsonSerializer.Deserialize<SavedMap>(File.ReadAllText(dlg.FileName))
                      ?? throw new InvalidDataException("empty file");
            ClearAll();
            foreach (var sh in map.Shapes)
            {
                foreach (var p in sh.Points)
                    _map.AddObstacle(new ObstacleEvent(0, p[0], p[1], sh.Id, p.Length > 2 ? p[2] : null));
                _map.FinishShape(new ShapeEvent(0, sh.Id, sh.Kind));
            }
            foreach (var p in map.Loose) _map.AddObstacle(new ObstacleEvent(0, p[0], p[1]));
            _roomShapeId = map.RoomShapeId;
            _map.RoomShapeId = map.RoomShapeId;
            foreach (var sh in map.Shapes) UpdateCorners(sh.Id);
            if (map.RoomWalls is { Length: >= 3 } savedWalls && _roomShapeId is int rid)
            {
                // Walls as they were saved, including any that were pushed out later
                _roomWalls = savedWalls.Select(w => (Nx: w[0], Ny: w[1], C: w[2])).ToList();
                if (Geometry.CornersFromWalls(_roomWalls, requireAll: false) is { } wc) _map.SetCorners(rid, wc);
            }
            _map.FitToData();
            UpdateShapeLabel();
            UpdateMissionButtons();
            SetStatus($"Map loaded. Put the robot on its start spot, facing the same way as when it was mapped.");
        }
        catch (Exception ex)
        {
            SetStatus("Couldn't load map: " + ex.Message);
        }
    }
}
