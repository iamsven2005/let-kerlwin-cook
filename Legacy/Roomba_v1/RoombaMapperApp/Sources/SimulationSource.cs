using System.Globalization;

namespace RoombaMapper.Sources;

/// <summary>
/// Pretends to be the robot: drives a back-and-forth "lawnmower" pattern across a
/// 3.4 m x 2.4 m area, with a fake router in one corner and a fake wall that weakens
/// the signal. It produces the exact same text lines the real robot sends, so the
/// whole parsing and mapping path gets tested.
/// </summary>
public sealed class SimulationSource : DataSourceBase
{
    private const double SpeedMmPerS = 150;         // forward speed
    private const double TurnRadPerS = Math.PI / 2; // 90 deg per second when turning
    private const double TimeScale = 4;             // run 4x faster than real time
    private const double TickMs = 50;

    private static readonly (double X, double Y) Router = (3300, 2300);
    // A wall segment between the robot and the router on part of the area
    private static readonly (double X1, double Y1, double X2, double Y2) Wall = (1800, -200, 1800, 1500);

    private readonly List<(double X, double Y)> _waypoints = new();
    private readonly Random _rng = new();
    private readonly object _lock = new();
    private System.Threading.Timer? _timer;

    private double _x, _y, _theta, _dist, _t;
    private int _wp;
    private bool _finished;

    public SimulationSource()
    {
        const double laneLength = 3400, laneGap = 300;
        const int lanes = 9;
        for (int i = 0; i < lanes; i++)
        {
            double y = i * laneGap;
            if (i % 2 == 0) { _waypoints.Add((0, y)); _waypoints.Add((laneLength, y)); }
            else { _waypoints.Add((laneLength, y)); _waypoints.Add((0, y)); }
        }
    }

    public override string Name => "Simulation";

    public override Task StartAsync()
    {
        _timer = new System.Threading.Timer(Tick, null, 0, (int)TickMs);
        OnStatus($"Simulation running ({TimeScale}x speed)");
        return Task.CompletedTask;
    }

    public override void Stop()
    {
        _timer?.Dispose();
        _timer = null;
    }

    private void Tick(object? _)
    {
        lock (_lock)
        {
            if (_finished) return;

            double simSeconds = TickMs / 1000.0 * TimeScale;
            const double dt = 0.02;
            for (double s = 0; s < simSeconds - 1e-9 && !_finished; s += dt) Step(dt);

            Emit();
            if (_finished) OnStatus("Simulation: survey complete");
        }
    }

    private void Step(double dt)
    {
        if (_wp >= _waypoints.Count) { _finished = true; return; }

        var (tx, ty) = _waypoints[_wp];
        double dx = tx - _x, dy = ty - _y;
        double d = Math.Sqrt(dx * dx + dy * dy);
        if (d < 2) { _wp++; return; }

        double err = Normalize(Math.Atan2(dy, dx) - _theta);
        if (Math.Abs(err) > 0.02)
        {
            // Turn in place first
            _theta = Normalize(_theta + Math.Sign(err) * Math.Min(Math.Abs(err), TurnRadPerS * dt));
        }
        else
        {
            double step = Math.Min(d, SpeedMmPerS * dt);
            _x += step * Math.Cos(_theta);
            _y += step * Math.Sin(_theta);
            _dist += step;
        }
        _t += dt;
    }

    private void Emit()
    {
        double dm = Math.Max(0.3, Math.Sqrt(Sq(_x - Router.X) + Sq(_y - Router.Y)) / 1000.0);
        double rssi = -38 - 27 * Math.Log10(dm); // simple path-loss model
        if (Intersects(_x, _y, Router.X, Router.Y, Wall.X1, Wall.Y1, Wall.X2, Wall.Y2))
            rssi -= 7;                           // wall loss
        rssi += Gaussian() * 2;                  // measurement noise

        var line = string.Format(CultureInfo.InvariantCulture,
            "P,{0},{1:0},{2:0},{3:0.0},{4:0},{5:0}\n",
            (long)(_t * 1000), _x, _y, _theta * 180 / Math.PI, _dist, rssi);
        OnText(line);
    }

    private double Gaussian()
    {
        double u1 = 1.0 - _rng.NextDouble(), u2 = _rng.NextDouble();
        return Math.Sqrt(-2 * Math.Log(u1)) * Math.Cos(2 * Math.PI * u2);
    }

    private static double Sq(double v) => v * v;

    private static double Normalize(double a)
    {
        while (a > Math.PI) a -= 2 * Math.PI;
        while (a <= -Math.PI) a += 2 * Math.PI;
        return a;
    }

    private static bool Intersects(double ax, double ay, double bx, double by,
                                   double cx, double cy, double dx, double dy)
    {
        double d1 = Cross(cx, cy, dx, dy, ax, ay), d2 = Cross(cx, cy, dx, dy, bx, by);
        double d3 = Cross(ax, ay, bx, by, cx, cy), d4 = Cross(ax, ay, bx, by, dx, dy);
        return (d1 > 0) != (d2 > 0) && (d3 > 0) != (d4 > 0);
    }

    private static double Cross(double ox, double oy, double ax, double ay, double bx, double by)
        => (ax - ox) * (by - oy) - (ay - oy) * (bx - ox);
}
