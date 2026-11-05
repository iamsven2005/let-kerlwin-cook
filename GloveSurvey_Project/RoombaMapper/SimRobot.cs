using RoombaMapper.Core;

/// <summary>
/// A simulated room: free floor = union of polygons; solid = boxes (polygons) and posts.
/// </summary>
public sealed class SimWorld
{
    public List<List<(double X, double Y)>> Floors { get; } = new();
    public List<List<(double X, double Y)>> Boxes { get; } = new();
    public List<(double X, double Y, double R)> Posts { get; } = new();

    public bool Solid(double x, double y)
    {
        if (!Floors.Any(f => Geometry.PointInPolygon(f, x, y))) return true;
        if (Boxes.Any(b => Geometry.PointInPolygon(b, x, y))) return true;
        foreach (var (px, py, r) in Posts) if ((x - px) * (x - px) + (y - py) * (y - py) <= r * r) return true;
        return false;
    }

    /// <summary>Distance along a ray to the first solid point (max if none).</summary>
    public double Ray((double X, double Y) o, double deg, double max)
    {
        double a = deg * Math.PI / 180, c = Math.Cos(a), s = Math.Sin(a);
        for (double d = 0; d <= max; d += 4)
            if (Solid(o.X + c * d, o.Y + s * d)) return d;
        return double.MaxValue;
    }

    public static List<(double X, double Y)> Rect(double x0, double y0, double x1, double y1) =>
        new() { (x0, y0), (x1, y0), (x1, y1), (x0, y1) };
}

/// <summary>
/// A simulated robot implementing the same primitives as the TinyScreen+ firmware:
/// MOVE (stops when the front IR sees something within R, after 2 readings), TURN,
/// GOTO, SCAN (wiggle), LOOK (turn 90 / look / turn back), MEASURE, HOME.
/// The IR is a single ray from the front edge. Collisions are counted (must stay 0).
/// Optional: heading drift, a stuck sensor, losing contact, obstacles appearing later.
/// </summary>
public sealed class SimRobot : IRobot
{
    private readonly SimWorld _w;
    private readonly RobotConfig _c;
    private double _x, _y, _h;                 // true pose
    private double _ox, _oy, _oh;              // odometry pose (what the robot reports)

    public int Commands { get; private set; }
    public int Collisions { get; private set; }
    public List<string> CollisionLog { get; } = new();
    public string LastCommand = "";
    public List<string> Trail { get; } = new();          // recent primitives, for debugging
    private void Note(string what) { Trail.Add($"#{Commands} {what} true=({_x:0},{_y:0},{_h:0})"); if (Trail.Count > 40) Trail.RemoveAt(0); }
    public double Travelled { get; private set; }
    public bool Stopped { get; private set; }
    public double HeadingDriftPerTurn { get; set; }        // e.g. 0.01 = 1% turn error
    public bool SensorStuck { get; set; }
    public int LoseContactAfter { get; set; } = int.MaxValue;
    public List<(int AfterCommand, List<(double X, double Y)> Box)> LateBoxes { get; } = new();
    public List<(double X, double Y, double Rssi)> Readings { get; } = new();
    public bool FastReadings { get; set; }
    public (double X, double Y) Router { get; set; } = (2000, 1500);
    private double _sinceReading;

    public SimRobot(SimWorld w, RobotConfig c, double x = 0, double y = 0, double h = 0)
    {
        _w = w; _c = c; _x = x; _y = y; _h = h;
    }

    public Pose Pose => new(_ox, _oy, Geometry.Wrap180(_oh));
    public (double X, double Y, double H) TruePose => (_x, _y, _h);

    private void Tick()
    {
        Commands++;
        if (Commands > LoseContactAfter) throw new RobotException("lost contact with the robot (simulated)");
        foreach (var lb in LateBoxes.Where(l => l.AfterCommand == Commands).ToList()) { _w.Boxes.Add(lb.Box); Note("BOX APPEARS"); Console.WriteLine($"  (box appeared at command {Commands}, robot at {_x:0},{_y:0})"); }
    }

    private bool FrontSees() =>
        SensorStuck || _w.Ray(Geometry.FromPolar((_x, _y), _h, _c.SensorOffsetMm), _h, _c.RangeMm) <= _c.RangeMm;

    private bool BodyHits(double x, double y)
    {
        double r = Math.Min(_c.LengthMm, _c.WidthMm) / 2;
        for (int k = 0; k < 12; k++)
        {
            var p = Geometry.FromPolar((x, y), k * 30, r);
            if (_w.Solid(p.X, p.Y)) return true;
        }
        return false;
    }

    public Task ResetPoseAsync(CancellationToken ct) { Tick(); _ox = _oy = _oh = 0; _x = _y = _h = 0; return Task.CompletedTask; }

    public Task<MoveResult> MoveAsync(double mm, bool ignoreFront, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        Tick();
        Stopped = false;
        double sign = Math.Sign(mm), left = Math.Abs(mm);
        int yes = 0;
        while (left > 0)
        {
            if (sign > 0 && !ignoreFront)
            {
                yes = FrontSees() ? yes + 1 : 0;
                if (yes >= 2) { Note($"move {mm:0} BLOCKED after {Math.Abs(mm) - left:0}"); return Task.FromResult(new MoveResult(true, Pose)); }
            }
            double step = Math.Min(5, left);
            double nx = _x + sign * step * Math.Cos(_h * Math.PI / 180);
            double ny = _y + sign * step * Math.Sin(_h * Math.PI / 180);
            if (BodyHits(nx, ny))
            {
                Collisions++;
                CollisionLog.Add($"move {mm:0}{(ignoreFront ? " IGNOREFRONT" : "")} at true ({_x:0},{_y:0}) h={_h:0} after {Math.Abs(mm) - left:0} mm\n      " + string.Join("\n      ", Trail.TakeLast(10)));
                return Task.FromResult(new MoveResult(true, Pose));
            }
            _x = nx; _y = ny;
            _ox += sign * step * Math.Cos(_oh * Math.PI / 180);
            _oy += sign * step * Math.Sin(_oh * Math.PI / 180);
            left -= step;
            Travelled += step;
            if (FastReadings && (_sinceReading += step) >= 45) { _sinceReading = 0; Readings.Add((_x, _y, Rssi())); }
        }
        Note($"move {mm:0} done");
        return Task.FromResult(new MoveResult(false, Pose));
    }

    public Task TurnAsync(double deg, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        Tick();
        _oh = Geometry.Wrap180(_oh + deg);
        _h = Geometry.Wrap180(_h + deg * (1 + HeadingDriftPerTurn));
        Note($"turn {deg:0}");
        return Task.CompletedTask;
    }

    public async Task<MoveResult> GotoAsync((double X, double Y) p, CancellationToken ct)
    {
        double dist = Math.Sqrt((p.X - _ox) * (p.X - _ox) + (p.Y - _oy) * (p.Y - _oy));
        if (dist < 5) { Tick(); return new MoveResult(false, Pose); }
        await TurnAsync(Geometry.Wrap180(Geometry.BearingDeg((_ox, _oy), p) - _oh), ct);
        return await MoveAsync(dist, false, ct);
    }

    public Task<ScanResult> ScanAsync(double halfDeg, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        Tick();
        var arcs = new List<Arc>();
        double h0 = _h, oh0 = _oh;
        double? start = null;
        double last = -halfDeg;
        for (double d = -halfDeg; d <= halfDeg + 1e-9; d += 2)
        {
            _h = h0 + d;
            bool yes = FrontSees();
            if (yes && start == null) start = d;
            if (!yes && start != null) { arcs.Add(new Arc(oh0 + start.Value, oh0 + last)); start = null; }
            last = d;
        }
        if (start != null) arcs.Add(new Arc(oh0 + start.Value, oh0 + halfDeg));
        _h = h0;
        Note($"scan {halfDeg:0}: " + string.Join(" ", arcs.Select(a => $"[{a.StartDeg:0},{a.EndDeg:0}]")));
        return Task.FromResult(new ScanResult(Pose, halfDeg, arcs));
    }

    public Task<LookResult> LookAsync(int side, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        Tick();
        double dir = _h + side * 90;
        bool seen = SensorStuck || _w.Ray(Geometry.FromPolar((_x, _y), dir, _c.SensorOffsetMm), dir, _c.RangeMm) <= _c.RangeMm;
        Note($"look {side}: {seen}");
        return Task.FromResult(new LookResult(seen, Pose, Geometry.Wrap180(_oh + side * 90)));
    }

    public Task MeasureAsync(CancellationToken ct)
    {
        Tick();
        Readings.Add((_x, _y, Rssi()));
        return Task.CompletedTask;
    }

    public async Task HomeAsync(CancellationToken ct)
    {
        await GotoAsync((0, 0), ct);
        await TurnAsync(Geometry.Wrap180(-_oh), ct);
    }

    public Task SetAutoRoamAsync(bool on, CancellationToken ct) { Tick(); return Task.CompletedTask; }

    public Task StopAsync() { Stopped = true; return Task.CompletedTask; }

    private double Rssi()
    {
        double d = Math.Max(0.3, Math.Sqrt((_x - Router.X) * (_x - Router.X) + (_y - Router.Y) * (_y - Router.Y)) / 1000);
        return -38 - 27 * Math.Log10(d);
    }
}
