namespace RoombaMapper.Core;

// =====================================================================================
//  The robot, as the navigation code sees it: a handful of primitives.
//  Implemented by the real robot link (app <-> TinyScreen+ over Wi-Fi) and by the
//  simulator used for testing. All navigation logic is built on these, so it behaves
//  the same on both.
// =====================================================================================

/// <summary>Result of a straight move or a go-to.</summary>
public readonly record struct MoveResult(bool Blocked, Pose End)
{
    /// <summary>The robot's facing when it stopped (the obstacle is straight ahead).</summary>
    public double FacingDeg => End.HeadingDeg;
}

/// <summary>A heading range where the IR said YES during a wiggle scan (degrees, absolute).</summary>
public readonly record struct Arc(double StartDeg, double EndDeg)
{
    public double Width => EndDeg - StartDeg;
    public double CenterDeg => (StartDeg + EndDeg) / 2;
}

/// <summary>
/// Wiggle scan: the robot turned from (heading - half) to (heading + half) and back.
/// TouchesLow / TouchesHigh: an arc reached the end of the sweep (the surface goes on).
/// </summary>
public sealed record ScanResult(Pose At, double HalfDeg, IReadOnlyList<Arc> Arcs)
{
    public bool Any => Arcs.Count > 0;
    public bool TouchesLow(Arc a) => a.StartDeg <= At.HeadingDeg - HalfDeg + 1;
    public bool TouchesHigh(Arc a) => a.EndDeg >= At.HeadingDeg + HalfDeg - 1;
}

/// <summary>Side look: is there something on that side? Facing = the direction looked.</summary>
public readonly record struct LookResult(bool Seen, Pose At, double FacingDeg);

public interface IRobot
{
    /// <summary>The robot's latest pose (from its wheel odometry).</summary>
    Pose Pose { get; }

    /// <summary>Set the current spot as (0,0,0).</summary>
    Task ResetPoseAsync(CancellationToken ct);

    /// <summary>Drive straight (negative = backwards). Stops if the front sensor sees something,
    /// unless ignoreFront (only used to pass something already classified as off the path).</summary>
    Task<MoveResult> MoveAsync(double mm, bool ignoreFront, CancellationToken ct);

    /// <summary>Turn on the spot by deg (positive = left / anticlockwise).</summary>
    Task TurnAsync(double deg, CancellationToken ct);

    /// <summary>Turn to face p, then drive straight to it. Stops if something is in the way.</summary>
    Task<MoveResult> GotoAsync((double X, double Y) p, CancellationToken ct);

    /// <summary>Wiggle scan: sweep +/- halfDeg, report where the IR said YES, come back.</summary>
    Task<ScanResult> ScanAsync(double halfDeg, CancellationToken ct);

    /// <summary>Look to one side (side: -1 right, +1 left). Uses a side sensor if fitted,
    /// otherwise turns 90 degrees, looks with the front sensor and turns back.</summary>
    Task<LookResult> LookAsync(int side, CancellationToken ct);

    /// <summary>Stop and average Wi-Fi readings here (SLOW mode).</summary>
    Task MeasureAsync(CancellationToken ct);

    /// <summary>Drive straight back to (0,0) and face the starting direction.</summary>
    Task HomeAsync(CancellationToken ct);

    /// <summary>Allow or block automatic access-point roaming (only ever while stopped).</summary>
    Task SetAutoRoamAsync(bool on, CancellationToken ct);

    /// <summary>Stop now. Must not throw.</summary>
    Task StopAsync();
}

/// <summary>The robot didn't answer, lost contact, or reported a fault.</summary>
public sealed class RobotException : Exception
{
    public RobotException(string message) : base(message) { }
}

/// <summary>The mission can't go on (reason in the message). The robot has been stopped.</summary>
public sealed class MissionException : Exception
{
    public MissionException(string message) : base(message) { }
}

/// <summary>What a mission reports back to whoever runs it (the app, or a test).</summary>
public interface IMissionHost
{
    /// <summary>Short status line, e.g. "Phase 2: tracing a pillar".</summary>
    void Status(string text);
    /// <summary>Progress 0..1 and a detail line, e.g. "seeds 3/7, 64% covered".</summary>
    void Progress(double fraction, string detail);
    /// <summary>Detailed log line (decisions, classifications, re-plans).</summary>
    void Log(string text);
    /// <summary>The map changed (redraw).</summary>
    void MapChanged();
    /// <summary>The planned route changed (null = none).</summary>
    void PlanChanged(IReadOnlyList<(double X, double Y)>? route);
}
