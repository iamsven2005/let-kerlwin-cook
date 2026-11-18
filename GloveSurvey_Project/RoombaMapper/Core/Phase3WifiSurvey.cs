namespace RoombaMapper.Core;

// =====================================================================================
//  PHASE 3: WI-FI SURVEY
//  Goal: even Wi-Fi readings over the free space. The map is final: the room geometry is
//  never changed; unexpected things get TEMPORARY footprints and are avoided, not traced.
//  Settings: wall correction OFF, auto-roam ON (the robot only roams while stopped).
// =====================================================================================
public sealed class Phase3WifiSurvey
{
    private readonly Nav _nav;
    private IRobot R => _nav.Robot;
    private RobotConfig C => _nav.Cfg;
    private WorldMap M => _nav.Map;
    private IMissionHost H => _nav.Host;

    public bool Slow { get; init; }
    public CoverageReport? Report { get; private set; }

    public Phase3WifiSurvey(Nav nav) { _nav = nav; }

    public async Task RunAsync(CancellationToken ct)
    {
        if (M.Room == null) throw new MissionException("load or build a map first (Phases 1 and 2).");
        _nav.WallCorrection = false;                                  // never modify the room here
        // The map is known, but something new may have appeared: guarded unless switched off
        _nav.Guarded = C.SurveyGuard;
        _nav.GuardStepMm = null;
        M.ClearTemporary();
        await R.SetAutoRoamAsync(true, ct);

        H.Status($"Phase 3: Wi-Fi survey ({(Slow ? "SLOW: stopping to measure" : "FAST: measuring while driving")})");
        var survey = new CoverageEngine(_nav)
        {
            Name = "Wi-Fi survey",
            SpacingMm = C.SurveyLaneMm,
            MeasureEveryMm = Slow ? C.SlowStepMm : 0,
            OnUnknownObstacle = TemporaryAsync,
        };
        Report = await survey.RunAsync(ct);

        H.Status("Phase 3: survey done. Going home...");
        await _nav.GoHomeAsync(ct);
        int temps = M.Obstacles.Count(o => o.Kind == ObstacleKind.Temporary);
        H.Status($"Phase 3 finished: {Report}, {temps} temporary obstacle{(temps == 1 ? "" : "s")}");
    }

    /// <summary>Something unexpected: one scan, a temporary footprint, re-plan. No tracing.</summary>
    private async Task TemporaryAsync(MoveResult mv, CancellationToken ct)
    {
        var (kind, arc, scan) = await _nav.ScanAndClassifyAsync(C.ScanHalfDeg, allowCloser: false, ct);
        var hit = arc is { } a ? _nav.ArcHitPoint(scan, a) : C.HitPoint(mv.End, mv.FacingDeg);
        List<(double X, double Y)> fp = kind == Detection.Wide
            ? Geometry.Square(Geometry.FromPolar(hit, mv.FacingDeg, C.WidthMm / 2), C.WidthMm, mv.FacingDeg)
            : Geometry.Circle(hit, C.RangeMm);
        M.AddObstacle(ObstacleKind.Temporary, fp, new[] { hit }, kind == Detection.Wide ? "unexpected, wide" : "unexpected, narrow");
        H.Log($"Phase 3: unexpected {(kind == Detection.Wide ? "wide" : "narrow")} obstacle at {hit.X:0},{hit.Y:0}: temporary footprint, re-planning");
        H.MapChanged();
    }
}

/// <summary>Footprint helpers.</summary>
public static class Footprints
{
    /// <summary>Hull of the points, each padded by 30 mm (so 1-2 points still give an area).</summary>
    public static List<(double X, double Y)> FromPoints(IEnumerable<(double X, double Y)> pts, double pad = 30)
    {
        var padded = pts.SelectMany(p => new[] { (p.X + pad, p.Y), (p.X - pad, p.Y), (p.X, p.Y + pad), (p.X, p.Y - pad) });
        return Geometry.ConvexHull(padded);
    }
}
