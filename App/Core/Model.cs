using System.Text.Json;
using System.Text.Json.Serialization;

namespace RoombaMapper.Core;

// =====================================================================================
//  Robot configuration: the definitions from the specification
// =====================================================================================

/// <summary>
/// Physical and behaviour settings. All distances in mm, angles in degrees.
///   L, W  = bot length and width          R = sensor trigger range
///   F     = fit threshold = W + 2R         G = grouping radius = 3L
/// The real IR sensor only says YES/NO, so where something is seen is estimated at
/// HitFraction * R beyond the sensor (the sensor sits at the front edge, L/2 from centre).
/// </summary>
public sealed class RobotConfig
{
    public double LengthMm { get; set; } = 160;          // L
    public double WidthMm { get; set; } = 160;           // W
    public double RangeMm { get; set; } = 100;           // R (calibrate on the real robot)
    public double HitFraction { get; set; } = 0.6;       // seen at ~0.6 R (YES/NO sensor)

    public double DoorMaxMm { get; set; } = 900;         // D_door: widest gap that counts as a doorway
    public double MinDoorMm { get; set; } = 300;         // narrower gaps are missed readings, not doors
    public double CollinearTolMm { get; set; } = 100;    // "wall reappears roughly in line"

    public double ScanHalfDeg { get; set; } = 45;        // full wiggle scan (stopped)
    public double SearchScanHalfDeg { get; set; } = 30;  // wiggle while searching / skirting
    public double ArcWideDeg { get; set; } = 60;         // arc >= this: wide (solid surface)
    public double ArcNarrowDeg { get; set; } = 25;       // arc <= this: narrow (leg, post)

    public double ProbeStepMm { get; set; } = 150;       // edge-follow step between side looks
    public double SearchStepMm { get; set; } = 300;      // Phase 1 search: wiggle every this far
    public double SurveyLaneMm { get; set; } = 400;      // Phase 3 lane spacing (user setting)
    public double SlowStepMm { get; set; } = 300;        // Phase 3 SLOW: measure every this far
    public double ExtraClearanceMm { get; set; } = 30;   // safety gap on top of the robot's size
    public double GuardScanHalfDeg { get; set; } = 60;   // guarded driving: wiggle this wide...
    /// <summary>
    /// ...this often. Something beside the path can only be seen once it is within the
    /// sensor's reach (L/2 + R from the centre) and must be seen before it is within the
    /// body (W/2): so the robot can drive at most L/2 + R - W/2 between wiggles.
    /// </summary>
    public double GuardStepMm => Math.Max(20, SensorOffsetMm + RangeMm - WidthMm / 2 - 5);
    /// <summary>Phase 3: also drive guarded (default). Off = faster, but the body may brush
    /// something new that the narrow front beam misses.</summary>
    public bool SurveyGuard { get; set; } = true;
    public double ArcGrowthWide { get; set; } = 1.6;     // closer rescan: arc grew this much -> wide

    public double FitMm => WidthMm + 2 * RangeMm;                        // F
    public double GroupMm => 3 * LengthMm;                               // G
    public double SensorOffsetMm => LengthMm / 2;                        // centre -> sensor
    public double TurnRadiusMm => Math.Sqrt(LengthMm * LengthMm + WidthMm * WidthMm) / 2;
    /// <summary>Planning keeps the robot's centre this far from anything (it may turn on the spot).</summary>
    public double ClearanceMm => TurnRadiusMm + ExtraClearanceMm;
    /// <summary>How far from a surface the sensor stops the robot (centre to surface).</summary>
    public double StopDistMm => SensorOffsetMm + HitFraction * RangeMm;
    /// <summary>Stand-off point for looking at a seed: close enough for the sensor to reach it.</summary>
    public double StandOffMm => Math.Max(ClearanceMm + 10, SensorOffsetMm + 0.7 * RangeMm);
    public double ExploreLaneMm => FitMm;                                // Phase 2 lane spacing
    public double CornerClearMm => WidthMm / 2 + 50;                     // past an outside corner

    /// <summary>Where the sensor sees something, for a robot at 'pose' looking along 'facingDeg'.</summary>
    public (double X, double Y) HitPoint(Pose pose, double facingDeg) =>
        Geometry.FromPolar(pose.Pos, facingDeg, SensorOffsetMm + HitFraction * RangeMm);
}

public readonly record struct Pose(double X, double Y, double HeadingDeg)
{
    [JsonIgnore] public (double X, double Y) Pos => (X, Y);
}

// =====================================================================================
//  The map
// =====================================================================================

/// <summary>How sure we are about a stretch of room wall.</summary>
public enum SectionKind { Confirmed, Inferred, Unresolved, Doorway }

/// <summary>
/// A stretch of room wall: edge 'Edge' of the corner polygon (corner Edge to corner Edge+1),
/// from fraction T0 to T1 along it. Stored as fractions so that when a wall moves, its
/// sections move with it.
/// </summary>
public sealed record WallSection(int Edge, double T0, double T1, SectionKind Kind);

/// <summary>
/// The room outline. Corners are the ACTUAL wall positions (never inflated), anticlockwise;
/// the robot's clearance is only added when planning. Sections label each stretch of wall.
/// </summary>
public sealed class Room
{
    public List<double[]> Corners { get; set; } = new();       // [x, y], anticlockwise
    public List<WallSection> Sections { get; set; } = new();
    public bool Closed { get; set; }                           // false = incomplete trace
    public double ClosureErrorMm { get; set; }
    public Dictionary<int, int> PushVotes { get; set; } = new(); // confirmed walls need 2 votes to move
    /// <summary>A single push bigger than this is not trusted (odometry or map error instead).</summary>
    public const double MaxPushMm = 300;

    [JsonIgnore] public List<(double X, double Y)> Polygon => Corners.Select(c => (c[0], c[1])).ToList();

    public ((double X, double Y) A, (double X, double Y) B) Ends(WallSection s)
    {
        var a = (Corners[s.Edge][0], Corners[s.Edge][1]);
        var b = (Corners[(s.Edge + 1) % Corners.Count][0], Corners[(s.Edge + 1) % Corners.Count][1]);
        return ((a.Item1 + (b.Item1 - a.Item1) * s.T0, a.Item2 + (b.Item2 - a.Item2) * s.T0),
                (a.Item1 + (b.Item1 - a.Item1) * s.T1, a.Item2 + (b.Item2 - a.Item2) * s.T1));
    }

    /// <summary>The section nearest to a point (null if the room has none).</summary>
    public WallSection? NearestSection((double X, double Y) p, out double dist)
    {
        dist = double.MaxValue;
        WallSection? best = null;
        foreach (var s in Sections)
        {
            var (a, b) = Ends(s);
            double d = Geometry.DistToSegment(p, a, b);
            if (d < dist) { dist = d; best = s; }
        }
        return best;
    }

    /// <summary>
    /// Wall correction: the robot was physically at 'p' (a body of half-width 'half'), so
    /// the wall it is beyond must be further out. Rules (spec, "always on"):
    ///   inferred / unresolved section: move on one piece of evidence
    ///   confirmed section: move only after 2 consistent pieces of evidence
    ///   doorway: never move (being there means the robot is IN the doorway)
    /// Moves the whole wall line outwards, keeping its direction. Returns how far it moved.
    /// </summary>
    public double PushOut((double X, double Y) p, double half)
    {
        int n = Corners.Count;
        if (n < 3) return 0;
        var poly = Polygon;
        double moved = 0;
        for (int k = 0; k < n; k++)
        {
            var a = poly[k];
            var b = poly[(k + 1) % n];
            double dx = b.X - a.X, dy = b.Y - a.Y, len = Math.Sqrt(dx * dx + dy * dy);
            if (len < 1) continue;
            double nx = dy / len, ny = -dx / len;                 // outward for anticlockwise corners
            double t = ((p.X - a.X) * dx + (p.Y - a.Y) * dy) / (len * len);
            if (t < 0 || t > 1) continue;                         // only the wall the point is beside
            double over = (p.X - a.X) * nx + (p.Y - a.Y) * ny + half;  // how far past the wall
            if (over <= 5) continue;

            var sec = Sections.FirstOrDefault(s => s.Edge == k && t >= s.T0 - 1e-9 && t <= s.T1 + 1e-9);
            if (sec?.Kind == SectionKind.Doorway) continue;
            if (over > MaxPushMm) continue;                       // too far: the map is wrong, not the wall
            if (sec == null || sec.Kind == SectionKind.Confirmed)
            {
                PushVotes[k] = PushVotes.GetValueOrDefault(k) + 1;
                if (PushVotes[k] < 2) continue;
            }
            PushVotes[k] = 0;
            MoveEdge(k, over);
            moved = Math.Max(moved, over);
            poly = Polygon;
        }
        return moved;
    }

    private void MoveEdge(int k, double by)
    {
        int n = Corners.Count;
        var lines = new List<(double Nx, double Ny, double C)>();
        for (int i = 0; i < n; i++)
        {
            var a = (Corners[i][0], Corners[i][1]);
            var b = (Corners[(i + 1) % n][0], Corners[(i + 1) % n][1]);
            double dx = b.Item1 - a.Item1, dy = b.Item2 - a.Item2, len = Math.Max(1e-9, Math.Sqrt(dx * dx + dy * dy));
            double nx = dy / len, ny = -dx / len;
            lines.Add((nx, ny, nx * a.Item1 + ny * a.Item2));
        }
        lines[k] = (lines[k].Nx, lines[k].Ny, lines[k].C + by);
        foreach (int c in new[] { k, (k + 1) % n })                // the two corners of edge k
        {
            var l1 = lines[(c - 1 + n) % n];
            var l2 = lines[c];
            double det = l1.Nx * l2.Ny - l1.Ny * l2.Nx;
            if (Math.Abs(det) < 1e-6)
            {
                Corners[c][0] += lines[k].Nx * by;                  // parallel neighbour: just slide
                Corners[c][1] += lines[k].Ny * by;
                continue;
            }
            Corners[c][0] = (l1.C * l2.Ny - l1.Ny * l2.C) / det;
            Corners[c][1] = (l1.Nx * l2.C - l2.Nx * l1.C) / det;
        }
    }
}

public enum ObstacleKind
{
    Pillar,             // solid, roughly square
    Partition,          // solid, long and thin
    AttachedPartition,  // solid, runs into the room outline
    WallPillar,         // found as an inner loop of the room trace (pillar touching a wall)
    Cluster,            // narrow things grouped (e.g. table legs)
    UnresolvedLoop,     // traced from inside: not a solid obstacle (logged only, not blocked)
    Temporary,          // Wi-Fi survey only: something unexpected
}

public enum ClusterState { NotACluster, Closed, WallAttached, ObstacleAttached, Unconfirmed }

public sealed class Obstacle
{
    public int Id { get; set; }
    public ObstacleKind Kind { get; set; }
    public ClusterState Cluster { get; set; }
    public List<double[]> Footprint { get; set; } = new();     // polygon [x, y]
    public List<double[]> Members { get; set; } = new();       // cluster hits / traced edge points
    public bool InferredEdge { get; set; }                     // closing edge along an inferred wall
    public bool Unconfirmed { get; set; }                      // couldn't be fully traced
    public string Note { get; set; } = "";

    [JsonIgnore] public List<(double X, double Y)> Polygon => Footprint.Select(c => (c[0], c[1])).ToList();
    [JsonIgnore] public bool Blocks => Kind != ObstacleKind.UnresolvedLoop && Footprint.Count >= 3;
}

public enum SeedState { Pending, Processed, Ghost, Unreachable }

/// <summary>Something worth a closer look in Phase 2 (a narrow hit, a bulge, a spike...).</summary>
public sealed class Seed
{
    public int Id { get; set; }
    public double X { get; set; }
    public double Y { get; set; }
    public string Source { get; set; } = "";
    public SeedState State { get; set; }
    [JsonIgnore] public (double X, double Y) Pos => (X, Y);
}

public sealed record Doorway(double Ax, double Ay, double Bx, double By);

/// <summary>
/// Everything known about the environment. Shared by all three phases.
/// Coverage is kept per phase (Phase 2 = explored, Phase 3 = surveyed).
/// </summary>
public sealed class WorldMap
{
    public Room? Room { get; set; }
    public List<Obstacle> Obstacles { get; set; } = new();
    public List<Seed> Seeds { get; set; } = new();
    public List<Doorway> Doorways { get; set; } = new();
    public List<double[]> UnassignedHits { get; set; } = new();  // single hits not yet explained

    // Coverage (50 mm cells). Not saved: it belongs to one run.
    [JsonIgnore] public HashSet<(int, int)> Covered { get; } = new();
    [JsonIgnore] public HashSet<(int, int)> Skipped { get; } = new();
    [JsonIgnore] public HashSet<(int, int)> Unreachable { get; } = new();
    [JsonIgnore] public HashSet<(int, int)> Remaining { get; } = new();
    [JsonIgnore] public List<(double X, double Y)> TracePath { get; } = new();   // latest trace, for display
    public const double CoverCellMm = 50;

    private int _nextId = 1;
    public int NextId() => _nextId++;

    public Seed AddSeed((double X, double Y) p, string source)
    {
        var s = new Seed { Id = NextId(), X = p.X, Y = p.Y, Source = source, State = SeedState.Pending };
        Seeds.Add(s);
        return s;
    }

    public Obstacle AddObstacle(ObstacleKind kind, IEnumerable<(double X, double Y)> footprint,
                                IEnumerable<(double X, double Y)>? members = null, string note = "")
    {
        var o = new Obstacle
        {
            Id = NextId(), Kind = kind, Note = note,
            Footprint = footprint.Select(p => new[] { p.X, p.Y }).ToList(),
            Members = (members ?? Enumerable.Empty<(double X, double Y)>()).Select(p => new[] { p.X, p.Y }).ToList(),
        };
        Obstacles.Add(o);
        return o;
    }

    public void ClearAll()
    {
        Room = null;
        Obstacles.Clear();
        Seeds.Clear();
        Doorways.Clear();
        UnassignedHits.Clear();
        ClearCoverage();
        TracePath.Clear();
    }

    public void ClearCoverage()
    {
        Covered.Clear(); Skipped.Clear(); Unreachable.Clear(); Remaining.Clear();
    }

    public void ClearTemporary() => Obstacles.RemoveAll(o => o.Kind == ObstacleKind.Temporary);

    /// <summary>Is the point on or near something already mapped (room wall or footprint)?</summary>
    public bool IsNearKnown((double X, double Y) p, double radiusMm)
    {
        if (Room is { } room && room.Corners.Count >= 3)
        {
            var poly = room.Polygon;
            if (!Geometry.PointInPolygon(poly, p.X, p.Y)) return true;
            if (Geometry.DistToPolygonEdge(poly, p) < radiusMm) return true;
        }
        foreach (var o in Obstacles.Where(o => o.Blocks))
        {
            var poly = o.Polygon;
            if (Geometry.PointInPolygon(poly, p.X, p.Y) || Geometry.DistToPolygonEdge(poly, p) < radiusMm) return true;
        }
        return false;
    }

    /// <summary>
    /// The planner: real room and footprints, grown by the robot's clearance locally
    /// (per occupied cell). Pending/unreachable seeds and unexplained hits are treated as
    /// small occupied spots; ghost seeds are not.
    /// </summary>
    public Planner BuildPlanner(RobotConfig cfg, IEnumerable<(double X, double Y)>? extraLoose = null)
    {
        var room = Room is { Corners.Count: >= 3 } r ? r.Polygon : null;
        var objects = Obstacles.Where(o => o.Blocks)
                               .Select(o => (IReadOnlyList<(double X, double Y)>)o.Polygon).ToList();
        var loose = UnassignedHits.Select(h => (h[0], h[1]))
            .Concat(Seeds.Where(s => s.State is SeedState.Pending or SeedState.Unreachable).Select(s => s.Pos))
            .Concat(extraLoose ?? Enumerable.Empty<(double X, double Y)>())
            .ToList();
        return new Planner(room, objects, loose, cfg.ClearanceMm);
    }

    // ---------------------------------------------------------------- saving

    private static readonly JsonSerializerOptions Json = new()
    {
        WriteIndented = true,
        Converters = { new JsonStringEnumConverter() },
    };

    public string ToJson(bool roomOnly)
    {
        var copy = new WorldMap { Room = Room, Doorways = Doorways.ToList() };
        if (!roomOnly)
        {
            copy.Obstacles = Obstacles.Where(o => o.Kind != ObstacleKind.Temporary).ToList();
            copy.Seeds = Seeds.ToList();
            copy.UnassignedHits = UnassignedHits.ToList();
        }
        else
        {
            copy.Seeds = Seeds.Select(s => new Seed { Id = s.Id, X = s.X, Y = s.Y, Source = s.Source, State = SeedState.Pending })
                              .Where(s => s.Source.StartsWith("phase1")).ToList();
        }
        return JsonSerializer.Serialize(copy, Json);
    }

    public static WorldMap FromJson(string json)
    {
        var m = JsonSerializer.Deserialize<WorldMap>(json, Json) ?? throw new InvalidDataException("empty map");
        int max = m.Obstacles.Select(o => o.Id).Concat(m.Seeds.Select(s => s.Id)).DefaultIfEmpty(0).Max();
        m._nextId = max + 1;
        return m;
    }
}
