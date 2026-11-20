namespace RoombaMapper.Core;

/// <summary>Coverage result, in m^2.</summary>
public sealed record CoverageReport(double CoveredM2, double SkippedM2, double UnreachableM2, double RemainingM2, int Steps)
{
    public override string ToString() =>
        $"covered {CoveredM2:0.00} m^2, skipped {SkippedM2:0.00} m^2, unreachable {UnreachableM2:0.00} m^2, left {RemainingM2:0.00} m^2";
}

/// <summary>
/// Serpentine coverage over the free space of the current map (Phase 2 sweep and Phase 3
/// survey). Every step re-plans from scratch on the current map:
///   lane pieces still to do = free, reachable, not covered, not skipped parts of lanes
///   next piece: rest of the current lane ahead -> nearest lane in the sweep direction
///   (from its nearer end, so the direction alternates) -> lanes left behind -> passed pieces
/// Blocked: near something known -> re-plan (and remember the spot); unknown -> the
/// phase's own handler (Phase 2 investigates it, Phase 3 adds a temporary footprint).
/// Pieces that can't be reached or fail 3 times are SKIPPED (recorded, shown amber).
/// Free space that can't be reached at all is recorded as UNREACHABLE.
/// </summary>
public sealed class CoverageEngine
{
    private readonly Nav _nav;
    private IRobot R => _nav.Robot;
    private RobotConfig C => _nav.Cfg;
    private WorldMap M => _nav.Map;
    private IMissionHost H => _nav.Host;

    public string Name { get; init; } = "Coverage";
    public double SpacingMm { get; init; } = 400;
    /// <summary>Stop every this far along a lane and call MeasureAsync (0 = don't).</summary>
    public double MeasureEveryMm { get; init; }
    /// <summary>Something unknown blocked the robot (it is stopped, facing it).</summary>
    public Func<MoveResult, CancellationToken, Task>? OnUnknownObstacle { get; init; }
    public int MaxSteps { get; init; } = 800;

    private bool _vertical;
    private double _anchor;
    private double? _curLaneV;
    private int _laneDir = 1, _sweep = 1;
    private readonly Dictionary<(long, long), int> _tries = new();
    private bool[]? _reach;
    private Planner? _planner;

    private readonly record struct Candidate(Planner.LaneSeg Seg, double StartU, double EndU, int Sweep);

    public CoverageEngine(Nav nav) { _nav = nav; }

    public async Task<CoverageReport> RunAsync(CancellationToken ct)
    {
        M.ClearCoverage();
        _planner = M.BuildPlanner(C);
        _vertical = LanesVertical();
        if (_planner.FreeRange(_vertical) is not { } range) throw new MissionException("no free space inside the room.");
        _anchor = range.VMin;
        H.Log($"{Name}: lanes {(_vertical ? "along Y" : "along X")}, {SpacingMm:0} mm apart");

        int step = 0;
        for (; step < MaxSteps; step++)
        {
            ct.ThrowIfCancellationRequested();
            _planner = M.BuildPlanner(C);
            _reach = _planner.ReachableFrom(R.Pose.Pos);
            var segs = _planner.LaneSegments(SpacingMm, _anchor, _vertical, IsDoneOrUnreachable);
            ReportProgress(segs);
            if (segs.Count == 0) break;

            var pose = R.Pose.Pos;
            Candidate? chosen = null;
            List<(double X, double Y)>? route = null;
            foreach (var c in Candidates(segs, pose, _curLaneV, _laneDir, _sweep))
            {
                route = _planner.Plan(pose, P(c.Seg.V, c.StartU));
                if (route != null) { chosen = c; break; }
                Skip(c.Seg, "no route to it");
            }
            if (chosen is not { } pick || route == null) break;

            var key = ((long)Math.Round(pick.Seg.V), (long)Math.Round(pick.StartU / 200));
            _tries[key] = _tries.GetValueOrDefault(key) + 1;
            if (_tries[key] > 3) { Skip(pick.Seg, "failed 3 times"); continue; }

            _sweep = pick.Sweep;
            ShowPlan(segs, route, pick);

            // 1. To the start of the piece
            var r = await _nav.DriveRouteAsync(route, ct);
            if (r.Blocked) { await BlockedAsync(r, null, ct); continue; }

            // 2. Along the lane
            _curLaneV = pick.Seg.V;
            _laneDir = pick.EndU > pick.StartU ? 1 : -1;
            var start = P(pick.Seg.V, pick.StartU);
            var end = P(pick.Seg.V, pick.EndU);
            var stops = MeasureEveryMm > 0
                ? Geometry.Densify(new List<(double X, double Y)> { start, end }, MeasureEveryMm).Skip(1).ToList()
                : new List<(double X, double Y)> { end };

            bool ok = true;
            var from = R.Pose.Pos;
            foreach (var stop in stops)
            {
                var mv = await _nav.GotoAsync(stop, ct);
                MarkLine(from, mv.End.Pos, M.Covered, CoverRadius);
                from = mv.End.Pos;
                if (mv.Blocked) { ok = false; await BlockedAsync(mv, end, ct); break; }
                if (MeasureEveryMm > 0) await R.MeasureAsync(ct);
            }
            if (ok) MarkLine(start, end, M.Covered, CoverRadius);
            H.MapChanged();
        }
        if (step >= MaxSteps) H.Log($"{Name}: stopped at the {MaxSteps}-step safety limit");

        var report = Verify();
        H.Log($"{Name}: {report}");
        H.PlanChanged(null);
        return report;
    }

    /// <summary>The robot stopped on a lane or on the way to one.</summary>
    private async Task BlockedAsync(MoveResult mv, (double X, double Y)? laneEnd, CancellationToken ct)
    {
        var hit = C.HitPoint(mv.End, mv.FacingDeg);
        if (M.IsNearKnown(hit, C.RangeMm + 50))
        {
            // Something already on the map. Just short of the lane end: the lane is done.
            if (laneEnd is { } e && Geometry.Dist(mv.End.Pos, e) < C.StopDistMm + 120)
            {
                MarkLine(mv.End.Pos, e, M.Covered, CoverRadius);
                return;
            }
            M.UnassignedHits.Add(new[] { hit.X, hit.Y });       // so the next plan keeps clear of it
            H.Log($"{Name}: blocked by something already mapped at {hit.X:0},{hit.Y:0}; re-planning");
            return;
        }
        if (OnUnknownObstacle != null) await OnUnknownObstacle(mv, ct);
        else M.UnassignedHits.Add(new[] { hit.X, hit.Y });
        H.MapChanged();
    }

    // ---------------------------------------------------------------- choosing the next piece

    private IEnumerable<Candidate> Candidates(List<Planner.LaneSeg> segs, (double X, double Y) pose,
                                              double? curV, int dir, int sweep)
    {
        double half = SpacingMm / 2;
        double pu = U(pose);

        if (curV is not double cv)
        {
            var first = segs.OrderBy(s => Math.Abs(s.V - V(pose))).ThenBy(s => NearEnd(s, pose)).FirstOrDefault();
            if (first == default) yield break;
            int more = segs.Count(s => s.V > first.V + half) >= segs.Count(s => s.V < first.V - half) ? 1 : -1;
            bool fromA = Geometry.Dist(pose, P(first.V, first.A)) <= Geometry.Dist(pose, P(first.V, first.B));
            yield return new Candidate(first, fromA ? first.A : first.B, fromA ? first.B : first.A, more);
            foreach (var c in Candidates(segs, pose, first.V, fromA ? 1 : -1, more))
                if (c.Seg != first) yield return c;
            yield break;
        }

        var same = segs.Where(s => Math.Abs(s.V - cv) < half).ToList();
        bool Ahead(Planner.LaneSeg s) => dir > 0 ? s.B > pu + 50 : s.A < pu - 50;
        foreach (var s in same.Where(Ahead).OrderBy(s => dir > 0 ? Math.Max(s.A, pu) : -Math.Min(s.B, pu)))
            yield return new Candidate(s, dir > 0 ? Math.Max(s.A, pu) : Math.Min(s.B, pu), dir > 0 ? s.B : s.A, sweep);

        foreach (int sw in new[] { sweep, -sweep })
            foreach (var lane in segs.Where(s => sw * (s.V - cv) > half).GroupBy(s => Math.Round(s.V))
                                     .OrderBy(g => Math.Abs(g.Key - cv)))
                foreach (var s in lane.OrderBy(s => NearEnd(s, pose)))
                {
                    bool fromA = Geometry.Dist(pose, P(s.V, s.A)) <= Geometry.Dist(pose, P(s.V, s.B));
                    yield return new Candidate(s, fromA ? s.A : s.B, fromA ? s.B : s.A, sw);
                }

        foreach (var s in same.Where(s => !Ahead(s)).OrderBy(s => NearEnd(s, pose)))
        {
            bool fromA = Geometry.Dist(pose, P(s.V, s.A)) <= Geometry.Dist(pose, P(s.V, s.B));
            yield return new Candidate(s, fromA ? s.A : s.B, fromA ? s.B : s.A, sweep);
        }
    }

    private void ShowPlan(List<Planner.LaneSeg> segs, List<(double X, double Y)> route, Candidate first)
    {
        var path = new List<(double X, double Y)>(route) { P(first.Seg.V, first.EndU) };
        var left = segs.Where(s => s != first.Seg).ToList();
        double? cv = first.Seg.V;
        int dir = first.EndU > first.StartU ? 1 : -1, sweep = first.Sweep;
        var at = P(first.Seg.V, first.EndU);
        for (int i = 0; i < 300 && left.Count > 0; i++)
        {
            var next = Candidates(left, at, cv, dir, sweep).FirstOrDefault();
            if (next.Seg == default) break;
            path.Add(P(next.Seg.V, next.StartU));
            path.Add(P(next.Seg.V, next.EndU));
            left.Remove(next.Seg);
            cv = next.Seg.V;
            dir = next.EndU > next.StartU ? 1 : -1;
            sweep = next.Sweep;
            at = P(next.Seg.V, next.EndU);
        }
        H.PlanChanged(path);
    }

    // ---------------------------------------------------------------- coverage cells

    private double CoverRadius => SpacingMm / 2 - 10;

    private static (int, int) Key(double x, double y) =>
        ((int)Math.Floor(x / WorldMap.CoverCellMm), (int)Math.Floor(y / WorldMap.CoverCellMm));

    private bool IsDoneOrUnreachable(double x, double y)
    {
        var k = Key(x, y);
        if (M.Covered.Contains(k) || M.Skipped.Contains(k)) return true;
        if (_planner != null && _reach != null)
        {
            var (ix, iy) = _planner.CellOf(x, y);
            if (ix >= 0 && iy >= 0 && ix < _planner.GridWidth && iy < _planner.GridHeight &&
                !_reach[iy * _planner.GridWidth + ix])
            {
                M.Unreachable.Add(k);
                return true;
            }
        }
        return false;
    }

    private static void MarkLine((double X, double Y) a, (double X, double Y) b, HashSet<(int, int)> set, double radius)
    {
        int n = Math.Max(1, (int)(Geometry.Dist(a, b) / (WorldMap.CoverCellMm / 2)));
        int r = (int)Math.Ceiling(radius / WorldMap.CoverCellMm);
        for (int i = 0; i <= n; i++)
        {
            double x = a.X + (b.X - a.X) * i / n, y = a.Y + (b.Y - a.Y) * i / n;
            var (cx, cy) = Key(x, y);
            for (int dy = -r; dy <= r; dy++)
                for (int dx = -r; dx <= r; dx++)
                {
                    double px = (cx + dx + 0.5) * WorldMap.CoverCellMm, py = (cy + dy + 0.5) * WorldMap.CoverCellMm;
                    if ((px - x) * (px - x) + (py - y) * (py - y) <= radius * radius) set.Add((cx + dx, cy + dy));
                }
        }
    }

    private void Skip(Planner.LaneSeg s, string why)
    {
        MarkLine(P(s.V, s.A), P(s.V, s.B), M.Skipped, WorldMap.CoverCellMm);
        H.Log($"{Name}: SKIPPED lane piece at {(_vertical ? "x" : "y")}={s.V:0} ({s.A:0}..{s.B:0}): {why}");
    }

    /// <summary>
    /// Final check: every free lane cell must be covered, skipped (with a reason in the log)
    /// or unreachable. Anything else is reported as remaining.
    /// </summary>
    private CoverageReport Verify()
    {
        var planner = M.BuildPlanner(C);
        var reach = planner.ReachableFrom(R.Pose.Pos);
        M.Remaining.Clear();
        double cell = WorldMap.CoverCellMm * WorldMap.CoverCellMm / 1e6;
        foreach (var s in planner.LaneSegments(SpacingMm, _anchor, _vertical, (x, y) =>
                 { var k = Key(x, y); return M.Covered.Contains(k) || M.Skipped.Contains(k); }))
        {
            for (double u = s.A; u <= s.B; u += WorldMap.CoverCellMm)
            {
                var p = P(s.V, u);
                var (ix, iy) = planner.CellOf(p.X, p.Y);
                bool reachable = ix >= 0 && iy >= 0 && ix < planner.GridWidth && iy < planner.GridHeight &&
                                 reach[iy * planner.GridWidth + ix];
                (reachable ? M.Remaining : M.Unreachable).Add(Key(p.X, p.Y));
            }
        }
        if (M.Remaining.Count > 0) H.Log($"{Name}: WARNING {M.Remaining.Count} reachable cells were not covered");
        return new CoverageReport(M.Covered.Count * cell, M.Skipped.Count * cell, M.Unreachable.Count * cell,
                                  M.Remaining.Count * cell, 0);
    }

    private void ReportProgress(List<Planner.LaneSeg> segs)
    {
        double left = segs.Sum(s => s.Length) / 1000.0;
        double done = M.Covered.Count * WorldMap.CoverCellMm * WorldMap.CoverCellMm / 1e6;
        double doneLen = done / (SpacingMm / 1000.0);
        double frac = doneLen + left > 0 ? doneLen / (doneLen + left) : 1;
        H.Progress(frac, $"{Name}: {frac * 100:0}% covered, {segs.Count} lane pieces left, " +
                         $"{M.Skipped.Count * 0.0025:0.00} m^2 skipped");
    }

    // ---------------------------------------------------------------- lane coordinates

    private double U((double X, double Y) p) => _vertical ? p.Y : p.X;
    private double V((double X, double Y) p) => _vertical ? p.X : p.Y;
    private (double X, double Y) P(double v, double u) => _vertical ? (v, u) : (u, v);
    private double NearEnd(Planner.LaneSeg s, (double X, double Y) pose) =>
        Math.Min(Geometry.Dist(pose, P(s.V, s.A)), Geometry.Dist(pose, P(s.V, s.B)));

    private bool LanesVertical()
    {
        if (M.Room is { Corners.Count: >= 3 } room)
        {
            var poly = room.Polygon;
            return poly.Max(p => p.Y) - poly.Min(p => p.Y) > poly.Max(p => p.X) - poly.Min(p => p.X);
        }
        return false;
    }
}
