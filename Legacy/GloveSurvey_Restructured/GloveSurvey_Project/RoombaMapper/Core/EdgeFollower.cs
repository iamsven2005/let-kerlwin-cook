namespace RoombaMapper.Core;

public enum TraceEnd { Closed, Incomplete, Aborted }

/// <summary>A point on the surface being followed.</summary>
public sealed record TraceHit((double X, double Y) P, double FacingDeg, int PathIndex, bool FromFront, bool Bulge);

/// <summary>A narrow thing passed or skirted while following (becomes a Phase 2 seed).</summary>
public sealed record NarrowHit((double X, double Y) P, bool Skirted);

/// <summary>Skirt round a narrow bulge: wall contact lost at A, found again at B.</summary>
public sealed record Skirt(TraceHit A, TraceHit B, bool Collinear, List<(double X, double Y)> Bulge);

public sealed class TraceResult
{
    public TraceEnd End { get; set; } = TraceEnd.Incomplete;
    public bool Inside { get; set; }          // closed with the bot inside the loop (a room)
    public string Reason { get; set; } = "";
    public Pose Start { get; set; }
    public List<Pose> Path { get; } = new();
    public List<double> PathDist { get; } = new();
    public List<double> PathTheta { get; } = new();   // theta at each path point
    /// <summary>Where the closed loop starts in Path (0 = at the trace start). The trace may
    /// begin on something else (e.g. a pillar) before settling onto the loop.</summary>
    public int LoopStart { get; set; }

    /// <summary>The closed loop only: path from LoopStart, hits re-indexed to it.</summary>
    public (List<(double X, double Y)> Path, List<double> Dist, List<TraceHit> Hits) Loop()
    {
        int s = Math.Clamp(LoopStart, 0, Math.Max(0, Path.Count - 1));
        double d0 = PathDist.Count > s ? PathDist[s] : 0;
        return (Path.Skip(s).Select(p => p.Pos).ToList(),
                PathDist.Skip(s).Select(d => d - d0).ToList(),
                Hits.Where(h => h.PathIndex >= s).Select(h => h with { PathIndex = h.PathIndex - s }).ToList());
    }
    public List<TraceHit> Hits { get; } = new();
    public List<Doorway> Doorways { get; } = new();
    public List<Skirt> Skirts { get; } = new();
    public List<NarrowHit> Narrow { get; } = new();
    public double ThetaDeg { get; set; }      // heading turned since the start (anticlockwise +)
    public double DistanceMm { get; set; }
}

/// <summary>
/// Follows a surface with it on one side (spec Phase 1 steps 4-9, also used for solid
/// obstacles in Phase 2). Works with only a front sensor: every ProbeStep it looks to the
/// follow side (turning to look if there is no side sensor).
///
///   front blocked -> wiggle scan
///       wide   -> concave corner: turn away
///       narrow -> estimate the gap between it and the wall
///                 gap >= F + R and off our path -> pass it, log a seed
///                 otherwise -> skirt outward round it (record A), keep following
///   follow side lost
///       DoorChecks on: hold heading for up to D_door, looking
///                 wall back in line -> doorway (if wider than MinDoor)
///                 not back -> go back to just past the last wall point, convex corner
///       DoorChecks off (objects): convex corner straight away
///   back near the start after ~360 deg -> closed; turned away from the follow side = inside
/// Guards: total distance, step count, 4 convex turns in a row without a wall, abort hook.
/// </summary>
public sealed class EdgeFollower
{
    private readonly Nav _nav;
    private IRobot R => _nav.Robot;
    private RobotConfig C => _nav.Cfg;

    public int Side { get; init; } = -1;                 // -1: surface on the right, +1: left
    public bool DoorChecks { get; init; } = true;
    public double MaxDistanceMm { get; init; } = 60000;
    public int MaxSteps { get; init; } = 2500;
    public double CloseRadiusMm { get; init; } = 250;
    public double CloseMinTurnDeg { get; init; } = 300;
    /// <summary>A loop must be at least this long to count (rooms: a few metres).</summary>
    public double MinLoopMm { get; init; } = 1000;
    /// <summary>Checked after every step; return a reason to stop (Aborted), or null.</summary>
    public Func<TraceResult, string?>? Abort { get; init; }
    public string Name { get; init; } = "trace";

    private int Away => -Side;
    private TraceResult _r = new();
    private TraceHit? _lastWall;
    private int _convexStreak;
    private int _glitches;
    private int _steps;
    private (TraceHit A, int FromHit, double AtDist)? _skirting;

    public EdgeFollower(Nav nav) { _nav = nav; }

    /// <summary>Start with the robot stopped, facing the surface (it just blocked a move).</summary>
    public async Task<TraceResult> RunAsync(CancellationToken ct)
    {
        _r = new TraceResult();
        _r.Path.Add(R.Pose);
        _r.PathDist.Add(0);
        _r.PathTheta.Add(0);

        // Step 4: square up to the surface (a flat surface's scan arc is centred on the
        // perpendicular), then turn so it is on the follow side, parallel to it
        var square = await R.ScanAsync(C.GuardScanHalfDeg, ct);
        double faceDeg = R.Pose.HeadingDeg;
        if (square.Any)
        {
            var a0 = Nav.NearestArc(square);
            if (Math.Abs(Geometry.Wrap180(a0.CenterDeg - faceDeg)) < 45 && !(square.TouchesLow(a0) ^ square.TouchesHigh(a0)))
                faceDeg = a0.CenterDeg;
        }
        AddHit(C.HitPoint(R.Pose, faceDeg), faceDeg, fromFront: true);
        await TurnAsync(Geometry.Wrap180(faceDeg + Away * 90 - R.Pose.HeadingDeg), ct);
        _r.Start = R.Pose;
        _r.ThetaDeg = 0;
        _nav.Host.Log($"{Name}: start at {_r.Start.X:0},{_r.Start.Y:0}");

        while (true)
        {
            ct.ThrowIfCancellationRequested();
            if (ClosedLoopStart() is int loopStart)
            {
                _r.End = TraceEnd.Closed;
                _r.LoopStart = loopStart;
                double turned = _r.ThetaDeg - _r.PathTheta[loopStart];
                _r.Inside = turned * Away > 0;           // turned away from the surface overall
                _r.Reason = (_r.Inside ? "closed, robot inside" : "closed, robot outside") +
                            (loopStart > 0 ? $" (loop starts {_r.PathDist[loopStart] / 1000:0.0} m in)" : "");
                break;
            }
            if (Abort?.Invoke(_r) is string why) { _r.End = TraceEnd.Aborted; _r.Reason = why; break; }
            if (_r.DistanceMm > MaxDistanceMm) { _r.End = TraceEnd.Incomplete; _r.Reason = "too long"; break; }
            if (++_steps > MaxSteps) { _r.End = TraceEnd.Incomplete; _r.Reason = "too many steps"; break; }
            if (_convexStreak > 4) { _r.End = TraceEnd.Incomplete; _r.Reason = "lost the surface"; break; }

            // Step 5: one step along, then look at the follow side
            var mv = await StepAsync(C.ProbeStepMm, ct);
            if (mv.Blocked) { await FrontBlockedAsync(ct); continue; }

            var look = await R.LookAsync(Side, ct);
            Track(look.At);
            if (look.Seen)
            {
                OnWallSeen(look);
                continue;
            }
            // Step 6: the follow side dropped to 0
            if (DoorChecks && _lastWall != null) await DoorCheckAsync(ct);
            else await ConvexCornerAsync(ct);
        }
        _nav.Host.Log($"{Name}: {_r.Reason}; {_r.DistanceMm / 1000:0.0} m, theta {_r.ThetaDeg:0} deg, " +
                      $"{_r.Hits.Count} points, {_r.Doorways.Count} doorways, {_r.Skirts.Count} skirts");
        return _r;
    }

    // ---------------------------------------------------------------- steps

    private void OnWallSeen(LookResult look)
    {
        var hit = AddHit(C.HitPoint(look.At, look.FacingDeg), look.FacingDeg, fromFront: false);
        _convexStreak = 0;
        CheckSkirtEnd(hit);
        _lastWall = hit;
    }

    /// <summary>Step 7/8: something ahead.</summary>
    private async Task FrontBlockedAsync(CancellationToken ct)
    {
        // As wide as the guard's scan, so whatever stopped the robot is seen again
        var (kind, arc, scan) = await _nav.ScanAndClassifyAsync(Math.Max(C.ScanHalfDeg, _nav.Guarded ? C.GuardScanHalfDeg : 0),
                                                                allowCloser: true, ct);
        if (kind == Detection.None || arc is not { } a)
        {
            // Stopped but nothing there when looking: a glitch. Three in a row = sensor problem.
            if (++_glitches > 3) throw new MissionException("the front sensor keeps stopping the robot with nothing there.");
            _nav.Host.Log($"{Name}: stopped, but the scan saw nothing; carrying on");
            return;
        }
        _glitches = 0;
        var pose = R.Pose;

        if (kind == Detection.Wide)
        {
            // Concave corner: record the surface ahead, turn away - parallel to it, using
            // the arc's centre as its perpendicular (keeps the follow straight)
            double perp = pose.HeadingDeg;
            if (Math.Abs(Geometry.Wrap180(a.CenterDeg - pose.HeadingDeg)) < 30 && !(scan.TouchesLow(a) ^ scan.TouchesHigh(a)))
                perp = a.CenterDeg;
            AddHit(_nav.ArcHitPoint(scan, a), perp, fromFront: true);
            _convexStreak = 0;
            await TurnAsync(Geometry.Wrap180(perp + Away * 90 - R.Pose.HeadingDeg), ct);
            return;
        }

        // Narrow in the path
        var hp = _nav.ArcHitPoint(scan, a);
        double along = Dot(Dir(pose.HeadingDeg), Sub(hp, pose.Pos));
        double lateral = Cross(Dir(pose.HeadingDeg), Sub(hp, pose.Pos)) * Away;   // + = away side
        double gap = _lastWall is { } w ? Math.Abs(Dot(Dir(w.FacingDeg), Sub(hp, w.P))) : double.MaxValue;

        if (lateral >= C.WidthMm / 2 + 30 && gap >= C.FitMm + C.RangeMm)
        {
            // 8a: it isn't actually in our way and the robot fits between it and the wall
            _r.Narrow.Add(new NarrowHit(hp, false));
            _nav.Host.Log($"{Name}: narrow thing {gap:0} mm from the wall, off our path: passing it");
            await MoveAsync(Math.Max(0, along) + C.SensorOffsetMm + 30, true, ct);
            return;
        }

        // 8b: skirt outward round it, keeping it on the follow side
        _r.Narrow.Add(new NarrowHit(hp, true));
        if (_skirting == null && _lastWall != null)
        {
            _skirting = (_lastWall, _r.Hits.Count, _r.DistanceMm);
            _nav.Host.Log($"{Name}: narrow thing {(gap == double.MaxValue ? 0 : gap):0} mm from the wall: skirting round it");
        }
        AddHit(hp, a.CenterDeg, fromFront: true, bulge: _skirting != null);
        await TurnAsync(Away * 90, ct);
    }

    /// <summary>Step 6a: hold heading for up to one door width, looking for the wall.</summary>
    private async Task DoorCheckAsync(CancellationToken ct)
    {
        var a = _lastWall!;
        double held = 0;
        while (held < C.DoorMaxMm)
        {
            var mv = await StepAsync(C.ProbeStepMm, ct);
            held += C.ProbeStepMm;
            if (mv.Blocked) { await FrontBlockedAsync(ct); return; }
            var look = await R.LookAsync(Side, ct);
            Track(look.At);
            if (!look.Seen) continue;

            var hp = C.HitPoint(look.At, look.FacingDeg);
            double offLine = Math.Abs(Dot(Dir(a.FacingDeg), Sub(hp, a.P)));
            double gapWidth = held;                                 // open stretch we drove past
            if (offLine <= C.CollinearTolMm && gapWidth >= C.MinDoorMm)
            {
                _r.Doorways.Add(new Doorway(a.P.X, a.P.Y, hp.X, hp.Y));
                _nav.Host.Log($"{Name}: doorway about {gapWidth / 1000:0.00} m wide");
            }
            OnWallSeen(look);
            return;
        }
        // 6a-ii: not back: an outside corner. Back to where the wall was first missed.
        if (held > 1) await MoveAsync(-held, true, ct);
        await ConvexCornerAsync(ct);
    }

    /// <summary>
    /// Outside (convex) corner. The edge is somewhere within the last probe step, so first
    /// pin it down: back up in small steps, looking, until the surface is seen again. Then
    /// drive past the edge by the robot's turning radius (+ margin) so the body clears the
    /// corner, and turn towards the follow side. The new face is then within sensor reach.
    /// </summary>
    private async Task ConvexCornerAsync(CancellationToken ct)
    {
        _convexStreak++;
        const double fine = 40;
        double backed = 0;
        bool found = false;
        while (backed < C.ProbeStepMm + fine)
        {
            await MoveAsync(-fine, true, ct);
            backed += fine;
            var look = await R.LookAsync(Side, ct);
            Track(look.At);
            if (look.Seen) { OnWallSeen(look); _convexStreak++; found = true; break; }
        }
        // How far the robot is from the face it was following (the new face starts that
        // far ahead once it has turned)
        double follow = _lastWall is { } w
            ? Math.Clamp(Math.Abs(Dot(Dir(w.FacingDeg), Sub(w.P, R.Pose.Pos))), C.SensorOffsetMm, C.SensorOffsetMm + C.RangeMm + 60)
            : C.StopDistMm;
        double past = C.TurnRadiusMm + fine + 10;
        if (!found) past = backed;                       // never seen again: go back where we were
        var mv = await MoveAsync(past, false, ct);
        if (mv.Blocked) { await FrontBlockedAsync(ct); return; }
        await TurnAsync(Side * 90, ct);
        if (!found) return;

        // Round the corner: drive until alongside the new face, then look
        mv = await MoveAsync(follow + fine, false, ct);
        if (mv.Blocked) { await FrontBlockedAsync(ct); return; }
        var look2 = await R.LookAsync(Side, ct);
        Track(look2.At);
        if (look2.Seen) { OnWallSeen(look2); _convexStreak = 0; }
    }

    // ---------------------------------------------------------------- skirts

    private void CheckSkirtEnd(TraceHit hit)
    {
        if (_skirting is not { } sk) return;
        if (_r.DistanceMm - sk.AtDist > 3000) { _skirting = null; return; }    // it was just more wall
        if (Geometry.AngleDiff(hit.FacingDeg, sk.A.FacingDeg) > 25) return;     // not the same wall direction
        double offLine = Math.Abs(Dot(Dir(sk.A.FacingDeg), Sub(hit.P, sk.A.P)));
        bool collinear = offLine <= C.CollinearTolMm;
        var bulge = _r.Hits.Skip(sk.FromHit).Where(h => h != hit).Select(h => h.P).ToList();
        _r.Skirts.Add(new Skirt(sk.A, hit, collinear, bulge));
        _nav.Host.Log($"{Name}: back on the wall after skirting ({(collinear ? "in line: inferred wall" : "not in line: unresolved")})");
        _skirting = null;
    }

    // ---------------------------------------------------------------- bookkeeping

    private TraceHit AddHit((double X, double Y) p, double facing, bool fromFront, bool bulge = false)
    {
        var h = new TraceHit(p, Geometry.Wrap180(facing), _r.Path.Count - 1, fromFront, bulge || _skirting != null);
        _r.Hits.Add(h);
        return h;
    }

    /// <summary>
    /// One step forward along the surface. When driving guarded (unexplored space) the step
    /// wiggle-scans first, so something about to brush the robot's side counts as "in the
    /// way" too. The heading is put back to the direction of travel either way, so the
    /// follow stays parallel.
    /// </summary>
    private async Task<MoveResult> StepAsync(double mm, CancellationToken ct)
    {
        if (!_nav.Guarded) return await MoveAsync(mm, false, ct);
        double h0 = R.Pose.HeadingDeg;
        var r = await _nav.GotoAsync(Geometry.FromPolar(R.Pose.Pos, h0, mm), ct, ignoreSide: Side);
        Track(r.End);
        double off = Geometry.Wrap180(h0 - R.Pose.HeadingDeg);
        if (Math.Abs(off) > 0.5) await TurnAsync(off, ct);
        return r with { End = R.Pose };
    }

    private async Task<MoveResult> MoveAsync(double mm, bool ignoreFront, CancellationToken ct)
    {
        var mv = await R.MoveAsync(mm, ignoreFront, ct);
        Track(mv.End);
        _nav.AfterMove(mv.End);
        return mv;
    }

    private async Task TurnAsync(double deg, CancellationToken ct)
    {
        await R.TurnAsync(deg, ct);
        Track(R.Pose);
    }

    private void Track(Pose p)
    {
        var last = _r.Path[^1];
        _r.ThetaDeg += Geometry.Wrap180(p.HeadingDeg - last.HeadingDeg);
        _r.DistanceMm += Geometry.Dist(last.Pos, p.Pos);
        _r.Path.Add(p);
        _r.PathDist.Add(_r.DistanceMm);
        _r.PathTheta.Add(_r.ThetaDeg);
    }

    /// <summary>
    /// Closure: back within CloseRadius of an earlier point of the trace, at least 1 m of
    /// driving later, having turned ~360 deg since then. Usually that earlier point is the
    /// start; if the trace began on something else and then settled onto a loop, it is the
    /// first point of that loop. Returns the loop's start index, or null.
    /// </summary>
    private int? ClosedLoopStart()
    {
        if (_r.DistanceMm <= MinLoopMm) return null;
        var here = R.Pose.Pos;
        int startIdx = Math.Max(0, _r.Path.FindIndex(p => p == _r.Start));
        for (int j = startIdx; j < _r.Path.Count; j++)
        {
            if (_r.DistanceMm - _r.PathDist[j] < MinLoopMm) break;
            if (Math.Abs(_r.ThetaDeg - _r.PathTheta[j]) < CloseMinTurnDeg) continue;
            if (Geometry.Dist(_r.Path[j].Pos, here) < CloseRadiusMm) return j;
        }
        return null;
    }

    private static (double X, double Y) Dir(double deg) => (Math.Cos(deg * Math.PI / 180), Math.Sin(deg * Math.PI / 180));
    private static (double X, double Y) Sub((double X, double Y) a, (double X, double Y) b) => (a.X - b.X, a.Y - b.Y);
    private static double Dot((double X, double Y) a, (double X, double Y) b) => a.X * b.X + a.Y * b.Y;
    private static double Cross((double X, double Y) a, (double X, double Y) b) => a.X * b.Y - a.Y * b.X;
}
