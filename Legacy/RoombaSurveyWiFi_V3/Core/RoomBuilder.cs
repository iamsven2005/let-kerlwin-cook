namespace RoombaMapper.Core;

/// <summary>
/// Turns a room trace into a room outline (spec step 10):
///   10a  closure error: the first wall is measured at the start and again at the end;
///        the difference is the drift, spread back along the trace by distance travelled
///   10b  spikes (an out-and-back narrower than F: a partition sticking out of the wall)
///        and inner loops (the path crossing itself: a pillar touching the wall) are cut
///        out of the outline and logged as solid obstacles
///   10c  walls: each run of points facing the same way is one wall; corners are where
///        neighbouring walls meet (falls back to the robot's path if that fails);
///        sections are labelled confirmed / doorway / inferred / unresolved
/// </summary>
public static class RoomBuilder
{
    public sealed record Output(Room Room, List<string> Notes);

    public static Output Build(TraceResult t, RobotConfig cfg, WorldMap map)
    {
        var notes = new List<string>();
        // Only the closed loop (a trace can start on something else before finding the walls)
        var (path, pathDist, hits) = t.End == TraceEnd.Closed
            ? t.Loop()
            : (t.Path.Select(p => p.Pos).ToList(), t.PathDist.ToList(), t.Hits.ToList());
        if (t.End == TraceEnd.Closed && t.LoopStart > 0)
            notes.Add($"the first {t.PathDist[t.LoopStart] / 1000:0.0} m of the trace were not part of the room loop");
        double totalDist = pathDist.Count > 0 ? pathDist[^1] : 0;

        // ---------------- 10a closure error
        double closure = 0;
        if (t.End == TraceEnd.Closed && hits.Count >= 4)
        {
            double f0 = hits[0].FacingDeg;
            var first = hits.TakeWhile(h => Geometry.AngleDiff(h.FacingDeg, f0) < 25).ToList();
            var last = Enumerable.Reverse(hits).TakeWhile(h => Geometry.AngleDiff(h.FacingDeg, f0) < 25).ToList();
            if (first.Count > 0 && last.Count > 0 && first.Count + last.Count < hits.Count)
            {
                var n = (Math.Cos(f0 * Math.PI / 180), Math.Sin(f0 * Math.PI / 180));
                double en = last.Average(h => n.Item1 * h.P.X + n.Item2 * h.P.Y)
                          - first.Average(h => n.Item1 * h.P.X + n.Item2 * h.P.Y);
                closure = Math.Abs(en);
                double total = Math.Max(1, totalDist);
                (double X, double Y) Fix((double X, double Y) p, int pathIndex)
                {
                    double s = pathDist[Math.Clamp(pathIndex, 0, pathDist.Count - 1)] / total;
                    return (p.X - s * en * n.Item1, p.Y - s * en * n.Item2);
                }
                hits = hits.Select(h => h with { P = Fix(h.P, h.PathIndex) }).ToList();
                path = path.Select((p, i) => Fix(p, i)).ToList();
                notes.Add($"closure error {closure:0} mm spread along the trace");
            }
        }

        // A closed trace is a loop: start it mid-way along its longest straight stretch, so
        // that nothing (e.g. a partition the trace happened to start on) wraps round the ends
        if (t.End == TraceEnd.Closed && path.Count > 10)
        {
            var s0 = SimplifyIdx(path, 60);
            int best = 0;
            double bestLen = -1;
            for (int i = 0; i + 1 < s0.Count; i++)
            {
                double len = Geometry.Dist(path[s0[i]], path[s0[i + 1]]);
                if (len > bestLen) { bestLen = len; best = (s0[i] + s0[i + 1]) / 2; }
            }
            int n = path.Count;
            path = path.Skip(best).Concat(path.Take(best)).ToList();
            hits = hits.Select(h => h with { PathIndex = (h.PathIndex - best + n) % n })
                       .OrderBy(h => h.PathIndex).ToList();
        }

        // ---------------- 10b spikes and inner loops (on the simplified path)
        var cut = new HashSet<int>();                    // hit indexes taken out of the walls
        var simple = SimplifyIdx(path, 60);
        // Inner loops: the path crosses itself
        for (int i = 0; i + 1 < simple.Count; i++)
            for (int j = i + 2; j + 1 < simple.Count; j++)
            {
                if (i == 0 && j + 1 == simple.Count - 1) continue;      // the closing join
                if (!Geometry.SegmentsCross(path[simple[i]], path[simple[i + 1]], path[simple[j]], path[simple[j + 1]], out _))
                    continue;
                var loop = Enumerable.Range(simple[i + 1], simple[j] - simple[i + 1] + 1).Select(k => path[k]).ToList();
                double area = Math.Abs(Geometry.SignedArea(loop));
                if (area > 1.5e6 || area < 1e4) continue;
                var inLoop = hits.Select((h, k) => (h, k))
                                 .Where(x => x.h.PathIndex >= simple[i + 1] && x.h.PathIndex <= simple[j]).ToList();
                if (inLoop.Count < 3) continue;
                foreach (var x in inLoop) cut.Add(x.k);
                map.AddObstacle(ObstacleKind.WallPillar, Footprints.FromPoints(inLoop.Select(x => x.h.P)),
                                inLoop.Select(x => x.h.P), "inner loop of the room trace");
                notes.Add($"inner loop ({area / 1e6:0.00} m^2): logged as a pillar touching the wall");
            }
        // Spikes: out along one side, back along the other, closer together than F
        for (int i = 0; i + 2 < simple.Count; i++)
        {
            var a = path[simple[i]];
            var b = path[simple[i + 1]];
            var c = path[simple[i + 2]];
            double ang = Geometry.AngleDiff(Geometry.BearingDeg(a, b), Geometry.BearingDeg(b, c));
            if (ang < 150) continue;
            double legs = Math.Min(Geometry.Dist(a, b), Geometry.Dist(b, c));
            if (legs < 150) continue;
            // The robot follows ~StopDist off each face, so its two legs are the partition's
            // thickness + 2 x StopDist apart: thinner than F means sep < F + 2 x StopDist.
            double sep = Geometry.DistToSegment(c, a, b);
            if (sep >= cfg.FitMm + 2 * cfg.StopDistMm) continue;
            var inSpike = hits.Select((h, k) => (h, k))
                              .Where(x => x.h.PathIndex > simple[i] && x.h.PathIndex <= simple[i + 2] && !cut.Contains(x.k))
                              .ToList();
            if (inSpike.Count < 2) continue;
            foreach (var x in inSpike) cut.Add(x.k);
            map.AddObstacle(ObstacleKind.AttachedPartition, Footprints.FromPoints(inSpike.Select(x => x.h.P)),
                            inSpike.Select(x => x.h.P), "spike of the room trace");
            notes.Add($"spike {legs / 1000:0.00} m long: logged as a partition attached to the wall");
        }

        // ---------------- 10c walls
        // Skirted bulges and cut-out pieces are not walls
        var wallHits = hits.Where((h, k) => !cut.Contains(k) && !h.Bulge).ToList();
        List<(double X, double Y)>? corners = null;
        var walls = Geometry.WallsFromFacing(wallHits.Select(h => (h.P.X, h.P.Y, Facing: h.FacingDeg)).ToList());
        if (walls != null) corners = Geometry.CornersFromWalls(walls, requireAll: false);
        if (!Plausible(corners, path))
        {
            var pathWalls = Geometry.WallsFromPath(path, cfg.StopDistMm);
            var pc = pathWalls == null ? null : Geometry.CornersFromWalls(pathWalls, requireAll: false);
            if (Plausible(pc, path)) { corners = pc; notes.Add("walls built from the robot's path"); }
        }
        if (!Plausible(corners, path))
        {
            corners = Geometry.ConvexHull(wallHits.Select(h => h.P));
            notes.Add("walls approximated by the outline of all wall points");
        }
        if (Geometry.SignedArea(corners!) < 0) corners!.Reverse();      // anticlockwise

        // Thin notches (narrower than F): a partition the walls were built round. Cut it out
        // of the outline and log it as an attached partition instead (spec 10b).
        for (int guard = 0; guard < 10 && corners!.Count >= 6; guard++)
        {
            int n = corners.Count;
            bool cutOne = false;
            for (int i = 0; i < n && !cutOne; i++)
            {
                var a = corners[i];
                var b = corners[(i + 1) % n];
                var c = corners[(i + 2) % n];
                var d = corners[(i + 3) % n];
                double width = Geometry.Dist(b, c);
                double ang = Geometry.AngleDiff(Geometry.BearingDeg(a, b), Geometry.BearingDeg(c, d));
                bool inward = Geometry.PointInPolygon(corners, (b.X + c.X) / 2 + 0, (b.Y + c.Y) / 2) ||
                              Geometry.SignedArea(new List<(double X, double Y)> { a, b, c, d }) < 0;
                if (width >= cfg.FitMm || ang < 150 || Math.Min(Geometry.Dist(a, b), Geometry.Dist(c, d)) < 150) continue;
                if (Geometry.SignedArea(new List<(double X, double Y)> { a, b, c, d }) >= 0) continue;   // pointing out
                map.AddObstacle(ObstacleKind.AttachedPartition, new List<(double X, double Y)> { a, b, c, d },
                                new[] { b, c }, "notch in the room outline narrower than F");
                notes.Add($"notch {width / 1000:0.00} m wide cut out of the outline: attached partition");
                var keep = new List<(double X, double Y)>();
                for (int k = 0; k < n; k++)
                    if (k != (i + 1) % n && k != (i + 2) % n) keep.Add(corners[k]);
                corners = keep;
                cutOne = true;
            }
            if (!cutOne) break;
        }
        // Drop spike/loop records that ended up flat (all points in a line)
        map.Obstacles.RemoveAll(o => o.Kind is ObstacleKind.AttachedPartition or ObstacleKind.WallPillar &&
                                     Math.Abs(Geometry.SignedArea(o.Polygon)) < 1000);

        var room = new Room
        {
            Corners = corners!.Select(c => new[] { c.X, c.Y }).ToList(),
            Closed = t.End == TraceEnd.Closed && t.Inside,
            ClosureErrorMm = closure,
        };

        // ---------------- sections
        var special = new List<(int Edge, double T0, double T1, SectionKind Kind)>();
        foreach (var d in t.Doorways) AddSpecial(room, special, (d.Ax, d.Ay), (d.Bx, d.By), SectionKind.Doorway);
        foreach (var s in t.Skirts)
            AddSpecial(room, special, s.A.P, s.B.P, s.Collinear ? SectionKind.Inferred : SectionKind.Unresolved);
        for (int e = 0; e < room.Corners.Count; e++)
        {
            var mine = special.Where(x => x.Edge == e).OrderBy(x => x.T0).ToList();
            double at = 0;
            foreach (var m in mine)
            {
                double t0 = Math.Max(m.T0, at);
                if (m.T1 <= t0) continue;
                if (t0 > at + 1e-6) room.Sections.Add(new WallSection(e, at, t0, SectionKind.Confirmed));
                room.Sections.Add(new WallSection(e, t0, m.T1, m.Kind));
                at = m.T1;
            }
            if (at < 1 - 1e-6) room.Sections.Add(new WallSection(e, at, 1, SectionKind.Confirmed));
        }
        if (!room.Closed) notes.Add("the trace did not close: the room outline is INCOMPLETE");
        return new Output(room, notes);
    }

    /// <summary>A stretch A-B of one wall gets its own label (projected onto the nearest edge).</summary>
    private static void AddSpecial(Room room, List<(int, double, double, SectionKind)> list,
                                   (double X, double Y) a, (double X, double Y) b, SectionKind kind)
    {
        var poly = room.Polygon;
        int best = -1;
        double bestD = double.MaxValue;
        for (int e = 0; e < poly.Count; e++)
        {
            var p = poly[e];
            var q = poly[(e + 1) % poly.Count];
            double d = Geometry.DistToSegment(a, p, q) + Geometry.DistToSegment(b, p, q);
            if (d < bestD) { bestD = d; best = e; }
        }
        if (best < 0) return;
        var e0 = poly[best];
        var e1 = poly[(best + 1) % poly.Count];
        double ta = Geometry.ProjectOnSegment(a, e0, e1).T, tb = Geometry.ProjectOnSegment(b, e0, e1).T;
        if (Math.Abs(tb - ta) < 1e-3) return;
        list.Add((best, Math.Min(ta, tb), Math.Max(ta, tb), kind));
    }

    /// <summary>A sensible room: a simple polygon (no self-crossing), 0.3 m^2, most of the path inside.</summary>
    private static bool Plausible(List<(double X, double Y)>? corners, List<(double X, double Y)> path)
    {
        if (corners is not { Count: >= 3 }) return false;
        if (Math.Abs(Geometry.SignedArea(corners)) < 3e5) return false;
        int n = corners.Count;
        for (int i = 0; i < n; i++)
            for (int j = i + 2; j < n; j++)
            {
                if (i == 0 && j == n - 1) continue;
                if (Geometry.SegmentsCross(corners[i], corners[(i + 1) % n], corners[j], corners[(j + 1) % n], out _))
                    return false;
            }
        int inside = path.Count(p => Geometry.PointInPolygon(corners, p.X, p.Y));
        return inside >= path.Count * 0.8;
    }

    /// <summary>Douglas-Peucker on an open path, returning the kept indexes.</summary>
    private static List<int> SimplifyIdx(List<(double X, double Y)> pts, double tol)
    {
        var keep = new bool[pts.Count];
        if (pts.Count == 0) return new List<int>();
        keep[0] = keep[^1] = true;
        var stack = new Stack<(int, int)>();
        stack.Push((0, pts.Count - 1));
        while (stack.Count > 0)
        {
            var (a, b) = stack.Pop();
            int idx = -1;
            double md = tol;
            for (int i = a + 1; i < b; i++)
            {
                double d = Geometry.DistToSegment(pts[i], pts[a], pts[b]);
                if (d > md) { md = d; idx = i; }
            }
            if (idx < 0) continue;
            keep[idx] = true;
            stack.Push((a, idx));
            stack.Push((idx, b));
        }
        return Enumerable.Range(0, pts.Count).Where(i => keep[i]).ToList();
    }
}
