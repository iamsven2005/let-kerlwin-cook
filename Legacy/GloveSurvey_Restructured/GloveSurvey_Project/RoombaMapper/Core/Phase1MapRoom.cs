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
