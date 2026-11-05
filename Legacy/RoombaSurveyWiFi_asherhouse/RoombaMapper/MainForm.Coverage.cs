using System.Drawing.Imaging;
using System.Runtime.InteropServices;

namespace RoombaMapper;

/// <summary>
/// Phases 2 and 3: serpentine (back-and-forth) coverage with obstacle mapping and
/// dynamic re-planning.
///
/// The app keeps a coverage map of what has been done. Every step it looks at what is
/// still left (the free, not-yet-done pieces of each lane, worked out fresh from the
/// current map) and picks the next piece in serpentine order:
///   1. the rest of the current lane, straight ahead
///   2. otherwise the nearest lane on in the sweep direction, starting from its nearer end
///      (so the direction alternates lane to lane)
///   3. only when nothing is left ahead: areas left behind (e.g. behind an obstacle)
/// It drives there along an obstacle-free route, then along the lane.
///
/// If something blocks the robot:
///   new obstacle -> stop, trace round it (probing), add its outline to the map,
///                   re-plan from where the robot now is, carry on with the lanes
///   known one    -> re-plan round it (no re-tracing, no repeated attempts)
///   couldn't trace it fully -> the part seen is blocked off with a margin
/// Lane pieces that can't be reached, or fail 3 times, are given up and marked, so the
/// robot never keeps trying the same blocked spot.
/// </summary>
public sealed partial class MainForm
{
    private const double CovCellMm = 50;

    private readonly HashSet<(int, int)> _covered = new();   // done (driven over)
    private readonly HashSet<(int, int)> _gaveUp = new();    // couldn't be reached
    private bool _coverageActive;
    private bool _markingLane;            // only lane driving counts as covered (keeps lanes whole)
    private double _coverRadiusMm;
    private DateTime _lastCoverageDraw = DateTime.MinValue;

    // Serpentine state
    private bool _lanesVertical;          // lanes along Y instead of X
    private double _laneAnchor;           // lane positions: anchor + k * spacing
    private double _laneSpacingMm;
    private double? _curLaneV;            // the lane being worked on
    private int _laneDir = 1;             // +1 = along the lane towards bigger X (or Y)
    private int _sweep = 1;               // +1 = next lane is at bigger V
    private readonly Dictionary<(long, long), int> _segmentTries = new();

    private readonly record struct Candidate(Planner.LaneSeg Seg, double StartU, double EndU, int Sweep);

    // ================================================================ phases

    private async Task Phase2Async(CancellationToken ct)
    {
        await CoverAsync("Phase 2", ExploreLaneSpacingMm, measure: false, ct);
        _map.PlannedPath = null;
        MissionStatus("Phase 2: all reachable lanes done. Going home...");
        await GoHomeAsync(ct);
    }

    private async Task Phase3Async(CancellationToken ct)
    {
        // Fresh survey: clear the old path and heat map, keep the room and obstacles
        _map.ClearSurveyData();
        _withSignal = 0;
        bool slow = (string?)_modeBox.SelectedItem == "SLOW";

        await CoverAsync("Phase 3", (double)_laneSpacing.Value * 10, measure: slow, ct);
        MissionStatus("Phase 3: survey done. Going home...");
        await GoHomeAsync(ct);

        MissionStatus($"Phase 3: Wi-Fi survey complete ({_map.Readings.Count} readings).");
        if (MessageBox.Show(this,
                $"The Wi-Fi survey is complete, with {_map.Readings.Count} readings.\n\n" +
                "Export the heat map now? (Saves a PNG picture and a CSV of the readings.)",
                "Survey complete", MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes)
            ExportHeatmap();
    }

    // ================================================================ the coverage loop

    private async Task CoverAsync(string name, double spacingMm, bool measure, CancellationToken ct)
    {
        _covered.Clear();
        _gaveUp.Clear();
        _segmentTries.Clear();
        _laneSpacingMm = spacingMm;
        _coverRadiusMm = spacingMm / 2 - 10;     // one lane's width, without eating into the next
        _curLaneV = null;
        _laneDir = 1;
        _sweep = 1;
        _coverageActive = true;
        DrawCoverage(force: true);

        try
        {
            var planner = BuildPlanner();
            _lanesVertical = LanesShouldBeVertical();
            if (planner.FreeRange(_lanesVertical) is not { } range)
                throw new MissionException("no free space found inside the room.");
            _laneAnchor = range.VMin;            // first lane as close to the wall as allowed

            for (int step = 1; ; step++)
            {
                ct.ThrowIfCancellationRequested();
                if (step > 500) { MissionStatus($"{name}: stopping after 500 steps (safety limit)."); break; }

                // Re-plan from scratch every step: the map may have changed
                planner = BuildPlanner();
                var segs = planner.LaneSegments(_laneSpacingMm, _laneAnchor, _lanesVertical, IsDone);
                if (segs.Count == 0) break;

                var pose = CurrentPose();
                Candidate? chosen = null;
                List<(double X, double Y)>? route = null;
                foreach (var c in Candidates(segs, pose, _curLaneV, _laneDir, _sweep))
                {
                    route = planner.Plan(pose, P(c.Seg.V, c.StartU));
                    if (route != null) { chosen = c; break; }
                    GiveUp(c.Seg);                                   // can't get there at all
                    MissionStatus($"{name}: a lane piece can't be reached; marked as skipped.");
                }
                if (chosen is not { } pick || route == null) break;  // nothing reachable is left

                // Don't keep trying the same piece forever
                var key = ((long)Math.Round(pick.Seg.V), (long)Math.Round(pick.StartU / 200));
                _segmentTries[key] = _segmentTries.GetValueOrDefault(key) + 1;
                if (_segmentTries[key] > 3)
                {
                    GiveUp(pick.Seg);
                    MissionStatus($"{name}: tried a lane piece 3 times; marked as skipped.");
                    continue;
                }

                _sweep = pick.Sweep;
                ShowPlan(segs, pose, route, pick);
                int left = segs.Count;
                MissionStatus($"{name}: lane {LaneNumber(pick.Seg.V)}, {left} lane piece{(left == 1 ? "" : "s")} left...");

                // 1. Get to the start of the lane piece (around anything in the way)
                bool ok = true;
                foreach (var wp in route.Skip(1))
                    if (!await DriveToAsync(wp, ct)) { ok = false; break; }
                if (!ok) { await HandleBlockedAsync(name, ct); continue; }

                // 2. Drive along the lane
                _curLaneV = pick.Seg.V;
                _laneDir = pick.EndU > pick.StartU ? 1 : -1;
                var start = P(pick.Seg.V, pick.StartU);
                var end = P(pick.Seg.V, pick.EndU);
                var stops = measure
                    ? Geometry.Densify(new List<(double X, double Y)> { start, end }, SlowMeasureSpacingMm).Skip(1).ToList()
                    : new List<(double X, double Y)> { end };

                _markingLane = true;
                MarkCovered(start.X, start.Y);
                try
                {
                    foreach (var stop in stops)
                    {
                        if (!await DriveToAsync(stop, ct)) { ok = false; break; }
                        if (measure) await MeasureAsync(ct);
                    }
                }
                finally { _markingLane = false; }

                if (ok)
                {
                    MarkLine(start, end, _covered, _coverRadiusMm);      // the whole piece is done
                    continue;
                }

                // Stopped on the lane. Just short of the end, by a wall or a mapped
                // obstacle? Then the lane is done (the sensor sees ~20 cm ahead).
                var hit = _lastObstacle is { } o ? (o.X, o.Y) : CurrentPose();
                if (Geometry.Dist(CurrentPose(), end) < 250 && IsNearTracedOutline(hit.Item1, hit.Item2, 200))
                {
                    MarkLine(start, end, _covered, _coverRadiusMm);
                    continue;
                }
                await HandleBlockedAsync(name, ct);
            }

            MissionStatus($"{name}: everything reachable is covered ({_covered.Count * CovCellMm * CovCellMm / 1e6:0.0} m²).");
        }
        finally
        {
            _coverageActive = false;
            _markingLane = false;
            DrawCoverage(force: true);
        }
    }

    /// <summary>
    /// Something stopped the robot. New obstacle: trace round it so it goes on the map.
    /// Known obstacle: nothing to do, the next plan goes round it.
    /// </summary>
    private async Task HandleBlockedAsync(string name, CancellationToken ct)
    {
        var hit = _lastObstacle is { } o ? (o.X, o.Y) : CurrentPose();
        if (IsNearTracedOutline(hit.Item1, hit.Item2, 200))
        {
            MissionStatus($"{name}: blocked by something already mapped. Re-planning round it...");
            return;
        }

        MissionStatus($"{name}: new obstacle. Tracing round it to map its shape...");
        var shape = await TraceAsync(room: false, ct);
        if (shape is { Kind: 1 })
            MissionStatus($"{name}: obstacle mapped. Re-planning the remaining lanes round it...");
        else
            MissionStatus($"{name}: couldn't map all of it; blocking off the part it saw, with a margin. Re-planning...");
    }

    // ================================================================ choosing the next lane piece

    /// <summary>
    /// Lane pieces in the order the serpentine pattern wants them (best first).
    /// Used both to pick the next piece and to preview the rest of the plan.
    /// </summary>
    private IEnumerable<Candidate> Candidates(List<Planner.LaneSeg> segs, (double X, double Y) pose,
                                              double? curV, int dir, int sweep)
    {
        double half = _laneSpacingMm / 2;
        double pu = U(pose);

        if (curV is not double cv)
        {
            // Starting out: the nearest lane, then sweep towards the side with more lanes
            var first = segs.OrderBy(s => Math.Abs(s.V - V(pose)))
                            .ThenBy(s => NearEndDist(s, pose)).FirstOrDefault();
            if (first == default) yield break;
            int more = segs.Count(s => s.V > first.V + half) >= segs.Count(s => s.V < first.V - half) ? 1 : -1;
            bool fromA = Geometry.Dist(pose, P(first.V, first.A)) <= Geometry.Dist(pose, P(first.V, first.B));
            yield return new Candidate(first, fromA ? first.A : first.B, fromA ? first.B : first.A, more);
            foreach (var c in Candidates(segs, pose, first.V, fromA ? 1 : -1, more))
                if (c.Seg != first) yield return c;
            yield break;
        }

        // 1. The rest of this lane, straight ahead (e.g. the far side of an obstacle)
        var sameLane = segs.Where(s => Math.Abs(s.V - cv) < half).ToList();
        var ahead = sameLane.Where(s => dir > 0 ? s.B > pu + 50 : s.A < pu - 50)
                            .OrderBy(s => dir > 0 ? Math.Max(s.A, pu) : -Math.Min(s.B, pu));
        foreach (var s in ahead)
        {
            double startU = dir > 0 ? Math.Max(s.A, pu) : Math.Min(s.B, pu);
            yield return new Candidate(s, startU, dir > 0 ? s.B : s.A, sweep);
        }

        // 2. The next lanes in the sweep direction, nearest lane first;
        // 3. then (only if needed) the lanes left behind, nearest first
        foreach (int sw in new[] { sweep, -sweep })
        {
            var lanes = segs.Where(s => sw * (s.V - cv) > half)
                            .GroupBy(s => Math.Round(s.V))
                            .OrderBy(g => Math.Abs(g.Key - cv));
            foreach (var lane in lanes)
                foreach (var s in lane.OrderBy(s => NearEndDist(s, pose)))
                {
                    bool fromA = Geometry.Dist(pose, P(s.V, s.A)) <= Geometry.Dist(pose, P(s.V, s.B));
                    yield return new Candidate(s, fromA ? s.A : s.B, fromA ? s.B : s.A, sw);
                }
        }

        // 4. Pieces of this lane that were passed (behind the robot)
        foreach (var s in sameLane.Where(s => !(dir > 0 ? s.B > pu + 50 : s.A < pu - 50))
                                  .OrderBy(s => NearEndDist(s, pose)))
        {
            bool fromA = Geometry.Dist(pose, P(s.V, s.A)) <= Geometry.Dist(pose, P(s.V, s.B));
            yield return new Candidate(s, fromA ? s.A : s.B, fromA ? s.B : s.A, sweep);
        }
    }

    /// <summary>Shows the route to the next piece plus a preview of the rest of the serpentine.</summary>
    private void ShowPlan(List<Planner.LaneSeg> segs, (double X, double Y) pose,
                          List<(double X, double Y)> route, Candidate first)
    {
        var path = new List<(double X, double Y)>(route) { P(first.Seg.V, first.EndU) };
        var left = segs.Where(s => s != first.Seg).ToList();
        double? cv = first.Seg.V;
        int dir = first.EndU > first.StartU ? 1 : -1, sweep = first.Sweep;
        var at = P(first.Seg.V, first.EndU);
        for (int i = 0; i < 200 && left.Count > 0; i++)
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
        _map.PlannedPath = path;
    }

    // ================================================================ lane helpers

    // Lane coordinates: U = along the lanes, V = across them
    private double U((double X, double Y) p) => _lanesVertical ? p.Y : p.X;
    private double V((double X, double Y) p) => _lanesVertical ? p.X : p.Y;
    private (double X, double Y) P(double v, double u) => _lanesVertical ? (v, u) : (u, v);
    private double NearEndDist(Planner.LaneSeg s, (double X, double Y) pose) =>
        Math.Min(Geometry.Dist(pose, P(s.V, s.A)), Geometry.Dist(pose, P(s.V, s.B)));
    private int LaneNumber(double v) => (int)Math.Round((v - _laneAnchor) / _laneSpacingMm) + 1;

    /// <summary>Lanes run along the longer side of the room (fewer turns).</summary>
    private bool LanesShouldBeVertical()
    {
        if (_roomShapeId is int rid && _map.Corners.TryGetValue(rid, out var room) && room.Count >= 3)
        {
            double w = room.Max(p => p.X) - room.Min(p => p.X);
            double h = room.Max(p => p.Y) - room.Min(p => p.Y);
            return h > w;
        }
        return false;
    }

    // ================================================================ coverage map

    private static (int, int) CovKey(double x, double y) =>
        ((int)Math.Floor(x / CovCellMm), (int)Math.Floor(y / CovCellMm));

    private bool IsDone(double x, double y)
    {
        var k = CovKey(x, y);
        return _covered.Contains(k) || _gaveUp.Contains(k);
    }

    /// <summary>Marks a lane-wide circle around the robot as done (called with its position).</summary>
    private void MarkCovered(double x, double y)
    {
        if (MarkDisk(_covered, x, y, _coverRadiusMm)) DrawCoverage(force: false);
    }

    private static bool MarkDisk(HashSet<(int, int)> set, double x, double y, double radius)
    {
        int r = (int)Math.Ceiling(radius / CovCellMm);
        var (cx, cy) = CovKey(x, y);
        bool added = false;
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
            {
                double px = (cx + dx + 0.5) * CovCellMm, py = (cy + dy + 0.5) * CovCellMm;
                if ((px - x) * (px - x) + (py - y) * (py - y) <= radius * radius)
                    added |= set.Add((cx + dx, cy + dy));
            }
        return added;
    }

    private void MarkLine((double X, double Y) a, (double X, double Y) b, HashSet<(int, int)> set, double radius)
    {
        int n = Math.Max(1, (int)(Geometry.Dist(a, b) / (CovCellMm / 2)));
        for (int i = 0; i <= n; i++)
            MarkDisk(set, a.X + (b.X - a.X) * i / n, a.Y + (b.Y - a.Y) * i / n, radius);
        DrawCoverage(force: false);
    }

    /// <summary>Give up on a lane piece: it won't be planned again in this phase.</summary>
    private void GiveUp(Planner.LaneSeg s) => MarkLine(P(s.V, s.A), P(s.V, s.B), _gaveUp, CovCellMm);

    /// <summary>Draws done areas (green) and given-up areas (amber) on the map.</summary>
    private void DrawCoverage(bool force)
    {
        if (!force && (DateTime.Now - _lastCoverageDraw).TotalMilliseconds < 400) return;
        _lastCoverageDraw = DateTime.Now;
        if (_covered.Count == 0 && _gaveUp.Count == 0) { _map.SetCoverage(null); return; }

        var all = _covered.Concat(_gaveUp).ToList();
        int minX = all.Min(k => k.Item1), maxX = all.Max(k => k.Item1);
        int minY = all.Min(k => k.Item2), maxY = all.Max(k => k.Item2);
        int w = maxX - minX + 1, h = maxY - minY + 1;
        if ((long)w * h > 4_000_000) return;

        var px = new int[w * h];
        int done = Color.FromArgb(55, 60, 180, 90).ToArgb();
        int skipped = Color.FromArgb(70, 230, 160, 30).ToArgb();
        foreach (var (x, y) in _covered) px[(maxY - y) * w + (x - minX)] = done;
        foreach (var (x, y) in _gaveUp) px[(maxY - y) * w + (x - minX)] = skipped;

        var bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb);
        var data = bmp.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        Marshal.Copy(px, 0, data.Scan0, px.Length);
        bmp.UnlockBits(data);
        _map.SetCoverage(bmp, minX * CovCellMm, (maxY + 1) * CovCellMm, CovCellMm);
    }
}
