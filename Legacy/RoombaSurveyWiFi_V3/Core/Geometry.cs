namespace RoombaMapper.Core;

public static class Geometry
{
    public static bool PointInPolygon(IReadOnlyList<(double X, double Y)> poly, double x, double y)
    {
        bool inside = false;
        for (int i = 0, j = poly.Count - 1; i < poly.Count; j = i++)
        {
            var (xi, yi) = poly[i];
            var (xj, yj) = poly[j];
            if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) inside = !inside;
        }
        return inside;
    }

    /// <summary>Reduces a closed outline to its corners (Douglas-Peucker).</summary>
    public static List<(double X, double Y)> SimplifyClosed(IReadOnlyList<(double X, double Y)> pts, double toleranceMm)
    {
        if (pts.Count < 4) return pts.ToList();
        int far = 0;
        double farD = -1;
        for (int i = 1; i < pts.Count; i++)
        {
            double d = Dist(pts[0], pts[i]);
            if (d > farD) { farD = d; far = i; }
        }
        var a = Simplify(pts.Take(far + 1).ToList(), toleranceMm);
        var b = Simplify(pts.Skip(far).Append(pts[0]).ToList(), toleranceMm);
        var result = new List<(double X, double Y)>(a);
        result.AddRange(b.Skip(1).Take(b.Count - 2));   // skip shared start and end
        return result;
    }

    private static List<(double X, double Y)> Simplify(List<(double X, double Y)> pts, double tol)
    {
        if (pts.Count < 3) return pts;
        int idx = 0;
        double maxD = 0;
        for (int i = 1; i < pts.Count - 1; i++)
        {
            double d = DistToSegment(pts[i], pts[0], pts[^1]);
            if (d > maxD) { maxD = d; idx = i; }
        }
        if (maxD <= tol) return new List<(double X, double Y)> { pts[0], pts[^1] };
        var left = Simplify(pts.Take(idx + 1).ToList(), tol);
        var right = Simplify(pts.Skip(idx).ToList(), tol);
        left.RemoveAt(left.Count - 1);
        left.AddRange(right);
        return left;
    }

    public static double Dist((double X, double Y) a, (double X, double Y) b) =>
        Math.Sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y));

    public static double DistToSegment((double X, double Y) p, (double X, double Y) a, (double X, double Y) b)
    {
        double dx = b.X - a.X, dy = b.Y - a.Y;
        double len2 = dx * dx + dy * dy;
        if (len2 < 1e-9) return Dist(p, a);
        double t = Math.Clamp(((p.X - a.X) * dx + (p.Y - a.Y) * dy) / len2, 0, 1);
        return Dist(p, (a.X + t * dx, a.Y + t * dy));
    }

    /// <summary>
    /// Works out corners from edge points that know which way the sensor was facing.
    /// A wall is square to the sensor that saw it, so each run of points facing roughly
    /// the same way is one wall (a straight line), and each corner is where two
    /// neighbouring walls meet. This gives true corners even from a handful of points
    /// (simple front-only tracing records only one point per wall). Null if it can't.
    /// </summary>
    public static List<(double X, double Y)>? CornersFromFacing(
        IReadOnlyList<(double X, double Y, double Facing)> pts, double groupDeg = 30)
    {
        var walls = WallsFromFacing(pts, groupDeg);
        return walls == null ? null : CornersFromWalls(walls, requireAll: false);
    }

    /// <summary>
    /// The walls behind those corners, as lines: (Nx, Ny) = the direction pointing out
    /// through the wall, C = its distance along that direction (a point p is on the
    /// room side when Nx*p.X + Ny*p.Y &lt; C). Wall k runs from corner k-1 to corner k.
    /// </summary>
    public static List<(double Nx, double Ny, double C)>? WallsFromFacing(
        IReadOnlyList<(double X, double Y, double Facing)> pts, double groupDeg = 30)
    {
        if (pts.Count < 3) return null;

        // Group points into walls
        var groups = new List<List<(double X, double Y, double Facing)>>();
        foreach (var p in pts)
        {
            if (groups.Count == 0 || AngleDiff(p.Facing, groups[^1][0].Facing) > groupDeg)
                groups.Add(new List<(double X, double Y, double Facing)>());
            groups[^1].Add(p);
        }
        // The trace ends on the wall it started on: join the last group onto the first
        if (groups.Count > 1 && AngleDiff(groups[0][0].Facing, groups[^1][0].Facing) <= groupDeg)
        {
            groups[0].InsertRange(0, groups[^1]);
            groups.RemoveAt(groups.Count - 1);
        }
        if (groups.Count < 3) return null;

        // Each wall: a line facing the average direction, through the average point
        return groups.Select(g =>
        {
            double sx = g.Sum(p => Math.Cos(p.Facing * Math.PI / 180));
            double sy = g.Sum(p => Math.Sin(p.Facing * Math.PI / 180));
            double len = Math.Sqrt(sx * sx + sy * sy);
            double nx = sx / len, ny = sy / len;
            double c = g.Average(p => nx * p.X + ny * p.Y);
            return (Nx: nx, Ny: ny, C: c);
        }).ToList();
    }

    /// <summary>
    /// Room walls from the path the robot drove while following them. Each straight
    /// stretch of the path runs alongside a wall, wallOffsetMm away on the outside
    /// (the robot's right when it went round anticlockwise). This only needs the
    /// robot's position, which arrives 5 times a second, so it still works if some
    /// wall-hit messages were lost.
    /// </summary>
    public static List<(double Nx, double Ny, double C)>? WallsFromPath(
        IReadOnlyList<(double X, double Y)> path, double wallOffsetMm)
    {
        if (path.Count < 8) return null;
        var simp = SimplifyClosed(path, 60);
        if (simp.Count < 3) return null;

        // Which way round did it go? Anticlockwise = the walls were on its right.
        double area = 0;
        for (int i = 0; i < simp.Count; i++)
        {
            var a = simp[i];
            var b = simp[(i + 1) % simp.Count];
            area += a.X * b.Y - b.X * a.Y;
        }
        int side = area > 0 ? 1 : -1;

        var walls = new List<(double Nx, double Ny, double C)>();
        for (int i = 0; i < simp.Count; i++)
        {
            var a = simp[i];
            var b = simp[(i + 1) % simp.Count];
            double dx = b.X - a.X, dy = b.Y - a.Y, len = Math.Sqrt(dx * dx + dy * dy);
            if (len < 150) continue;                       // a little jog at a corner
            dx /= len; dy /= len;
            double nx = side > 0 ? dy : -dy, ny = side > 0 ? -dx : dx;   // pointing out of the room
            double c = Math.Max(nx * a.X + ny * a.Y, nx * b.X + ny * b.Y) + wallOffsetMm;

            // Same direction as the previous wall: one wall, keep the outer line
            if (walls.Count > 0 && nx * walls[^1].Nx + ny * walls[^1].Ny > 0.94)
            {
                if (c > walls[^1].C) walls[^1] = (walls[^1].Nx, walls[^1].Ny, c);
                continue;
            }
            walls.Add((nx, ny, c));
        }
        if (walls.Count > 1 && walls[0].Nx * walls[^1].Nx + walls[0].Ny * walls[^1].Ny > 0.94)
        {
            walls[0] = (walls[0].Nx, walls[0].Ny, Math.Max(walls[0].C, walls[^1].C));
            walls.RemoveAt(walls.Count - 1);
        }
        return walls.Count >= 3 ? walls : null;
    }

    /// <summary>
    /// Corners where neighbouring walls meet. requireAll: return null if any two
    /// neighbouring walls are parallel (then corner k doesn't line up with wall k).
    /// </summary>
    public static List<(double X, double Y)>? CornersFromWalls(
        IReadOnlyList<(double Nx, double Ny, double C)> lines, bool requireAll)
    {
        var corners = new List<(double X, double Y)>();
        for (int i = 0; i < lines.Count; i++)
        {
            var (n1x, n1y, c1) = lines[i];
            var (n2x, n2y, c2) = lines[(i + 1) % lines.Count];
            double det = n1x * n2y - n1y * n2x;
            if (Math.Abs(det) < 0.2)             // nearly parallel walls: no corner between them
            {
                if (requireAll) return null;
                continue;
            }
            corners.Add(((c1 * n2y - n1y * c2) / det, (n1x * c2 - n2x * c1) / det));
        }
        return corners.Count >= 3 ? corners : null;
    }

    public static double AngleDiff(double a, double b)
    {
        double d = Math.Abs(a - b) % 360;
        return d > 180 ? 360 - d : d;
    }

    public static double SignedArea(IReadOnlyList<(double X, double Y)> poly)
    {
        double a = 0;
        for (int i = 0; i < poly.Count; i++)
        {
            var p = poly[i];
            var q = poly[(i + 1) % poly.Count];
            a += p.X * q.Y - q.X * p.Y;
        }
        return a / 2;
    }

    /// <summary>Smallest convex outline around the points (monotone chain), anticlockwise.</summary>
    public static List<(double X, double Y)> ConvexHull(IEnumerable<(double X, double Y)> points)
    {
        var p = points.Distinct().OrderBy(q => q.X).ThenBy(q => q.Y).ToList();
        if (p.Count < 3) return p;
        static double Cross((double X, double Y) o, (double X, double Y) a, (double X, double Y) b) =>
            (a.X - o.X) * (b.Y - o.Y) - (a.Y - o.Y) * (b.X - o.X);
        var hull = new List<(double X, double Y)>();
        foreach (var q in p)                                  // lower half
        {
            while (hull.Count >= 2 && Cross(hull[^2], hull[^1], q) <= 0) hull.RemoveAt(hull.Count - 1);
            hull.Add(q);
        }
        int lower = hull.Count + 1;
        for (int i = p.Count - 2; i >= 0; i--)                // upper half
        {
            var q = p[i];
            while (hull.Count >= lower && Cross(hull[^2], hull[^1], q) <= 0) hull.RemoveAt(hull.Count - 1);
            hull.Add(q);
        }
        hull.RemoveAt(hull.Count - 1);
        return hull;
    }

    /// <summary>Pushes every corner of a convex outline outwards from its centre by mm.</summary>
    public static List<(double X, double Y)> Grow(IReadOnlyList<(double X, double Y)> poly, double mm)
    {
        double cx = poly.Average(p => p.X), cy = poly.Average(p => p.Y);
        return poly.Select(p =>
        {
            double d = Dist(p, (cx, cy));
            return d < 1 ? p : (p.X + (p.X - cx) / d * mm, p.Y + (p.Y - cy) / d * mm);
        }).ToList();
    }

    /// <summary>Adds points along a route so there is one at least every stepMm.</summary>
    public static List<(double X, double Y)> Densify(IReadOnlyList<(double X, double Y)> path, double stepMm)
    {
        var result = new List<(double X, double Y)>();
        if (path.Count == 0) return result;
        result.Add(path[0]);
        for (int i = 1; i < path.Count; i++)
        {
            var a = path[i - 1];
            var b = path[i];
            int n = Math.Max(1, (int)Math.Ceiling(Dist(a, b) / stepMm));
            for (int k = 1; k <= n; k++)
                result.Add((a.X + (b.X - a.X) * k / n, a.Y + (b.Y - a.Y) * k / n));
        }
        return result;
    }

    /// <summary>Wraps an angle in degrees to (-180, 180].</summary>
    public static double Wrap180(double deg)
    {
        deg %= 360;
        if (deg > 180) deg -= 360;
        if (deg <= -180) deg += 360;
        return deg;
    }

    public static (double X, double Y) FromPolar((double X, double Y) origin, double headingDeg, double dist)
    {
        double a = headingDeg * Math.PI / 180;
        return (origin.X + dist * Math.Cos(a), origin.Y + dist * Math.Sin(a));
    }

    public static double BearingDeg((double X, double Y) from, (double X, double Y) to) =>
        Math.Atan2(to.Y - from.Y, to.X - from.X) * 180 / Math.PI;

    /// <summary>Closest point on segment a-b to p, and how far along (0..1) it is.</summary>
    public static ((double X, double Y) Point, double T) ProjectOnSegment(
        (double X, double Y) p, (double X, double Y) a, (double X, double Y) b)
    {
        double dx = b.X - a.X, dy = b.Y - a.Y, len2 = dx * dx + dy * dy;
        if (len2 < 1e-9) return (a, 0);
        double t = Math.Clamp(((p.X - a.X) * dx + (p.Y - a.Y) * dy) / len2, 0, 1);
        return ((a.X + t * dx, a.Y + t * dy), t);
    }

    /// <summary>Distance from p to the closed polygon's edges.</summary>
    public static double DistToPolygonEdge(IReadOnlyList<(double X, double Y)> poly, (double X, double Y) p)
    {
        double best = double.MaxValue;
        for (int i = 0; i < poly.Count; i++)
            best = Math.Min(best, DistToSegment(p, poly[i], poly[(i + 1) % poly.Count]));
        return best;
    }

    /// <summary>Do segments p1-p2 and p3-p4 cross (proper crossing)? Returns the crossing point.</summary>
    public static bool SegmentsCross((double X, double Y) p1, (double X, double Y) p2,
                                     (double X, double Y) p3, (double X, double Y) p4,
                                     out (double X, double Y) at)
    {
        at = default;
        double d1x = p2.X - p1.X, d1y = p2.Y - p1.Y, d2x = p4.X - p3.X, d2y = p4.Y - p3.Y;
        double den = d1x * d2y - d1y * d2x;
        if (Math.Abs(den) < 1e-9) return false;
        double t = ((p3.X - p1.X) * d2y - (p3.Y - p1.Y) * d2x) / den;
        double u = ((p3.X - p1.X) * d1y - (p3.Y - p1.Y) * d1x) / den;
        if (t <= 1e-6 || t >= 1 - 1e-6 || u <= 1e-6 || u >= 1 - 1e-6) return false;
        at = (p1.X + t * d1x, p1.Y + t * d1y);
        return true;
    }

    /// <summary>
    /// Smallest-area rectangle around the points (rotating calipers over the hull).
    /// Returns its long and short side lengths (used to tell pillars from partitions).
    /// </summary>
    public static (double Long, double Short) MinAreaRect(IEnumerable<(double X, double Y)> points)
    {
        var hull = ConvexHull(points);
        if (hull.Count < 2) return (0, 0);
        if (hull.Count == 2) return (Dist(hull[0], hull[1]), 0);
        double bestArea = double.MaxValue, bestL = 0, bestS = 0;
        for (int i = 0; i < hull.Count; i++)
        {
            var a = hull[i];
            var b = hull[(i + 1) % hull.Count];
            double len = Dist(a, b);
            if (len < 1e-6) continue;
            double ux = (b.X - a.X) / len, uy = (b.Y - a.Y) / len;
            double minU = double.MaxValue, maxU = double.MinValue, minV = double.MaxValue, maxV = double.MinValue;
            foreach (var p in hull)
            {
                double u = (p.X - a.X) * ux + (p.Y - a.Y) * uy;
                double v = -(p.X - a.X) * uy + (p.Y - a.Y) * ux;
                minU = Math.Min(minU, u); maxU = Math.Max(maxU, u);
                minV = Math.Min(minV, v); maxV = Math.Max(maxV, v);
            }
            double w = maxU - minU, h = maxV - minV;
            if (w * h < bestArea) { bestArea = w * h; bestL = Math.Max(w, h); bestS = Math.Min(w, h); }
        }
        return (bestL, bestS);
    }

    /// <summary>A circle as a polygon (for footprints).</summary>
    public static List<(double X, double Y)> Circle((double X, double Y) c, double r, int n = 12) =>
        Enumerable.Range(0, n).Select(i => FromPolar(c, i * 360.0 / n, r)).ToList();

    /// <summary>A square of side 'size' centred on c, aligned with 'headingDeg'.</summary>
    public static List<(double X, double Y)> Square((double X, double Y) c, double size, double headingDeg)
    {
        double h = size / 2;
        var corners = new[] { (h, h), (-h, h), (-h, -h), (h, -h) };
        double a = headingDeg * Math.PI / 180, ca = Math.Cos(a), sa = Math.Sin(a);
        return corners.Select(p => (c.X + p.Item1 * ca - p.Item2 * sa, c.Y + p.Item1 * sa + p.Item2 * ca)).ToList();
    }
}
