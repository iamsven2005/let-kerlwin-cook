namespace RoombaMapper.Core;

// Phase 2 (see Phase2FindObstacles.cs): solid obstacles - trace the outline, classify pillar / partition / attached
public sealed partial class Phase2FindObstacles
{
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
}
