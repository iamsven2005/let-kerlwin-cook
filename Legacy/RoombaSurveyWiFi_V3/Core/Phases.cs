namespace RoombaMapper.Core;

// =====================================================================================
//  PHASE 1: MAP ROOM
//  Goal: the room outline only. Narrow things are logged as seeds, never investigated.
//  Settings: wall correction off while tracing, auto-roam off, door checks on.
// =====================================================================================
public sealed class Phase1MapRoom
{
    private readonly Nav _nav;
    private IRobot R => _nav.Robot;
    private RobotConfig C => _nav.Cfg;
    private WorldMap M => _nav.Map;
    private IMissionHost H => _nav.Host;

    public List<string> Notes { get; } = new();
    public TraceResult? Trace { get; private set; }

    public Phase1MapRoom(Nav nav) { _nav = nav; }

    public async Task RunAsync(CancellationToken ct)
    {
        M.ClearAll();
        _nav.WallCorrection = false;
        _nav.Guarded = true;                                          // nothing is known yet
        _nav.GuardStepMm = null;
        H.Status("Phase 1: Map room - starting");
        await R.ResetPoseAsync(ct);                                  // step 1
        await R.SetAutoRoamAsync(false, ct);

        TraceResult? room = null;
        for (int attempt = 1; attempt <= 6 && room == null; attempt++)
        {
            H.Status("Phase 1: searching for a wall");
            if (!await SearchForWallAsync(ct))                       // steps 2-3
                throw new MissionException("no wall found after 20 m of searching.");

            H.Status("Phase 1: following the walls");
            var follower = new EdgeFollower(_nav) { Side = -1, DoorChecks = true, Name = "room trace", MinLoopMm = 3000 };
            var t = await follower.RunAsync(ct);                     // steps 4-9
            M.TracePath.Clear();
            M.TracePath.AddRange(t.Path.Select(p => p.Pos));
            H.MapChanged();

            if (t.End == TraceEnd.Closed && !t.Inside)
            {
                // 9b: went round the outside of something: a free-standing solid obstacle
                var pts = t.Loop().Hits.Select(h => h.P).ToList();
                var (l, s) = Geometry.MinAreaRect(pts);
                var kind = l / Math.Max(s, 1) >= 2.5 ? ObstacleKind.Partition : ObstacleKind.Pillar;
                M.AddObstacle(kind, Footprints.FromPoints(pts), pts, "found while searching for the room walls");
                H.Log($"Phase 1: that was a free-standing {kind} ({l / 1000:0.00} x {s / 1000:0.00} m), not the room; searching again");
                H.MapChanged();
                await R.TurnAsync(180, ct);
                await R.MoveAsync(C.FitMm, false, ct);
                continue;
            }
            // A room loop must enclose a real area; a small loop is the robot going round
            // something (e.g. caught between a table leg and the wall)
            if (t.End == TraceEnd.Closed)
            {
                var loop = t.Loop().Path;
                double area = Math.Abs(Geometry.SignedArea(loop)) / 1e6;
                if (area < 1.0)
                {
                    var c = (loop.Average(p => p.X), loop.Average(p => p.Y));
                    if (!NearSeed(c)) M.AddSeed(c, "phase1-small-loop");
                    H.Log($"Phase 1: that loop encloses only {area:0.00} m^2 - not the room; searching again");
                    H.MapChanged();
                    await R.TurnAsync(180, ct);
                    await _nav.GotoAsync(Geometry.FromPolar(R.Pose.Pos, R.Pose.HeadingDeg, C.FitMm), ct);
                    continue;
                }
            }
            room = t;
        }
        if (room == null) throw new MissionException("kept finding obstacles instead of the room walls.");
        Trace = room;

        // Step 10: post-process
        var built = RoomBuilder.Build(room, C, M);
        M.Room = built.Room;
        M.Doorways.AddRange(room.Doorways);
        Notes.AddRange(built.Notes);
        foreach (var n in room.Narrow)
            if (!NearSeed(n.P)) M.AddSeed(n.P, "phase1-narrow");
        foreach (var sk in room.Skirts)
        {
            var c = sk.Bulge.Count > 0 ? (sk.Bulge.Average(p => p.X), sk.Bulge.Average(p => p.Y))
                                       : ((sk.A.P.X + sk.B.P.X) / 2, (sk.A.P.Y + sk.B.P.Y) / 2);
            M.AddSeed(c, sk.Collinear ? "phase1-bulge-wall-attached" : "phase1-bulge");
        }
        // Seeds already explained by an obstacle found during the trace (spikes, loops)
        foreach (var sd in M.Seeds.Where(x => x.State == SeedState.Pending))
            if (M.Obstacles.Any(o => o.Blocks && (Geometry.PointInPolygon(o.Polygon, sd.X, sd.Y) ||
                                                  Geometry.DistToPolygonEdge(o.Polygon, sd.Pos) < C.RangeMm)))
                sd.State = SeedState.Processed;
        foreach (var note in Notes) H.Log("Phase 1: " + note);
        H.MapChanged();

        var poly = M.Room.Polygon;
        double w = poly.Max(p => p.X) - poly.Min(p => p.X), h = poly.Max(p => p.Y) - poly.Min(p => p.Y);
        H.Status($"Phase 1: room {(M.Room.Closed ? "closed" : "INCOMPLETE")}, {poly.Count} corners, " +
                 $"{w / 1000:0.0} x {h / 1000:0.0} m, {M.Doorways.Count} doorways, {M.Seeds.Count} seeds. Going home...");

        _nav.WallCorrection = true;
        await _nav.GoHomeAsync(ct);                                  // return home
        H.Status($"Phase 1 finished: room {(M.Room.Closed ? "mapped" : "INCOMPLETE - check the outline")}, " +
                 $"{M.Seeds.Count(s => s.State == SeedState.Pending)} seeds for Phase 2");
    }

    /// <summary>
    /// Steps 2-3: drive straight, wiggle scanning every SearchStep, until something is
    /// found. Wide -> wall candidate (true, robot facing it). Narrow -> seed, turn away.
    /// </summary>
    private async Task<bool> SearchForWallAsync(CancellationToken ct)
    {
        double travelled = 0;
        for (int i = 0; i < 80 && travelled < 20000; i++)
        {
            ct.ThrowIfCancellationRequested();
            // Wiggle scan at this spot: anything not already logged as a seed?
            var sc = await R.ScanAsync(C.SearchScanHalfDeg, ct);
            var fresh = sc.Arcs.Where(a => !NearSeed(_nav.ArcHitPoint(sc, a))).ToList();
            MoveResult mv;
            if (fresh.Count > 0)
            {
                var a = fresh.OrderBy(x => Math.Abs(Geometry.Wrap180(x.CenterDeg - sc.At.HeadingDeg))).First();
                await R.TurnAsync(Geometry.Wrap180(a.CenterDeg - R.Pose.HeadingDeg), ct);
                mv = await R.MoveAsync(C.RangeMm + C.SensorOffsetMm, false, ct);
            }
            else
            {
                var ahead = Geometry.FromPolar(R.Pose.Pos, R.Pose.HeadingDeg, C.SearchStepMm);
                mv = await _nav.GotoAsync(ahead, ct);              // guarded: wiggles every GuardStep
            }
            travelled += Geometry.Dist(sc.At.Pos, mv.End.Pos);
            if (!mv.Blocked) continue;

            // Step 3: full scan at the detection point
            var (kind, arc, scan) = await _nav.ScanAndClassifyAsync(C.ScanHalfDeg, allowCloser: true, ct);
            if (kind == Detection.None || arc is not { } hitArc) continue;
            if (kind == Detection.Wide)
            {
                await R.TurnAsync(Geometry.Wrap180(hitArc.CenterDeg - R.Pose.HeadingDeg), ct);
                await R.MoveAsync(C.RangeMm, false, ct);               // up to it (stops when it sees it)
                H.Log($"Phase 1: wall candidate at {R.Pose.X:0},{R.Pose.Y:0} (arc {hitArc.Width:0} deg)");
                return true;
            }
            var hp = _nav.ArcHitPoint(scan, hitArc);
            if (!NearSeed(hp)) M.AddSeed(hp, "phase1-narrow");
            H.Log($"Phase 1: narrow thing at {hp.X:0},{hp.Y:0} (arc {hitArc.Width:0} deg): seed, turning away");
            H.MapChanged();
            await R.TurnAsync(90, ct);
        }
        return false;
    }

    private bool NearSeed((double X, double Y) p) =>
        M.Seeds.Any(s => Geometry.Dist(s.Pos, p) < C.GroupMm) ||
        M.Obstacles.Any(o => o.Blocks && Geometry.PointInPolygon(o.Polygon, p.X, p.Y));
}

// =====================================================================================
//  PHASE 2: FIND OBSTACLES
//  Goal: a footprint for every obstacle. Every seed and every unknown hit is investigated.
//  Settings: wall correction on (by section type), auto-roam off, lanes W + 2R apart.
// =====================================================================================
public sealed class Phase2FindObstacles
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

    // ---------------------------------------------------------------- solid obstacles

    private async Task<Obstacle> SolidAsync(ScanResult scan, Arc a, CancellationToken ct)
    {
        H.Status("Phase 2: solid obstacle - tracing its outline");
        await R.TurnAsync(Geometry.Wrap180(a.CenterDeg - R.Pose.HeadingDeg), ct);
        await R.MoveAsync(C.RangeMm + C.SensorOffsetMm, false, ct);   // up to it
        var follower = new EdgeFollower(_nav)
        {
            Side = -1, DoorChecks = false, MaxDistanceMm = 15000, Name = "obstacle trace",
            Abort = RunsIntoRoom,
        };
        var t = await follower.RunAsync(ct);
        var pts = (t.End == TraceEnd.Closed ? t.Loop().Hits : t.Hits).Select(h => h.P).ToList();
        var (l, s) = Geometry.MinAreaRect(pts);
        var byShape = l / Math.Max(s, 1) >= 2.5 ? ObstacleKind.Partition : ObstacleKind.Pillar;

        Obstacle o;
        switch (t.End)
        {
            case TraceEnd.Closed when !t.Inside:                    // 3a
                o = M.AddObstacle(byShape, Footprints.FromPoints(pts), pts, "traced all the way round");
                break;
            case TraceEnd.Aborted:                                   // 3b: runs into the room outline
                var withWall = pts.Concat(WallProjections(pts)).ToList();
                o = M.AddObstacle(ObstacleKind.AttachedPartition, Footprints.FromPoints(withWall), pts,
                                  "runs into the room wall");
                break;
            case TraceEnd.Closed:                                    // 3c: robot was inside the loop
                o = M.AddObstacle(ObstacleKind.UnresolvedLoop, Footprints.FromPoints(pts), pts,
                                  "traced from the inside: not a solid obstacle");
                break;
            default:                                                 // couldn't finish: conservative
                o = M.AddObstacle(byShape, Footprints.FromPoints(pts), pts, "trace incomplete: " + t.Reason);
                o.Unconfirmed = true;
                break;
        }
        H.Log($"Phase 2: {o.Kind}{(o.Unconfirmed ? " (unconfirmed)" : "")} {l / 1000:0.00} x {s / 1000:0.00} m - {o.Note}");

        // Fragments mapped earlier that are really part of this obstacle (seen from another side)
        if (o.Blocks)
            foreach (var frag in M.Obstacles.Where(x => x != o && x.Kind == ObstacleKind.Cluster && x.Members.Count > 0 &&
                         x.Members.All(m => Geometry.PointInPolygon(o.Polygon, m[0], m[1]) ||
                                            Geometry.DistToPolygonEdge(o.Polygon, (m[0], m[1])) < C.RangeMm + 60)).ToList())
            {
                var all = o.Polygon.Concat(frag.Members.Select(m => (m[0], m[1]))).ToList();
                o.Footprint = Footprints.FromPoints(all, 0).Select(p => new[] { p.X, p.Y }).ToList();
                M.Obstacles.Remove(frag);
                H.Log($"Phase 2: an earlier fragment belongs to this {o.Kind}: merged");
            }
        return o;
    }

    private string? RunsIntoRoom(TraceResult t)
    {
        if (M.Room is not { } room || t.DistanceMm < 400 || t.Hits.Count == 0) return null;
        var last = t.Hits[^1].P;
        return Geometry.DistToPolygonEdge(room.Polygon, last) < C.RangeMm ? "ran into the room outline" : null;
    }

    /// <summary>Points on the room wall next to the given points (to close a footprint against it).</summary>
    private IEnumerable<(double X, double Y)> WallProjections(IEnumerable<(double X, double Y)> pts)
    {
        if (M.Room is not { } room) yield break;
        var poly = room.Polygon;
        foreach (var p in pts)
        {
            if (Geometry.DistToPolygonEdge(poly, p) > C.RangeMm + C.SensorOffsetMm) continue;
            var best = poly.Select((q, i) => Geometry.ProjectOnSegment(p, q, poly[(i + 1) % poly.Count]).Point)
                           .OrderBy(q => Geometry.Dist(q, p)).First();
            yield return best;
        }
    }

    // ---------------------------------------------------------------- narrow clusters

    private sealed class SkirtEnd
    {
        public bool Closed;
        public (double X, double Y) At;
        public string Why = "";
    }

    private async Task<Obstacle> ClusterAsync(ScanResult scan, CancellationToken ct)
    {
        H.Status("Phase 2: narrow obstacle - skirting round it");
        var members = scan.Arcs.Select(a => _nav.ArcHitPoint(scan, a)).ToList();
        var startPose = R.Pose;

        var first = await SkirtAsync(members, +1, ct);
        ClusterState state;
        bool inferred = false;
        var footprintPts = new List<(double X, double Y)>(members);
        if (first.Closed)
        {
            state = ClusterState.Closed;
            await CorrectByReDetectionAsync(members, ct);
            footprintPts = new List<(double X, double Y)>(members);
        }
        else
        {
            // Step 7: the other way round, to find the second end
            var back = M.BuildPlanner(C, members).Plan(R.Pose.Pos, startPose.Pos);
            if (back != null) await _nav.DriveRouteAsync(back, ct);
            var second = await SkirtAsync(members, -1, ct);
            (state, inferred) = ClassifyEnds(first, second, members, footprintPts);
            if (state == ClusterState.Unconfirmed)
            {
                // 7c: open: one more try from the other side
                H.Log("Phase 2: cluster still open; trying once from the other side");
                var c = Centroid(members);
                var away = Geometry.FromPolar(c, Geometry.BearingDeg(R.Pose.Pos, c), RingRadius(members, c));
                var planner = M.BuildPlanner(C, members);
                var route = planner.IsFree(away.X, away.Y) ? planner.Plan(R.Pose.Pos, away) : null;
                if (route != null)
                {
                    await _nav.DriveRouteAsync(route, ct);
                    var a2 = await SkirtAsync(members, +1, ct);
                    if (a2.Closed) state = ClusterState.Closed;
                    else
                    {
                        var b2 = await SkirtAsync(members, -1, ct);
                        footprintPts = new List<(double X, double Y)>(members);
                        (state, inferred) = ClassifyEnds(a2, b2, members, footprintPts);
                    }
                }
                if (state == ClusterState.Closed) footprintPts = new List<(double X, double Y)>(members);
            }
        }

        // Hits all round, close together, over a real width: it was a solid thing seen
        // corner-on, not a group of legs. Classify it by shape instead.
        var hull = Geometry.ConvexHull(members);
        double perim = 0;
        for (int i = 0; i < hull.Count; i++) perim += Geometry.Dist(hull[i], hull[(i + 1) % hull.Count]);
        var (rl, rs) = Geometry.MinAreaRect(members);
        if (rs >= 120 && members.Count >= perim / 150 && state != ClusterState.WallAttached)
        {
            var solidKind = rl / Math.Max(rs, 1) >= 2.5 ? ObstacleKind.Partition : ObstacleKind.Pillar;
            var so = M.AddObstacle(solidKind, Footprints.FromPoints(members), members,
                                   state == ClusterState.Closed ? "closed all round by dense hits: solid"
                                                                : "dense hits, not closed all round: solid (unconfirmed)");
            so.Unconfirmed = state != ClusterState.Closed;
            H.Log($"Phase 2: dense closed outline {rl / 1000:0.00} x {rs / 1000:0.00} m: {solidKind}, not a cluster");
            return so;
        }

        // Hits right against an obstacle already mapped are part of it (seen from another
        // side): extend that obstacle instead of starting a new one
        var host = M.Obstacles.FirstOrDefault(x => x.Blocks && x.Kind != ObstacleKind.Cluster &&
            members.All(m => Geometry.PointInPolygon(x.Polygon, m.X, m.Y) || Geometry.DistToPolygonEdge(x.Polygon, m) < C.RangeMm + 60));
        if (host != null)
        {
            var all = host.Polygon.Concat(members).ToList();
            host.Footprint = Footprints.FromPoints(all, 0).Select(p => new[] { p.X, p.Y }).ToList();
            host.Members.AddRange(members.Select(p => new[] { p.X, p.Y }));
            H.Log($"Phase 2: {members.Count} hits belong to the {host.Kind} already mapped there: footprint extended");
            return host;
        }

        var o = M.AddObstacle(ObstacleKind.Cluster, Footprints.FromPoints(footprintPts), members,
                              $"{members.Count} narrow hits");
        o.Cluster = state;
        o.InferredEdge = inferred;
        o.Unconfirmed = state == ClusterState.Unconfirmed;
        H.Log($"Phase 2: cluster of {members.Count} narrow hits: {state}{(inferred ? " (closing edge inferred)" : "")}");
        return MergeClusters(o);
    }

    /// <summary>
    /// Skirt round the cluster on a ring (members' spread + stand-off), dir +1 anticlockwise.
    /// Each step: go to the next ring point, face the cluster, scan. Hits within G join.
    /// If no new hit for G of travel: full scan; still nothing -> an end point.
    /// </summary>
    private async Task<SkirtEnd> SkirtAsync(List<(double X, double Y)> members, int dir, CancellationToken ct)
    {
        var c = Centroid(members);
        double ring = RingRadius(members, c);
        double angle = Geometry.BearingDeg(c, R.Pose.Pos);
        double turned = 0, sinceHit = 0;
        int blocks = 0, recentres = 0;

        while (turned < 360)
        {
            ct.ThrowIfCancellationRequested();
            double stepDeg = C.ProbeStepMm / ring * 180 / Math.PI;
            var target = Geometry.FromPolar(c, angle + dir * stepDeg, ring);
            var planner = M.BuildPlanner(C, members);
            if (!planner.IsFree(target.X, target.Y))
            {
                bool memberInWay = members.Any(m => Geometry.Dist(m, target) < C.ClearanceMm + 20);
                if (memberInWay && recentres++ < 6)
                {
                    c = Centroid(members);
                    ring = RingRadius(members, c);
                    angle = Geometry.BearingDeg(c, R.Pose.Pos);
                    continue;
                }
                return new SkirtEnd { At = R.Pose.Pos, Why = "wall or obstacle in the way" };
            }
            var route = planner.Plan(R.Pose.Pos, target);
            if (route == null) return new SkirtEnd { At = R.Pose.Pos, Why = "no way round" };

            var mv = await _nav.DriveRouteAsync(route, ct);
            if (mv.Blocked)
            {
                var hit = C.HitPoint(mv.End, mv.FacingDeg);
                if (members.Any(m => Geometry.Dist(m, hit) <= C.GroupMm))
                {
                    members.Add(hit);
                    ring = Math.Max(ring, RingRadius(members, c));
                    if (++blocks > 4) return new SkirtEnd { At = R.Pose.Pos, Why = "kept bumping into it" };
                    continue;
                }
                if (!M.IsNearKnown(hit, C.RangeMm)) M.AddSeed(hit, "phase2-near-cluster");
                return new SkirtEnd { At = R.Pose.Pos, Why = "blocked by something else" };
            }
            angle += dir * stepDeg;
            turned += stepDeg;
            sinceHit += C.ProbeStepMm;

            await _nav.FaceAsync(c, ct);
            if (ScanForMembers(await R.ScanAsync(C.SearchScanHalfDeg, ct), members) > 0)
            {
                sinceHit = 0;
                ring = Math.Max(ring, RingRadius(members, c));
                continue;
            }
            if (sinceHit > C.GroupMm)
            {
                // 15b: stop, full wiggle scan
                if (ScanForMembers(await R.ScanAsync(C.ScanHalfDeg, ct), members) > 0) { sinceHit = 0; continue; }
                return new SkirtEnd { At = R.Pose.Pos, Why = "nothing more found" };
            }
        }
        return new SkirtEnd { Closed = true, At = R.Pose.Pos };       // step 6: all the way round
    }

    private int ScanForMembers(ScanResult sc, List<(double X, double Y)> members)
    {
        int added = 0;
        foreach (var a in sc.Arcs)
        {
            var hp = _nav.ArcHitPoint(sc, a);
            if (members.Any(m => Geometry.Dist(m, hp) <= C.GroupMm))
            {
                if (members.All(m => Geometry.Dist(m, hp) > 25)) { members.Add(hp); added++; }
                else added++;                                         // seen again: still there
            }
            else if (!M.IsNearKnown(hp, C.RangeMm) && !M.Seeds.Any(s => Geometry.Dist(s.Pos, hp) < C.GroupMm))
                M.AddSeed(hp, "phase2-near-cluster");
        }
        return added;
    }

    /// <summary>Closed cluster: look at the first member again; spread any drift over the members.</summary>
    private async Task CorrectByReDetectionAsync(List<(double X, double Y)> members, CancellationToken ct)
    {
        if (members.Count < 2) return;
        await _nav.FaceAsync(members[0], ct);
        var sc = await R.ScanAsync(C.SearchScanHalfDeg, ct);
        if (!sc.Any) return;
        var again = sc.Arcs.Select(a => _nav.ArcHitPoint(sc, a)).OrderBy(p => Geometry.Dist(p, members[0])).First();
        if (Geometry.Dist(again, members[0]) > C.GroupMm / 2) return;
        double ex = again.X - members[0].X, ey = again.Y - members[0].Y;
        for (int i = 0; i < members.Count; i++)
        {
            double f = (double)i / (members.Count - 1);
            members[i] = (members[i].X + ex * f, members[i].Y + ey * f);
        }
        H.Log($"Phase 2: cluster closure error {Math.Sqrt(ex * ex + ey * ey):0} mm spread over its hits");
    }

    /// <summary>Step 7: what are the two ends up against?</summary>
    private (ClusterState, bool Inferred) ClassifyEnds(SkirtEnd a, SkirtEnd b, List<(double X, double Y)> members,
                                                      List<(double X, double Y)> footprintPts)
    {
        double near = C.StandOffMm + C.RangeMm + 60;
        bool RoomNear((double X, double Y) p, out WallSection? sec)
        {
            sec = null;
            if (M.Room is not { } room) return false;
            sec = room.NearestSection(p, out double d);
            return d < near;
        }
        bool ObstacleNear((double X, double Y) p) =>
            M.Obstacles.Any(o => o.Blocks && Geometry.DistToPolygonEdge(o.Polygon, p) < near);

        footprintPts.Clear();
        footprintPts.AddRange(members);
        if (RoomNear(a.At, out var sa) && RoomNear(b.At, out var sb))
        {
            // 17a: wall-attached. Footprint includes the wall segment A-B.
            var room = M.Room!;
            var poly = room.Polygon;
            foreach (var end in new[] { a.At, b.At })
            {
                var proj = poly.Select((q, i) => Geometry.ProjectOnSegment(end, q, poly[(i + 1) % poly.Count]).Point)
                               .OrderBy(q => Geometry.Dist(q, end)).First();
                footprintPts.Add(proj);
            }
            bool inferred = sa?.Kind == SectionKind.Inferred || sb?.Kind == SectionKind.Inferred;
            return (ClusterState.WallAttached, inferred);
        }
        if ((ObstacleNear(a.At) || RoomNear(a.At, out _)) && (ObstacleNear(b.At) || RoomNear(b.At, out _)))
            return (ClusterState.ObstacleAttached, false);              // 17b
        return (ClusterState.Unconfirmed, false);                       // 17c: closed with a straight A-B line
    }

    /// <summary>Clusters whose hits are within G of each other are one cluster (e.g. table legs).</summary>
    private Obstacle MergeClusters(Obstacle o)
    {
        var mine = o.Members.Select(m => (m[0], m[1])).ToList();
        foreach (var other in M.Obstacles.Where(x => x != o && x.Kind == ObstacleKind.Cluster).ToList())
        {
            var theirs = other.Members.Select(m => (m[0], m[1])).ToList();
            if (!mine.Any(p => theirs.Any(q => Geometry.Dist(p, q) <= C.GroupMm))) continue;
            mine.AddRange(theirs);
            if (other.Cluster == ClusterState.WallAttached) o.Cluster = ClusterState.WallAttached;
            else if (other.Cluster == ClusterState.Unconfirmed && o.Cluster == ClusterState.Closed) o.Cluster = ClusterState.Unconfirmed;
            o.InferredEdge |= other.InferredEdge;
            var pts = mine.Concat(other.Polygon).ToList();
            o.Footprint = Footprints.FromPoints(pts).Select(p => new[] { p.X, p.Y }).ToList();
            M.Obstacles.Remove(other);
            H.Log($"Phase 2: merged two clusters within G ({C.GroupMm:0} mm) of each other");
        }
        o.Members = mine.Select(p => new[] { p.Item1, p.Item2 }).ToList();
        o.Note = $"{mine.Count} narrow hits";
        return o;
    }

    private bool InsideFootprint((double X, double Y) p) =>
        M.Obstacles.Any(o => o.Blocks && (Geometry.PointInPolygon(o.Polygon, p.X, p.Y) ||
                                          Geometry.DistToPolygonEdge(o.Polygon, p) < C.RangeMm));

    private static (double X, double Y) Centroid(List<(double X, double Y)> pts) =>
        (pts.Average(p => p.X), pts.Average(p => p.Y));

    private double RingRadius(List<(double X, double Y)> pts, (double X, double Y) c) =>
        pts.Max(p => Geometry.Dist(p, c)) + C.StandOffMm;
}

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
