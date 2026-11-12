using System.Globalization;
using RoombaMapper.Core;

namespace RoombaMapper;

/// <summary>
/// The real robot, as the navigation Core sees it (IRobot), over the app's existing
/// command channel. Each primitive is one command; the robot confirms it straight away
/// (K line) and reports when it has fully finished through the "done id" in its
/// telemetry. Scan and side-look results arrive as W and Q lines.
/// </summary>
public sealed class RobotLink : IRobot
{
    /// <summary>Sends a command, waits for the robot to confirm it, returns its id.</summary>
    private readonly Func<string, string, CancellationToken, Task<long>> _command;
    private readonly Action _stop;
    private Telemetry? _last;
    private long _doneId = -1;
    private DateTime _lastSeen = DateTime.Now;
    private readonly Dictionary<long, string[]> _results = new();
    private static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

    public RobotLink(Func<string, string, CancellationToken, Task<long>> command, Action stop)
    {
        _command = command;
        _stop = stop;
    }

    public Pose Pose => _last is { } t ? new Pose(t.X, t.Y, t.HeadingDeg) : default;

    // ---------------------------------------------------------------- incoming (UI thread)

    public void OnTelemetry(Telemetry t)
    {
        _last = t;
        _lastSeen = DateTime.Now;
        if (t.DoneId is long d && d > _doneId) _doneId = d;
    }

    public void OnResultLine(string line)
    {
        var p = line.Trim().Split(',');
        if (p.Length >= 2 && long.TryParse(p[1], NumberStyles.Integer, Inv, out var id)) _results[id] = p;
    }

    // ---------------------------------------------------------------- primitives

    public async Task ResetPoseAsync(CancellationToken ct) => await RunAsync("RESET", "Set start", ct, 10);

    public async Task<MoveResult> MoveAsync(double mm, bool ignoreFront, CancellationToken ct)
    {
        var t = await RunAsync(string.Create(Inv, $"MOVE,{mm:0}{(ignoreFront ? ",I" : "")}"), "Move", ct, 30 + Math.Abs(mm) / 50);
        return new MoveResult(t.State == "BLOCKED", PoseOf(t));
    }

    public async Task TurnAsync(double deg, CancellationToken ct)
    {
        if (Math.Abs(deg) < 0.5) return;
        await RunAsync(string.Create(Inv, $"TURN,{deg:0.#}"), "Turn", ct, 30);
    }

    public async Task<MoveResult> GotoAsync((double X, double Y) p, CancellationToken ct)
    {
        double dist = Geometry.Dist(Pose.Pos, p);
        var t = await RunAsync(string.Create(Inv, $"GOTO,{p.X:0},{p.Y:0},S"), "Go to point", ct, 30 + dist / 50);
        return new MoveResult(t.State == "BLOCKED", PoseOf(t));
    }

    public async Task<ScanResult> ScanAsync(double halfDeg, CancellationToken ct)
    {
        var (id, t) = await RunWithIdAsync(string.Create(Inv, $"SCAN,{halfDeg:0}"), "Scan", ct, 40);
        var p = await ResultAsync(id, ct);
        // W,id,x,y,heading,half,n,s1,e1,...
        double D(int i) => double.Parse(p[i], Inv);
        var at = new Pose(D(2), D(3), D(4));
        int n = (int)D(6);
        var arcs = new List<Arc>();
        for (int i = 0; i < n && 8 + 2 * i < p.Length; i++) arcs.Add(new Arc(D(7 + 2 * i), D(8 + 2 * i)));
        return new ScanResult(at, D(5), arcs);
    }

    public async Task<LookResult> LookAsync(int side, CancellationToken ct)
    {
        var (id, _) = await RunWithIdAsync(side > 0 ? "LOOK,L" : "LOOK,R", "Look", ct, 20);
        var p = await ResultAsync(id, ct);
        // Q,id,seen,x,y,heading,facing
        double D(int i) => double.Parse(p[i], Inv);
        return new LookResult(p[2] == "1", new Pose(D(3), D(4), D(5)), D(6));
    }

    public async Task MeasureAsync(CancellationToken ct) => await RunAsync("MEASURE", "Measure", ct, 30);

    public async Task HomeAsync(CancellationToken ct) => await RunAsync("HOME", "Return to start", ct, 120);

    public async Task SetAutoRoamAsync(bool on, CancellationToken ct) =>
        await RunAsync(on ? "AUTOROAM,ON" : "AUTOROAM,OFF", "Auto roaming", ct, 10);

    public Task StopAsync()
    {
        try { _stop(); } catch { /* never throw while stopping */ }
        return Task.CompletedTask;
    }

    // ---------------------------------------------------------------- plumbing

    private async Task<Telemetry> RunAsync(string cmd, string label, CancellationToken ct, double timeoutS) =>
        (await RunWithIdAsync(cmd, label, ct, timeoutS)).T;

    /// <summary>Send, then wait until the robot reports this command finished.</summary>
    private async Task<(long Id, Telemetry T)> RunWithIdAsync(string cmd, string label, CancellationToken ct, double timeoutS)
    {
        long id = await _command(cmd, label, ct);
        var start = DateTime.Now;
        while (_doneId < id || _last == null)
        {
            await Task.Delay(30, ct);
            if ((DateTime.Now - _lastSeen).TotalSeconds > 15)
                throw new RobotException("lost contact with the robot (no data for 15 s)");
            if ((DateTime.Now - start).TotalSeconds > timeoutS)
                throw new RobotException($"the robot didn't finish \"{label}\" within {timeoutS:0} s");
        }
        return (id, _last.Value);
    }

    /// <summary>
    /// The W/Q line for a command (sent just before its completion). Wi-Fi can drop a
    /// packet, so if it hasn't arrived after a second, ask the robot to send it again
    /// (it keeps its last result), up to 3 times.
    /// </summary>
    private async Task<string[]> ResultAsync(long id, CancellationToken ct)
    {
        for (int attempt = 0; attempt <= 3; attempt++)
        {
            var start = DateTime.Now;
            while ((DateTime.Now - start).TotalSeconds < 1.0)
            {
                if (_results.Remove(id, out var p)) return p;
                await Task.Delay(20, ct);
            }
            if (attempt < 3) await _command("RESEND", "Resend result", ct);
        }
        throw new RobotException("the robot's scan/look result never arrived, even after asking 3 times. " +
                                 "Check the log for lines starting with W, or Q, from the robot.");
    }

    private static Pose PoseOf(Telemetry t) => new(t.X, t.Y, t.HeadingDeg);
}
