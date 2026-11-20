namespace RoombaMapper.Core;

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

    // ---------------------------------------------------------------- grid access (for drawing)
    public int GridWidth => _w;
    public int GridHeight => _h;
    public double GridX0 => _x0;
    public double GridY0 => _y0;
    /// <summary>Something is actually there (room outside, obstacle, recorded hit).</summary>
    public bool IsOccupiedCell(int ix, int iy) => InGrid(ix, iy) && _occ[iy * _w + ix];
    /// <summary>The robot's centre may not be here (occupied, or within the clearance of it).</summary>
    public bool IsBlockedCell(int ix, int iy) => Blocked(ix, iy);
    public (int Ix, int Iy) CellOf(double x, double y) => Cell(x, y);
    public (double X, double Y) CellCenter(int ix, int iy) => (CenterX(ix), CenterY(iy));

    /// <summary>
    /// Free cells that can be reached from 'from' (8-connected flood fill).
    /// Used to tell reachable areas from unreachable ones.
    /// </summary>
    public bool[] ReachableFrom((double X, double Y) from)
    {
        var seen = new bool[_w * _h];
        var (sx, sy) = Cell(from.X, from.Y);
        var start = NearestFree(sx, sy, 10);
        if (start == null) return seen;
        var q = new Queue<int>();
        int s0 = start.Value.Item2 * _w + start.Value.Item1;
        seen[s0] = true;
        q.Enqueue(s0);
        while (q.Count > 0)
        {
            int c = q.Dequeue();
            int cx = c % _w, cy = c / _w;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                {
                    int nx = cx + dx, ny = cy + dy;
                    if (Blocked(nx, ny)) continue;
                    int n = ny * _w + nx;
                    if (seen[n]) continue;
                    seen[n] = true;
                    q.Enqueue(n);
                }
        }
        return seen;
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
        // The robot may be a little outside the mapped free space (e.g. in an alcove the room
        // outline cut off): start from the nearest free spot within 1.5 m
        var start = NearestFree(sx, sy, 30);
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
}
