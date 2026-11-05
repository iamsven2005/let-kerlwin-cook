namespace RoombaMapper;

/// <summary>
/// Turns the traced room and obstacle outlines into a grid of "free" and "blocked"
/// squares, then plans routes on it (A* search) and lays out back-and-forth lanes.
/// "Blocked" includes a keep-away margin, so any route keeps the robot's centre at least
/// that far from walls and obstacles.
/// </summary>
public sealed class Planner
{
    public const double CellMm = 50;

    private readonly int _w, _h;
    private readonly double _x0, _y0;
    private readonly bool[] _occ;      // something is actually there
    private readonly bool[] _blocked;  // too close to something for the robot's centre

    public Planner(IReadOnlyList<(double X, double Y)>? room,
                   IEnumerable<IReadOnlyList<(double X, double Y)>> objects,
                   IEnumerable<(double X, double Y)> loose,
                   double keepAwayMm)
    {
        var objList = objects.Where(o => o.Count >= 3).ToList();
        var looseList = loose.ToList();
        var roomList = room is { Count: >= 3 } ? room : null;

        var all = new List<(double X, double Y)> { (0, 0) };
        if (roomList != null) all.AddRange(roomList);
        foreach (var o in objList) all.AddRange(o);
        all.AddRange(looseList);

        double pad = keepAwayMm + 300;
        double minX = all.Min(p => p.X) - pad, maxX = all.Max(p => p.X) + pad;
        double minY = all.Min(p => p.Y) - pad, maxY = all.Max(p => p.Y) + pad;
        _x0 = minX;
        _y0 = minY;
        _w = (int)Math.Ceiling((maxX - minX) / CellMm) + 1;
        _h = (int)Math.Ceiling((maxY - minY) / CellMm) + 1;
        _occ = new bool[_w * _h];

        // Outside the room, or inside a traced object = occupied
        for (int iy = 0; iy < _h; iy++)
            for (int ix = 0; ix < _w; ix++)
            {
                double cx = CenterX(ix), cy = CenterY(iy);
                bool occ = roomList != null && !Geometry.PointInPolygon(roomList, cx, cy);
                for (int k = 0; k < objList.Count && !occ; k++)
                    occ = Geometry.PointInPolygon(objList[k], cx, cy);
                _occ[iy * _w + ix] = occ;
            }

        // Every recorded edge point = occupied
        foreach (var p in all.Skip(1))
        {
            var (ix, iy) = Cell(p.X, p.Y);
            if (InGrid(ix, iy)) _occ[iy * _w + ix] = true;
        }

        // Grow the occupied area by the keep-away distance
        _blocked = new bool[_w * _h];
        int r = (int)Math.Ceiling(keepAwayMm / CellMm);
        var disk = new List<(int dx, int dy)>();
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
                if (dx * dx + dy * dy <= r * r) disk.Add((dx, dy));

        for (int iy = 0; iy < _h; iy++)
            for (int ix = 0; ix < _w; ix++)
            {
                if (!_occ[iy * _w + ix]) continue;
                _blocked[iy * _w + ix] = true;
                if (!HasFreeNeighbour(ix, iy)) continue;   // only edges need growing
                foreach (var (dx, dy) in disk)
                {
                    int nx = ix + dx, ny = iy + dy;
                    if (InGrid(nx, ny)) _blocked[ny * _w + nx] = true;
                }
            }
    }

    /// <summary>
    /// A picture of where the robot's centre may not go: dark = something is there,
    /// light = too close to something. One pixel per grid square, top row = highest Y.
    /// </summary>
    public Bitmap NoGoImage(out double x0, out double yTop, out double cellMm)
    {
        var bmp = new Bitmap(_w, _h, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
        int clear = Color.FromArgb(0, 255, 255, 255).ToArgb();
        int near = Color.FromArgb(70, 120, 120, 140).ToArgb();
        int solid = Color.FromArgb(120, 70, 70, 90).ToArgb();
        var px = new int[_w * _h];
        for (int j = 0; j < _h; j++)
        {
            int iy = _h - 1 - j;
            for (int ix = 0; ix < _w; ix++)
            {
                int c = iy * _w + ix;
                px[j * _w + ix] = _occ[c] ? solid : _blocked[c] ? near : clear;
            }
        }
        var data = bmp.LockBits(new Rectangle(0, 0, _w, _h), System.Drawing.Imaging.ImageLockMode.WriteOnly,
                                System.Drawing.Imaging.PixelFormat.Format32bppArgb);
        System.Runtime.InteropServices.Marshal.Copy(px, 0, data.Scan0, px.Length);
        bmp.UnlockBits(data);
        x0 = _x0;
        yTop = _y0 + _h * CellMm;
        cellMm = CellMm;
        return bmp;
    }

    // ---------------------------------------------------------------- grid helpers

    private double CenterX(int ix) => _x0 + (ix + 0.5) * CellMm;
    private double CenterY(int iy) => _y0 + (iy + 0.5) * CellMm;
    private (int, int) Cell(double x, double y) =>
        ((int)Math.Floor((x - _x0) / CellMm), (int)Math.Floor((y - _y0) / CellMm));
    private bool InGrid(int ix, int iy) => ix >= 0 && ix < _w && iy >= 0 && iy < _h;
    private bool Blocked(int ix, int iy) => !InGrid(ix, iy) || _blocked[iy * _w + ix];

    private bool HasFreeNeighbour(int ix, int iy)
    {
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
            {
                int nx = ix + dx, ny = iy + dy;
                if (InGrid(nx, ny) && !_occ[ny * _w + nx]) return true;
            }
        return false;
    }

    public bool IsFree(double x, double y)
    {
        var (ix, iy) = Cell(x, y);
        return !Blocked(ix, iy);
    }

    /// <summary>Is there already something recorded within radiusMm of this point?</summary>
    public bool IsNearKnownObstacle(double x, double y, double radiusMm)
    {
        var (cx, cy) = Cell(x, y);
        int r = (int)Math.Ceiling(radiusMm / CellMm);
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
            {
                int nx = cx + dx, ny = cy + dy;
                if (InGrid(nx, ny) && _occ[ny * _w + nx] && dx * dx + dy * dy <= r * r) return true;
            }
        return false;
    }

    private (int, int)? NearestFree(int ix, int iy, int maxR)
    {
        if (!Blocked(ix, iy)) return (ix, iy);
        (int, int)? best = null;
        int bestD = int.MaxValue;
        for (int r = 1; r <= maxR; r++)
        {
            for (int dy = -r; dy <= r; dy++)
                for (int dx = -r; dx <= r; dx++)
                {
                    if (Math.Max(Math.Abs(dx), Math.Abs(dy)) != r) continue;
                    int nx = ix + dx, ny = iy + dy;
                    int d = dx * dx + dy * dy;
                    if (!Blocked(nx, ny) && d < bestD) { bestD = d; best = (nx, ny); }
                }
            if (best != null) return best;
        }
        return null;
    }

    // ---------------------------------------------------------------- route planning

    /// <summary>
    /// A route from one point to another that keeps clear of everything, as a short list
    /// of corner points (the first is 'from'). Null if there is no way through.
    /// </summary>
    public List<(double X, double Y)>? Plan((double X, double Y) from, (double X, double Y) to)
    {
        var (sx, sy) = Cell(from.X, from.Y);
        var (gx, gy) = Cell(to.X, to.Y);
        var start = NearestFree(sx, sy, 10);
        var goal = NearestFree(gx, gy, 6);
        if (start == null || goal == null) return null;

        int s = start.Value.Item2 * _w + start.Value.Item1;
        int g = goal.Value.Item2 * _w + goal.Value.Item1;
        int n = _w * _h;
        var cost = new double[n];
        var came = new int[n];
        var done = new bool[n];
        Array.Fill(cost, double.PositiveInfinity);
        Array.Fill(came, -1);

        double H(int c)
        {
            int dx = Math.Abs(c % _w - goal.Value.Item1), dy = Math.Abs(c / _w - goal.Value.Item2);
            return Math.Max(dx, dy) + 0.4142 * Math.Min(dx, dy);
        }

        var open = new PriorityQueue<int, double>();
        cost[s] = 0;
        open.Enqueue(s, H(s));
        while (open.TryDequeue(out int cur, out _))
        {
            if (done[cur]) continue;
            if (cur == g) break;
            done[cur] = true;
            int cx = cur % _w, cy = cur / _w;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                {
                    if (dx == 0 && dy == 0) continue;
                    int nx = cx + dx, ny = cy + dy;
                    if (Blocked(nx, ny)) continue;
                    bool diag = dx != 0 && dy != 0;
                    if (diag && (Blocked(cx + dx, cy) || Blocked(cx, cy + dy))) continue;  // no corner cutting
                    int nb = ny * _w + nx;
                    double nc = cost[cur] + (diag ? 1.4142 : 1.0);
                    if (nc < cost[nb])
                    {
                        cost[nb] = nc;
                        came[nb] = cur;
                        open.Enqueue(nb, nc + H(nb));
                    }
                }
        }
        if (double.IsPositiveInfinity(cost[g])) return null;

        var cells = new List<(double X, double Y)>();
        for (int c = g; c != -1; c = came[c]) cells.Add((CenterX(c % _w), CenterY(c / _w)));
        cells.Reverse();
        if (IsFree(to.X, to.Y)) cells[^1] = to;   // finish exactly on the target if it's free

        // Straighten: from each point, jump to the furthest point still in clear view
        var result = new List<(double X, double Y)> { from };
        int i = 0;
        while (i < cells.Count - 1)
        {
            int j = cells.Count - 1;
            while (j > i + 1 && !LineFree(cells[i], cells[j])) j--;
            result.Add(cells[j]);
            i = j;
        }
        if (result.Count == 1) result.Add(cells[^1]);
        return result;
    }

    private bool LineFree((double X, double Y) a, (double X, double Y) b)
    {
        double len = Math.Sqrt((b.X - a.X) * (b.X - a.X) + (b.Y - a.Y) * (b.Y - a.Y));
        int steps = Math.Max(1, (int)(len / (CellMm / 2)));
        for (int k = 0; k <= steps; k++)
        {
            double t = (double)k / steps;
            if (!IsFree(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t)) return false;
        }
        return true;
    }

    // ---------------------------------------------------------------- coverage lanes

    /// <summary>A stretch of one lane: the lane is at V (across), free from A to B (along, A &lt; B).</summary>
    public readonly record struct LaneSeg(double V, double A, double B)
    {
        public double Length => B - A;
    }

    /// <summary>
    /// Range of free space across the lanes (V) for lanes along X (vertical = false) or Y.
    /// Null if there is no free space.
    /// </summary>
    public (double VMin, double VMax)? FreeRange(bool vertical)
    {
        double vMin = double.MaxValue, vMax = double.MinValue;
        for (int iy = 0; iy < _h; iy++)
            for (int ix = 0; ix < _w; ix++)
            {
                if (_blocked[iy * _w + ix]) continue;
                double v = vertical ? CenterX(ix) : CenterY(iy);
                vMin = Math.Min(vMin, v);
                vMax = Math.Max(vMax, v);
            }
        return vMin <= vMax ? (vMin, vMax) : null;
    }

    /// <summary>
    /// The parts of each lane still to be done: lanes every spacingMm starting at anchorV,
    /// split wherever the lane is blocked or 'done' (already cleaned, or given up).
    /// Pieces shorter than minLenMm are ignored.
    /// </summary>
    public List<LaneSeg> LaneSegments(double spacingMm, double anchorV, bool vertical,
                                      Func<double, double, bool> done, double minLenMm = 150)
    {
        var result = new List<LaneSeg>();
        if (FreeRange(vertical) is not { } range) return result;

        int kMin = (int)Math.Ceiling((range.VMin - anchorV) / spacingMm - 1e-6);
        int kMax = (int)Math.Floor((range.VMax - anchorV) / spacingMm + 1e-6);
        int minCells = Math.Max(1, (int)Math.Ceiling(minLenMm / CellMm));
        for (int k = kMin; k <= kMax; k++)
        {
            double v = anchorV + k * spacingMm;
            var (cx, cy) = vertical ? Cell(v, _y0) : Cell(_x0, v);
            int n = vertical ? _h : _w;
            int run = -1;
            for (int i = 0; i <= n; i++)
            {
                bool ok = false;
                if (i < n)
                {
                    int ix = vertical ? cx : i, iy = vertical ? i : cy;
                    if (!Blocked(ix, iy))
                    {
                        double x = vertical ? v : CenterX(ix), y = vertical ? CenterY(iy) : v;
                        ok = !done(x, y);
                    }
                }
                if (ok && run < 0) run = i;
                if (!ok && run >= 0)
                {
                    if (i - run >= minCells)
                    {
                        double a = vertical ? CenterY(run) : CenterX(run);
                        double b = vertical ? CenterY(i - 1) : CenterX(i - 1);
                        result.Add(new LaneSeg(v, a, b));
                    }
                    run = -1;
                }
            }
        }
        return result;
    }

    // ---------------------------------------------------------------- lanes

    /// <summary>
    /// Back-and-forth lanes across all the free space, spacingMm apart, as an ordered list
    /// of lane start/end points. Lanes are split where an obstacle crosses them.
    /// Starts from the end nearest to 'home'.
    /// </summary>
    public List<(double X, double Y)> LaneTargets(double spacingMm, (double X, double Y) home)
    {
        int minRow = -1, maxRow = -1;
        for (int iy = 0; iy < _h; iy++)
            for (int ix = 0; ix < _w; ix++)
                if (!_blocked[iy * _w + ix]) { if (minRow < 0) minRow = iy; maxRow = iy; break; }
        var result = new List<(double X, double Y)>();
        if (minRow < 0) return result;

        var rows = new List<int>();
        int step = Math.Max(1, (int)Math.Round(spacingMm / CellMm));
        for (int iy = minRow; iy <= maxRow; iy += step) rows.Add(iy);
        if (maxRow - rows[^1] > step / 2) rows.Add(maxRow);

        // Start from the side (bottom or top) nearest home
        if (Math.Abs(home.Y - CenterY(maxRow)) < Math.Abs(home.Y - CenterY(minRow))) rows.Reverse();

        bool leftToRight = true;
        bool first = true;
        foreach (int iy in rows)
        {
            var intervals = new List<(int a, int b)>();
            int ix = 0;
            while (ix < _w)
            {
                if (Blocked(ix, iy)) { ix++; continue; }
                int a = ix;
                while (ix < _w && !Blocked(ix, iy)) ix++;
                if (ix - a >= 3) intervals.Add((a, ix - 1));
            }
            if (intervals.Count == 0) continue;

            if (first)
            {
                // First lane: start from the end nearest home
                double left = CenterX(intervals[0].a), right = CenterX(intervals[^1].b);
                leftToRight = Math.Abs(home.X - left) <= Math.Abs(home.X - right);
                first = false;
            }

            if (!leftToRight) intervals.Reverse();
            foreach (var (a, b) in intervals)
            {
                var (s, e) = leftToRight ? (a, b) : (b, a);
                result.Add((CenterX(s), CenterY(iy)));
                result.Add((CenterX(e), CenterY(iy)));
            }
            leftToRight = !leftToRight;
        }
        return result;
    }
}

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

    private static double DistToSegment((double X, double Y) p, (double X, double Y) a, (double X, double Y) b)
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

    private static double AngleDiff(double a, double b)
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
}
