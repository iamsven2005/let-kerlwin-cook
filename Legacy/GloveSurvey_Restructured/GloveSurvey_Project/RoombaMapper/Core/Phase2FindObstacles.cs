namespace RoombaMapper.Core;

// =====================================================================================
//  PHASE 2: FIND OBSTACLES
//  Goal: a footprint for every obstacle. Every seed and every unknown hit is investigated.
//  Settings: wall correction on (by section type), auto-roam off, lanes W + 2R apart.
// =====================================================================================
public sealed partial class Phase2FindObstacles
{
    private readonly Nav _nav;
    private IRobot R => _nav.Robot;
    private RobotConfig C => _nav.Cfg;
    private WorldMap M => _nav.Map;
    private IMissionHost H => _nav.Host;

    public CoverageReport? Report { get; private set; }

    public Phase2FindObstacles(Nav nav) { _nav = nav; }

    public async Task RunAsync(CancellationToken ct)
    {
        if (M.Room == null) throw new MissionException("map the room first (Phase 1) or load a room map.");
        _nav.WallCorrection = true;
        _nav.Guarded = true;                                          // obstacles still unknown
        _nav.GuardStepMm = null;
        await R.SetAutoRoamAsync(false, ct);

        // Step 1: seeds = Phase 1 seeds + single hits nobody explained yet
        foreach (var h in M.UnassignedHits) M.AddSeed((h[0], h[1]), "unassigned-hit");
        M.UnassignedHits.Clear();

        for (int round = 1; round <= 4; round++)
        {
            await ProcessSeedsAsync(ct);                              // steps 2-8
            H.Status($"Phase 2: sweeping the free space (round {round})");
            var sweep = new CoverageEngine(_nav)
            {
                Name = "Phase 2 sweep",
                SpacingMm = C.ExploreLaneMm,
                OnUnknownObstacle = async (mv, c) =>                  // 9a: unknown hit -> seed -> investigate
                {
                    var hit = C.HitPoint(mv.End, mv.FacingDeg);
                    var seed = M.AddSeed(hit, "phase2-sweep");
                    H.Log($"Phase 2: new obstacle at {hit.X:0},{hit.Y:0} during the sweep");
                    await InvestigateAsync(seed, c);
                },
            };
            Report = await sweep.RunAsync(ct);                        // step 9
            if (!M.Seeds.Any(s => s.State == SeedState.Pending)) break;
        }

        H.Status("Phase 2: done. Going home...");
        await _nav.GoHomeAsync(ct);                                   // step 10
        H.Status($"Phase 2 finished: {M.Obstacles.Count(o => o.Blocks)} obstacles, {Report}");
    }

    // ---------------------------------------------------------------- seeds

    private async Task ProcessSeedsAsync(CancellationToken ct)
    {
        while (true)
        {
            ct.ThrowIfCancellationRequested();
            var pending = M.Seeds.Where(s => s.State == SeedState.Pending).ToList();
            int done = M.Seeds.Count - pending.Count;
            H.Progress(M.Seeds.Count == 0 ? 1 : (double)done / M.Seeds.Count,
                       $"Phase 2: seeds {done}/{M.Seeds.Count}, obstacles {M.Obstacles.Count(o => o.Blocks)}");
            if (pending.Count == 0) return;

            var seed = pending.OrderBy(s => Geometry.Dist(s.Pos, R.Pose.Pos)).First();
            if (InsideFootprint(seed.Pos)) { seed.State = SeedState.Processed; continue; }

            // Step 2: to a stand-off point facing the seed
            var stand = BestStandOff(seed, out var route);
            if (stand == null || route == null)
            {
                seed.State = SeedState.Unreachable;
                H.Log($"Phase 2: seed {seed.Id} at {seed.X:0},{seed.Y:0} is UNREACHABLE (no stand-off point with a route)");
                H.MapChanged();
                continue;
            }
            H.Status($"Phase 2: going to look at seed {seed.Id} ({seed.Source})");
            H.PlanChanged(route);
            var r = await _nav.DriveRouteAsync(route, ct);
            if (r.Blocked)
            {
                var hit = C.HitPoint(r.End, r.FacingDeg);
                if (Geometry.Dist(hit, seed.Pos) > C.GroupMm && !M.IsNearKnown(hit, C.RangeMm))
                {
                    M.AddSeed(hit, "phase2-route-hit");               // something else: look at it too
                    H.Log($"Phase 2: blocked on the way at {hit.X:0},{hit.Y:0}: new seed");
                    continue;
                }
            }
            await _nav.FaceAsync(seed.Pos, ct);
            await InvestigateAsync(seed, ct);
        }
    }

    /// <summary>A free spot ~StandOff from the seed (8 directions), with the shortest route.</summary>
    private (double X, double Y)? BestStandOff(Seed seed, out List<(double X, double Y)>? best)
    {
        var planner = M.BuildPlanner(C);
        best = null;
        (double X, double Y)? at = null;
        double bestLen = double.MaxValue;
        foreach (double dist in new[] { C.StandOffMm, C.StandOffMm + 60 })
            for (int k = 0; k < 8; k++)
            {
                var p = Geometry.FromPolar(seed.Pos, k * 45, dist);
                if (!planner.IsFree(p.X, p.Y)) continue;
                var route = planner.Plan(R.Pose.Pos, p);
                if (route == null) continue;
                double len = 0;
                for (int i = 1; i < route.Count; i++) len += Geometry.Dist(route[i - 1], route[i]);
                if (len < bestLen) { bestLen = len; best = route; at = p; }
            }
        return at;
    }

    /// <summary>Steps 2-8 for one seed: scan, then trace (wide) or skirt (narrow).</summary>
    private async Task InvestigateAsync(Seed seed, CancellationToken ct)
    {
        var (kind, arc, scan) = await _nav.ScanAndClassifyAsync(C.ScanHalfDeg, allowCloser: true, ct);
        if (kind == Detection.None || arc is not { } a)
        {
            seed.State = SeedState.Ghost;                            // nothing there any more
            H.Log($"Phase 2: seed {seed.Id}: nothing there (ghost)");
            H.MapChanged();
            return;
        }
        Obstacle o = kind == Detection.Wide
            ? await SolidAsync(scan, a, ct)                          // step 3
            : await ClusterAsync(scan, ct);                          // steps 4-7
        seed.State = SeedState.Processed;
        // Step 8: seeds inside the footprint (or within G of its members) are done
        foreach (var s in M.Seeds.Where(s => s.State == SeedState.Pending))
            if (Geometry.PointInPolygon(o.Polygon, s.X, s.Y) || Geometry.DistToPolygonEdge(o.Polygon, s.Pos) < C.RangeMm ||
                (o.Kind == ObstacleKind.Cluster && o.Members.Any(m => Geometry.Dist((m[0], m[1]), s.Pos) < C.GroupMm)))
                s.State = SeedState.Processed;
        H.MapChanged();
    }
}
