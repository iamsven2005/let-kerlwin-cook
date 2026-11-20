namespace RoombaMapper.Core;

public enum Detection { None, Narrow, Wide }

/// <summary>
/// Navigation pieces shared by all three phases: wiggle scan + classification, route
/// driving, returning home, and safety checks. Phases differ in what they do with the
/// results, not in how the robot moves.
/// </summary>
public sealed class Nav
{
    public IRobot Robot { get; }
    public RobotConfig Cfg { get; }
    public WorldMap Map { get; }
    public IMissionHost Host { get; }

    /// <summary>Wall correction from the robot's position (phases 1 and 2 only).</summary>
    public bool WallCorrection { get; set; }

    /// <summary>
    /// Guarded driving (phases 1 and 2: space not explored yet). The front IR reaches only
    /// R beyond the robot's front edge along a narrow beam, so on its own it can't see
    /// something about to brush the robot's side. So every GuardStep the robot wiggles
    /// +/- GuardScanHalf and stops if anything unknown is inside the strip its body will
    /// sweep. Slower, but the body is protected. Phase 3 (map known) drives unguarded.
    /// </summary>
    public bool Guarded { get; set; }
    /// <summary>Guard step for the current phase (null = Cfg.GuardStepMm).</summary>
    public double? GuardStepMm { get; set; }

    private readonly List<(Pose At, double Heading)> _fullScans = new();

    public Nav(IRobot robot, RobotConfig cfg, WorldMap map, IMissionHost host)
    {
        Robot = robot; Cfg = cfg; Map = map; Host = host;
    }

    // ---------------------------------------------------------------- scanning

    /// <summary>
    /// Wiggle scan and classify the arc nearest to straight ahead.
    ///   arc >= ArcWide, or it runs off both ends of the sweep -> Wide (solid surface)
    ///   arc <= ArcNarrow                                       -> Narrow (leg, post)
    ///   in between -> move 0.3R closer and scan again (a wall's arc grows a lot, a post's
    ///   only a little); still in between -> Narrow (the safer answer: it gets investigated).
    /// </summary>
    public async Task<(Detection Kind, Arc? Arc, ScanResult Scan)> ScanAndClassifyAsync(
        double halfDeg, bool allowCloser, CancellationToken ct)
    {
        var sc = await Robot.ScanAsync(halfDeg, ct);
        CheckSensor(sc);
        if (!sc.Any) return (Detection.None, null, sc);

        var arc = NearestArc(sc);
        var kind = Classify(sc, arc);
        if (kind != null || !allowCloser) return (kind ?? Detection.Narrow, arc, sc);
        double firstWidth = arc.Width;

        // Ambiguous: face it, creep closer, look again, then back off to where we were
        // and turn back to the original heading
        double heading0 = Robot.Pose.HeadingDeg;
        await Robot.TurnAsync(Geometry.Wrap180(arc.CenterDeg - Robot.Pose.HeadingDeg), ct);
        double creep = 0.3 * Cfg.RangeMm;
        await Robot.MoveAsync(creep, ignoreFront: true, ct);
        var sc2 = await Robot.ScanAsync(halfDeg, ct);
        await Robot.MoveAsync(-creep, ignoreFront: true, ct);
        await Robot.TurnAsync(Geometry.Wrap180(heading0 - Robot.Pose.HeadingDeg), ct);
        CheckSensor(sc2);
        if (!sc2.Any) return (Detection.Narrow, arc, sc);
        var arc2 = NearestArc(sc2);
        // A surface's arc grows a lot when the robot gets closer, a post's only a little
        var kind2 = Classify(sc2, arc2) ??
                    (arc2.Width >= firstWidth * Cfg.ArcGrowthWide ? Detection.Wide : Detection.Narrow);
        Host.Log($"ambiguous arc {arc.Width:0} deg -> closer {arc2.Width:0} deg -> {kind2}");
        // Report the arc as seen from where the robot is now (same spot as the first scan)
        var shifted = sc2.Arcs.Select(x => x).ToList();
        return (kind2, arc2, sc2 with { At = new Pose(Robot.Pose.X, Robot.Pose.Y, sc2.At.HeadingDeg) });
    }

    public static Arc NearestArc(ScanResult sc) =>
        sc.Arcs.OrderBy(a => Math.Abs(Geometry.Wrap180(a.CenterDeg - sc.At.HeadingDeg))).First();

    public Detection? Classify(ScanResult sc, Arc a)
    {
        if (a.Width >= Cfg.ArcWideDeg || (sc.TouchesLow(a) && sc.TouchesHigh(a))) return Detection.Wide;
        if (a.Width <= Cfg.ArcNarrowDeg) return Detection.Narrow;
        return null;
    }

    /// <summary>
    /// Sensor fault check: the IR saying YES across the whole sweep in two scans facing
    /// opposite ways from the same spot is impossible unless the gap is narrower than the
    /// robot (which plans never allow) - so the sensor is stuck.
    /// </summary>
    private void CheckSensor(ScanResult sc)
    {
        bool full = sc.Arcs.Any(a => sc.TouchesLow(a) && sc.TouchesHigh(a));
        if (!full) return;
        foreach (var (at, h) in _fullScans.TakeLast(4))
        {
            if (Geometry.Dist(at.Pos, sc.At.Pos) < 100 && Geometry.AngleDiff(h, sc.At.HeadingDeg) > 135)
                throw new MissionException("the IR sensor reads YES in every direction - it seems stuck or blocked. " +
                                           "Check the sensor, then start the phase again.");
        }
        _fullScans.Add((sc.At, sc.At.HeadingDeg));
    }

    /// <summary>Where the arc's middle points, at the estimated sensor reach.</summary>
    public (double X, double Y) ArcHitPoint(ScanResult sc, Arc a) => Cfg.HitPoint(sc.At, a.CenterDeg);

    // ---------------------------------------------------------------- moving

    /// <summary>Called after every move: wall correction (if allowed).</summary>
    public void AfterMove(Pose p)
    {
        if (!WallCorrection || Map.Room is not { } room) return;
        double moved = room.PushOut(p.Pos, Cfg.WidthMm / 2);
        if (moved > 0)
        {
            Host.Log($"wall moved out {moved:0} mm (robot was beyond it at {p.X:0},{p.Y:0})");
            Host.MapChanged();
        }
    }

    /// <summary>Drive a planned route (first point = current position). Stops at the first block.</summary>
    public async Task<MoveResult> DriveRouteAsync(IReadOnlyList<(double X, double Y)> route, CancellationToken ct)
    {
        var last = new MoveResult(false, Robot.Pose);
        foreach (var wp in route.Skip(1))
        {
            last = await GotoAsync(wp, ct);
            if (last.Blocked) return last;
        }
        return last;
    }

    /// <summary>
    /// Go to a point: guarded (wiggle every GuardStep) when Guarded is on.
    /// ignoreSide (-1 right, +1 left): while following a surface on that side, anything seen
    /// more than 40 deg towards it is that surface (nothing can be between it and the robot).
    /// </summary>
    public async Task<MoveResult> GotoAsync((double X, double Y) p, CancellationToken ct, int ignoreSide = 0)
    {
        if (!Guarded)
        {
            var mv = await Robot.GotoAsync(p, ct);
            AfterMove(mv.End);
            return mv;
        }
        while (true)
        {
            double dist = Geometry.Dist(Robot.Pose.Pos, p);
            if (dist < 10) return new MoveResult(false, Robot.Pose);
            await FaceAsync(p, ct);
            var sc = await Robot.ScanAsync(Cfg.GuardScanHalfDeg, ct);
            CheckSensor(sc);
            double step = Math.Min(GuardStepMm ?? Cfg.GuardStepMm, dist);
            if (InBodyPath(sc, step, ignoreSide) is { } danger)
            {
                // Something unknown would brush the robot: face it and report it as a block
                await Robot.TurnAsync(Geometry.Wrap180(danger.CenterDeg - Robot.Pose.HeadingDeg), ct);
                return new MoveResult(true, Robot.Pose);
            }
            var mv = await Robot.MoveAsync(step, false, ct);
            AfterMove(mv.End);
            if (mv.Blocked) return mv;
        }
    }

    /// <summary>An arc showing something unknown inside the strip the body sweeps over the next step.</summary>
    private Arc? InBodyPath(ScanResult sc, double step, int ignoreSide)
    {
        var dir = sc.At.HeadingDeg * Math.PI / 180;
        double ux = Math.Cos(dir), uy = Math.Sin(dir);
        foreach (var a in sc.Arcs)
            for (double d = a.StartDeg; d <= a.EndDeg + 0.01; d += 2)
            {
                if (ignoreSide != 0 && Geometry.Wrap180(d - sc.At.HeadingDeg) * ignoreSide > 40) continue;
                var hp = Cfg.HitPoint(sc.At, d);
                if (Map.IsNearKnown(hp, Cfg.RangeMm)) continue;           // walls / mapped things are planned round
                double along = (hp.X - sc.At.X) * ux + (hp.Y - sc.At.Y) * uy;
                double lateral = Math.Abs(-(hp.X - sc.At.X) * uy + (hp.Y - sc.At.Y) * ux);
                if (along > -20 && along < step + Cfg.TurnRadiusMm && lateral < Cfg.WidthMm / 2 + 40) return a;
            }
        return null;
    }

    /// <summary>Turn to face a point.</summary>
    public Task FaceAsync((double X, double Y) p, CancellationToken ct) =>
        Robot.TurnAsync(Geometry.Wrap180(Geometry.BearingDeg(Robot.Pose.Pos, p) - Robot.Pose.HeadingDeg), ct);

    /// <summary>
    /// Return home: plan a route round everything known, drive it (re-planning round
    /// anything that blocks it, up to 6 times), then the robot's own HOME for the last
    /// stretch and to face the starting direction.
    /// </summary>
    public async Task GoHomeAsync(CancellationToken ct)
    {
        for (int attempt = 0; attempt < 8; attempt++)
        {
            if (Geometry.Dist(Robot.Pose.Pos, (0, 0)) < 60) break;
            var planner = Map.BuildPlanner(Cfg);
            var route = planner.Plan(Robot.Pose.Pos, (0, 0));
            if (route == null) throw new MissionException("no route home: the robot has stopped where it is.");
            Host.PlanChanged(route);
            var r = await DriveRouteAsync(route, ct);
            if (!r.Blocked) break;
            var hit = Cfg.HitPoint(r.End, r.FacingDeg);
            Map.UnassignedHits.Add(new[] { hit.X, hit.Y });
            Host.Log($"blocked on the way home at {hit.X:0},{hit.Y:0}; re-planning round it");
            Host.MapChanged();
        }
        if (Geometry.Dist(Robot.Pose.Pos, (0, 0)) > 150)
            throw new MissionException("couldn't get back home (kept being blocked): the robot has stopped where it is.");
        await Robot.HomeAsync(ct);                // last few cm + face the starting direction
        Host.PlanChanged(null);
    }
}
