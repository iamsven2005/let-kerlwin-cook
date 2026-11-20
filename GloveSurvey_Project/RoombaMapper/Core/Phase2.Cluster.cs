namespace RoombaMapper.Core;

// Phase 2 (see Phase2FindObstacles.cs): narrow clusters - skirt round them, group the hits, classify the ends
public sealed partial class Phase2FindObstacles
{
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
